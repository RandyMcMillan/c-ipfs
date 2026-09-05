#ifndef IPFS_CORE_V2_LISTEN_H
#define IPFS_CORE_V2_LISTEN_H

#include "ipfs/core/ipfs_node.h"

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

#endif
