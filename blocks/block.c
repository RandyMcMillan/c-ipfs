/**
 * The implementation of methods around IPFS blocks
 */

#include <stdlib.h>
#include <string.h>

#include "libp2p/crypto/sha256.h"
#include "ipfs/blocks/block.h"
#include "ipfs/cid/cid.h"

/***
 * The protobuf functions
 */
// protobuf fields:                            data & data_length            cid
enum WireType ipfs_block_message_fields[] = { WIRETYPE_LENGTH_DELIMITED, WIRETYPE_LENGTH_DELIMITED};

/**
 * Determine the approximate size of an encoded block
 * @param block the block to measure
 * @returns the approximate size needed to encode the protobuf
 */
size_t ipfs_blocks_block_protobuf_encode_size(const struct Block* block) {
	return 22 + ipfs_cid_protobuf_encode_size(block->cid) + block->data_length;
}

/**
 * Encode the Block into protobuf format
 * @param block the block to encode
 * @param buffer the buffer to fill
 * @param max_buffer_size the max size of the buffer
 * @param bytes_written the number of bytes used
 * @returns true(1) on success
 */
int ipfs_blocks_block_protobuf_encode(const struct Block* block, unsigned char* buffer, size_t max_buffer_length, size_t* bytes_written) {
	// data & data_size
	size_t bytes_used = 0;
	*bytes_written = 0;
	int retVal = 0;
	retVal = protobuf_encode_length_delimited(1, ipfs_block_message_fields[0], (char*)block->data, block->data_length, &buffer[*bytes_written], max_buffer_length - *bytes_written, &bytes_used);
	*bytes_written += bytes_used;
	// cid
	size_t cid_size = ipfs_cid_protobuf_encode_size(block->cid);
	unsigned char cid[cid_size];
	retVal = ipfs_cid_protobuf_encode(block->cid, cid, cid_size, &cid_size);
	if (retVal == 0)
		return 0;
	retVal = protobuf_encode_length_delimited(2, ipfs_block_message_fields[1], (char*)cid, cid_size, &buffer[*bytes_written], max_buffer_length - *bytes_written, &bytes_used);
	if (retVal == 0)
		return 0;
	*bytes_written += bytes_used;
	return 1;
}

/***
 * Decode from a protobuf stream into a Block struct
 * @param buffer the buffer to pull from
 * @param buffer_length the length of the buffer
 * @param block the block to fill
 * @returns true(1) on success
 */
int ipfs_blocks_block_protobuf_decode(const unsigned char* buffer, const size_t buffer_length, struct Block** block) {
	size_t pos = 0;
	int retVal = 0;
	unsigned char* temp_buffer = NULL;
	size_t temp_size;

	*block = ipfs_block_new();
	if (*block == NULL)
		goto exit;

	while(pos < buffer_length) {
		size_t bytes_read = 0;
		int field_no;
		enum WireType field_type;
		if (protobuf_decode_field_and_type(&buffer[pos], buffer_length, &field_no, &field_type, &bytes_read) == 0) {
			goto exit;
		}
		pos += bytes_read;
		switch(field_no) {
			case (1): // data
				if (protobuf_decode_length_delimited(&buffer[pos], buffer_length - pos, (char**)&((*block)->data), &((*block)->data_length), &bytes_read) == 0)
					goto exit;
				pos += bytes_read;
				break;
			case (2): // cid
				if (protobuf_decode_length_delimited(&buffer[pos], buffer_length - pos, (char**)&temp_buffer, &temp_size, &bytes_read) == 0)
					goto exit;
				pos += bytes_read;
				if (ipfs_cid_protobuf_decode(temp_buffer, temp_size, &((*block)->cid)) == 0)
					goto exit;
				free(temp_buffer);
				temp_buffer = NULL;
				break;
		}
	}

	retVal = 1;

exit:
	if (retVal == 0) {
		ipfs_block_free(*block);
	}
	if (temp_buffer != NULL)
		free(temp_buffer);

	return retVal;
}


/***
 * Create a new block based on the incoming data
 * @param data the data to base the block on
 * @param data_size the length of the data array
 * @param block a pointer to the struct Block that will be created
 * @returns true(1) on success
 */
struct Block* ipfs_block_new() {

	// allocate memory for structure
	struct Block* block = (struct Block*)malloc(sizeof(struct Block));
	if ( block == NULL)
		return 0;
	block->data = NULL;
	block->data_length = 0;
	block->cid = NULL;

	return block;
}

int ipfs_blocks_block_add_data(const unsigned char* data, size_t data_size, struct Block* block) {
	// cid
	unsigned char hash[32];
	if (libp2p_crypto_hashing_sha256(data, data_size, &hash[0]) == 0) {
		return 0;
	}

	block->cid = ipfs_cid_new(0, hash, 32, CID_DAG_PROTOBUF);
	if (block->cid == NULL) {
		return 0;
	}

	block->data_length = data_size;

	block->data = malloc(sizeof(unsigned char) * data_size);
	if ( block->data == NULL) {
		ipfs_cid_free(block->cid);
		return 0;
	}

	memcpy( block->data, data, data_size);
	return 1;
}

struct Block* ipfs_block_new_raw(const unsigned char* data, size_t data_size) {
	struct Block* block = ipfs_block_new();
	if (block == NULL)
		return NULL;

	unsigned char hash[32];
	if (libp2p_crypto_hashing_sha256(data, data_size, hash) == 0) {
		ipfs_block_free(block);
		return NULL;
	}

	block->cid = ipfs_cid_new(1, hash, 32, CID_RAW);
	if (block->cid == NULL) {
		ipfs_block_free(block);
		return NULL;
	}

	block->data = malloc(data_size);
	if (block->data == NULL) {
		ipfs_block_free(block);
		return NULL;
	}
	memcpy(block->data, data, data_size);
	block->data_length = data_size;
	return block;
}

int ipfs_block_validate(const struct Block* block) {
	if (block == NULL || block->cid == NULL || block->data == NULL)
		return 0;

	unsigned char hash[32];
	if (libp2p_crypto_hashing_sha256(block->data, block->data_length, hash) == 0)
		return 0;

	if (block->cid->hash_length != 32)
		return 0;

	return memcmp(block->cid->hash, hash, 32) == 0;
}

/***
 * Free resources used by the creation of a block
 * @param block the block to free
 * @returns true(1) on success
 */
int ipfs_block_free(struct Block* block) {
	if (block != NULL) {
		if (block->cid != NULL)
			ipfs_cid_free(block->cid);
		if (block->data != NULL)
			free(block->data);
		free(block);
	}
	return 1;
}

/***
 * Make a copy of a block
 * @param original the original
 * @returns a new Block that is a copy
 */
struct Block* ipfs_block_copy(struct Block* original) {
	struct Block* copy = ipfs_block_new();
	if (copy != NULL) {
		copy->data_length = original->data_length;
		copy->data = (unsigned char*) malloc(original->data_length);
		if (copy->data == NULL) {
			ipfs_block_free(copy);
			return NULL;
		}
		memcpy(copy->data, original->data, original->data_length);
		copy->cid = ipfs_cid_copy(original->cid);
		if (copy->cid == NULL) {
			ipfs_block_free(copy);
			return NULL;
		}
	}
	return copy;
}
