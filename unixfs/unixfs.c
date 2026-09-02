/***
 * A unix-like file system over IPFS blocks
 *
 * Protobuf schema (go-unixfs / Kubo v0.43.0 compatible):
 * message Data {
 *  enum DataType {
 *    Raw = 0;
 *    Directory = 1;
 *    File = 2;
 *    Metadata = 3;
 *    Symlink = 4;
 *    HAMTShard = 5;
 *  }
 *  required DataType Type = 1;
 *  optional bytes Data = 2;
 *  optional uint64 filesize = 3;
 *  repeated uint64 blocksizes = 4;
 *  optional uint64 hashType = 5;
 *  optional uint64 fanout = 6;
 *  optional uint32 mode = 7;
 *  optional Timestamp mtime = 8;
 * }
 *
 * message Timestamp {
 *  required int64 Seconds = 1;
 *  optional fixed32 FractionalNanoseconds = 2;
 * }
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libp2p/crypto/encoding/base58.h"
#include "libp2p/crypto/sha256.h"
#include "libp2p/utils/logger.h"
#include "ipfs/unixfs/unixfs.h"
#include "protobuf.h"
#include "varint.h"

/**
 * Allocate memory for a new UnixFS struct
 * @param obj the pointer to the new object
 * @returns true(1) on success
 */
int ipfs_unixfs_new(struct UnixFS** obj) {
	*obj = (struct UnixFS*)malloc(sizeof(struct UnixFS));
	if (*obj == NULL)
		return 0;
	(*obj)->bytes = NULL;
	(*obj)->bytes_size = 0;
	(*obj)->data_type = UNIXFS_RAW;
	(*obj)->block_size_head = NULL;
	(*obj)->hash = NULL;
	(*obj)->hash_length = 0;
	(*obj)->file_size = 0;
	(*obj)->hash_type = 0;
	(*obj)->fanout = 0;
	(*obj)->mode = 0;
	(*obj)->mtime = NULL;
	(*obj)->has_mode = 0;
	return 1;
}

struct UnixFSBlockSizeNode* ipfs_unixfs_get_last_block_size(struct UnixFS* obj) {
	struct UnixFSBlockSizeNode* current = obj->block_size_head;
	if (current != NULL) {
		while (current->next != NULL)
			current = current->next;
	}
	return current;
}

int ipfs_unixfs_remove_blocksize(struct UnixFS* obj, struct UnixFSBlockSizeNode* to_delete) {
	struct UnixFSBlockSizeNode* current = obj->block_size_head;
	struct UnixFSBlockSizeNode* previous = NULL;
	while (current != NULL && current != to_delete) {
		previous = current;
		current = current->next;
	}
	if (previous == NULL) {
		free(obj->block_size_head);
		obj->block_size_head = NULL;
	} else {
		struct UnixFSBlockSizeNode* holder = NULL;
		if (current != NULL && current->next != NULL) {
			holder = current->next;
		}
		free(previous->next);
		if (holder == NULL)
			previous->next = NULL;
		else
			previous->next = holder;
	}
	return 1;
}

int ipfs_unixfs_free(struct UnixFS* obj) {
	if (obj != NULL) {
		if (obj->hash != NULL) {
			free(obj->hash);
		}
		if (obj->bytes != NULL) {
			free(obj->bytes);
		}
		if (obj->block_size_head != NULL) {
			struct UnixFSBlockSizeNode* current = ipfs_unixfs_get_last_block_size(obj);
			while(current != NULL) {
				ipfs_unixfs_remove_blocksize(obj, current);
				current = ipfs_unixfs_get_last_block_size(obj);
			}
		}
		if (obj->mtime != NULL) {
			free(obj->mtime);
		}
		free(obj);
		obj = NULL;
	}
	return 1;
}

/***
 * Write data to data section of a UnixFS stuct. NOTE: this also calculates a sha256 hash
 * @param data the data to write
 * @param data_length the length of the data
 * @param unix_fs the struct to add to
 * @returns true(1) on success
 */
int ipfs_unixfs_add_data(unsigned char* data, size_t data_length, struct UnixFS* unix_fs) {

	unix_fs->bytes_size = data_length;
	unix_fs->bytes = malloc(sizeof(unsigned char) * data_length);
	if ( unix_fs->bytes == NULL) {
		return 0;
	}
	memcpy( unix_fs->bytes, data, data_length);

	// now compute the hash
	unix_fs->hash_length = 32;
	unix_fs->hash = (unsigned char*)malloc(unix_fs->hash_length);
	if (unix_fs->hash == NULL) {
		free(unix_fs->bytes);
		return 0;
	}
	if (libp2p_crypto_hashing_sha256(data, data_length, &unix_fs->hash[0]) == 0) {
		free(unix_fs->bytes);
		free(unix_fs->hash);
		return 0;
	}

	// debug: display hash
	if (libp2p_logger_watching_class("unixfs")) {
		size_t b58size = 100;
		uint8_t *b58key = (uint8_t *) malloc(b58size);
		if (b58key != NULL) {
			libp2p_crypto_encoding_base58_encode(unix_fs->hash, unix_fs->hash_length, &b58key, &b58size);
			libp2p_logger_debug("unixfs", "Saving hash of %s to unixfs object.\n", b58key);
			free(b58key);
		}
	}

	return 1;

}

int ipfs_unixfs_add_blocksize(const struct UnixFSBlockSizeNode* blocksize, struct UnixFS* unix_fs) {
	struct UnixFSBlockSizeNode* last = unix_fs->block_size_head;

	if (last == NULL) {
		// we're the first one
		unix_fs->block_size_head = (struct UnixFSBlockSizeNode*)malloc(sizeof(struct UnixFSBlockSizeNode));
		if (unix_fs->block_size_head == NULL) {
			return 0;
		}
		unix_fs->block_size_head->block_size = blocksize->block_size;
		unix_fs->block_size_head->next = NULL;
	} else {
		// find the last one
		while (last->next != NULL) {
			last = last->next;
		}
		last->next = (struct UnixFSBlockSizeNode*)malloc(sizeof(struct UnixFSBlockSizeNode));
		if (last->next == NULL)
			return 0;
		last->next->block_size = blocksize->block_size;
		last->next->next = NULL;
	}


	return 1;
}

int ipfs_unixfs_set_mode(struct UnixFS* unix_fs, unsigned long long mode) {
	if (unix_fs == NULL)
		return 0;
	unix_fs->mode = mode;
	unix_fs->has_mode = 1;
	return 1;
}

int ipfs_unixfs_set_mtime(struct UnixFS* unix_fs, long long seconds, unsigned int fractional_nanoseconds) {
	if (unix_fs == NULL)
		return 0;
	if (unix_fs->mtime == NULL) {
		unix_fs->mtime = (struct UnixFSTimestamp*)malloc(sizeof(struct UnixFSTimestamp));
		if (unix_fs->mtime == NULL)
			return 0;
	}
	unix_fs->mtime->seconds = seconds;
	unix_fs->mtime->fractional_nanoseconds = fractional_nanoseconds;
	unix_fs->mtime->has_fractional_nanoseconds = 1;
	return 1;
}

int ipfs_unixfs_clear_mtime(struct UnixFS* unix_fs) {
	if (unix_fs == NULL)
		return 0;
	if (unix_fs->mtime != NULL) {
		free(unix_fs->mtime);
		unix_fs->mtime = NULL;
	}
	return 1;
}

int ipfs_unixfs_set_hamt_params(struct UnixFS* unix_fs, unsigned long long hash_type, unsigned long long fanout) {
	if (unix_fs == NULL)
		return 0;
	unix_fs->hash_type = hash_type;
	unix_fs->fanout = fanout;
	return 1;
}

/**
 * Protobuf functions
 */

// Wire types for fields 1-7 (varint or length-delimited)
// Field 8 (mtime) is length-delimited (nested message)

/**
 * Encode a fixed32 (4 bytes little-endian) with its tag
 */
static int _encode_fixed32(int field_number, unsigned int value, unsigned char* buffer, size_t max_buffer_size, size_t* bytes_written) {
	if (max_buffer_size < 5)
		return 0;
	unsigned int tag = (field_number << 3) | 5; // wire type 5 = 32-bit
	size_t tag_bytes = 0;
	varint_encode(tag, buffer, max_buffer_size, &tag_bytes);
	buffer[tag_bytes + 0] = (value >> 0) & 0xFF;
	buffer[tag_bytes + 1] = (value >> 8) & 0xFF;
	buffer[tag_bytes + 2] = (value >> 16) & 0xFF;
	buffer[tag_bytes + 3] = (value >> 24) & 0xFF;
	*bytes_written = tag_bytes + 4;
	return 1;
}

/**
 * Calculate the max size of the protobuf before encoding
 * @param obj what will be encoded
 * @returns the size of the buffer necessary to encode the object
 */
size_t ipfs_unixfs_protobuf_encode_size(const struct UnixFS* obj) {
	size_t sz = 0;
	// data type (field 1)
	sz += 2;
	// bytes (field 2)
	sz += obj->bytes_size + 11;
	// file_size (field 3)
	sz += 11;
	// block sizes (field 4)
	struct UnixFSBlockSizeNode* currNode = obj->block_size_head;
	while(currNode != NULL) {
		sz += 11;
		currNode = currNode->next;
	}
	// hash_type (field 5)
	sz += 11;
	// fanout (field 6)
	sz += 11;
	// mode (field 7)
	sz += 11;
	// mtime (field 8) - length delimited nested message
	if (obj->mtime != NULL) {
		// tag + length + timestamp body
		// timestamp body: seconds varint + optional fractional fixed32
		sz += 2 + 1 + 11 + 9;
	}
	return sz;
}

static int _encode_timestamp(const struct UnixFSTimestamp* ts, unsigned char* out, size_t max_size, size_t* bytes_written) {
	size_t pos = 0;
	size_t used = 0;
	// Seconds = 1, varint
	if (!protobuf_encode_varint(1, WIRETYPE_VARINT, (unsigned long long)ts->seconds, &out[pos], max_size - pos, &used))
		return 0;
	pos += used;
	// FractionalNanoseconds = 2, fixed32
	if (ts->has_fractional_nanoseconds) {
		if (!_encode_fixed32(2, ts->fractional_nanoseconds, &out[pos], max_size - pos, &used))
			return 0;
		pos += used;
	}
	*bytes_written = pos;
	return 1;
}

/***
 * Encode a UnixFS object into protobuf format
 * @param incoming the incoming object
 * @param outgoing where the bytes will be placed
 * @param max_buffer_size the size of the outgoing buffer
 * @param bytes_written how many bytes were written in the buffer
 * @returns true(1) on success
 */
int ipfs_unixfs_protobuf_encode(const struct UnixFS* incoming, unsigned char* outgoing, size_t max_buffer_size, size_t* bytes_written) {
	size_t bytes_used = 0;
	*bytes_written = 0;
	int retVal = 0;
	if (incoming != NULL) {
		// data type (required, field 1)
		retVal = protobuf_encode_varint(1, WIRETYPE_VARINT, incoming->data_type, outgoing, max_buffer_size - *bytes_written, &bytes_used);
		if (retVal == 0)
			return 0;
		*bytes_written += bytes_used;
		// bytes (optional, field 2)
		if (incoming->bytes_size > 0) {
			retVal = protobuf_encode_length_delimited(2, WIRETYPE_LENGTH_DELIMITED, (char*)incoming->bytes, incoming->bytes_size, &outgoing[*bytes_written], max_buffer_size - (*bytes_written), &bytes_used);
			if (retVal == 0)
				return 0;
			*bytes_written += bytes_used;
		}
		// file size (optional, field 3)
		if (incoming->file_size > 0) {
			retVal = protobuf_encode_varint(3, WIRETYPE_VARINT, incoming->file_size, &outgoing[*bytes_written], max_buffer_size - (*bytes_written), &bytes_used);
			if (retVal == 0)
				return 0;
			*bytes_written += bytes_used;
		}
		// block sizes (field 4)
		struct UnixFSBlockSizeNode* currNode = incoming->block_size_head;
		while (currNode != NULL) {
			retVal = protobuf_encode_varint(4, WIRETYPE_VARINT, currNode->block_size, &outgoing[*bytes_written], max_buffer_size - (*bytes_written), &bytes_used);
			*bytes_written += bytes_used;
			currNode = currNode->next;
		}
		// hash_type (field 5)
		if (incoming->hash_type > 0) {
			retVal = protobuf_encode_varint(5, WIRETYPE_VARINT, incoming->hash_type, &outgoing[*bytes_written], max_buffer_size - (*bytes_written), &bytes_used);
			if (retVal == 0)
				return 0;
			*bytes_written += bytes_used;
		}
		// fanout (field 6)
		if (incoming->fanout > 0) {
			retVal = protobuf_encode_varint(6, WIRETYPE_VARINT, incoming->fanout, &outgoing[*bytes_written], max_buffer_size - (*bytes_written), &bytes_used);
			if (retVal == 0)
				return 0;
			*bytes_written += bytes_used;
		}
		// mode (field 7)
		if (incoming->has_mode) {
			retVal = protobuf_encode_varint(7, WIRETYPE_VARINT, incoming->mode, &outgoing[*bytes_written], max_buffer_size - (*bytes_written), &bytes_used);
			if (retVal == 0)
				return 0;
			*bytes_written += bytes_used;
		}
		// mtime (field 8, length-delimited nested Timestamp)
		if (incoming->mtime != NULL) {
			unsigned char ts_body[64];
			size_t ts_body_size = 0;
			if (!_encode_timestamp(incoming->mtime, ts_body, sizeof(ts_body), &ts_body_size))
				return 0;
			retVal = protobuf_encode_length_delimited(8, WIRETYPE_LENGTH_DELIMITED, (char*)ts_body, ts_body_size, &outgoing[*bytes_written], max_buffer_size - (*bytes_written), &bytes_used);
			if (retVal == 0)
				return 0;
			*bytes_written += bytes_used;
		}
	}
	return 1;
}

static int _decode_fixed32(const unsigned char* buffer, size_t buffer_length, unsigned int* result, size_t* bytes_read) {
	if (buffer_length < 4)
		return 0;
	*result = ((unsigned int)buffer[0]) |
	          (((unsigned int)buffer[1]) << 8) |
	          (((unsigned int)buffer[2]) << 16) |
	          (((unsigned int)buffer[3]) << 24);
	*bytes_read = 4;
	return 1;
}

/***
 * Decodes a protobuf array of bytes into a UnixFS object
 * @param incoming the array of bytes
 * @param incoming_size the length of the array
 * @param outgoing the UnixFS object
 * @returns true(1) on success, false(0) on error
 */
int ipfs_unixfs_protobuf_decode(unsigned char* incoming, size_t incoming_size, struct UnixFS** outgoing) {
	// short cut for nulls
	if (incoming_size == 0) {
		*outgoing = NULL;
		return 0;
	}

	size_t pos = 0;
	int retVal = 0;

	if (ipfs_unixfs_new(outgoing) == 0) {
		return 0;
	}
	struct UnixFS* result = *outgoing;

	while(pos < incoming_size) {
		size_t bytes_read = 0;
		int field_no;
		enum WireType field_type;
		if (protobuf_decode_field_and_type(&incoming[pos], incoming_size - pos, &field_no, &field_type, &bytes_read) == 0) {
			return 0;
		}
		pos += bytes_read;
		switch(field_no) {
			case (1): // data type (varint)
				result->data_type = varint_decode(&incoming[pos], incoming_size - pos, &bytes_read);
				pos += bytes_read;
				break;
			case (2): // bytes (length delimited)
				retVal = protobuf_decode_length_delimited(&incoming[pos], incoming_size - pos, (char**)&(result->bytes), &(result->bytes_size), &bytes_read);
				if (retVal == 0)
					return 0;
				pos += bytes_read;
				break;
			case (3): // file size
				result->file_size = varint_decode(&incoming[pos], incoming_size - pos, &bytes_read);
				pos += bytes_read;
				break;
			case (4): { // block sizes (linked list from varint)
				struct UnixFSBlockSizeNode bs;
				bs.next = NULL;
				bs.block_size = varint_decode(&incoming[pos], incoming_size - pos, &bytes_read);
				ipfs_unixfs_add_blocksize(&bs, result);
				pos += bytes_read;
				break;
			}
			case (5): // hash_type
				result->hash_type = varint_decode(&incoming[pos], incoming_size - pos, &bytes_read);
				pos += bytes_read;
				break;
			case (6): // fanout
				result->fanout = varint_decode(&incoming[pos], incoming_size - pos, &bytes_read);
				pos += bytes_read;
				break;
			case (7): // mode
				result->mode = varint_decode(&incoming[pos], incoming_size - pos, &bytes_read);
				result->has_mode = 1;
				pos += bytes_read;
				break;
			case (8): { // mtime (length-delimited nested Timestamp)
				size_t sub_len = 0;
				unsigned long long sub_len_varint = varint_decode(&incoming[pos], incoming_size - pos, &bytes_read);
				pos += bytes_read;
				sub_len = (size_t)sub_len_varint;
				if (sub_len > incoming_size - pos)
					return 0;
				if (result->mtime == NULL) {
					result->mtime = (struct UnixFSTimestamp*)malloc(sizeof(struct UnixFSTimestamp));
					if (result->mtime == NULL)
						return 0;
					result->mtime->has_fractional_nanoseconds = 0;
				}
				size_t sub_pos = 0;
				while (sub_pos < sub_len) {
					size_t sub_br = 0;
					int sub_fn;
					enum WireType sub_ft;
					if (protobuf_decode_field_and_type(&incoming[pos + sub_pos], sub_len - sub_pos, &sub_fn, &sub_ft, &sub_br) == 0)
						return 0;
					sub_pos += sub_br;
					switch (sub_fn) {
						case 1:
							result->mtime->seconds = (long long)varint_decode(&incoming[pos + sub_pos], sub_len - sub_pos, &sub_br);
							sub_pos += sub_br;
							break;
						case 2:
							if (!_decode_fixed32(&incoming[pos + sub_pos], sub_len - sub_pos, &result->mtime->fractional_nanoseconds, &sub_br))
								return 0;
							result->mtime->has_fractional_nanoseconds = 1;
							sub_pos += sub_br;
							break;
						default:
							// skip unknown
							if (sub_ft == WIRETYPE_VARINT) {
								varint_decode(&incoming[pos + sub_pos], sub_len - sub_pos, &sub_br);
								sub_pos += sub_br;
							} else if (sub_ft == WIRETYPE_LENGTH_DELIMITED) {
								size_t skip_len = varint_decode(&incoming[pos + sub_pos], sub_len - sub_pos, &sub_br);
								sub_pos += sub_br + skip_len;
							} else if (sub_ft == WIRETYPE_32BIT) {
								sub_pos += 4;
							} else if (sub_ft == WIRETYPE_64BIT) {
								sub_pos += 8;
							}
							break;
					}
				}
				pos += sub_len;
				break;
			}
			default:
				// skip unknown field
				if (field_type == WIRETYPE_VARINT) {
					varint_decode(&incoming[pos], incoming_size - pos, &bytes_read);
					pos += bytes_read;
				} else if (field_type == WIRETYPE_LENGTH_DELIMITED) {
					size_t skip_len = varint_decode(&incoming[pos], incoming_size - pos, &bytes_read);
					pos += bytes_read + skip_len;
				} else if (field_type == WIRETYPE_32BIT) {
					pos += 4;
				} else if (field_type == WIRETYPE_64BIT) {
					pos += 8;
				}
				break;
		}

	}

	return 1;
}
