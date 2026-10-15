#ifndef IPFS_TRANSPORT_SWARM_DIALER_V2_BRIDGE_H
#define IPFS_TRANSPORT_SWARM_DIALER_V2_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ipfs_v2_stream_bridge ipfs_v2_stream_bridge_t;

/**
 * Dial a peer over the v2 wire protocol stack.
 *
 * @param multiaddr_str  Target multiaddr, e.g. "/ip4/127.0.0.1/tcp/4001".
 * @param protocol       Optional application protocol to negotiate after Identify
 *                       (e.g. "/ipfs/bitswap/1.2.0"). May be NULL.
 * @param private_key    Local node's private key handle (struct RsaPrivateKey*).
 * @param out_bridge     Output bridge holding the Yamux session and stream.
 * @return 0 on success, negative errno on failure.
 */
int ipfs_swarm_connect_v2_bridge(const char *multiaddr_str,
                                 const char *protocol,
                                 void *private_key,
                                 ipfs_v2_stream_bridge_t **out_bridge);

ssize_t ipfs_v2_stream_write(ipfs_v2_stream_bridge_t *bridge, const void *buf, size_t count);
ssize_t ipfs_v2_stream_read(ipfs_v2_stream_bridge_t *bridge, void *buf, size_t count);
void ipfs_v2_stream_free(ipfs_v2_stream_bridge_t *bridge);

#ifdef __cplusplus
}
#endif

#endif /* IPFS_TRANSPORT_SWARM_DIALER_V2_BRIDGE_H */
