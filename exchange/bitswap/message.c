#include <stdlib.h>
#include "protobuf.h"
#include "varint.h"
#include "libp2p/utils/vector.h"
#include "ipfs/blocks/block.h"
#include "ipfs/exchange/bitswap/message.h"
#include "ipfs/exchange/bitswap/peer_request_queue.h"

/***
 * Allocate memory for a struct BitswapBlock
 * @returns a new BitswapBlock
 */
struct BitswapBlock* ipfs_bitswap_block_new() {
	struct BitswapBlock* block = (struct BitswapBlock*) malloc(sizeof(struct BitswapBlock));
	if (block != NULL) {
		block->bytes_size = 0;
		block->bytes = NULL;
		block->prefix_size = 0;
		block->prefix = NULL;
	}
	return block;
}

/**
 * Deallocate memory for a struct BitswapBlock
 * @param block the block to deallocate
 * @returns true(1)
 */
int ipfs_bitswap_block_free(struct BitswapBlock* block) {
	if (block != NULL) {
		if (block->bytes != NULL)
			free(block->bytes);
		if (block->prefix != NULL)
			free(block->prefix);
		free(block);
	}
	return 1;
}

/**
 * Retrieve an estimate of the size of a protobuf'd BitswapBlock
 * @returns the approximate (maximum actually) size of a protobuf'd BitswapBlock
 */
size_t ipfs_bitswap_message_block_protobuf_size(struct BitswapBlock* block) {
	// protobuf prefix + prefix + bytes = 33 + array sizes
	return 33 + block->prefix_size + block->bytes_size;
}

/***
 * Encode a BitswapBlock
 * @param incoming the block to encode
 * @param outgoing where to place the results
 * @param max_size the maximum allocated space for outgoing
 * @param bytes_written the number of bytes written to outgoing
 */
int ipfs_bitswap_message_block_protobuf_encode(struct BitswapBlock* incoming, uint8_t* outgoing, size_t max_size, size_t* bytes_written) {
	// 2 WIRETYPE_LENGTH_DELIMITED fields of prefix and bytes
	size_t bytes_used;
	*bytes_written = 0;

	if (incoming != NULL) {
		if (!protobuf_encode_length_delimited(1, WIRETYPE_LENGTH_DELIMITED, (char*)incoming->prefix, incoming->prefix_size, outgoing, max_size, &bytes_used))
			return 0;
		*bytes_written += bytes_used;
		if (!protobuf_encode_length_delimited(2, WIRETYPE_LENGTH_DELIMITED, (char*)incoming->bytes, incoming->bytes_size, &outgoing[*bytes_written], max_size - (*bytes_written), &bytes_used))
			return 0;
		*bytes_written += bytes_used;
	}
	return 1;
}

/***
 * Decode a protobuf to a BitswapBlock
 * @param buffer the incoming protobuf
 * @param buffer_length the length of the incoming protobuf buffer
 * @param output a pointer to the BitswapBlock that will be allocated
 * @returns true(1) on success, false(0) if not. If false, any memory was deallocated
 */
int ipfs_bitswap_message_block_protobuf_decode(uint8_t* buffer, size_t buffer_length, struct BitswapBlock** output) {
	size_t pos = 0;
	int retVal = 0;

	*output = NULL;

	// short cut for nulls
	if (buffer_length == 0)
		return 1;

	*output = (struct BitswapBlock*) malloc(sizeof(struct BitswapBlock));
	if (*output == NULL)
		goto exit;

	struct BitswapBlock* block = *output;

	while(pos < buffer_length) {
		size_t bytes_read = 0;
		int field_no;
		enum WireType field_type;
		if (protobuf_decode_field_and_type(&buffer[pos], buffer_length, &field_no, &field_type, &bytes_read) == 0) {
			goto exit;
		}
		pos += bytes_read;
		switch(field_no) {
			case (1):
				if (!protobuf_decode_length_delimited(&buffer[pos], buffer_length - pos, (char**)&block->prefix, &block->prefix_size, &bytes_read))
					goto exit;
				break;
			case (2):
				if (!protobuf_decode_length_delimited(&buffer[pos], buffer_length - pos, (char**)&block->bytes, &block->bytes_size, &bytes_read))
					goto exit;
				break;
		}
	}

	retVal = 1;
	exit:
	if (retVal == 0) {
		if (*output != NULL)
			free(*output);
		*output = NULL;
	}
	return retVal;
}

/***
 * Bitswap BlockPresence (1.2.0)
 */

struct BitswapBlockPresence* ipfs_bitswap_block_presence_new() {
	struct BitswapBlockPresence* presence = (struct BitswapBlockPresence*) malloc(sizeof(struct BitswapBlockPresence));
	if (presence != NULL) {
		presence->cid = NULL;
		presence->cid_size = 0;
		presence->type = BLOCK_PRESENCE_HAVE;
	}
	return presence;
}

int ipfs_bitswap_block_presence_free(struct BitswapBlockPresence* presence) {
	if (presence != NULL) {
		if (presence->cid != NULL)
			free(presence->cid);
		free(presence);
	}
	return 1;
}

size_t ipfs_bitswap_block_presence_protobuf_encode_size(struct BitswapBlockPresence* presence) {
	return 22 + presence->cid_size;
}

int ipfs_bitswap_block_presence_protobuf_encode(struct BitswapBlockPresence* presence, unsigned char* buffer, size_t buffer_length, size_t* bytes_written) {
	size_t bytes_used;
	*bytes_written = 0;
	if (presence != NULL) {
		if (!protobuf_encode_length_delimited(1, WIRETYPE_LENGTH_DELIMITED, (char*)presence->cid, presence->cid_size, &buffer[*bytes_written], buffer_length - (*bytes_written), &bytes_used))
			return 0;
		*bytes_written += bytes_used;
		if (!protobuf_encode_varint(2, WIRETYPE_VARINT, presence->type, &buffer[*bytes_written], buffer_length - (*bytes_written), &bytes_used))
			return 0;
		*bytes_written += bytes_used;
	}
	return 1;
}

int ipfs_bitswap_block_presence_protobuf_decode(unsigned char* buffer, size_t buffer_length, struct BitswapBlockPresence** output) {
	size_t pos = 0;
	int retVal = 0;
	*output = NULL;
	if (buffer_length == 0)
		return 1;
	*output = ipfs_bitswap_block_presence_new();
	if (*output == NULL)
		goto exit;
	struct BitswapBlockPresence* presence = *output;
	while(pos < buffer_length) {
		size_t bytes_read = 0;
		int field_no;
		enum WireType field_type;
		if (protobuf_decode_field_and_type(&buffer[pos], buffer_length, &field_no, &field_type, &bytes_read) == 0)
			goto exit;
		pos += bytes_read;
		switch(field_no) {
			case (1):
				if (!protobuf_decode_length_delimited(&buffer[pos], buffer_length - pos, (char**)&presence->cid, &presence->cid_size, &bytes_read))
					goto exit;
				pos += bytes_read;
				break;
			case (2):
				presence->type = (enum BitswapBlockPresenceType) varint_decode(&buffer[pos], buffer_length - pos, &bytes_read);
				pos += bytes_read;
				break;
		}
	}
	retVal = 1;
	exit:
	if (retVal == 0) {
		if (*output != NULL)
			ipfs_bitswap_block_presence_free(*output);
		*output = NULL;
	}
	return retVal;
}

/***
 * Allocate memory for a new WantlistEntry
 * @returns the newly allocated WantlistEntry
 */
struct WantlistEntry* ipfs_bitswap_wantlist_entry_new() {
	struct WantlistEntry* entry = (struct WantlistEntry*) malloc(sizeof(struct WantlistEntry));
	if (entry == NULL)
		return NULL;

	entry->block = NULL;
	entry->block_size = 0;
	entry->cancel = 0;
	entry->priority = 1;
	entry->want_type = WANT_TYPE_BLOCK;
	entry->send_dont_have = 0;

	return entry;
}

/***
 * Free allocations of a WantlistEntry
 * @param entry the WantlistEntry
 * @returns true(1)
 */
int ipfs_bitswap_wantlist_entry_free(struct WantlistEntry* entry) {
	if (entry != NULL) {
		if (entry->block != NULL)
			free(entry->block);
		free(entry);
		entry = NULL;
	}
	return 1;
}

/**
 * Retrieve an estimate of the size of a protobuf'd WantlistEntry
 * @param entry the struct to examine
 * @returns the approximate (maximum actually) size of a protobuf'd WantlistEntry
 */
size_t ipfs_bitswap_wantlist_entry_protobuf_encode_size(struct WantlistEntry* entry) {
	// protobuf prefix + block + cancel + priority + wantType + sendDontHave
	return 44 + entry->block_size;
}

/***
 * Encode a WantlistEntry into a Protobuf
 * @param entry the WantlistEntry to encode
 * @param buffer where to put the results
 * @param buffer_length the maximum size of the buffer
 * @param bytes_written the number of bytes written into the buffer
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_bitswap_wantlist_entry_protobuf_encode(struct WantlistEntry* entry, unsigned char* buffer, size_t buffer_length, size_t* bytes_written) {
	size_t bytes_used;
	*bytes_written = 0;

	if (entry != NULL) {
		if (!protobuf_encode_length_delimited(1, WIRETYPE_LENGTH_DELIMITED, (char*)entry->block, entry->block_size, &buffer[*bytes_written], buffer_length - (*bytes_written), &bytes_used))
			return 0;
		*bytes_written += bytes_used;
		if (!protobuf_encode_varint(2, WIRETYPE_VARINT, entry->cancel, &buffer[*bytes_written], buffer_length - (*bytes_written), &bytes_used))
			return 0;
		*bytes_written += bytes_used;
		if (!protobuf_encode_varint(3, WIRETYPE_VARINT, entry->priority, &buffer[*bytes_written], buffer_length - (*bytes_written), &bytes_used))
			return 0;
		*bytes_written += bytes_used;
		if (!protobuf_encode_varint(4, WIRETYPE_VARINT, entry->want_type, &buffer[*bytes_written], buffer_length - (*bytes_written), &bytes_used))
			return 0;
		*bytes_written += bytes_used;
		if (!protobuf_encode_varint(5, WIRETYPE_VARINT, entry->send_dont_have, &buffer[*bytes_written], buffer_length - (*bytes_written), &bytes_used))
			return 0;
		*bytes_written += bytes_used;
	}
	return 1;
}

/***
 * Decode a protobuf into a struct WantlistEntry
 * @param buffer the protobuf buffer
 * @param buffer_length the length of the data in the protobuf buffer
 * @param output the resultant WantlistEntry
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_wantlist_entry_protobuf_decode(unsigned char* buffer, size_t buffer_length, struct WantlistEntry** output) {
	size_t pos = 0;
	int retVal = 0;
	struct WantlistEntry* entry = NULL;

	*output = NULL;

	// short cut for nulls
	if (buffer_length == 0)
		return 1;

	*output = ipfs_bitswap_wantlist_entry_new();
	if (*output == NULL)
		goto exit;

	entry = *output;

	while(pos < buffer_length) {
		size_t bytes_read = 0;
		int field_no;
		enum WireType field_type;
		if (protobuf_decode_field_and_type(&buffer[pos], buffer_length, &field_no, &field_type, &bytes_read) == 0) {
			goto exit;
		}
		pos += bytes_read;
		switch(field_no) {
			case (1):
				if (!protobuf_decode_length_delimited(&buffer[pos], buffer_length - pos, (char**)&entry->block, &entry->block_size, &bytes_read))
					goto exit;
				pos += bytes_read;
				break;
			case (2):
				entry->cancel = varint_decode(&buffer[pos], buffer_length - pos, &bytes_read);
				pos += bytes_read;
				break;
			case (3):
				entry->priority = varint_decode(&buffer[pos], buffer_length - pos, &bytes_read);
				pos += bytes_read;
				break;
			case (4):
				entry->want_type = (enum BitswapWantType) varint_decode(&buffer[pos], buffer_length - pos, &bytes_read);
				pos += bytes_read;
				break;
			case (5):
				entry->send_dont_have = varint_decode(&buffer[pos], buffer_length - pos, &bytes_read);
				pos += bytes_read;
				break;
		}

	}

	retVal = 1;
	exit:
	if (retVal == 0) {
		if (entry != NULL)
			free(entry);
		*output = NULL;
	}
	return retVal;
}

/***
 * Allocate memory for a new Bitswap Message WantList
 * @returns the allocated struct BitswapWantlist
 */
struct BitswapWantlist* ipfs_bitswap_wantlist_new() {
	struct BitswapWantlist* list = (struct BitswapWantlist*) malloc(sizeof(struct BitswapWantlist));

	if (list != NULL) {
		list->entries = NULL;
		list->full = 0;
	}

	return list;
}

/**
 * Free the resources used by a Wantlist
 * @param list the list to free
 * @returns true(1)
 */
int ipfs_bitswap_wantlist_free(struct BitswapWantlist* list) {
	if (list != NULL) {
		if (list->entries != NULL) {
			for(int i = 0; i < list->entries->total; i++) {
				// free each item in the vector
				struct WantlistEntry* entry = (struct WantlistEntry*) libp2p_utils_vector_get(list->entries, i);
				ipfs_bitswap_wantlist_entry_free(entry);
			}
			libp2p_utils_vector_free(list->entries);
		}
		free(list);
	}
	return 1;
}

/***
 * Calculate the maximum size of a protobuf'd BitswapWantlist
 * @param list the Wantlist
 * @returns the maximum size of the protobuf'd list
 */
size_t ipfs_bitswap_wantlist_protobuf_encode_size(struct BitswapWantlist* list) {
	size_t total = 0;
	if (list != NULL) {
		if (list->entries != NULL) {
			for(int i = 0; i < list->entries->total; i++) {
				struct WantlistEntry* entry = (struct WantlistEntry*) libp2p_utils_vector_get(list->entries, i);
				total += ipfs_bitswap_wantlist_entry_protobuf_encode_size(entry);
			}
		}
		if (list->full != 0) {
			total += 11 + 12 + 11;
		}
	}
	return total;
}

/***
 * Encode a BitswapWantlist into a protobuf buffer
 * @param list the list to encode
 * @param buffer the buffer to fill
 * @param buffer_length the length of the allocated buffer
 * @param bytes_written the total number of bytes written to the buffer
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_wantlist_protobuf_encode(struct BitswapWantlist* list, unsigned char* buffer, size_t buffer_length, size_t* bytes_written) {
	size_t bytes_used = 0;
	*bytes_written = 0;

	if (list != NULL) {
		// the vector of entries
		if (list->entries != NULL) {
			for(int i = 0; i < list->entries->total; i++) {
				struct WantlistEntry* entry = (struct WantlistEntry*) libp2p_utils_vector_get(list->entries, i);
				// protobuf the entry
				size_t temp_buffer_size = ipfs_bitswap_wantlist_entry_protobuf_encode_size(entry);
				uint8_t* temp_buffer = (uint8_t*) malloc(temp_buffer_size);
				if (temp_buffer == NULL)
					return 0;
				if (!ipfs_bitswap_wantlist_entry_protobuf_encode(entry, temp_buffer, temp_buffer_size, &temp_buffer_size)) {
					free(temp_buffer);
					return 0;
				}
				// we've got the protobuf'd entry, now put it in the real buffer
				if (!protobuf_encode_length_delimited(1, WIRETYPE_LENGTH_DELIMITED, (char*)temp_buffer, temp_buffer_size, &buffer[*bytes_written], buffer_length - (*bytes_written), &bytes_used)) {
					free(temp_buffer);
					return 0;
				}
				// all went okay. Clean up and do it again...
				free(temp_buffer);
				*bytes_written += bytes_used;
			}
		}
		// if this is the full list or not...
		if (list->full != 0) {
			if (!protobuf_encode_varint(2, WIRETYPE_VARINT, list->full, &buffer[*bytes_written], buffer_length - (*bytes_written), &bytes_used))
				return 0;
			*bytes_written += bytes_used;
		}
	}
	return 1;
}

/***
 * Decode a Wantlist from a protobuf
 * @param buffer the protobuf
 * @param buffer_length the length of the protobuf
 * @param output the newly allocated BitswapWantlist
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_wantlist_protobuf_decode(unsigned char* buffer, size_t buffer_length, struct BitswapWantlist** output) {
	size_t pos = 0;

	*output = NULL;

	// short cut for nulls
	if (buffer_length == 0)
		return 1;

	*output = ipfs_bitswap_wantlist_new();
	if (*output == NULL)
		return 0;

	struct BitswapWantlist* list = *output;

	while(pos < buffer_length) {
		size_t bytes_read = 0;
		int field_no;
		enum WireType field_type;
		if (protobuf_decode_field_and_type(&buffer[pos], buffer_length, &field_no, &field_type, &bytes_read) == 0) {
			return 0;
		}
		pos += bytes_read;
		switch(field_no) {
			case (1): {
				// a WantlistEntry
				size_t temp_size = 0;
				uint8_t* temp = NULL;
				if (!protobuf_decode_length_delimited(&buffer[pos], buffer_length - pos, (char**)&temp, &temp_size, &bytes_read)) {
					return 0;
				}
				struct WantlistEntry* entry = NULL;
				if (!ipfs_bitswap_wantlist_entry_protobuf_decode(temp, temp_size, &entry)) {
					free(temp);
					return 0;
				}
				free(temp);
				if (list->entries == NULL) {
					list->entries = libp2p_utils_vector_new(1);
				}
				libp2p_utils_vector_add(list->entries, (void*)entry);
				pos += bytes_read;
				break;
			}
			case (2): {
				list->full = varint_decode(&buffer[pos], buffer_length - pos, &bytes_read);
				pos += bytes_read;
				break;
			}
		}
	}

	return 1;
}

/***
 * Bitswap Message
 *
 */

/***
 * Allocate memory for a new Bitswap Message
 * @returns the allocated struct BitswapMessage
 */
struct BitswapMessage* ipfs_bitswap_message_new() {
	struct BitswapMessage* message = (struct BitswapMessage*) malloc(sizeof(struct BitswapMessage));

	if (message != NULL) {
		message->blocks = NULL;
		message->payload = NULL;
		message->wantlist = NULL;
		message->block_presences = NULL;
		message->pending_bytes = 0;
	}

	return message;
}

/**
 * Free the resources used by a BitswapMessage
 * @param message the BitswapMessage to free
 * @returns true(1)
 */
int ipfs_bitswap_message_free(struct BitswapMessage* message) {
	if (message != NULL) {
		if (message->blocks != NULL) {
			// blocks are just byte arrays in bitswap 1.0.0, so throw it in a struct
			// so it can be put in a vector
			for(int i = 0; i < message->blocks->total; i++) {
				// free each item in the vector
				struct Block* entry = (struct Block*) libp2p_utils_vector_get(message->blocks, i);
				ipfs_block_free(entry);
			}
			libp2p_utils_vector_free(message->blocks);
		}
		if (message->payload != NULL) {
			for(int i = 0; i < message->payload->total; i++) {
				// free each item in the vector
				struct Block* entry = (struct Block*) libp2p_utils_vector_get(message->payload, i);
				ipfs_block_free(entry);
			}
			libp2p_utils_vector_free(message->payload);
		}
		if (message->wantlist != NULL) {
			ipfs_bitswap_wantlist_free(message->wantlist);
		}
		if (message->block_presences != NULL) {
			for(int i = 0; i < message->block_presences->total; i++) {
				struct BitswapBlockPresence* entry = (struct BitswapBlockPresence*) libp2p_utils_vector_get(message->block_presences, i);
				ipfs_bitswap_block_presence_free(entry);
			}
			libp2p_utils_vector_free(message->block_presences);
		}
		free(message);
	}
	return 1;
}

/***
 * Calculate the maximum size of a protobuf'd BitswapMessage
 * @param message the BitswapMessage
 * @returns the maximum size of the protobuf'd BitswapMessage
 */
size_t ipfs_bitswap_message_protobuf_encode_size(const struct BitswapMessage* message) {
	size_t total = 0;
	if (message != NULL) {
		if (message->blocks != NULL) {
			for(int i = 0; i < message->blocks->total; i++) {
				struct Block* entry = (struct Block*) libp2p_utils_vector_get(message->blocks, i);
				total += 11 + entry->data_length;
			}
		}
		if (message->payload != NULL) {
			for(int i = 0; i < message->payload->total; i++) {
				struct Block* entry = (struct Block*) libp2p_utils_vector_get(message->payload, i);
				total += 11 + ipfs_blocks_block_protobuf_encode_size(entry);
			}
		}
		if (message->wantlist != NULL) {
			total += ipfs_bitswap_wantlist_protobuf_encode_size(message->wantlist);
		}
		if (message->block_presences != NULL) {
			for(int i = 0; i < message->block_presences->total; i++) {
				struct BitswapBlockPresence* entry = (struct BitswapBlockPresence*) libp2p_utils_vector_get(message->block_presences, i);
				total += 11 + ipfs_bitswap_block_presence_protobuf_encode_size(entry);
			}
		}
		total += 11 + 12 + 11;
	}
	return total;
}

/***
 * Encode a BitswapMessage into a protobuf buffer
 * @param message the message to encode
 * @param buffer the buffer to fill
 * @param buffer_length the length of the allocated buffer
 * @param bytes_written the total number of bytes written to the buffer
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_message_protobuf_encode(const struct BitswapMessage* message, unsigned char* buffer, size_t buffer_length, size_t* bytes_written) {
	size_t bytes_used = 0;
	*bytes_written = 0;

	if (message != NULL) {
		// the vector of blocks that are actually to be turned back into byte arrays
		if (message->blocks != NULL) {
			for(int i = 0; i < message->blocks->total; i++) {
				struct Block* entry = (struct Block*) libp2p_utils_vector_get(message->blocks, i);
				// blocks are just variable length byte streams
				if (!protobuf_encode_length_delimited(1, WIRETYPE_LENGTH_DELIMITED, (char*)entry->data, entry->data_length, &buffer[*bytes_written], buffer_length - (*bytes_written), &bytes_used)) {
					return 0;
				}
				*bytes_written += bytes_used;
			}
		}
		// the vector of Blocks that are actually blocks
		if (message->payload != NULL) {
			for(int i = 0; i < message->payload->total; i++) {
				struct Block* entry = (struct Block*) libp2p_utils_vector_get(message->payload, i);
				// protobuf it
				size_t temp_size = ipfs_blocks_block_protobuf_encode_size(entry);
				uint8_t* temp = (uint8_t*) malloc(temp_size);
				if (temp == NULL) {
					// memory issues
					return 0;
				}
				if (!ipfs_blocks_block_protobuf_encode(entry, temp, temp_size, &temp_size)) {
					free(temp);
					return 0;
				}
				// put it in the buffer
				if (!protobuf_encode_length_delimited(2, WIRETYPE_LENGTH_DELIMITED, (char*)temp, temp_size, &buffer[*bytes_written], buffer_length - (*bytes_written), &bytes_used)) {
					free(temp);
					return 0;
				}
				*bytes_written += bytes_used;
				free(temp);
			}
		}
		// the WantList
		if (message->wantlist != NULL) {
			size_t temp_size = ipfs_bitswap_wantlist_protobuf_encode_size(message->wantlist);
			uint8_t* temp = (uint8_t*) malloc(temp_size);
			if (temp == NULL) {
				return 0;
			}
			if (!ipfs_bitswap_wantlist_protobuf_encode(message->wantlist, temp, temp_size, &temp_size)) {
				free(temp);
				return 0;
			}
			if (!protobuf_encode_length_delimited(3, WIRETYPE_LENGTH_DELIMITED, (char*)temp, temp_size, &buffer[*bytes_written], buffer_length - (*bytes_written), &bytes_used)) {
				free(temp);
				return 0;
			}
			*bytes_written += bytes_used;
			free(temp);
		}
		// block presences (bitswap 1.2.0)
		if (message->block_presences != NULL) {
			for(int i = 0; i < message->block_presences->total; i++) {
				struct BitswapBlockPresence* entry = (struct BitswapBlockPresence*) libp2p_utils_vector_get(message->block_presences, i);
				size_t temp_size = ipfs_bitswap_block_presence_protobuf_encode_size(entry);
				uint8_t* temp = (uint8_t*) malloc(temp_size);
				if (temp == NULL)
					return 0;
				if (!ipfs_bitswap_block_presence_protobuf_encode(entry, temp, temp_size, &temp_size)) {
					free(temp);
					return 0;
				}
				if (!protobuf_encode_length_delimited(4, WIRETYPE_LENGTH_DELIMITED, (char*)temp, temp_size, &buffer[*bytes_written], buffer_length - (*bytes_written), &bytes_used)) {
					free(temp);
					return 0;
				}
				*bytes_written += bytes_used;
				free(temp);
			}
		}
		// pending bytes (bitswap 1.2.0)
		if (message->pending_bytes != 0) {
			if (!protobuf_encode_varint(5, WIRETYPE_VARINT, message->pending_bytes, &buffer[*bytes_written], buffer_length - (*bytes_written), &bytes_used))
				return 0;
			*bytes_written += bytes_used;
		}

	}
	return 1;
}

/***
 * Decode a BitswapMessage from a protobuf
 * @param buffer the protobuf
 * @param buffer_length the length of the protobuf
 * @param output the newly allocated BitswapMessage
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_message_protobuf_decode(const uint8_t* buffer, size_t buffer_length, struct BitswapMessage** output) {
	size_t pos = 0;

	*output = NULL;

	// short cut for nulls
	if (buffer_length == 0)
		return 1;

	*output = (struct BitswapMessage*) ipfs_bitswap_message_new();
	if (*output == NULL)
		return 0;

	struct BitswapMessage* message = *output;

	while(pos < buffer_length) {
		size_t bytes_read = 0;
		int field_no;
		enum WireType field_type;
		if (protobuf_decode_field_and_type(&buffer[pos], buffer_length, &field_no, &field_type, &bytes_read) == 0) {
			return 0;
		}
		pos += bytes_read;
		switch(field_no) {
			case (1): {
				// a Blocks entry that is just an array of bytes
				struct Block* temp = ipfs_block_new();
				if (!protobuf_decode_length_delimited(&buffer[pos], buffer_length - pos, (char**)&temp->data, &temp->data_length, &bytes_read)) {
					return 0;
				}
				if (message->blocks == NULL) {
					message->blocks = libp2p_utils_vector_new(1);
				}
				libp2p_utils_vector_add(message->blocks, (void*)temp);
				pos += bytes_read;
				break;
			}
			case (2): {
				// a block entry that is a real block struct
				size_t temp_size = 0;
				uint8_t* temp = NULL;
				if (!protobuf_decode_length_delimited(&buffer[pos], buffer_length - pos, (char**)&temp, &temp_size, &bytes_read)) {
					return 0;
				}
				// we have the bytes, turn it into a Block struct
				struct Block* block = NULL;
				if (!ipfs_blocks_block_protobuf_decode(temp, temp_size, &block)) {
					free(temp);
					return 0;
				}
				free(temp);
				if (message->payload == NULL) {
					message->payload = libp2p_utils_vector_new(1);
				}
				libp2p_utils_vector_add(message->payload, (void*)block);
				pos += bytes_read;
				break;
			}
			case(3): {
				// a Wantlist
				size_t temp_size = 0;
				uint8_t* temp = NULL;
				if (!protobuf_decode_length_delimited(&buffer[pos], buffer_length - pos, (char**)&temp, &temp_size, &bytes_read)) {
					return 0;
				}
				// we have the protobuf'd wantlist, now turn it into a Wantlist struct.
				if (!ipfs_bitswap_wantlist_protobuf_decode(temp, temp_size, &message->wantlist)) {
					free(temp);
					return 0;
				}
				free(temp);
				pos += bytes_read;
				break;
			}
			case(4): {
				// a BlockPresence (bitswap 1.2.0)
				size_t temp_size = 0;
				uint8_t* temp = NULL;
				if (!protobuf_decode_length_delimited(&buffer[pos], buffer_length - pos, (char**)&temp, &temp_size, &bytes_read)) {
					return 0;
				}
				struct BitswapBlockPresence* presence = NULL;
				if (!ipfs_bitswap_block_presence_protobuf_decode(temp, temp_size, &presence)) {
					free(temp);
					return 0;
				}
				free(temp);
				if (message->block_presences == NULL) {
					message->block_presences = libp2p_utils_vector_new(1);
				}
				libp2p_utils_vector_add(message->block_presences, (void*)presence);
				pos += bytes_read;
				break;
			}
			case(5): {
				// pendingBytes (bitswap 1.2.0)
				message->pending_bytes = (int32_t) varint_decode(&buffer[pos], buffer_length - pos, &bytes_read);
				pos += bytes_read;
				break;
			}
		}
	}

	return 1;
}

/****
 * Add a vector of Cids to the bitswap message
 * @param message the message
 * @param cids a Libp2pVector of cids
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_message_add_wantlist_items(struct BitswapMessage* message, struct Libp2pVector* cids) {
	if (message->wantlist == NULL) {
		message->wantlist = ipfs_bitswap_wantlist_new();
		if (message->wantlist == NULL)
			return 0;
	}
	if (message->wantlist->entries == NULL) {
		message->wantlist->entries = libp2p_utils_vector_new(1);
		if (message->wantlist->entries == NULL)
			return 0;
	}
	for(int i = 0; i < cids->total; i++) {
		struct CidEntry* cidEntry = (struct CidEntry*)libp2p_utils_vector_get(cids, i);
		if (cidEntry->cancel && cidEntry->cancel_has_been_sent)
			continue;
		if (!cidEntry->cancel && cidEntry->request_has_been_sent)
			continue;
		struct WantlistEntry* entry = ipfs_bitswap_wantlist_entry_new();
		entry->block_size = ipfs_cid_protobuf_encode_size(cidEntry->cid);
		entry->block = (unsigned char*) malloc(entry->block_size);
		if (entry->block == NULL) {
			return 0;
		}
		if (!ipfs_cid_protobuf_encode(cidEntry->cid, entry->block, entry->block_size, &entry->block_size)) {
			// Clean up partially built entry and skip this CID
			ipfs_bitswap_wantlist_entry_free(entry);
			continue;
		}
		entry->cancel = cidEntry->cancel;
		entry->priority = 1;
		libp2p_utils_vector_add(message->wantlist->entries, entry);
		if (cidEntry->cancel)
			cidEntry->cancel_has_been_sent = 1;
		else
			cidEntry->request_has_been_sent = 1;
	}
	return 1;
}

/***
 * Look through vector for specific Cid, then mark it cancel
 * @param vector the vector of CidEntrys
 * @param incoming_cid the cid to look for
 * @returns true(1) if found one, false(0) if not
 */
int ipfs_bitswap_message_cancel_cid(struct Libp2pVector* vector, struct Cid* incoming_cid) {
	for(int i = 0; i < vector->total; i++) {
		struct CidEntry* entry = (struct CidEntry*)libp2p_utils_vector_get(vector, i);
		if (ipfs_cid_compare(entry->cid, incoming_cid) == 0) {
			entry->cancel = 1;
			return 1;
		}
	}
	return 0;
}

/***
 * Add the blocks to the BitswapMessage
 * @param message the message
 * @param blocks the requested blocks
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_bitswap_message_add_blocks(struct BitswapMessage* message, struct Libp2pVector* blocks, struct Libp2pVector* cids_they_want) {
	// bitswap 1.0 uses blocks, bitswap 1.1 uses payload

	if (message == NULL)
		return 0;
	if (blocks == NULL || blocks->total == 0)
		return 0;
	if (message->payload == NULL) {
		message->payload = libp2p_utils_vector_new(blocks->total);
		if (message->payload == NULL)
			return 0;
	}
	int tot_blocks = blocks->total;
	for(int i = 0; i < tot_blocks; i++) {
		const struct Block* current = (const struct Block*) libp2p_utils_vector_get(blocks, i);
		libp2p_utils_vector_add(message->payload, current);
		ipfs_bitswap_message_cancel_cid(cids_they_want, current->cid);
	}

	for (int i = 0; i < tot_blocks; i++) {
		libp2p_utils_vector_delete(blocks, 0);
	}
	return 1;
}


