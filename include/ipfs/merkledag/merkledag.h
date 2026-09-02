/***
 * Merkledag methods
 */
#ifndef __IPFS_MERKLEDAG_H__
#define __IPFS_MERKLEDAG_H__

#include "ipfs/merkledag/node.h"
#include "ipfs/repo/fsrepo/fs_repo.h"

/***
 * Adds a node to the dagService and blockService
 * @param node the node to add
 * @param fs_repo the repo to add to
 * @param bytes_written the number of bytes written
 * @returns true(1) on success
 */
int ipfs_merkledag_add(struct HashtableNode* node, struct FSRepo* fs_repo, size_t* bytes_written);

/***
 * Retrieves a node from the datastore based on the cid
 * @param cid the key to look for
 * @param node the node to be created
 * @param fs_repo the repository
 * @returns true(1) on success
 */
int ipfs_merkledag_get(const unsigned char* hash, size_t hash_size, struct HashtableNode** node, const struct FSRepo* fs_repo);

/***
 * Retrieves a node from the datastore based on the multihash
 * @param multihash the base58 encoded multihash (should start with Qm) as a null terminated string
 * @param node the node to be created
 * @param fs_repo the repository
 * @returns true(1) on success
 */
int ipfs_merkledag_get_by_multihash(const unsigned char* multihash, size_t multihash_length, struct HashtableNode** node, const struct FSRepo* fs_repo);

/***
 * Convert the data within a block to a HashtableNode
 * @param block the block
 * @param node_ptr where to put the results
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_merkledag_convert_block_to_node(struct Block* block, struct HashtableNode** node_ptr);

/***
 * Traverse the MerkleDAG starting from a hash, visiting each node up to max_depth.
 * @param fs_repo the repository to fetch nodes from
 * @param hash the starting hash (raw digest)
 * @param hash_size the length of the hash
 * @param max_depth the maximum depth to traverse (0 = just the root)
 * @param visit callback invoked for each visited node; receives node, depth, and ctx
 * @param ctx opaque pointer passed to visit callback
 * @returns true(1) on success
 */
int ipfs_merkledag_traverse(struct FSRepo* fs_repo, const unsigned char* hash, size_t hash_size,
	int max_depth, int (*visit)(struct HashtableNode*, int depth, void* ctx), void* ctx);

#endif
