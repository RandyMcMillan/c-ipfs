#include <string.h>

#include "mh/hashes.h"
#include "mh/multihash.h"

#include "ipfs/cid/cid.h"
#include "ipfs/multibase/multibase.h"

#include "varint.h"
#include "libp2p/crypto/sha256.h"

int test_cid_new_free() {

	unsigned char* hash = (unsigned char*)"ABC123";

	struct Cid* cid = ipfs_cid_new(0, hash, strlen((char*)hash), CID_DAG_PROTOBUF);

	if (cid == NULL)
		return 0;

	if (cid->version != 0)
		return 0;

	if (cid->codec != CID_DAG_PROTOBUF)
		return 0;

	if (cid->hash_length != strlen((char*)hash))
		return 0;

	if (strncmp((char*)cid->hash, (char*)hash, 6) != 0)
		return 0;

	return ipfs_cid_free(cid);
}

/***
 * Test sending a multibase encoded multihash into cid_cast method
 * that should return a Cid struct
 */
int test_cid_cast_multihash() {
	// first, build a multihash
	char* string_to_hash = "Hello, World!";
	unsigned char hashed[32];
	memset(hashed, 0, 32);
	// hash the string
	libp2p_crypto_hashing_sha256((unsigned char*)string_to_hash, strlen(string_to_hash), hashed);
	size_t multihash_size = mh_new_length(MH_H_SHA2_256, 32);
	unsigned char multihash[multihash_size];
	memset(multihash, 0, multihash_size);
	unsigned char* ptr = multihash;

	int retVal = mh_new(ptr, MH_H_SHA2_256, hashed, 32);
	if (retVal < 0)
		return 0;

	// now call cast
	struct Cid cid;
	retVal = ipfs_cid_cast(multihash, multihash_size, &cid);
	if (retVal == 0)
		return 0;
	// check results
	if (cid.version != 0)
		return 0;
	if (cid.hash_length != 32)
		return 0;
	if (cid.codec != CID_DAG_PROTOBUF)
		return 0;
	if (strncmp((char*)hashed, (char*)cid.hash, 32) != 0)
		return 0;

	return 1;
}

int test_cid_cast_non_multihash() {
	// first, build a hash
	char* string_to_hash = "Hello, World!";
	unsigned char hashed[32];
	memset(hashed, 0, 32);
	// hash the string
	libp2p_crypto_hashing_sha256((unsigned char*)string_to_hash, strlen(string_to_hash), hashed);

	// now make it a hash with a version and codec embedded in varints before the hash
	size_t array_size = 34; // 32 for the hash, 2 for the 2 varints
	unsigned char array[array_size];
	memset(array, 0, array_size);
	// first the version
	array[0] = 0;
	// then the codec
	array[1] = CID_DAG_PROTOBUF;
	// then the hash
	memcpy(&array[2], hashed, 32);

	// now call cast
	struct Cid cid;
	int retVal = ipfs_cid_cast(array, array_size, &cid);
	if (retVal == 0)
		return 0;
	// check results
	if (cid.version != 0)
		return 0;
	if (cid.hash_length != 32)
		return 0;
	if (cid.codec != CID_DAG_PROTOBUF)
		return 0;
	if (strncmp((char*)hashed, (char*)cid.hash, 32) != 0)
		return 0;

	return 1;
}

int test_cid_protobuf_encode_decode() {
	struct Cid tester;
	tester.version = 1;
	tester.codec = CID_ETHEREUM_BLOCK;
	tester.hash = (unsigned char*)"ABC123";
	tester.hash_length = 6;
	size_t bytes_written_to_buffer;

	// encode
	size_t buffer_length = ipfs_cid_protobuf_encode_size(&tester);
	unsigned char buffer[buffer_length];
	ipfs_cid_protobuf_encode(&tester, buffer, buffer_length, &bytes_written_to_buffer);

	// decode
	struct Cid* results;
	ipfs_cid_protobuf_decode(buffer, bytes_written_to_buffer, &results);

	// compare
	if (tester.version != results->version) {
		printf("Version %d does not match version %d\n", tester.version, results->version);
		ipfs_cid_free(results);
		return 0;
	}

	if (tester.codec != results->codec) {
		printf("Codec %02x does not match %02x\n", tester.codec, results->codec);
		ipfs_cid_free(results);
		return 0;
	}

	if (tester.hash_length != results->hash_length) {
		printf("Hash length %d does not match %d\n", (int)tester.hash_length, (int)results->hash_length);
		ipfs_cid_free(results);
		return 0;
	}

	for(int i = 0; i < 6; i++) {
		if (tester.hash[i] != results->hash[i]) {
			printf("Hash character %c does not match %c at position %d", tester.hash[i], results->hash[i], i);
			ipfs_cid_free(results);
			return 0;
		}
	}

	ipfs_cid_free(results);
	return 1;
}

int test_cid_v1_multibase_roundtrip() {
	char* string_to_hash = "Hello, CIDv1!";
	unsigned char hashed[32];
	memset(hashed, 0, 32);
	libp2p_crypto_hashing_sha256((unsigned char*)string_to_hash, strlen(string_to_hash), hashed);

	size_t multihash_size = mh_new_length(MH_H_SHA2_256, 32);
	unsigned char multihash[multihash_size];
	memset(multihash, 0, multihash_size);
	if (mh_new(multihash, MH_H_SHA2_256, hashed, 32) < 0)
		return 0;

	size_t cid_bytes_size = varint_encoding_length(1) + varint_encoding_length(CID_RAW) + multihash_size;
	unsigned char cid_bytes[cid_bytes_size];
	size_t bytes_written = 0;
	varint_encode(1, cid_bytes, cid_bytes_size, &bytes_written);
	size_t codec_written = 0;
	varint_encode(CID_RAW, &cid_bytes[bytes_written], cid_bytes_size - bytes_written, &codec_written);
	bytes_written += codec_written;
	memcpy(&cid_bytes[bytes_written], multihash, multihash_size);
	bytes_written += multihash_size;

	size_t encoded_size = multibase_encode_size(MULTIBASE_BASE32, cid_bytes, bytes_written);
	unsigned char encoded[encoded_size];
	memset(encoded, 0, encoded_size);
	size_t encoded_length = encoded_size;
	if (!multibase_encode(MULTIBASE_BASE32, cid_bytes, bytes_written, encoded, encoded_size, &encoded_length))
		return 0;

	if (encoded[0] != MULTIBASE_BASE32)
		return 0;

	struct Cid* decoded = NULL;
	if (!ipfs_cid_decode_hash_from_base58(encoded, encoded_length - 1, &decoded))
		return 0;

	if (decoded->version != 1 || decoded->codec != CID_RAW || decoded->hash_length != multihash_size) {
		ipfs_cid_free(decoded);
		return 0;
	}

	if (memcmp(decoded->hash, multihash, multihash_size) != 0) {
		ipfs_cid_free(decoded);
		return 0;
	}

	char path[256];
	snprintf(path, sizeof(path), "/ipfs/%s/sub/path", encoded);
	char original_path[256];
	memcpy(original_path, path, sizeof(path));

	struct Cid* path_cid = NULL;
	if (!ipfs_cid_decode_hash_from_ipfs_ipns_string(path, &path_cid)) {
		ipfs_cid_free(decoded);
		return 0;
	}

	if (strcmp(path, original_path) != 0) {
		ipfs_cid_free(decoded);
		ipfs_cid_free(path_cid);
		return 0;
	}

	if (path_cid->version != decoded->version || path_cid->codec != decoded->codec || path_cid->hash_length != decoded->hash_length || memcmp(path_cid->hash, decoded->hash, decoded->hash_length) != 0) {
		ipfs_cid_free(decoded);
		ipfs_cid_free(path_cid);
		return 0;
	}

	char* rendered = NULL;
	if (ipfs_cid_to_string(decoded, &rendered) == NULL || rendered == NULL) {
		ipfs_cid_free(decoded);
		ipfs_cid_free(path_cid);
		return 0;
	}

	int retVal = strcmp(rendered, (char*)encoded) == 0;
	free(rendered);
	ipfs_cid_free(decoded);
	ipfs_cid_free(path_cid);
	return retVal;
}
