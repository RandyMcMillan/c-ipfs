#pragma once

#include "libp2p/net/stream.h"

struct IpfsListener {
	char* conCh;
	char* protocol;
	int listen_fd;
	int port;
};

/**
 * Do a socket accept
 * @param listener the listener
 * @param stream the returned stream
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_core_net_accept(struct IpfsListener* listener, struct Stream* stream);

/**
 * Listen using a particular protocol
 * @param node the node
 * @param protocol the protocol to use
 * @param listener the results
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_core_net_listen(struct IpfsNode* node, char* protocol, struct IpfsListener* listener);

/***
 * Dial a peer
 * @param node this node
 * @param peer_id who to dial
 * @param protocol the protocol to use
 * @param stream the resultant stream
 * @returns true(1) on success, otherwise false(0)
 */
int ipsf_core_net_dial(struct IpfsNode* node, char* peer_id, char* protocol, struct Stream* stream);
