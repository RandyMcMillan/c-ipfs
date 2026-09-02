/***
 * HAMT (Hash Array Mapped Trie) directory sharding for UnixFS.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ipfs/unixfs/hamt.h"
#include "ipfs/unixfs/unixfs.h"
#include "ipfs/merkledag/node.h"
#include "ipfs/merkledag/merkledag.h"
#include "libp2p/crypto/encoding/base58.h"

/* ------------------------------------------------------------------ */
/* murmur3-x64-64 (first half of 128-bit output)                     */

static inline uint64_t rotl64(uint64_t x, int8_t r) {
	return (x << r) | (x >> (64 - r));
}

static inline uint64_t fmix64(uint64_t k) {
	k ^= k >> 33;
	k *= 0xff51afd7ed558ccdULL;
	k ^= k >> 33;
	k *= 0xc4ceb9fe1a85ec53ULL;
	k ^= k >> 33;
	return k;
}

static uint64_t murmur3_x64_64(const void* key, int len, uint32_t seed) {
	const uint8_t* data = (const uint8_t*)key;
	const int nblocks = len / 16;
	uint64_t h1 = seed;
	uint64_t h2 = seed;
	const uint64_t c1 = 0x87c37b91114253d5ULL;
	const uint64_t c2 = 0x4cf5ad432745937fULL;

	const uint64_t* blocks = (const uint64_t*)(data + nblocks * 16);
	for (int i = -nblocks; i; i++) {
		uint64_t k1 = blocks[i * 2];
		uint64_t k2 = blocks[i * 2 + 1];

		k1 *= c1; k1 = rotl64(k1, 31); k1 *= c2; h1 ^= k1;
		h1 = rotl64(h1, 27); h1 += h2; h1 = h1 * 5 + 0x52dce729;

		k2 *= c2; k2 = rotl64(k2, 33); k2 *= c1; h2 ^= k2;
		h2 = rotl64(h2, 31); h2 += h1; h2 = h2 * 5 + 0x38495ab5;
	}

	const uint8_t* tail = (const uint8_t*)(data + nblocks * 16);
	uint64_t k1 = 0;
	uint64_t k2 = 0;

	switch (len & 15) {
		case 15: k2 ^= ((uint64_t)tail[14]) << 48;
		case 14: k2 ^= ((uint64_t)tail[13]) << 40;
		case 13: k2 ^= ((uint64_t)tail[12]) << 32;
		case 12: k2 ^= ((uint64_t)tail[11]) << 24;
		case 11: k2 ^= ((uint64_t)tail[10]) << 16;
		case 10: k2 ^= ((uint64_t)tail[ 9]) << 8;
		case  9: k2 ^= ((uint64_t)tail[ 8]) << 0;
		         k2 *= c2; k2 = rotl64(k2, 33); k2 *= c1; h2 ^= k2;
		case  8: k1 ^= ((uint64_t)tail[ 7]) << 56;
		case  7: k1 ^= ((uint64_t)tail[ 6]) << 48;
		case  6: k1 ^= ((uint64_t)tail[ 5]) << 40;
		case  5: k1 ^= ((uint64_t)tail[ 4]) << 32;
		case  4: k1 ^= ((uint64_t)tail[ 3]) << 24;
		case  3: k1 ^= ((uint64_t)tail[ 2]) << 16;
		case  2: k1 ^= ((uint64_t)tail[ 1]) << 8;
		case  1: k1 ^= ((uint64_t)tail[ 0]) << 0;
		         k1 *= c1; k1 = rotl64(k1, 31); k1 *= c2; h1 ^= k1;
	}

	h1 ^= (uint64_t)len; h2 ^= (uint64_t)len;
	h1 += h2;
	h2 += h1;
	h1 = fmix64(h1);
	h2 = fmix64(h2);
	h1 += h2;
	return h1;
}

/* ------------------------------------------------------------------ */

static void hash_key(const char* name, uint8_t* out, size_t out_len) {
	uint64_t h = murmur3_x64_64(name, (int)strlen(name), 0);
	/* murmur3 produces 64 bits; write as little-endian */
	for (size_t i = 0; i < 8 && i < out_len; i++) {
		out[i] = (uint8_t)(h >> (i * 8));
	}
	/* zero-extend if caller wants more than 8 bytes (shouldn't happen) */
	for (size_t i = 8; i < out_len; i++) {
		out[i] = 0;
	}
}

static int byte_at_prefix(const uint8_t* hash, int prefix_len) {
	return hash[prefix_len] & 0xFF;
}

static struct HAMTChild* find_child(struct HAMTNode* node, int slot) {
	struct HAMTChild* c = node->children;
	while (c != NULL) {
		if (c->slot == slot)
			return c;
		c = c->next;
	}
	return NULL;
}

static void remove_child(struct HAMTNode* node, int slot) {
	struct HAMTChild** pp = &node->children;
	while (*pp != NULL) {
		if ((*pp)->slot == slot) {
			struct HAMTChild* to_free = *pp;
			*pp = (*pp)->next;
			if (to_free->type == 1) {
				free(to_free->data.leaf.name);
				if (to_free->data.leaf.hash) free(to_free->data.leaf.hash);
			}
			free(to_free);
			return;
		}
		pp = &(*pp)->next;
	}
}

static struct HAMTChild* append_child(struct HAMTNode* node, int slot, int type) {
	struct HAMTChild* c = (struct HAMTChild*)calloc(1, sizeof(struct HAMTChild));
	if (!c) return NULL;
	c->slot = slot;
	c->type = type;
	c->next = node->children;
	node->children = c;
	return c;
}

static struct HAMTNode* make_subshard(struct HAMTNode* parent, int slot) {
	struct HAMTNode* sub = ipfs_hamt_new(parent->hash_type, parent->fanout);
	if (!sub) return NULL;
	sub->prefix_len = parent->prefix_len + 1;
	sub->prefix = (uint8_t*)malloc(sub->prefix_len);
	if (!sub->prefix) {
		free(sub);
		return NULL;
	}
	if (parent->prefix_len > 0)
		memcpy(sub->prefix, parent->prefix, parent->prefix_len);
	sub->prefix[parent->prefix_len] = (uint8_t)slot;
	return sub;
}

/* ------------------------------------------------------------------ */

struct HAMTNode* ipfs_hamt_new(int hash_type, int fanout) {
	struct HAMTNode* node = (struct HAMTNode*)calloc(1, sizeof(struct HAMTNode));
	if (!node) return NULL;
	node->hash_type = hash_type;
	node->fanout = fanout;
	node->prefix_len = 0;
	node->prefix = NULL;
	node->children = NULL;
	return node;
}

void ipfs_hamt_free(struct HAMTNode* node) {
	if (!node) return;
	struct HAMTChild* c = node->children;
	while (c != NULL) {
		struct HAMTChild* next = c->next;
		if (c->type == 1) {
			free(c->data.leaf.name);
			if (c->data.leaf.hash) free(c->data.leaf.hash);
		} else if (c->type == 2) {
			ipfs_hamt_free(c->data.shard);
		}
		free(c);
		c = next;
	}
	free(node->prefix);
	free(node);
}

int ipfs_hamt_add(struct HAMTNode* node, const char* name,
	const unsigned char* hash, size_t hash_size, size_t t_size) {
	if (!node || !name || !hash) return 0;

	uint8_t key_hash[64];
	hash_key(name, key_hash, sizeof(key_hash));

	struct HAMTNode* current = node;
	while (1) {
		int slot = byte_at_prefix(key_hash, current->prefix_len);
		struct HAMTChild* child = find_child(current, slot);

		if (child == NULL) {
			/* empty slot: insert leaf */
			child = append_child(current, slot, 1);
			if (!child) return 0;
			child->data.leaf.name = strdup(name);
			child->data.leaf.hash = (unsigned char*)malloc(hash_size);
			if (!child->data.leaf.hash) return 0;
			memcpy(child->data.leaf.hash, hash, hash_size);
			child->data.leaf.hash_size = hash_size;
			child->data.leaf.t_size = t_size;
			return 1;
		}

		if (child->type == 1) {
			/* leaf: check for replacement or collision */
			if (strcmp(child->data.leaf.name, name) == 0) {
				/* replace */
				free(child->data.leaf.hash);
				child->data.leaf.hash = (unsigned char*)malloc(hash_size);
				if (!child->data.leaf.hash) return 0;
				memcpy(child->data.leaf.hash, hash, hash_size);
				child->data.leaf.hash_size = hash_size;
				child->data.leaf.t_size = t_size;
				return 1;
			}
			/* collision: create sub-shard and move existing leaf */
			struct HAMTNode* sub = make_subshard(current, slot);
			if (!sub) return 0;

			/* move existing leaf into sub-shard */
			uint8_t old_hash[64];
			hash_key(child->data.leaf.name, old_hash, sizeof(old_hash));
			int old_slot = byte_at_prefix(old_hash, sub->prefix_len);
			struct HAMTChild* moved = append_child(sub, old_slot, 1);
			if (!moved) {
				ipfs_hamt_free(sub);
				return 0;
			}
			moved->data.leaf.name = child->data.leaf.name;
			moved->data.leaf.hash = child->data.leaf.hash;
			moved->data.leaf.hash_size = child->data.leaf.hash_size;
			moved->data.leaf.t_size = child->data.leaf.t_size;

			/* replace child in current with sub-shard */
			child->type = 2;
			child->data.shard = sub;
			/* leaf name/hash ownership transferred to sub; don't free */

			/* now insert new leaf into sub-shard */
			current = sub;
			continue;
		}

		if (child->type == 2) {
			/* sub-shard: descend */
			current = child->data.shard;
			continue;
		}
	}
}

int ipfs_hamt_find(struct HAMTNode* node, const char* name,
	const unsigned char** hash_out, size_t* hash_size_out, size_t* t_size_out) {
	if (!node || !name) return 0;

	uint8_t key_hash[64];
	hash_key(name, key_hash, sizeof(key_hash));

	struct HAMTNode* current = node;
	while (1) {
		int slot = byte_at_prefix(key_hash, current->prefix_len);
		struct HAMTChild* child = find_child(current, slot);
		if (child == NULL)
			return 0;

		if (child->type == 1) {
			if (strcmp(child->data.leaf.name, name) == 0) {
				if (hash_out) *hash_out = child->data.leaf.hash;
				if (hash_size_out) *hash_size_out = child->data.leaf.hash_size;
				if (t_size_out) *t_size_out = child->data.leaf.t_size;
				return 1;
			}
			return 0;
		}

		if (child->type == 2) {
			current = child->data.shard;
			continue;
		}

		return 0;
	}
}

/* ------------------------------------------------------------------ */

static void bytes_to_hex(const uint8_t* bytes, int len, char* out) {
	static const char hex[] = "0123456789abcdef";
	for (int i = 0; i < len; i++) {
		out[i * 2]     = hex[bytes[i] >> 4];
		out[i * 2 + 1] = hex[bytes[i] & 0x0f];
	}
	out[len * 2] = '\0';
}

static int _hamt_persist_node(struct HAMTNode* hamt, struct FSRepo* fs_repo,
	struct HashtableNode** out_node, size_t* out_size);

static int _persist_shard_or_leaf(struct HAMTChild* child, struct FSRepo* fs_repo,
	struct HashtableNode* parent, size_t* accumulated_size) {
	if (child->type == 1) {
		struct NodeLink* link = NULL;
		if (!ipfs_node_link_create(child->data.leaf.name,
			child->data.leaf.hash, child->data.leaf.hash_size, &link))
			return 0;
		link->t_size = child->data.leaf.t_size;
		if (!ipfs_hashtable_node_add_link(parent, link))
			return 0;
		if (accumulated_size)
			*accumulated_size += child->data.leaf.t_size;
		return 1;
	}

	if (child->type == 2) {
		struct HashtableNode* shard_node = NULL;
		size_t shard_size = 0;
		if (!_hamt_persist_node(child->data.shard, fs_repo, &shard_node, &shard_size))
			return 0;

		char hex_name[129];
		bytes_to_hex(child->data.shard->prefix, child->data.shard->prefix_len, hex_name);

		struct NodeLink* link = NULL;
		if (!ipfs_node_link_create(hex_name, shard_node->hash, shard_node->hash_size, &link)) {
			ipfs_hashtable_node_free(shard_node);
			return 0;
		}
		link->t_size = shard_size;
		if (!ipfs_hashtable_node_add_link(parent, link)) {
			ipfs_hashtable_node_free(shard_node);
			return 0;
		}
		ipfs_hashtable_node_free(shard_node);
		if (accumulated_size)
			*accumulated_size += shard_size;
		return 1;
	}

	return 0;
}

static int _hamt_persist_node(struct HAMTNode* hamt, struct FSRepo* fs_repo,
	struct HashtableNode** out_node, size_t* out_size) {
	struct HashtableNode* node = NULL;
	if (!ipfs_hashtable_node_new(&node))
		return 0;

	/* build UnixFS HAMTShard data */
	struct UnixFS* ufs = NULL;
	if (!ipfs_unixfs_new(&ufs)) {
		ipfs_hashtable_node_free(node);
		return 0;
	}
	ufs->data_type = UNIXFS_HAMT_SHARD;
	ipfs_unixfs_set_hamt_params(ufs, (unsigned long long)hamt->hash_type, (unsigned long long)hamt->fanout);

	size_t pb_size = ipfs_unixfs_protobuf_encode_size(ufs);
	unsigned char pb[pb_size];
	size_t pb_written = 0;
	if (!ipfs_unixfs_protobuf_encode(ufs, pb, pb_size, &pb_written)) {
		ipfs_unixfs_free(ufs);
		ipfs_hashtable_node_free(node);
		return 0;
	}
	ipfs_unixfs_free(ufs);
	ipfs_hashtable_node_set_data(node, pb, pb_written);

	size_t acc_size = 0;
	struct HAMTChild* c = hamt->children;
	while (c != NULL) {
		if (!_persist_shard_or_leaf(c, fs_repo, node, &acc_size)) {
			ipfs_hashtable_node_free(node);
			return 0;
		}
		c = c->next;
	}

	if (!ipfs_merkledag_add(node, fs_repo, out_size)) {
		ipfs_hashtable_node_free(node);
		return 0;
	}

	*out_node = node;
	return 1;
}

int ipfs_hamt_persist(struct HAMTNode* hamt, struct FSRepo* fs_repo,
	struct HashtableNode** out_node, size_t* out_size) {
	if (!hamt || !fs_repo || !out_node || !out_size)
		return 0;
	return _hamt_persist_node(hamt, fs_repo, out_node, out_size);
}
