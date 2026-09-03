
#include <string.h>
#include "ipfs/core/net.h"
#include "ipfs/core/ipfs_node.h"
#include "libp2p/conn/dialer.h"
#include "libp2p/utils/logger.h"

/**
 * Do a socket accept
 * @param listener the listener
 * @param stream the returned stream
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_core_net_accept(struct IpfsListener* listener, struct Stream* stream) {
	(void)listener;
	(void)stream;
	/* TODO: Full accept implementation requires socket descriptor in IpfsListener */
	libp2p_logger_debug("net", "ipfs_core_net_accept: stub\n");
	return 0;
}

/**
 * Listen using a particular protocol
 * @param node the node
 * @param protocol the protocol to use
 * @param listener the results
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_core_net_listen(struct IpfsNode* node, char* protocol, struct IpfsListener* listener) {
	(void)node;
	(void)protocol;
	(void)listener;
	/* TODO: Full listen implementation requires protocol-specific listener setup */
	libp2p_logger_debug("net", "ipfs_core_net_listen: stub\n");
	return 0;
}

/***
 * Dial a peer
 * @param node this node
 * @param peer_id who to dial (null terminated string)
 * @param protocol the protocol to use
 * @param stream the resultant stream
 * @returns true(1) on success, otherwise false(0)
 */
int ipsf_core_net_dial(const struct IpfsNode* node, const char* peer_id, const char* protocol, struct Stream* stream) {
	(void)protocol;
	(void)stream;
	if (!node || !peer_id)
		return 0;

	struct Libp2pPeer* peer = libp2p_peerstore_get_peer(node->peerstore, (const unsigned char*)peer_id, strlen(peer_id));
	if (peer == NULL) {
		libp2p_logger_error("net", "Dial failed: peer %s not found in peerstore.\n", peer_id);
		return 0;
	}

	if (!libp2p_peer_connect(node->dialer, peer, node->peerstore, node->repo->config->datastore, 10)) {
		libp2p_logger_error("net", "Dial failed: could not connect to peer %s.\n", peer_id);
		return 0;
	}

	libp2p_logger_debug("net", "Dial to peer %s succeeded.\n", peer_id);
	return 1;
}
