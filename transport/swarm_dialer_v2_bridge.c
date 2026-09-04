#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "ipfs/desktop/swarm.h"
#include "ipfs/transport/registry.h"
#include "libp2p/v2/swarm.h"
#include "libp2p/v2/yamux.h"
#include "libp2p/v2/noise.h"
#include "libp2p/v2/multistream.h"

#define IPFS_BITSWAP_PROTO_120 "/ipfs/bitswap/1.2.0"
#define IPFS_IDENTIFY_PROTO_100 "/ipfs/id/1.0.0"

typedef struct ipfs_v2_stream_bridge {
    libp2p_yamux_stream_t *yamux_stream;
    libp2p_yamux_session_t *yamux_session;
    int socket_fd;
    char protocol_id[64];
} ipfs_v2_stream_bridge_t;

static int bridge_negotiate_protocol(libp2p_yamux_stream_t *stream, const char *protocol) {
    char send_buf[128];
    char recv_buf[128];

    const char *ms_header = "/multistream/1.0.0\n";
    int len = snprintf(send_buf, sizeof(send_buf), "%c%s", (char)strlen(ms_header), ms_header);
    if (libp2p_yamux_stream_write(stream, (const uint8_t*)send_buf, len) < 0) {
        return -1;
    }

    len = snprintf(send_buf, sizeof(send_buf), "%c%s\n", (char)(strlen(protocol) + 1), protocol);
    if (libp2p_yamux_stream_write(stream, (const uint8_t*)send_buf, len) < 0) {
        return -1;
    }

    int bytes_read = libp2p_yamux_stream_read(stream, (uint8_t*)recv_buf, sizeof(recv_buf) - 1);
    if (bytes_read <= 0) {
        return -1;
    }
    recv_buf[bytes_read] = '\0';

    if (strstr(recv_buf, protocol) != NULL) {
        return 0;
    }

    return -1;
}

int ipfs_swarm_connect_v2_bridge(const char *multiaddr_str, ipfs_v2_stream_bridge_t **out_bridge) {
    if (!multiaddr_str || !out_bridge) return -EINVAL;

    int fd = ipfs_transport_registry_dial(multiaddr_str);
    if (fd < 0) {
        fprintf(stderr, "[v2 Bridge] Failed to dial transport for multiaddr: %s\n", multiaddr_str);
        return fd;
    }

    libp2p_noise_session_t *noise_sess = NULL;
    if (libp2p_noise_handshake_raw(fd, &noise_sess) != 0) {
        fprintf(stderr, "[v2 Bridge] Noise handshake failed on socket %d\n", fd);
        close(fd);
        return -ECONNREFUSED;
    }

    libp2p_yamux_session_t *yamux_sess = libp2p_yamux_session_new(fd, YAMUX_MODE_CLIENT);
    if (!yamux_sess) {
        fprintf(stderr, "[v2 Bridge] Failed to allocate Yamux session\n");
        close(fd);
        return -ENOMEM;
    }

    libp2p_yamux_stream_t *stream = libp2p_yamux_stream_open(yamux_sess);
    if (!stream) {
        fprintf(stderr, "[v2 Bridge] Failed to open Yamux stream\n");
        libp2p_yamux_session_free(yamux_sess);
        close(fd);
        return -EIO;
    }

    if (bridge_negotiate_protocol(stream, IPFS_BITSWAP_PROTO_120) != 0) {
        fprintf(stderr, "[v2 Bridge] Failed protocol negotiation for %s\n", IPFS_BITSWAP_PROTO_120);
        libp2p_yamux_stream_close(stream);
        libp2p_yamux_session_free(yamux_sess);
        close(fd);
        return -EPROTONOSUPPORT;
    }

    ipfs_v2_stream_bridge_t *bridge = malloc(sizeof(ipfs_v2_stream_bridge_t));
    bridge->socket_fd = fd;
    bridge->yamux_session = yamux_sess;
    bridge->yamux_stream = stream;
    strncpy(bridge->protocol_id, IPFS_BITSWAP_PROTO_120, sizeof(bridge->protocol_id));

    *out_bridge = bridge;
    return 0;
}

ssize_t ipfs_v2_stream_write(ipfs_v2_stream_bridge_t *bridge, const void *buf, size_t count) {
    if (!bridge || !bridge->yamux_stream) return -EINVAL;
    return libp2p_yamux_stream_write(bridge->yamux_stream, (const uint8_t*)buf, count);
}

ssize_t ipfs_v2_stream_read(ipfs_v2_stream_bridge_t *bridge, void *buf, size_t count) {
    if (!bridge || !bridge->yamux_stream) return -EINVAL;
    return libp2p_yamux_stream_read(bridge->yamux_stream, (uint8_t*)buf, count);
}

void ipfs_v2_stream_free(ipfs_v2_stream_bridge_t *bridge) {
    if (!bridge) return;
    if (bridge->yamux_stream) libp2p_yamux_stream_close(bridge->yamux_stream);
    if (bridge->yamux_session) libp2p_yamux_session_free(bridge->yamux_session);
    if (bridge->socket_fd >= 0) close(bridge->socket_fd);
    free(bridge);
}
