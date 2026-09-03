#include <stdlib.h>
#include <pthread.h>

#include "../test_helper.h"
#include "ipfs/core/ipfs_node.h"

int test_node_peerstore() {
	int retVal = 0;
	const char *repo_path = "./tmp/test1";
	char* peer_id = NULL;
	struct IpfsNode *local_node = NULL;
	struct Libp2pPeer* peer = NULL;

	if (!drop_and_build_repository(repo_path, 4001, NULL, &peer_id))
		goto exit;

	if (!ipfs_node_online_new(repo_path, &local_node))
		goto exit;

	// add a peer to the peerstore
	peer = libp2p_peer_new();
	if (peer == NULL)
		goto exit;

	peer->id_size = strlen(peer_id);
	peer->id = malloc(peer->id_size);
	memcpy(peer->id, peer_id, peer->id_size);

	if (!libp2p_peerstore_add_peer(local_node->peerstore, peer))
		goto exit;

	// add a second peer by changing the id a bit
	char tmp = peer->id[3];
	char tmp2 = peer->id[4];
	char tmp3 = peer->id[5];
	peer->id[3] = tmp2;
	peer->id[4] = tmp3;
	peer->id[5] = tmp;

	if (!libp2p_peerstore_add_peer(local_node->peerstore, peer))
		goto exit;


	retVal = 1;
	exit:
	if (peer_id != NULL)
		free(peer_id);
	if (peer != NULL)
		libp2p_peer_free(peer);
	if (local_node != NULL)
		ipfs_node_free(local_node);

	return retVal;
}
