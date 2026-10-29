#ifndef IPFS_CORE_V2_LISTEN_H
#define IPFS_CORE_V2_LISTEN_H

#include "ipfs/core/ipfs_node.h"
#include "libp2p/net/quic.h"

/**
 * Handle an inbound TCP connection using the v2 libp2p stack:
 * multistream → Noise XX → Yamux → Identify.
 *
 * This is intended to be called from ipfs_null_listen (core/null.c)
 * for each accepted socket. It runs synchronously in the caller's
 * thread and closes the fd when done.
 *
 * @param fd        The accepted TCP socket file descriptor.
 * @param local_node The local IpfsNode (for identity key, config, etc.)
 * @return 1 on success, 0 on failure.
 */
int ipfs_v2_listen_handler(int fd, struct IpfsNode* local_node);

/**
 * Handle inbound QUIC streams from a listener using the v2 libp2p stack.
 * TLS 1.3 and QUIC together provide security and multiplexing, so this
 * skips Noise and yamux and runs multistream-select Identify directly.
 *
 * This blocks accepting streams until the listener is shut down.
 *
 * @param listener   A QUIC listener created with libp2p_net_quic_listener_new().
 * @param local_node The local IpfsNode (for identity key, config, etc.)
 */
void ipfs_v2_quic_listen_handler(struct Libp2pQuicListener *listener, struct IpfsNode* local_node);

#endif
