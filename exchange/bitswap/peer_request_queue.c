/***
 * A queue for requests from remote peers
 * NOTE: This should handle multiple threads
 */

#include <stdlib.h>
#include "libp2p/conn/session.h"
#include "libp2p/utils/logger.h"
#include "libp2p/utils/debug.h"
#include "ipfs/cid/cid.h"
#include "ipfs/exchange/bitswap/peer_request_queue.h"
#include "ipfs/exchange/bitswap/message.h"
#include "ipfs/exchange/bitswap/network.h"

/***
 * Allocate memory for CidEntry
 * @returns new CidEntry struct
 */
struct CidEntry* ipfs_bitswap_peer_request_cid_entry_new() {
	struct CidEntry* entry = (struct CidEntry*) malloc(sizeof(struct CidEntry));
	if (entry != NULL) {
		entry->cid = NULL;
		entry->cancel = 0;
		entry->cancel_has_been_sent = 0;
		entry->request_has_been_sent = 0;
	}
	return entry;
}
/**
 * Allocate resources for a new PeerRequest
 * @returns a new PeerRequest struct or NULL if there was a problem
 */
struct PeerRequest* ipfs_bitswap_peer_request_new() {
	int retVal = 0;
	struct PeerRequest* request = (struct PeerRequest*) malloc(sizeof(struct PeerRequest));
	if (request != NULL) {
		request->cids_they_want = libp2p_utils_vector_new(1);
		if (request->cids_they_want == NULL)
			goto exit;
		request->cids_we_want = libp2p_utils_vector_new(1);
		if (request->cids_we_want == NULL)
			goto exit;
		request->blocks_we_want_to_send = libp2p_utils_vector_new(1);
		if (request->blocks_we_want_to_send == NULL)
			goto exit;
		request->peer = NULL;
	}
	retVal = 1;
	exit:
	if (retVal == 0 && request != NULL) {
		if (request->blocks_we_want_to_send != NULL)
			libp2p_utils_vector_free(request->blocks_we_want_to_send);
		if (request->cids_they_want != NULL)
			libp2p_utils_vector_free(request->cids_they_want);
		if (request->cids_we_want != NULL)
			libp2p_utils_vector_free(request->cids_we_want);
		free(request);
		request = NULL;
	}
	return request;
}

int ipfs_bitswap_cid_entry_free(struct CidEntry* entry) {
	if (entry != NULL) {
		if (entry->cid != NULL) {
			ipfs_cid_free(entry->cid);
			entry->cid = NULL;
		}
		free(entry);
	}
	return 1;
}

/**
 * Free resources from a PeerRequest
 * @param request the request to free
 * @returns true(1)
 */
int ipfs_bitswap_peer_request_free(struct PeerRequest* request) {
	if (request != NULL) {
		for(int i = 0; i < request->cids_we_want->total; i++) {
			struct CidEntry* entry = (struct CidEntry*)libp2p_utils_vector_get(request->cids_we_want, i);
			ipfs_bitswap_cid_entry_free(entry);
		}
		libp2p_utils_vector_free(request->cids_we_want);
		request->cids_we_want = NULL;
		for(int i = 0; i < request->cids_they_want->total; i++) {
			struct CidEntry* entry = (struct CidEntry*)libp2p_utils_vector_get(request->cids_they_want, i);
			ipfs_bitswap_cid_entry_free(entry);
		}
		libp2p_utils_vector_free(request->cids_they_want);
		request->cids_they_want = NULL;
		for(int i = 0; i < request->blocks_we_want_to_send->total; i++) {
			struct Block* block = (struct Block*)libp2p_utils_vector_get(request->blocks_we_want_to_send, i);
			ipfs_block_free(block);
		}
		libp2p_utils_vector_free(request->blocks_we_want_to_send);
		request->blocks_we_want_to_send = NULL;
		free(request);

	}
	return 1;
}

/**
 * Allocate resources for a new queue
 */
struct PeerRequestQueue* ipfs_bitswap_peer_request_queue_new() {
	struct PeerRequestQueue* queue = malloc(sizeof(struct PeerRequestQueue));
	if (queue != NULL) {
		pthread_mutex_init(&queue->queue_mutex, NULL);
		queue->first = NULL;
		queue->last = NULL;
	}
	return queue;
}

/**
 * Free all resources related to the queue
 * @param queue the queue
 * @returns true(1)
 */
int ipfs_bitswap_peer_request_queue_free(struct PeerRequestQueue* queue) {
	pthread_mutex_lock(&queue->queue_mutex);
	struct PeerRequestEntry* current = queue->last;
	while (current != NULL) {
		struct PeerRequestEntry* prior = current->prior;
		ipfs_bitswap_peer_request_entry_free(current);
		current = prior;
	}
	pthread_mutex_unlock(&queue->queue_mutex);
	free(queue);
	return 1;
}

/**
 * Adds a peer request to the end of the queue
 * @param queue the queue
 * @param request the request
 * @returns true(1) on success, otherwise false
 */
int ipfs_bitswap_peer_request_queue_add(struct PeerRequestQueue* queue, struct PeerRequest* request) {
	if (request != NULL) {
		struct PeerRequestEntry* entry = ipfs_bitswap_peer_request_entry_new();
		entry->current = request;
		pthread_mutex_lock(&queue->queue_mutex);
		entry->prior = queue->last;
		queue->last = entry;
		if (queue->first == NULL) {
			queue->first = entry;
		}
		pthread_mutex_unlock(&queue->queue_mutex);
		return 1;
	}
	return 0;
}

/**
 * Removes a peer request from the queue, no mather where it is
 * @param queue the queue
 * @param request the request
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_peer_request_queue_remove(struct PeerRequestQueue* queue, struct PeerRequest* request) {
	if (request != NULL) {
		struct PeerRequestEntry* entry = ipfs_bitswap_peer_request_queue_find_entry(queue, request->peer);
		if (entry != NULL) {
			pthread_mutex_lock(&queue->queue_mutex);
			// remove the entry's link, and hook prior and next together
			entry->prior->next = entry->next;
			entry->prior = NULL;
			entry->next = NULL;
			ipfs_bitswap_peer_request_entry_free(entry);
			pthread_mutex_unlock(&queue->queue_mutex);
			return 1;
		}
	}
	return 0;
}

/**
 * Finds a PeerRequestEntry that contains the specified Peer
 * @param queue the queue to look through
 * @param peer what we're looking for
 * @returns the PeerRequestEntry or NULL if not found
 */
struct PeerRequestEntry* ipfs_bitswap_peer_request_queue_find_entry(struct PeerRequestQueue* queue, struct Libp2pPeer* peer) {
	if (peer != NULL) {
		struct PeerRequestEntry* current = queue->first;
		while (current != NULL) {
			if (libp2p_peer_compare(current->current->peer, peer) == 0)
				return current;
			current = current->next;
		}
	}
	return NULL;
}

/***
 * Determine if any of the cids in the list are waiting to be filled
 * @param cidEntries a Vector of CidEntry objects
 * @returns true(1) if we have some waiting, false(0) otherwise
 */
int ipfs_bitswap_peer_request_cids_waiting(struct Libp2pVector* cidEntries) {
	if (cidEntries == NULL)
		return 0;
	for(int i = 0; i < cidEntries->total; i++) {
		const struct CidEntry* entry = (const struct CidEntry*)libp2p_utils_vector_get(cidEntries, i);
		if (entry != NULL && !entry->cancel)
			return 1;
	}
	return 0;
}

/***
 * Determine if there is something to process in this request
 * @param entry the entry to look at
 * @returns true(1) if there is something to do
 */
int ipfs_bitswap_peer_request_something_to_do(struct PeerRequestEntry* entry) {
	if (entry != NULL) {
		struct PeerRequest* request = entry->current;
		// do we have something in the queue?
		if (request->blocks_we_want_to_send->total > 0)
			return 1;
		if (request->cids_we_want->total > 0)
			return 1;
		if (ipfs_bitswap_peer_request_cids_waiting(request->cids_they_want))
			return 1;
		// is there something waiting for us on the network?
		if (request->peer->connection_type == CONNECTION_TYPE_CONNECTED) {
			int retVal = request->peer->sessionContext->default_stream->peek(request->peer->sessionContext);
			if (retVal < 0) {
				libp2p_logger_debug("peer_request_queue", "Connection returned %d. Marking connection NOT CONNECTED.\n", retVal);
				libp2p_peer_handle_connection_error(request->peer);
				return 0;
			}
			if (retVal > 0) {
				libp2p_logger_debug("peer_request_queue", "We have something to read. %d bytes.\n", retVal);
			}
			return retVal;
		}
	}
	return 0;
}

/**
 * Pull a PeerRequest off the queue
 * @param queue the queue
 * @returns the PeerRequest that should be handled next, or NULL if the queue is empty
 */
struct PeerRequest* ipfs_bitswap_peer_request_queue_pop(struct PeerRequestQueue* queue) {
	struct PeerRequest* retVal = NULL;
	if (queue != NULL) {
		pthread_mutex_lock(&queue->queue_mutex);
		struct PeerRequestEntry* entry = queue->first;
		if (entry != NULL) {
			if (ipfs_bitswap_peer_request_something_to_do(entry)) {
				retVal = entry->current;
				// move to the end of the queue
				if (queue->first->next != NULL) {
					queue->first = queue->first->next;
					queue->last->next = entry;
					queue->last = entry;
				}
			}
		}
		pthread_mutex_unlock(&queue->queue_mutex);
		// disable temporarily
		// JMJ Debugging
		/*
		if (entry != NULL)
			ipfs_bitswap_peer_request_entry_free(entry);
		*/
	}
	return retVal;
}

/***
 * Allocate resources for a PeerRequestEntry struct
 * @returns the allocated struct or NULL if there was a problem
 */
struct PeerRequestEntry* ipfs_bitswap_peer_request_entry_new() {
	struct PeerRequestEntry* entry = (struct PeerRequestEntry*) malloc(sizeof(struct PeerRequestEntry));
	if (entry != NULL) {
		entry->current = NULL;
		entry->next = NULL;
		entry->prior = NULL;
	}
	return entry;
}

/**
 * Frees resources allocated
 * @param entry the PeerRequestEntry to free
 * @returns true(1)
 */
int ipfs_bitswap_peer_request_entry_free(struct PeerRequestEntry* entry) {
	entry->next = NULL;
	entry->prior = NULL;
	ipfs_bitswap_peer_request_free(entry->current);
	entry->current = NULL;
	free(entry);
	return 1;
}

/***
 * Add a block to the appropriate peer's queue
 * @param queue the queue
 * @param who the session context that identifies the peer
 * @param block the block
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_peer_request_queue_fill(struct PeerRequestQueue* queue, struct Libp2pPeer* who, struct Block* block) {
	// find the right entry
	struct PeerRequest* entry = ipfs_peer_request_queue_find_peer(queue, who);
	if (entry != NULL)
	{
		// add to the block array
		libp2p_utils_vector_add(entry->blocks_we_want_to_send, block);
	}
	return 0;
}

/****
 * Find blocks they want, and put them in the request
 */
int ipfs_bitswap_peer_request_get_blocks_they_want(const struct BitswapContext* context, struct PeerRequest* request) {
	for(int i = 0; i < request->cids_they_want->total; i++) {
		struct CidEntry* cidEntry = (struct CidEntry*)libp2p_utils_vector_get(request->cids_they_want, i);
		if (cidEntry != NULL && !cidEntry->cancel) {
			struct Block* block = NULL;
			context->ipfsNode->blockstore->Get(context->ipfsNode->blockstore->blockstoreContext, cidEntry->cid, &block);
			DBG_SCOPE("bitswap_engine", "blockstore get cid_version=%d codec=%d hash_len=%zu found=%d\n",
			          cidEntry->cid->version, cidEntry->cid->codec, cidEntry->cid->hash_length, block != NULL);
			if (block != NULL) {
				libp2p_utils_vector_add(request->blocks_we_want_to_send, block);
				cidEntry->cancel = 1;
			}
		}
	}
	return 0;
}

/***
 * Determine if we have anything we want (that we haven't sent already)
 * @param cid_entries the list of CidEntries that are in our queue to be sent
 * @returns true(1) if we have something to send, false(0) otherwise
 */
int ipfs_bitswap_peer_request_we_want_cids(struct Libp2pVector* cid_entries) {
	if (cid_entries == NULL)
		return 0;
	if (cid_entries->total == 0)
		return 0;
	for(int i = 0; i < cid_entries->total; i++) {
		const struct CidEntry* entry = (const struct CidEntry*) libp2p_utils_vector_get(cid_entries, i);
		if (entry->cancel && !entry->cancel_has_been_sent)
			return 1;
		if (!entry->cancel && !entry->request_has_been_sent)
			return 1;
	}
	return 0;
}

/****
 * Handle a PeerRequest
 * @param context the BitswapContext
 * @param request the request to process
 * @returns true(1) if something was done, otherwise false(0)
 */
int ipfs_bitswap_peer_request_process_entry(const struct BitswapContext* context, struct PeerRequest* request) {
	// determine if we have enough information to continue
	if (request == NULL)
		return 0;
	if (request->peer == NULL)
		return 0;
	if (ipfs_bitswap_peer_request_cids_waiting(request->cids_they_want))
		DBG_SCOPE("bitswap_engine", "process_entry peer=%s conn=%d addr=%p sess=%p local=%d\n",
		          libp2p_peer_id_to_string(request->peer), request->peer->connection_type,
		          (void*)request->peer->addr_head, (void*)request->peer->sessionContext, request->peer->is_local);
	if (!request->peer->is_local) {
		if (request->peer->connection_type != CONNECTION_TYPE_CONNECTED)
			if (request->peer->addr_head == NULL || request->peer->addr_head->item == NULL)
				return 0;
	}
	// determine if we're connected
	int connected = request->peer->is_local || request->peer->connection_type == CONNECTION_TYPE_CONNECTED;
	int need_to_connect = ipfs_bitswap_peer_request_we_want_cids(request->cids_we_want) || ipfs_bitswap_peer_request_cids_waiting(request->cids_they_want) || request->blocks_we_want_to_send->total != 0;

	// determine if we need to connect
	if (need_to_connect) {
		if (!connected) {
			// connect
			connected = libp2p_peer_connect(context->ipfsNode->dialer, request->peer, context->ipfsNode->peerstore, context->ipfsNode->repo->config->datastore, 0);
		}
		if (connected) {
			// build a message
			struct BitswapMessage* msg = ipfs_bitswap_message_new();
			// see if we can fulfill any of their requests. If so, fill in msg->payload
			ipfs_bitswap_peer_request_get_blocks_they_want(context, request);
			ipfs_bitswap_message_add_blocks(msg, request->blocks_we_want_to_send, request->cids_they_want);
			// add requests that we would like
			ipfs_bitswap_message_add_wantlist_items(msg, request->cids_we_want);
			// send message
			if (ipfs_bitswap_network_send_message(context, request->peer, msg)) {
				ipfs_bitswap_message_free(msg);
				return 1;
			}
			ipfs_bitswap_message_free(msg);
		}
	}
	return 0;
}

/***
 * Find a PeerRequest related to a peer. If one is not found, it is created.
 *
 * @param peer_request_queue the queue to look through
 * @param peer the peer to look for
 * @returns a PeerRequestEntry or NULL on error
 */
struct PeerRequest* ipfs_peer_request_queue_find_peer(struct PeerRequestQueue* queue, struct Libp2pPeer* peer) {

	struct PeerRequestEntry* entry = queue->first;
	while (entry != NULL) {
		if (libp2p_peer_compare(entry->current->peer, peer) == 0) {
			return entry->current;
		}
		entry = entry->next;
	}

	// we didn't find one, so create one
	entry = ipfs_bitswap_peer_request_entry_new();
	entry->current = ipfs_bitswap_peer_request_new();
	entry->current->peer = peer;
	// attach it to the queue
	if (queue->first == NULL) {
		queue->first = entry;
		queue->last = entry;
	} else {
		queue->last->next = entry;
		entry->prior = queue->last;
		queue->last = entry;
	}

	return entry->current;
}







