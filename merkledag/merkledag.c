/**
 * A basic storage building block of the IPFS system
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libp2p/crypto/sha256.h"
#include "mh/multihash.h"
#include "mh/hashes.h"
#include "ipfs/merkledag/merkledag.h"
#include "ipfs/unixfs/unixfs.h"

/***
 * Convert a HashtableNode into a Block
 * @param node the node to convert
 * @param blockResult where to put the results
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_merkledag_convert_node_to_block(struct HashtableNode* node, struct Block** blockResult) {
	*blockResult = ipfs_block_new();
	if (*blockResult == NULL)
		return 0;
	struct Block* block = *blockResult;
	block->cid = ipfs_cid_new(1, node->hash, node->hash_size, CID_DAG_PROTOBUF);
	if (block->cid == NULL) {
		ipfs_block_free(block);
		*blockResult = NULL;
		return 0;
	}
	block->data_length = ipfs_hashtable_node_protobuf_encode_size(node);
	block->data = malloc(block->data_length);
	if (block->data == NULL) {
		ipfs_block_free(block);
		*blockResult = NULL;
		return 0;
	}
	if (!ipfs_hashtable_node_protobuf_encode(node, block->data, block->data_length, &block->data_length)) {
		ipfs_block_free(block);
		*blockResult = NULL;
		return 0;
	}
	return 1;
}

/***
 * Convert the data within a block to a HashtableNode
 * @param block the block
 * @param node_ptr where to put the results
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_merkledag_convert_block_to_node(struct Block* block, struct HashtableNode** node_ptr) {
	return ipfs_hashtable_node_protobuf_decode(block->data, block->data_length, node_ptr);
}

/***
 * Adds a node to the dagService and blockService
 * @param node the node to add
 * @param fs_repo the repo to add to
 * @param bytes_written the number of bytes written
 * @returns true(1) on success
 */
int ipfs_merkledag_add(struct HashtableNode* node, struct FSRepo* fs_repo, size_t* bytes_written) {
	// taken from merkledag.go line 59
	int retVal = 0;

	// compute the hash if necessary
	if (node->hash == NULL) {
		size_t protobuf_size = ipfs_hashtable_node_protobuf_encode_size(node);
		unsigned char protobuf[protobuf_size];
		size_t bytes_encoded;
		retVal = ipfs_hashtable_node_protobuf_encode(node, protobuf, protobuf_size, &bytes_encoded);

		node->hash_size = 32;
		node->hash = (unsigned char*)malloc(node->hash_size);
		if (node->hash == NULL) {
			return 0;
		}
		if (libp2p_crypto_hashing_sha256(protobuf, bytes_encoded, &node->hash[0]) == 0) {
			free(node->hash);
			return 0;
		}
	}

	// write to block store & datastore
	struct Block* block = NULL;
	if (!ipfs_merkledag_convert_node_to_block(node, &block)) {
		return 0;
	}
	retVal = ipfs_repo_fsrepo_block_write(block, fs_repo, bytes_written);
	if (retVal == 0) {
		ipfs_block_free(block);
		return 0;
	}
	ipfs_block_free(block);
	// TODO: call HasBlock (unsure why as yet)
	return 1;
}

/***
 * Retrieves a node from the datastore based on the hash
 * @param hash the key to look for
 * @param hash_size the length of the key
 * @param node the node to be created
 * @param fs_repo the repository
 * @returns true(1) on success
 */
int ipfs_merkledag_get(const unsigned char* hash, size_t hash_size, struct HashtableNode** node, const struct FSRepo* fs_repo) {
	int retVal = 1;
	struct DatastoreRecord* datastore_record = NULL;

	// look for the node in the datastore. If it is not there, it is not a node.
	// If it exists, it is only a block.
	retVal = fs_repo->config->datastore->datastore_get(hash, hash_size, &datastore_record, fs_repo->config->datastore);
	if (retVal == 0)
		return 0;

	libp2p_datastore_record_free(datastore_record);

	// we have the record from the db. Go get the node from the blockstore
	if (!ipfs_repo_fsrepo_node_read(hash, hash_size, node, fs_repo))
		return 0;

	// set the hash
	ipfs_hashtable_node_set_hash(*node, hash, hash_size);

	return 1;
}

int ipfs_merkledag_get_by_multihash(const unsigned char* multihash, size_t multihash_length, struct HashtableNode** node, const struct FSRepo* fs_repo) {
	// convert to hash
	size_t hash_size = 0;
	unsigned char* hash = NULL;
	if (mh_multihash_digest(multihash, multihash_length, &hash, &hash_size) < 0) {
		return 0;
	}
	return ipfs_merkledag_get(hash, hash_size, node, fs_repo);
}

static int traverse_helper(struct FSRepo* fs_repo, const unsigned char* hash, size_t hash_size,
	int depth, int max_depth, int (*visit)(struct HashtableNode*, int depth, void* ctx), void* ctx)
{
	if (depth > max_depth)
		return 1;

	struct HashtableNode* node = NULL;
	if (!ipfs_merkledag_get(hash, hash_size, &node, fs_repo))
		return 0;

	int ret = visit(node, depth, ctx);
	if (ret == 0) {
		ipfs_hashtable_node_free(node);
		return 0;
	}

	struct NodeLink* link = node->head_link;
	while (link != NULL) {
		if (!traverse_helper(fs_repo, link->hash, link->hash_size, depth + 1, max_depth, visit, ctx)) {
			ipfs_hashtable_node_free(node);
			return 0;
		}
		link = link->next;
	}

	ipfs_hashtable_node_free(node);
	return 1;
}

int ipfs_merkledag_traverse(struct FSRepo* fs_repo, const unsigned char* hash, size_t hash_size,
	int max_depth, int (*visit)(struct HashtableNode*, int depth, void* ctx), void* ctx)
{
	if (max_depth < 0 || visit == NULL)
		return 0;
	return traverse_helper(fs_repo, hash, hash_size, 0, max_depth, visit, ctx);
}
