#pragma once
/**
 * This is a list of requests from a peer (including locally).
 * NOTE: This tracks who wants what. If 2 peers want the same file,
 * there will be 1 WantListEntry in the WantList. There will be 2 entries in
 * WantListEntry.sessionsRequesting.
 */
#include <pthread.h>
#include "ipfs/cid/cid.h"
#include "ipfs/blocks/block.h"
#include "ipfs/exchange/bitswap/bitswap.h"
#include "ipfs/exchange/bitswap/future.h"

enum WantListSessionType { WANTLIST_SESSION_TYPE_LOCAL, WANTLIST_SESSION_TYPE_REMOTE };

struct WantListSession {
	enum WantListSessionType type;
	void* context; // either an IpfsNode (local) or a Libp2pPeer (remote)
};

struct WantListQueueEntry {
	struct Cid* cid;
	int priority;
	// a vector of WantListSessions
	struct Libp2pVector* sessionsRequesting;
	struct Block* block;
	int asked_network;
	int attempts;
	struct WantListQueueEntry* next;
	pthread_mutex_t block_mutex;
	pthread_cond_t block_cond;
	bitswap_future_t* future;
};

struct WantListQueue {
	pthread_mutex_t wantlist_mutex;
	struct WantListQueueEntry* head;
	size_t size;
};

/***
 * Initialize a WantListQueueEntry
 * @returns a new WantListQueueEntry
 */
struct WantListQueueEntry* ipfs_bitswap_wantlist_queue_entry_new();

/***
 * Remove resources, freeing a WantListQueueEntry
 * @param entry the WantListQueueEntry
 * @returns true(1)
 */
int ipfs_bitswap_wantlist_queue_entry_free(struct WantListQueueEntry* entry);

/***
 * Initialize a new Wantlist (there should only be 1 per instance)
 * @returns a new WantList
 */
struct WantListQueue* ipfs_bitswap_wantlist_queue_new();

/***
 * Deallocate resources of a WantList
 * @param wantlist the WantList
 * @returns true(1)
 */
int ipfs_bitswap_wantlist_queue_free(struct WantListQueue* wantlist);

/***
 * Add a Cid to the WantList
 * @param wantlist the WantList to add to
 * @param cid the Cid to add
 * @returns the correct WantListEntry or NULL if error
 */
struct WantListQueueEntry* ipfs_bitswap_wantlist_queue_add(struct WantListQueue* wantlist, const struct Cid* cid, const struct WantListSession* session);

/***
 * Remove (decrement the counter) a Cid from the WantList
 * @param wantlist the WantList
 * @param cid the Cid
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_wantlist_queue_remove(struct WantListQueue* wantlist, const struct Cid* cid, const struct WantListSession* session);

/***
 * Find a Cid in the WantList
 * @param wantlist the list
 * @param cid the Cid
 * @returns the WantListQueueEntry
 */
struct WantListQueueEntry* ipfs_bitswap_wantlist_queue_find(struct WantListQueue* wantlist, const struct Cid* cid);

/***
 * compare 2 sessions for equality
 * @param a side a
 * @param b side b
 * @returns 0 if equal, <0 if A wins, >0 if b wins
 */
int ipfs_bitswap_wantlist_session_compare(const struct WantListSession* a, const struct WantListSession* b);

/**
 * Create a new WantListSession
 * @returns the newly allocated WantListSession
 */
struct WantListSession* ipfs_bitswap_wantlist_session_new();

/**
 * Called by the Bitswap engine, this processes an item on the WantListQueue
 * @param context the context
 * @param entry the WantListQueueEntry
 * @returns true(1) on success, false(0) if not.
 */
int ipfs_bitswap_wantlist_process_entry(struct BitswapContext* context, struct WantListQueueEntry* entry);

/***
 * Pops the top one off the queue
 *
 * @param wantlist the list
 * @returns the WantListQueueEntry
 */
struct WantListQueueEntry* ipfs_bitswap_wantlist_queue_pop(struct WantListQueue* wantlist);

