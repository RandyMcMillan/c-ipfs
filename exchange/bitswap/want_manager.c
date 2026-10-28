#include <pthread.h>

#include "ipfs/exchange/bitswap/want_manager.h"
#include "ipfs/exchange/bitswap/wantlist_queue.h"

/***
 * Add a Cid to the wantlist
 * @param context the context
 * @param cid the Cid
 * @returns the added entry
 */
struct WantListQueueEntry* ipfs_bitswap_want_manager_add(const struct BitswapContext* context, const struct Cid* cid, const struct WantListSession* session) {
	// add if not there, and increment reference count
	return ipfs_bitswap_wantlist_queue_add(context->localWantlist, cid, session);
}

/***
 * Checks to see if the requested block has been received
 * @param context the context
 * @param cid the Cid
 * @returns true(1) if it has been received, false(0) otherwise
 */
int ipfs_bitswap_want_manager_received(const struct BitswapContext* context, const struct Cid* cid) {
	// find the entry
	ipfs_bitswap_wantlist_queue_lock(context->localWantlist);
	struct WantListQueueEntry* entry = ipfs_bitswap_wantlist_queue_find(context->localWantlist, cid);
	// check the status
	int retVal = (entry != NULL && entry->block != NULL);
	ipfs_bitswap_wantlist_queue_unlock(context->localWantlist);
	return retVal;
}

/***
 * retrieve a block from the WantManager. NOTE: a call to want_manager_received should be done first
 * @param context the context
 * @param cid the Cid to get
 * @param block a pointer to the block that will be allocated
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_bitswap_want_manager_get_block(const struct BitswapContext* context, const struct Cid* cid, struct Block** block) {
	ipfs_bitswap_wantlist_queue_lock(context->localWantlist);
	struct WantListQueueEntry* entry = ipfs_bitswap_wantlist_queue_find(context->localWantlist, cid);
	int retVal = 0;
	if (entry != NULL && entry->block != NULL) {
		// return a copy of the block
		*block = ipfs_block_copy(entry->block);
		if (*block != NULL) {
			retVal = 1;
		}
	}
	ipfs_bitswap_wantlist_queue_unlock(context->localWantlist);
	return retVal;
}

/***
 * We no longer are requesting this block, so remove it from the queue
 * NOTE: This is reference counted, as another process may have asked for it.
 * @param context the context
 * @param cid the Cid
 * @returns true(1) on success, false(0) otherwise.
 */
int ipfs_bitswap_want_manager_remove(const struct BitswapContext* context, const struct Cid* cid) {
	// decrement the reference count
	// if it is zero, remove the entry (lock first)
	struct WantListSession session;
	session.type = WANTLIST_SESSION_TYPE_LOCAL;
	session.context = (void*) context->ipfsNode;
	return ipfs_bitswap_wantlist_queue_remove(context->localWantlist, cid, &session);
}
