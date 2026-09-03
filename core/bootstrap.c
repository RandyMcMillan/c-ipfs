#include <pthread.h>

#include "libp2p/peer/peer.h"
#include "libp2p/utils/logger.h"
#include "ipfs/routing/routing.h"
#include "ipfs/core/ipfs_node.h"
#include "ipfs/thirdparty/ipfsaddr/ipfs_addr.h"
#include "multiaddr/multiaddr.h"

/***
 * Begin to connect to the swarm
 */
/**
 * NOTE: Bootstrap implementation has moved to core/bootstrap_impl.c.
 * The functions below are legacy stubs kept for reference.
 */
/*
void *ipfs_bootstrap_swarm(void* param) {
	struct IpfsNode* local_node = (struct IpfsNode*)param;
	for(int i = 0; i < local_node->repo->config->peer_addresses.num_peers; i++) {
		struct IPFSAddr* ipfs_addr = local_node->repo->config->peer_addresses.peers[i];
		struct MultiAddress* ma = multiaddress_new_from_string(ipfs_addr->entire_string);
		char* ptr;
		if ( (ptr = strstr(ipfs_addr->entire_string, "/ipfs/")) != NULL) {
			ptr += 6;
			if (ptr[0] == 'Q' && ptr[1] == 'm') {
				struct Libp2pPeer* peer = libp2p_peer_new_from_data(ptr, strlen(ptr), ma);
				libp2p_peerstore_add_peer(local_node->peerstore, peer);
			}
		}
	}
	return (void*)1;
}
*/

/***
 * Announce to the network all of the files that I have in storage
 * @param local_node the context
 */
/*
void ipfs_bootstrap_announce_files(struct IpfsNode* local_node) {
	struct Datastore* db = local_node->repo->config->datastore;
	if (!db->datastore_cursor_open(db))
		return;
	unsigned char* key = NULL;
	int key_size = 0;
	enum DatastoreCursorOp op = CURSOR_FIRST;
	while (db->datastore_cursor_get(&key, &key_size, NULL, 0, op, db)) {
		libp2p_logger_debug("bootstrap", "Announcing a file to the world.\n");
		local_node->routing->Provide(local_node->routing, key, key_size);
		op = CURSOR_NEXT;
		free(key);
	}

	// close cursor
	db->datastore_cursor_close(db);

	return;
}
*/

/***
 * connect to the swarm
 * NOTE: This fills in the IpfsNode->routing struct
 *
 * @param param the IpfsNode information
 * @returns nothing useful
 */
/*
void *ipfs_bootstrap_routing(void* param) {
	struct IpfsNode* local_node = (struct IpfsNode*)param;
	local_node->routing = ipfs_routing_new_online(local_node, &local_node->identity->private_key, NULL);
	local_node->routing->Bootstrap(local_node->routing);
	ipfs_bootstrap_announce_files(local_node);
	return (void*)2;
}
*/
