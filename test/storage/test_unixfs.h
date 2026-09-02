#include "ipfs/unixfs/unixfs.h"
#include "ipfs/unixfs/hamt.h"
#include "ipfs/core/ipfs_node.h"
#include "ipfs/repo/init.h"
#include "ipfs/repo/fsrepo/fs_repo.h"

int test_unixfs_encode_decode_extended() {
	struct UnixFS* unixfs = NULL;
	int retVal;

	retVal = ipfs_unixfs_new(&unixfs);
	if (!retVal) return 0;
	unixfs->data_type = UNIXFS_HAMT_SHARD;
	ipfs_unixfs_set_hamt_params(unixfs, 0x22, 256);
	ipfs_unixfs_set_mode(unixfs, 0755);
	ipfs_unixfs_set_mtime(unixfs, 1693603200, 123456789);

	size_t buffer_size = ipfs_unixfs_protobuf_encode_size(unixfs);
	unsigned char buffer[buffer_size];
	size_t bytes_written = 0;

	retVal = ipfs_unixfs_protobuf_encode(unixfs, buffer, buffer_size, &bytes_written);
	if (retVal == 0) {
		ipfs_unixfs_free(unixfs);
		return 0;
	}

	struct UnixFS* results = NULL;
	retVal = ipfs_unixfs_protobuf_decode(buffer, bytes_written, &results);
	if (retVal == 0) {
		ipfs_unixfs_free(unixfs);
		return 0;
	}

	if (results->data_type != unixfs->data_type ||
	    results->hash_type != 0x22 ||
	    results->fanout != 256 ||
	    results->has_mode == 0 ||
	    results->mode != 0755 ||
	    results->mtime == NULL ||
	    results->mtime->seconds != 1693603200 ||
	    results->mtime->has_fractional_nanoseconds == 0 ||
	    results->mtime->fractional_nanoseconds != 123456789) {
		ipfs_unixfs_free(unixfs);
		ipfs_unixfs_free(results);
		return 0;
	}

	ipfs_unixfs_free(unixfs);
	ipfs_unixfs_free(results);
	return 1;
}

int test_unixfs_encode_decode() {
	struct UnixFS* unixfs = NULL;
	int retVal;

	// a directory
	retVal = ipfs_unixfs_new(&unixfs);
	unixfs->data_type = UNIXFS_DIRECTORY;

	// serialize
	size_t buffer_size = ipfs_unixfs_protobuf_encode_size(unixfs);
	unsigned char buffer[buffer_size];
	size_t bytes_written = 0;

	retVal = ipfs_unixfs_protobuf_encode(unixfs, buffer, buffer_size, &bytes_written);
	if (retVal == 0) {
		ipfs_unixfs_free(unixfs);
		return 0;
	}

	// unserialize
	struct UnixFS* results = NULL;
	retVal = ipfs_unixfs_protobuf_decode(buffer, bytes_written, &results);
	if (retVal == 0) {
		ipfs_unixfs_free(unixfs);
		return 0;
	}

	// compare
	if (results->data_type != unixfs->data_type) {
		ipfs_unixfs_free(unixfs);
		ipfs_unixfs_free(results);
		return 0;
	}

	if (results->block_size_head != unixfs->block_size_head) {
		ipfs_unixfs_free(unixfs);
		ipfs_unixfs_free(results);
		return 0;
	}

	ipfs_unixfs_free(unixfs);
	ipfs_unixfs_free(results);
	return 1;
}

int test_unixfs_encode_smallfile() {
	struct UnixFS* unixfs = NULL;
	ipfs_unixfs_new(&unixfs);

	unsigned char bytes[] = {
			0x54, 0x68, 0x69, 0x73, 0x20,
			0x69, 0x73, 0x20, 0x74, 0x65,
			0x78, 0x74, 0x20, 0x77, 0x69,
			0x74, 0x68, 0x69, 0x6e, 0x20,
			0x48, 0x65, 0x6c, 0x6c, 0x6f,
			0x57, 0x65, 0x72, 0x6c, 0x64,
			0x2e, 0x74, 0x78, 0x74, 0x0a };
	unsigned char expected_results[] = {
			0x08, 0x02, 0x12, 0x23,
			0x54, 0x68, 0x69, 0x73, 0x20,
			0x69, 0x73, 0x20, 0x74, 0x65,
			0x78, 0x74, 0x20, 0x77, 0x69,
			0x74, 0x68, 0x69, 0x6e, 0x20,
			0x48, 0x65, 0x6c, 0x6c, 0x6f,
			0x57, 0x65, 0x72, 0x6c, 0x64,
			0x2e, 0x74, 0x78, 0x74, 0x0a
	};

	unixfs->bytes = (unsigned char*)malloc(35);
	memcpy(unixfs->bytes, bytes, 35);
	unixfs->bytes_size = 35;
	unixfs->data_type = UNIXFS_FILE;

	size_t protobuf_size = 43;
	unsigned char protobuf[protobuf_size];
	size_t bytes_written;
	ipfs_unixfs_protobuf_encode(unixfs, protobuf, protobuf_size, &bytes_written);

	int retVal = 1;

	if (bytes_written != 39) {
		printf("Length should be %lu, but is %lu\n", 41LU, bytes_written);
		retVal = 0;
	}

	for(int i = 0; i < bytes_written; i++) {
		if (expected_results[i] != protobuf[i]) {
			printf("Byte at position %d should be %02x but is %02x\n", i, expected_results[i], protobuf[i]);
			retVal = 0;
		}
	}

	ipfs_unixfs_free(unixfs);

	return retVal;
}

int test_hamt_basic() {
	struct HAMTNode* hamt = ipfs_hamt_new(0x22, 256);
	if (!hamt) return 0;

	unsigned char hash1[32] = {0};
	unsigned char hash2[32] = {0};
	unsigned char hash3[32] = {0};
	hash1[0] = 1; hash2[0] = 2; hash3[0] = 3;

	if (!ipfs_hamt_add(hamt, "file1.txt", hash1, 32, 100)) {
		ipfs_hamt_free(hamt);
		return 0;
	}
	if (!ipfs_hamt_add(hamt, "file2.txt", hash2, 32, 200)) {
		ipfs_hamt_free(hamt);
		return 0;
	}
	if (!ipfs_hamt_add(hamt, "file3.txt", hash3, 32, 300)) {
		ipfs_hamt_free(hamt);
		return 0;
	}

	const unsigned char* out_hash = NULL;
	size_t out_hash_size = 0;
	size_t out_t_size = 0;

	if (!ipfs_hamt_find(hamt, "file1.txt", &out_hash, &out_hash_size, &out_t_size) ||
	    out_t_size != 100 || out_hash_size != 32 || out_hash[0] != 1) {
		ipfs_hamt_free(hamt);
		return 0;
	}
	if (!ipfs_hamt_find(hamt, "file2.txt", &out_hash, &out_hash_size, &out_t_size) ||
	    out_t_size != 200) {
		ipfs_hamt_free(hamt);
		return 0;
	}
	if (!ipfs_hamt_find(hamt, "file3.txt", &out_hash, &out_hash_size, &out_t_size) ||
	    out_t_size != 300) {
		ipfs_hamt_free(hamt);
		return 0;
	}
	if (ipfs_hamt_find(hamt, "missing.txt", &out_hash, &out_hash_size, &out_t_size)) {
		ipfs_hamt_free(hamt);
		return 0;
	}

	ipfs_hamt_free(hamt);
	return 1;
}

int test_hamt_persist() {
	const char* repo_dir = "/tmp/ipfs_hamt_test";
	struct IpfsNode* local_node = NULL;
	struct HAMTNode* hamt = NULL;
	struct HashtableNode* node = NULL;
	int retVal = 0;

	if (!drop_and_build_repository(repo_dir, 4003, NULL, NULL)) {
		fprintf(stderr, "Unable to build repo\n");
		return 0;
	}
	if (!ipfs_node_offline_new(repo_dir, &local_node)) {
		fprintf(stderr, "Unable to create node\n");
		return 0;
	}

	hamt = ipfs_hamt_new(0x22, 256);
	if (!hamt) goto exit;

	unsigned char hash1[32] = {0};
	unsigned char hash2[32] = {0};
	hash1[0] = 1; hash2[0] = 2;

	if (!ipfs_hamt_add(hamt, "a.txt", hash1, 32, 100)) goto exit;
	if (!ipfs_hamt_add(hamt, "b.txt", hash2, 32, 200)) goto exit;

	size_t node_size = 0;
	if (!ipfs_hamt_persist(hamt, local_node->repo, &node, &node_size)) goto exit;

	// verify the persisted node has HAMTShard type
	struct UnixFS* ufs = NULL;
	if (!ipfs_unixfs_protobuf_decode(node->data, node->data_size, &ufs)) goto exit;
	if (ufs->data_type != UNIXFS_HAMT_SHARD || ufs->hash_type != 0x22 || ufs->fanout != 256) {
		ipfs_unixfs_free(ufs);
		goto exit;
	}
	ipfs_unixfs_free(ufs);

	// verify we can retrieve it back by hash
	struct HashtableNode* retrieved = NULL;
	if (!ipfs_merkledag_get(node->hash, node->hash_size, &retrieved, local_node->repo)) goto exit;
	if (retrieved->head_link == NULL) {
		ipfs_hashtable_node_free(retrieved);
		goto exit;
	}
	ipfs_hashtable_node_free(retrieved);

	retVal = 1;
exit:
	if (node) ipfs_hashtable_node_free(node);
	if (hamt) ipfs_hamt_free(hamt);
	if (local_node) ipfs_node_free(local_node);
	return retVal;
}
