#include <stdlib.h>
#include "ipfs/exchange/bitswap/peer_request_queue.h"

/***
 * Create a queue, do some work, free the queue, make sure valgrind likes it.
 */
int test_bitswap_peer_request_queue_new() {
	int retVal = 0;
	struct PeerRequestQueue* queue = NULL;
	struct PeerRequest* request = NULL;

	// create a queue
	queue = ipfs_bitswap_peer_request_queue_new();
	if (queue == NULL)
		goto exit;

	// add a request
	request = ipfs_bitswap_peer_request_new();
	if (request == NULL)
		goto exit;
	ipfs_bitswap_peer_request_queue_add(queue, request);

	retVal = 1;
	exit:
	// clean up
	ipfs_bitswap_peer_request_queue_free(queue);
	return retVal;
}

int test_bitswap_peer_request_queue_find() {
	return 0;
	/*
	int retVal = 0;
	struct PeerRequestQueue* queue = NULL;
	struct PeerRequest* request1 = NULL;
	struct PeerRequest* request2 = NULL;
	struct PeerRequestEntry* foundRequestEntry = NULL;

	// create a queue
	queue = ipfs_bitswap_peer_request_queue_new();
	if (queue == NULL)
		goto exit;

	// add a request
	request1 = ipfs_bitswap_peer_request_new();
	if (request1 == NULL)
		goto exit;
	request1->peer_id = 1;
	ipfs_bitswap_peer_request_queue_add(queue, request1);

	// add a second request
	request2 = ipfs_bitswap_peer_request_new();
	if (request2 == NULL)
		goto exit;
	request2->peer_id = 2;
	ipfs_bitswap_peer_request_queue_add(queue, request2);

	// find the first
	foundRequestEntry = ipfs_bitswap_peer_request_queue_find_entry(queue, request1);
	if (foundRequestEntry == NULL)
		goto exit;
	if (foundRequestEntry->current->peer_id != 1)
		goto exit;

	// find the second
	foundRequestEntry = ipfs_bitswap_peer_request_queue_find_entry(queue, request2);
	if (foundRequestEntry == NULL)
		goto exit;
	if (foundRequestEntry->current->peer_id != 2)
		goto exit;

	retVal = 1;
	exit:
	// clean up
	ipfs_bitswap_peer_request_queue_free(queue);
	return retVal;
	*/
}

#include "ipfs/cid/cid.h"
#include "ipfs/exchange/bitswap/network.h"
#include "ipfs/exchange/bitswap/message.h"

int test_bitswap_network_handle_block_presence_cancel(void) {
	int retVal = 0;
	struct PeerRequest* request = NULL;
	struct Cid* cid = NULL;
	struct BitswapBlockPresence* presence = NULL;

	request = ipfs_bitswap_peer_request_new();
	if (request == NULL) goto exit;

	request->cids_we_want = libp2p_utils_vector_new(1);
	if (request->cids_we_want == NULL) goto exit;

	cid = ipfs_cid_new(0, (const unsigned char*)"01234567890123456789012345678901", 32, CID_DAG_PROTOBUF);
	if (cid == NULL) goto exit;

	struct CidEntry* entry = ipfs_bitswap_peer_request_cid_entry_new();
	if (entry == NULL) goto exit;
	entry->cid = cid;
	cid = NULL;
	libp2p_utils_vector_add(request->cids_we_want, entry);

	presence = ipfs_bitswap_block_presence_new();
	if (presence == NULL) goto exit;
	presence->cid = malloc(32);
	if (presence->cid == NULL) goto exit;
	memcpy(presence->cid, "01234567890123456789012345678901", 32);
	presence->cid_size = 32;
	presence->type = BLOCK_PRESENCE_HAVE;

	if (!ipfs_bitswap_network_handle_block_presence(request, presence)) goto exit;
	if (request->cids_we_want->total != 0) goto exit;

	retVal = 1;
exit:
	if (cid) ipfs_cid_free(cid);
	if (request) ipfs_bitswap_peer_request_free(request);
	if (presence) ipfs_bitswap_block_presence_free(presence);
	return retVal;
}
