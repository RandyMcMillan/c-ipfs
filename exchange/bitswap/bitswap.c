/**
 * Methods for the Bitswap exchange
 */
#include <stdlib.h>
#include <unistd.h> // for sleep()
#include <pthread.h>
#include <sys/time.h>
#include "libp2p/os/utils.h"
#include "libp2p/utils/logger.h"
#include "libp2p/net/stream.h"
#include "libp2p/net/connectionstream.h"
#include "ipfs/core/ipfs_node.h"
#include "ipfs/datastore/ds_helper.h"
#include "ipfs/exchange/exchange.h"
#include "ipfs/exchange/bitswap/bitswap.h"
#include "ipfs/exchange/bitswap/message.h"
#include "ipfs/exchange/bitswap/network.h"
#include "ipfs/exchange/bitswap/peer_request_queue.h"
#include "ipfs/exchange/bitswap/want_manager.h"

int ipfs_bitswap_can_handle(const struct StreamMessage* msg) {
	if (msg == NULL || msg->data == NULL || msg->data_size == 0)
		return 0;
	char* result = strnstr((char*)msg->data, "/ipfs/bitswap", msg->data_size);
	if(result == NULL || result != (char*)msg->data)
		return 0;
	return 1;
}

int ipfs_bitswap_shutdown_handler(void* context) {
	return 1;
}

/***
 * Handles the message
 * @param incoming the incoming data buffer
 * @param incoming_size the size of the incoming data buffer
 * @param session_context the information about the incoming connection
 * @param protocol_context the protocol-dependent context
 * @returns 0 if the caller should not continue looping, <0 on error, >0 on success
 */
int ipfs_bitswap_handle_message(const struct StreamMessage* msg, struct Stream* stream, void* protocol_context) {
	struct IpfsNode* local_node = (struct IpfsNode*)protocol_context;
	struct SessionContext* session_context = libp2p_net_connection_get_session_context(stream);
	if (session_context == NULL)
		return -1;
	int retVal = ipfs_bitswap_network_handle_message(local_node, session_context, msg->data, msg->data_size);
	if (retVal == 0)
		return -1;
	return retVal;
}

struct Libp2pProtocolHandler* ipfs_bitswap_build_protocol_handler(const struct IpfsNode* local_node) {
	struct Libp2pProtocolHandler* handler = (struct Libp2pProtocolHandler*) malloc(sizeof(struct Libp2pProtocolHandler));
	if (handler != NULL) {
		handler->context = (void*)local_node;
		handler->CanHandle = ipfs_bitswap_can_handle;
		handler->HandleMessage = ipfs_bitswap_handle_message;
		handler->Shutdown = ipfs_bitswap_shutdown_handler;
	}
	return handler;
}

/**
 * Create a new bitswap exchange
 * @param sessionContext the context
 * @returns an allocated Exchange structure
 */
struct Exchange* ipfs_bitswap_new(struct IpfsNode* ipfs_node) {
	struct Exchange* exchange = (struct Exchange*) malloc(sizeof(struct Exchange));
	if (exchange != NULL) {
		struct BitswapContext* bitswapContext = (struct BitswapContext*) malloc(sizeof(struct BitswapContext));
		if (bitswapContext == NULL) {
			free(exchange);
			return NULL;
		}
		bitswapContext->bitswap_engine = ipfs_bitswap_engine_new();
		if (bitswapContext->bitswap_engine == NULL) {
			free(bitswapContext);
			free(exchange);
			return NULL;
		}
		bitswapContext->localWantlist = ipfs_bitswap_wantlist_queue_new();
		bitswapContext->peerRequestQueue = ipfs_bitswap_peer_request_queue_new();
		bitswapContext->ipfsNode = ipfs_node;

		exchange->exchangeContext = (void*) bitswapContext;
		exchange->IsOnline = ipfs_bitswap_is_online;
		exchange->Close = ipfs_bitswap_close;
		exchange->HasBlock = ipfs_bitswap_has_block;
		exchange->GetBlock = ipfs_bitswap_get_block;
		exchange->GetBlockAsync = ipfs_bitswap_get_block_async;
		exchange->GetBlocks = ipfs_bitswap_get_blocks;

		// Start the threads for the network
		ipfs_bitswap_engine_start(bitswapContext);
		libp2p_logger_debug("bitswap", "Bitswap engine started\n");
	}
	return exchange;
}

/**
 * Clean up resources within an Exchange struct
 * @param exchange the exchange to free
 * @returns true(1)
 */
int ipfs_bitswap_free(struct Exchange* exchange) {
	if (exchange != NULL) {
		if (exchange->exchangeContext != NULL) {
			struct BitswapContext* bitswapContext = (struct BitswapContext*) exchange->exchangeContext;
			if (bitswapContext != NULL)
				ipfs_bitswap_engine_stop(bitswapContext);
			if (bitswapContext->localWantlist != NULL) {
				ipfs_bitswap_wantlist_queue_free(bitswapContext->localWantlist);
				bitswapContext->localWantlist = NULL;
			}
			if (bitswapContext->peerRequestQueue != NULL) {
				ipfs_bitswap_peer_request_queue_free(bitswapContext->peerRequestQueue);
				bitswapContext->peerRequestQueue = NULL;
			}
			free(exchange->exchangeContext);
		}
		free(exchange);
	}
	return 1;
}

/**
 * Implements the Exchange->IsOnline method
 */
int ipfs_bitswap_is_online(struct Exchange* exchange) {
	return 1;
}

/***
 * Implements the Exchange->Close method
 */
int ipfs_bitswap_close(struct Exchange* exchange) {
	ipfs_bitswap_free(exchange);
	return 0;
}

/**
 * Implements the Exchange->HasBlock method
 * Some notes from the GO version say that this is normally called by user
 * interaction (i.e. user added a file).
 * But this does not make sense right now, as the GO code looks like it
 * adds the block to the blockstore. This still has to be sorted.
 */
int ipfs_bitswap_has_block(struct Exchange* exchange, struct Block* block) {
	// add the block to the blockstore
	struct BitswapContext* context = exchange->exchangeContext;
	size_t bytes_written;
	context->ipfsNode->blockstore->Put(context->ipfsNode->blockstore->blockstoreContext, block, &bytes_written);
	// add it to the datastore
	ipfs_datastore_helper_add_block_to_datastore(block, context->ipfsNode->repo->config->datastore);
	// update requests
	struct WantListQueueEntry* queueEntry = ipfs_bitswap_wantlist_queue_find(context->localWantlist, block->cid);
	if (queueEntry != NULL) {
		pthread_mutex_lock(&queueEntry->block_mutex);
		queueEntry->block = block;
		pthread_cond_broadcast(&queueEntry->block_cond);
		if (queueEntry->future != NULL) {
			bitswap_future_resolve(queueEntry->future, block);
		}
		pthread_mutex_unlock(&queueEntry->block_mutex);
	}
	// Announce to connected peers that we now have the block (Bitswap 1.2.0 HAVE)
	struct BitswapMessage* announce = ipfs_bitswap_message_new();
	if (announce != NULL) {
		struct BitswapBlockPresence* bp = ipfs_bitswap_block_presence_new();
		if (bp != NULL) {
			bp->cid = (unsigned char*)malloc(block->cid->hash_length);
			if (bp->cid != NULL) {
				memcpy(bp->cid, block->cid->hash, block->cid->hash_length);
				bp->cid_size = block->cid->hash_length;
				bp->type = BLOCK_PRESENCE_HAVE;
				if (announce->block_presences == NULL)
					announce->block_presences = libp2p_utils_vector_new(1);
				libp2p_utils_vector_add(announce->block_presences, bp);

				// Send to all connected peers in the peer request queue
				struct PeerRequestEntry* entry = context->peerRequestQueue->first;
				while (entry != NULL) {
					struct PeerRequest* pr = entry->current;
					if (pr != NULL && pr->peer != NULL &&
					    pr->peer->connection_type == CONNECTION_TYPE_CONNECTED) {
						ipfs_bitswap_network_send_message(context, pr->peer, announce);
					}
					entry = entry->next;
				}
			} else {
				ipfs_bitswap_block_presence_free(bp);
			}
		}
		ipfs_bitswap_message_free(announce);
	}
	return 0;
}

/**
 * Implements the Exchange->GetBlock method
 * We're asking for this method to get the block from peers. Perhaps this should be
 * taking in a pointer to a callback, as this could take a while (or fail).
 * @param exchangeContext a BitswapContext
 * @param cid the Cid to look for
 * @param block a pointer to where to put the result
 * @returns true(1) if found, false(0) if not
 */
int ipfs_bitswap_get_block(struct Exchange* exchange, struct Cid* cid, struct Block** block) {
	struct BitswapContext* bitswapContext = (struct BitswapContext*)exchange->exchangeContext;
	if (bitswapContext != NULL) {
		// check locally first
		if (bitswapContext->ipfsNode->blockstore->Get(bitswapContext->ipfsNode->blockstore->blockstoreContext, cid, block))
			return 1;
		// now ask the network
		//NOTE: this timeout should be configurable
		int timeout = 60;
		int waitSecs = 1;
		int timeTaken = 0;
		struct WantListSession *wantlist_session = ipfs_bitswap_wantlist_session_new();
		wantlist_session->type = WANTLIST_SESSION_TYPE_LOCAL;
		wantlist_session->context = (void*)bitswapContext->ipfsNode;
		struct WantListQueueEntry* want_entry = ipfs_bitswap_want_manager_add(bitswapContext, cid, wantlist_session);
		if (want_entry != NULL) {
			struct timespec ts;
			struct timeval tv;
			gettimeofday(&tv, NULL);
			ts.tv_sec = tv.tv_sec + timeout;
			ts.tv_nsec = tv.tv_usec * 1000;

			pthread_mutex_lock(&want_entry->block_mutex);
			while (want_entry->block == NULL) {
				int rc = pthread_cond_timedwait(&want_entry->block_cond, &want_entry->block_mutex, &ts);
				if (rc == ETIMEDOUT) {
					break;
				}
			}
			if (want_entry->block != NULL) {
				*block = ipfs_block_copy(want_entry->block);
			}
			pthread_mutex_unlock(&want_entry->block_mutex);
			ipfs_bitswap_want_manager_remove(bitswapContext, cid);
			if (*block != NULL) {
				return 1;
			}
		}
	}
	return 0;
}

/**
 * Implements the Exchange->GetBlock method
 * We're asking for this method to get the block from peers. Perhaps this should be
 * taking in a pointer to a callback, as this could take a while (or fail).
 * @param exchangeContext a BitswapContext
 * @param cid the Cid to look for
 * @param block a pointer to where to put the result
 * @returns true(1) if found, false(0) if not
 */
int ipfs_bitswap_get_block_async(struct Exchange* exchange, struct Cid* cid, struct Block** block) {
	struct BitswapContext* bitswapContext = (struct BitswapContext*)exchange->exchangeContext;
	if (bitswapContext != NULL) {
		// check locally first
		struct Block* local_block = NULL;
		if (bitswapContext->ipfsNode->blockstore->Get(bitswapContext->ipfsNode->blockstore->blockstoreContext, cid, &local_block)) {
			if (block != NULL)
				*block = local_block;
			return 1;
		}
		// now ask the network
		struct WantListSession* wantlist_session = ipfs_bitswap_wantlist_session_new();
		wantlist_session->type = WANTLIST_SESSION_TYPE_LOCAL;
		wantlist_session->context = (void*)bitswapContext->ipfsNode;
		struct WantListQueueEntry* entry = ipfs_bitswap_want_manager_add(bitswapContext, cid, wantlist_session);
		if (entry != NULL) {
			// Attach an async future so callers can watch for completion
			entry->future = bitswap_future_create();
		}
		return 1;
	}
	return 0;
}

/**
 * Implements the Exchange->GetBlocks method
 */
int ipfs_bitswap_get_blocks(struct Exchange* exchange, struct Libp2pVector* Cids, struct Libp2pVector** blocks) {
	if (exchange == NULL || Cids == NULL || blocks == NULL)
		return 0;

	struct BitswapContext* bitswapContext = (struct BitswapContext*)exchange->exchangeContext;
	if (bitswapContext == NULL)
		return 0;

	*blocks = libp2p_utils_vector_new(1);
	if (*blocks == NULL)
		return 0;

	for (int i = 0; i < Cids->total; i++) {
		struct Cid* cid = (struct Cid*)libp2p_utils_vector_get(Cids, i);
		if (cid == NULL)
			continue;
		struct Block* block = NULL;
		if (ipfs_bitswap_get_block(exchange, cid, &block)) {
			libp2p_utils_vector_add(*blocks, block);
		} else {
			// Partial failure: clean up collected blocks and report failure
			for (int j = 0; j < (*blocks)->total; j++) {
				struct Block* b = (struct Block*)libp2p_utils_vector_get(*blocks, j);
				if (b != NULL)
					ipfs_block_free(b);
			}
			libp2p_utils_vector_free(*blocks);
			*blocks = NULL;
			return 0;
		}
	}
	return 1;
}
