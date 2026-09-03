/***
 * a thin wrapper over a datastore for getting and putting block objects
 */

#ifndef __IPFS_BLOCKS_BLOCKSTORE_H__
#define __IPFS_BLOCKS_BLOCKSTORE_H__

#include "ipfs/cid/cid.h"
#include "ipfs/repo/fsrepo/fs_repo.h"

struct BlockstoreContext {
	const struct FSRepo* fs_repo;
};

struct BlockstoreEntry {
	unsigned char* hash;
	size_t hash_size;
	size_t block_size;
	struct BlockstoreEntry* next;
};

struct Blockstore {
	struct BlockstoreContext* blockstoreContext;
	int (*Delete)(const struct BlockstoreContext* context, struct Cid* cid);
	int (*Has)(const struct BlockstoreContext* context, struct Cid* cid);
	/**
	 * Retrieve a block from the blockstore
	 */
	int (*Get)(const struct BlockstoreContext* context, struct Cid* cid, struct Block** block);
	int (*Put)(const struct BlockstoreContext* context, struct Block* block, size_t* bytes_written);
};

/***
 * Create a new Blockstore struct
 * @param fs_repo the FSRepo to use
 * @returns the new Blockstore struct, or NULL if there was a problem.
 */
struct Blockstore* ipfs_blockstore_new(const struct FSRepo* fs_repo);

/**
 * Release resources of a Blockstore struct
 * @param blockstore the struct to free
 * @returns true(1)
 */
int ipfs_blockstore_free(struct Blockstore* blockstore);

/**
 * Delete a block based on its Cid
 * @param context the context
 * @param cid the Cid to look for
 * @param returns true(1) on success
 */
int ipfs_blockstore_delete(const struct BlockstoreContext* context, struct Cid* cid);

/***
 * Determine if the Cid can be found
 * @param context the context
 * @param cid the Cid to look for
 * @returns true(1) if found
 */
int ipfs_blockstore_has(const struct BlockstoreContext* context, struct Cid* cid);

/***
 * Find a block based on its Cid
 * @param context the context
 * @param cid the Cid to look for
 * @param block where to put the data to be returned
 * @returns true(1) on success
 */
int ipfs_blockstore_get(const struct BlockstoreContext* context, struct Cid* cid, struct Block** block);

/***
 * Put a block in the blockstore
 * @param block the block to store
 * @returns true(1) on success
 */
int ipfs_blockstore_put(const struct BlockstoreContext* context, struct Block* block, size_t* bytes_written);

/***
 * Put a struct UnixFS in the blockstore
 * @param unix_fs the structure
 * @param fs_repo the repo to place the strucure in
 * @param bytes_written the number of bytes written to the blockstore
 * @returns true(1) on success
 */
int ipfs_blockstore_put_unixfs(const struct UnixFS* unix_fs, const struct FSRepo* fs_repo, size_t* bytes_written);

/***
 * Find a UnixFS struct based on its hash
 * @param hash the hash to look for
 * @param hash_length the length of the hash
 * @param unix_fs the struct to fill
 * @param fs_repo where to look for the data
 * @returns true(1) on success
 */
int ipfs_blockstore_get_unixfs(const unsigned char* hash, size_t hash_length, struct UnixFS** block, const struct FSRepo* fs_repo);

/**
 * Put a struct Node in the blockstore
 */
int ipfs_blockstore_put_node(const struct HashtableNode* node, const struct FSRepo* fs_repo, size_t* bytes_written);
int ipfs_blockstore_get_node(const unsigned char* hash, size_t hash_length, struct HashtableNode** node, const struct FSRepo* fs_repo);

/**
 * List all blocks in the blockstore.
 * @param fs_repo the repository
 * @param entries linked list of BlockstoreEntry (caller must free with ipfs_blockstore_list_free)
 * @returns number of entries on success, -1 on error
 */
int ipfs_blockstore_list(const struct FSRepo* fs_repo, struct BlockstoreEntry** entries);

/**
 * Free a linked list of BlockstoreEntry
 */
void ipfs_blockstore_list_free(struct BlockstoreEntry* entries);

#endif
