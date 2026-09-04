#ifndef IPFS_TRANSPORT_SWARM_DIALER_V2_BRIDGE_H
#define IPFS_TRANSPORT_SWARM_DIALER_V2_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ipfs_v2_stream_bridge ipfs_v2_stream_bridge_t;

int ipfs_swarm_connect_v2_bridge(const char *multiaddr_str, ipfs_v2_stream_bridge_t **out_bridge);
ssize_t ipfs_v2_stream_write(ipfs_v2_stream_bridge_t *bridge, const void *buf, size_t count);
ssize_t ipfs_v2_stream_read(ipfs_v2_stream_bridge_t *bridge, void *buf, size_t count);
void ipfs_v2_stream_free(ipfs_v2_stream_bridge_t *bridge);

#ifdef __cplusplus
}
#endif

#endif /* IPFS_TRANSPORT_SWARM_DIALER_V2_BRIDGE_H */
