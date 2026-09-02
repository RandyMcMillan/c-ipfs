/**
 * Some code to help with the datastore / blockstore interface
 * NOTE: the datastore stores things under a multihash key
 */
#include <stdlib.h>
#include "libp2p/utils/logger.h"
#include "libp2p/crypto/encoding/base32.h"
#include "ipfs/datastore/ds_helper.h"
/**
 * Generate a base32 key based on the passed in binary_array (which is normally a multihash)
 * @param binary_array what to base the key on
 * @param array_length the size of the binary array
 * @param results where the key will be put
 * @param max_results_length the size of the results buffer
 * @param results_length the length of the generated key
 * @returns true(1) on success
 */
int ipfs_datastore_helper_ds_key_from_binary(const unsigned char* binary_array, size_t array_length,
		unsigned char* results, size_t max_results_length, size_t* results_length) {

	size_t encoded_length = libp2p_crypto_encoding_base32_encode_size(array_length);
	if (encoded_length > max_results_length)
		return 0;

	*results_length = max_results_length;
	int retVal = libp2p_crypto_encoding_base32_encode(binary_array, array_length, results, results_length);
	if (retVal == 0) {
		*results_length = 0;
		return 0;
	}

	return 1;
}

/**
 * Generate a binary array (normally a multihash) based on the passed in datastore key
 * @param ds_key the base32 encoded key
 * @param key_length the length of the base32 "string"
 * @param binary_array where to put the decoded value
 * @param max_binary_array_length the memory size of binary_array
 * @param completed_binary_array_length the length of what was written to the binary_array
 * @returns true(1) on success
 */
int ipfs_datastore_helper_binary_from_ds_key(const unsigned char* ds_key, size_t key_length, unsigned char* binary_array,
		size_t max_binary_array_length, size_t* completed_binary_array_length) {

	size_t decoded_length = libp2p_crypto_encoding_base32_decode_size(key_length);
	if (decoded_length > max_binary_array_length)
		return 0;

	*completed_binary_array_length = max_binary_array_length;
	int retVal = libp2p_crypto_encoding_base32_decode(ds_key, key_length, binary_array, completed_binary_array_length);
	if (retVal == 0) {
		*completed_binary_array_length = 0;
		return 0;
	}
	return 1;
}

/***
 * Add a record in the datastore based on a block
 * @param block the block
 * @param datastore the Datastore
 * @reutrns true(1) on success, false(0) otherwise
 */
int ipfs_datastore_helper_add_block_to_datastore(struct Block* block, struct Datastore* datastore) {
	struct DatastoreRecord* rec = libp2p_datastore_record_new();
	if (rec == NULL)
		return 0;
	rec->key_size = block->cid->hash_length;
	rec->key = (uint8_t*) malloc(rec->key_size);
	if (rec->key == NULL) {
		libp2p_datastore_record_free(rec);
		return 0;
	}
	memcpy(rec->key, block->cid->hash, rec->key_size);
	rec->timestamp = 0;
	// convert the key to base32, and store it in the DatabaseRecord->value section
	size_t fs_key_length = 100;
	uint8_t fs_key[fs_key_length];
	if (!ipfs_datastore_helper_ds_key_from_binary(block->cid->hash, block->cid->hash_length, fs_key, fs_key_length, &fs_key_length)) {
		libp2p_datastore_record_free(rec);
		return 0;
	}
	rec->value_size = fs_key_length;
	rec->value = malloc(rec->value_size);
	if (rec->value == NULL) {
		libp2p_datastore_record_free(rec);
		return 0;
	}
	memcpy(rec->value, fs_key, rec->value_size);
	int retVal = datastore->datastore_put(rec, datastore);
	libp2p_datastore_record_free(rec);
	return retVal;
}

