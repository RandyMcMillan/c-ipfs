#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>

#include "ipfs/transport/swarm_dialer_v2_bridge.h"
#include "ipfs/transport/noise_v2_bridge.h"
#include "libp2p/net/tcp.h"
#include "libp2p/conn/yamux.h"
#include "libp2p/conn/noise.h"
#include "libp2p/conn/multistream.h"
#include "libp2p/utils/logger.h"

#define IPFS_IDENTIFY_PROTO_100 "/ipfs/id/1.0.0"

#define BRIDGE_IDENTIFY_BUF_SIZE 4096

struct ipfs_v2_stream_bridge {
    struct YamuxSession *yamux_session;
    struct Libp2pV2Stream *yamux_stream;
    int connected;
    unsigned char identify_buf[BRIDGE_IDENTIFY_BUF_SIZE];
    size_t identify_len;
    size_t identify_pos;
};

static int bridge_parse_multiaddr(const char *multiaddr_str, char *ip_out, size_t ip_len, uint16_t *port_out)
{
    if (!multiaddr_str || !ip_out || !port_out)
        return 0;

    char ip[INET_ADDRSTRLEN];
    unsigned int port = 0;
    int parsed = sscanf(multiaddr_str, "/ip4/%15[^/]/tcp/%u", ip, &port);
    if (parsed != 2)
        return 0;
    if (port == 0 || port > 65535)
        return 0;

    strncpy(ip_out, ip, ip_len - 1);
    ip_out[ip_len - 1] = '\0';
    *port_out = (uint16_t)port;
    return 1;
}

static int bridge_negotiate_protocol(struct Libp2pV2Stream *stream, const char *protocol)
{
    if (!stream || !protocol)
        return 0;

    if (!libp2p_v2_net_multistream_connect(stream))
        return 0;

    return libp2p_net_multistream_negotiate_protocol(stream, protocol);
}

int ipfs_swarm_connect_v2_bridge(const char *multiaddr_str,
                                 const char *protocol,
                                 void *private_key,
                                 ipfs_v2_stream_bridge_t **out_bridge)
{
    if (!multiaddr_str || !private_key || !out_bridge)
        return -EINVAL;

    char ip[INET_ADDRSTRLEN];
    uint16_t port = 0;
    if (!bridge_parse_multiaddr(multiaddr_str, ip, sizeof(ip), &port)) {
        libp2p_logger_error("v2_bridge", "Cannot parse multiaddr: %s\n", multiaddr_str);
        return -EINVAL;
    }

    struct Libp2pV2Stream *tcp_stream = libp2p_net_tcp_dial(ip, port);
    if (!tcp_stream) {
        libp2p_logger_error("v2_bridge", "TCP dial to %s:%u failed\n", ip, port);
        return -ECONNREFUSED;
    }

    /* Multistream: /noise */
    if (!libp2p_v2_net_multistream_connect(tcp_stream)) {
        libp2p_logger_error("v2_bridge", "Multistream handshake failed for /noise\n");
        tcp_stream->close(tcp_stream);
        return -EPROTO;
    }

    if (!libp2p_net_multistream_negotiate_protocol(tcp_stream, "/noise")) {
        libp2p_logger_error("v2_bridge", "Peer did not accept /noise\n");
        tcp_stream->close(tcp_stream);
        return -EPROTONOSUPPORT;
    }

    /* Noise XX initiator handshake */
    const noise_identity_callbacks_t *noise_cbs = ipfs_noise_rsa_callbacks();
    struct Libp2pV2Stream *noise_stream = libp2p_noise_handshake_raw(tcp_stream, private_key, NULL, noise_cbs);
    if (!noise_stream) {
        libp2p_logger_error("v2_bridge", "Noise initiator handshake failed\n");
        tcp_stream->close(tcp_stream);
        return -ECONNABORTED;
    }

    /* Yamux client session */
    struct YamuxSession *session = libp2p_yamux_session_new(noise_stream, 0);
    if (!session) {
        libp2p_logger_error("v2_bridge", "Yamux client session creation failed\n");
        noise_stream->close(noise_stream);
        return -ENOMEM;
    }

    /* Identify exchange: open a stream and negotiate /ipfs/id/1.0.0.
     * The Identify response is read now so later operations on the underlying
     * transport do not discard this stream's DATA frames. */
    struct Libp2pV2Stream *id_stream = libp2p_yamux_stream_open(session);
    if (!id_stream) {
        libp2p_logger_error("v2_bridge", "Yamux identify stream open failed\n");
        libp2p_yamux_session_free(session);
        return -ECONNABORTED;
    }

    if (!bridge_negotiate_protocol(id_stream, IPFS_IDENTIFY_PROTO_100)) {
        libp2p_logger_error("v2_bridge", "Identify protocol negotiation failed\n");
        id_stream->close(id_stream);
        libp2p_yamux_session_free(session);
        return -EPROTO;
    }

    /* Read the Identify response now, before any other operation on the
     * underlying transport can discard this stream's DATA frames. */
    ipfs_v2_stream_bridge_t *bridge = (ipfs_v2_stream_bridge_t *)calloc(1, sizeof(*bridge));
    if (!bridge) {
        id_stream->close(id_stream);
        libp2p_yamux_session_free(session);
        return -ENOMEM;
    }
    bridge->yamux_session = session;
    bridge->yamux_stream = id_stream;
    bridge->connected = 1;

    size_t total = 0;
    while (total < sizeof(bridge->identify_buf)) {
        ssize_t n = id_stream->read(id_stream, bridge->identify_buf + total,
                                    sizeof(bridge->identify_buf) - total);
        if (n <= 0)
            break;
        total += (size_t)n;
    }
    bridge->identify_len = total;

    /* Exercise the session by opening and immediately closing a second stream.
     * This proves the Yamux session stays alive after the Identify stream. */
    struct Libp2pV2Stream *probe = libp2p_yamux_stream_open(session);
    if (probe)
        probe->close(probe);

    /* Optionally negotiate a caller-supplied application protocol on a fresh
     * stream and replace the Identify stream with it. */
    if (protocol) {
        struct Libp2pV2Stream *app_stream = libp2p_yamux_stream_open(session);
        if (app_stream && bridge_negotiate_protocol(app_stream, protocol)) {
            id_stream->close(id_stream);
            bridge->yamux_stream = app_stream;
        } else if (app_stream) {
            app_stream->close(app_stream);
        }
    }

    *out_bridge = bridge;
    return 0;
}

ssize_t ipfs_v2_stream_write(ipfs_v2_stream_bridge_t *bridge, const void *buf, size_t count)
{
    if (!bridge || !bridge->yamux_stream || !bridge->connected)
        return -EINVAL;

    return bridge->yamux_stream->write(bridge->yamux_stream, (const unsigned char *)buf, count);
}

ssize_t ipfs_v2_stream_read(ipfs_v2_stream_bridge_t *bridge, void *buf, size_t count)
{
    if (!bridge || !bridge->connected)
        return -EINVAL;

    /* Serve the buffered Identify response first. */
    if (bridge->identify_pos < bridge->identify_len) {
        size_t avail = bridge->identify_len - bridge->identify_pos;
        size_t to_copy = count < avail ? count : avail;
        memcpy(buf, bridge->identify_buf + bridge->identify_pos, to_copy);
        bridge->identify_pos += to_copy;
        return (ssize_t)to_copy;
    }

    if (!bridge->yamux_stream)
        return -EINVAL;

    return bridge->yamux_stream->read(bridge->yamux_stream, (unsigned char *)buf, count);
}

void ipfs_v2_stream_free(ipfs_v2_stream_bridge_t *bridge)
{
    if (!bridge)
        return;

    if (bridge->yamux_stream)
        bridge->yamux_stream->close(bridge->yamux_stream);

    if (bridge->yamux_session)
        libp2p_yamux_session_free(bridge->yamux_session);

    free(bridge);
}
