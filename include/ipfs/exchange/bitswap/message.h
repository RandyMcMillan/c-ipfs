#pragma once
/***
 * A protobuf-able Bitswap Message
 */
#include <stdint.h>
#include <stddef.h>
#include "libp2p/utils/vector.h"

enum BitswapWantType {
	WANT_TYPE_BLOCK = 0,
	WANT_TYPE_HAVE = 1
};

struct WantlistEntry {
	// optional string block = 1, the block cid (cidV0 in bitswap 1.0.0, cidV1 in bitswap 1.1.0
	unsigned char* block;
	size_t block_size;
	// optional int32 priority = 2, the priority (normalized). default to 1
	uint32_t priority;
	// optional bool cancel = 3, whether this revokes an entry
	uint8_t cancel;
	// optional WantType wantType = 4, default Block (0)
	enum BitswapWantType want_type;
	// optional bool sendDontHave = 5, default false
	uint8_t send_dont_have;
};

struct BitswapWantlist {
	// repeated WantlistEntry entries = 1, a list of wantlist entries
	struct Libp2pVector* entries;
	// optional bool full = 2, whether this is the full wantlist. default to false
	uint8_t full;
};

struct BitswapBlock {
	// optional bytes prefix = 1, // CID prefix (cid version, multicodec, and multihash prefix (type + length))
	uint8_t* prefix;
	size_t prefix_size;
	// optional bytes data = 2
	uint8_t* bytes;
	size_t bytes_size;
};

enum BitswapBlockPresenceType {
	BLOCK_PRESENCE_HAVE = 0,
	BLOCK_PRESENCE_DONT_HAVE = 1
};

struct BitswapBlockPresence {
	// optional bytes cid = 1
	unsigned char* cid;
	size_t cid_size;
	// optional BlockPresenceType type = 2
	enum BitswapBlockPresenceType type;
};

struct BitswapMessage {
	// optional Wantlist wantlist = 1
	struct BitswapWantlist* wantlist;
	// repeated bytes blocks = 2, used to send Blocks in bitswap 1.0.0
	struct Libp2pVector* blocks;
	// repeated Block payload = 3, used to send Blocks in bitswap 1.1.0
	struct Libp2pVector* payload;
	// repeated BlockPresence blockPresences = 4, NEW in bitswap 1.2.0
	struct Libp2pVector* block_presences;
	// optional int32 pendingBytes = 5, NEW in bitswap 1.2.0
	int32_t pending_bytes;
};

/***
 * Allocate memory for a struct BitswapBlock
 * @returns a new BitswapBlock
 */
struct BitswapBlock* ipfs_bitswap_block_new();

/**
 * Deallocate memory for a struct BitswapBlock
 * @param block the block to deallocate
 * @returns true(1)
 */
int ipfs_bitswap_block_free(struct BitswapBlock* block);

/**
 * Retrieve an estimate of the size of a protobuf'd BitswapBlock
 * @returns the approximate (maximum actually) size of a protobuf'd BitswapBlock
 */
size_t ipfs_bitswap_message_block_protobuf_size(struct BitswapBlock* block);

/***
 * Encode a BitswapBlock
 * @param incoming the block to encode
 * @param outgoing where to place the results
 * @param max_size the maximum allocated space for outgoing
 * @param bytes_written the number of bytes written to outgoing
 */
int ipfs_bitswap_message_block_protobuf_encode(struct BitswapBlock* incoming, uint8_t* outgoing, size_t max_size, size_t* bytes_written);

/***
 * Decode a protobuf to a BitswapBlock
 * @param buffer the incoming protobuf
 * @param buffer_length the length of the incoming protobuf buffer
 * @param output a pointer to the BitswapBlock that will be allocated
 * @returns true(1) on success, false(0) if not. If false, any memory was deallocated
 */
int ipfs_bitswap_message_block_protobuf_decode(uint8_t* buffer, size_t buffer_length, struct BitswapBlock** output);

/***
 * Allocate memory for a new WantlistEntry
 * @returns the newly allocated WantlistEntry
 */
struct WantlistEntry* ipfs_bitswap_wantlist_entry_new();

/***
 * Free allocations of a WantlistEntry
 * @param entry the WantlistEntry
 * @returns true(1)
 */
int ipfs_bitswap_wantlist_entry_free(struct WantlistEntry* entry);

/**
 * Retrieve an estimate of the size of a protobuf'd WantlistEntry
 * @param entry the struct to examine
 * @returns the approximate (maximum actually) size of a protobuf'd WantlistEntry
 */
size_t ipfs_bitswap_wantlist_entry_protobuf_encode_size(struct WantlistEntry* entry);

/***
 * Encode a WantlistEntry into a Protobuf
 * @param entry the WantlistEntry to encode
 * @param buffer where to put the results
 * @param buffer_length the maximum size of the buffer
 * @param bytes_written the number of bytes written into the buffer
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_bitswap_wantlist_entry_protobuf_encode(struct WantlistEntry* entry, unsigned char* buffer, size_t buffer_length, size_t* bytes_written);

/***
 * Decode a protobuf into a struct WantlistEntry
 * @param buffer the protobuf buffer
 * @param buffer_length the length of the data in the protobuf buffer
 * @param output the resultant WantlistEntry
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_wantlist_entry_protobuf_decode(unsigned char* buffer, size_t buffer_length, struct WantlistEntry** output);

/***
 * Allocate memory for a new Bitswap Message WantList
 * @returns the allocated struct BitswapWantlist
 */
struct BitswapWantlist* ipfs_bitswap_wantlist_new();

/**
 * Free the resources used by a Wantlist
 * @param list the list to free
 * @returns true(1)
 */
int ipfs_bitswap_wantlist_free(struct BitswapWantlist* list);

/***
 * Calculate the maximum size of a protobuf'd BitswapWantlist
 * @param list the Wantlist
 * @returns the maximum size of the protobuf'd list
 */
size_t ipfs_bitswap_wantlist_protobuf_encode_size(struct BitswapWantlist* list);

/***
 * Encode a BitswapWantlist into a protobuf buffer
 * @param list the list to encode
 * @param buffer the buffer to fill
 * @param buffer_length the length of the allocated buffer
 * @param bytes_written the total number of bytes written to the buffer
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_wantlist_protobuf_encode(struct BitswapWantlist* list, unsigned char* buffer, size_t buffer_length, size_t* bytes_written);

/***
 * Decode a Wantlist from a protobuf
 * @param buffer the protobuf
 * @param buffer_length the length of the protobuf
 * @param output the newly allocated BitswapWantlist
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_wantlist_protobuf_decode(unsigned char* buffer, size_t buffer_length, struct BitswapWantlist** output);

/***
 * Bitswap Message
 *
 */

/***
 * Allocate memory for a new Bitswap Message
 * @returns the allocated struct BitswapMessage
 */
struct BitswapMessage* ipfs_bitswap_message_new();

/**
 * Free the resources used by a BitswapMessage
 * @param message the BitswapMessage to free
 * @returns true(1)
 */
int ipfs_bitswap_message_free(struct BitswapMessage* message);

/***
 * Calculate the maximum size of a protobuf'd BitswapMessage
 * @param message the BitswapMessage
 * @returns the maximum size of the protobuf'd BitswapMessage
 */
size_t ipfs_bitswap_message_protobuf_encode_size(const struct BitswapMessage* message);

/***
 * Encode a BitswapMessage into a protobuf buffer
 * @param message the message to encode
 * @param buffer the buffer to fill
 * @param buffer_length the length of the allocated buffer
 * @param bytes_written the total number of bytes written to the buffer
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_message_protobuf_encode(const struct BitswapMessage* message, unsigned char* buffer, size_t buffer_length, size_t* bytes_written);

/***
 * Decode a BitswapMessage from a protobuf
 * @param buffer the protobuf
 * @param buffer_length the length of the protobuf
 * @param output the newly allocated BitswapMessage
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_message_protobuf_decode(const uint8_t* buffer, size_t buffer_length, struct BitswapMessage** output);

/****
 * Allocate memory for a new BitswapBlockPresence
 * @returns the newly allocated BitswapBlockPresence
 */
struct BitswapBlockPresence* ipfs_bitswap_block_presence_new();

/**
 * Free the resources used by a BitswapBlockPresence
 * @param presence the BitswapBlockPresence to free
 * @returns true(1)
 */
int ipfs_bitswap_block_presence_free(struct BitswapBlockPresence* presence);

/***
 * Calculate the maximum size of a protobuf'd BitswapBlockPresence
 * @param presence the BitswapBlockPresence
 * @returns the maximum size
 */
size_t ipfs_bitswap_block_presence_protobuf_encode_size(struct BitswapBlockPresence* presence);

/***
 * Encode a BitswapBlockPresence into a protobuf buffer
 * @param presence the presence to encode
 * @param buffer the buffer to fill
 * @param buffer_length the length of the allocated buffer
 * @param bytes_written the total number of bytes written to the buffer
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_block_presence_protobuf_encode(struct BitswapBlockPresence* presence, unsigned char* buffer, size_t buffer_length, size_t* bytes_written);

/***
 * Decode a BitswapBlockPresence from a protobuf
 * @param buffer the protobuf
 * @param buffer_length the length of the protobuf
 * @param output the newly allocated BitswapBlockPresence
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_block_presence_protobuf_decode(unsigned char* buffer, size_t buffer_length, struct BitswapBlockPresence** output);

/****
 * Add a vector of Cids to the bitswap message
 * @param message the message
 * @param cids a Libp2pVector of cids
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_message_add_wantlist_items(struct BitswapMessage* message, struct Libp2pVector* cids);

/***
 * Add the blocks to the BitswapMessage
 * @param message the message
 * @param blocks the requested blocks
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_bitswap_message_add_blocks(struct BitswapMessage* message, struct Libp2pVector* blocks, struct Libp2pVector* cids_they_want);
