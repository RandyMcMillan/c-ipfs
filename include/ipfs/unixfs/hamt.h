/***
 * HAMT (Hash Array Mapped Trie) directory sharding for UnixFS.
 * Compatible with go-unixfs / Kubo v0.43.0 HAMT layout.
 *
 * Default parameters:
 *  - hash function: murmur3-x64-64 (multicodec 0x22)
 *  - fanout: 256
 *  - bucket size: 3 (protocol constant; not stored in protobuf)
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>

/* forward declarations */
struct FSRepo;
struct HashtableNode;

struct HAMTLeaf {
	char* name;
	unsigned char* hash;
	size_t hash_size;
	size_t t_size;
};

struct HAMTChild {
	int slot;          // 0 .. (fanout-1)
	int type;          // 1 = leaf, 2 = shard
	union {
		struct HAMTLeaf leaf;
		struct HAMTNode* shard;
	} data;
	struct HAMTChild* next;
};

struct HAMTNode {
	int hash_type;     // multicodec of hash function (default 0x22 = murmur3)
	int fanout;        // arity (default 256)
	int prefix_len;    // number of hash bytes consumed above this node
	uint8_t* prefix;   // the prefix bytes (length = prefix_len)
	struct HAMTChild* children;
};

/**
 * Create a new empty HAMT root node
 */
struct HAMTNode* ipfs_hamt_new(int hash_type, int fanout);

/**
 * Free a HAMT node and all its descendants
 */
void ipfs_hamt_free(struct HAMTNode* node);

/**
 * Add a leaf entry to the HAMT. If an entry with the same name exists,
 * it is replaced.
 * @param node the HAMT node to add to
 * @param name the file name (key)
 * @param hash the CID hash of the target node
 * @param hash_size the length of the hash
 * @param t_size the cumulative size of the target
 * @returns 1 on success, 0 on failure
 */
int ipfs_hamt_add(struct HAMTNode* node, const char* name,
	const unsigned char* hash, size_t hash_size, size_t t_size);

/**
 * Find a leaf entry by name.
 * @param node the HAMT root to search
 * @param name the file name (key)
 * @param hash_out if found, set to the hash pointer (do not free)
 * @param hash_size_out if found, set to the hash size
 * @param t_size_out if found, set to the target size
 * @returns 1 if found, 0 otherwise
 */
int ipfs_hamt_find(struct HAMTNode* node, const char* name,
	const unsigned char** hash_out, size_t* hash_size_out, size_t* t_size_out);

/**
 * Persist a HAMT node as a DAG-PB HashtableNode.
 * This recursively persists all sub-shards and returns the root node.
 * @param hamt the HAMT to persist
 * @param fs_repo the repository
 * @param out_node where to store the resulting HashtableNode
 * @param out_size where to store the serialized size of the root node
 * @returns 1 on success, 0 on failure
 */
int ipfs_hamt_persist(struct HAMTNode* hamt, struct FSRepo* fs_repo,
	struct HashtableNode** out_node, size_t* out_size);
