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
    (void)stream;
    (void)protocol;
    return -ENOTSUP;
}


int ipfs_swarm_connect_v2_bridge(const char *multiaddr_str, ipfs_v2_stream_bridge_t **out_bridge) {
    if (!multiaddr_str || !out_bridge) return -EINVAL;

    (void)multiaddr_str; (void)out_bridge;
    fprintf(stderr, "[v2 Bridge] not implemented in this build\n");
    return -ENOTSUP;

}

ssize_t ipfs_v2_stream_write(ipfs_v2_stream_bridge_t *bridge, const void *buf, size_t count) {
    (void)bridge; (void)buf; (void)count;
    return -EINVAL;
}

ssize_t ipfs_v2_stream_read(ipfs_v2_stream_bridge_t *bridge, void *buf, size_t count) {
    (void)bridge; (void)buf; (void)count;
    return -1;
}

void ipfs_v2_stream_free(ipfs_v2_stream_bridge_t *bridge) {
    if (!bridge) return;
    if (bridge->yamux_stream) (void)bridge->yamux_stream;
    if (bridge->yamux_session) (void)bridge->yamux_session;
    if (bridge->socket_fd >= 0) close(bridge->socket_fd);
    free(bridge);
}
