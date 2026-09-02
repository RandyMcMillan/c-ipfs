#include <stdio.h>
#include <pthread.h>

#include "../test_helper.h"
#include "ipfs/importer/importer.h"
#include "ipfs/importer/exporter.h"
#include "ipfs/merkledag/merkledag.h"
#include "mh/hashes.h"
#include "mh/multihash.h"
#include "libp2p/crypto/encoding/base58.h"
#include "ipfs/core/ipfs_node.h"
#include "ipfs/repo/fsrepo/lmdb_cursor.h"

int test_import_large_file() {
	size_t bytes_size = 1000000; //1mb
	unsigned char file_bytes[bytes_size];
	const char* fileName = "/tmp/test_import_large.tmp";
	const char* repo_dir = "/tmp/ipfs_1";
	struct IpfsNode* local_node = NULL;
	int retVal = 0;
	// cid should be the same each time
	unsigned char cid_test[10] = { 0xc0 ,0x1a ,0x80 ,0x8d, 0x83, 0xc9, 0x96, 0x34, 0x1e, 0xbf };
	struct HashtableNode* read_node = NULL;
	struct HashtableNode* write_node = NULL;
	struct HashtableNode* read_node2 = NULL;
	size_t bytes_written = 0;
	size_t base58_size = 55;
	unsigned char base58[base58_size];
	size_t bytes_read1 = 1;
	size_t bytes_read2 = 1;
	unsigned char buf1[100];
	unsigned char buf2[100];

	// create the necessary file
	create_bytes(file_bytes, bytes_size);
	create_file(fileName, file_bytes, bytes_size);

	// get the repo
	if (!drop_and_build_repository(repo_dir, 4001, NULL, NULL)) {
		fprintf(stderr, "Unable to drop and build test repository at %s\n", repo_dir);
		goto exit;
	}

	if (!ipfs_node_offline_new(repo_dir, &local_node)) {
		fprintf(stderr, "Unable to create new IpfsNode\n");
		goto exit;
	}

	// write to ipfs
	if (ipfs_import_file(NULL, fileName, &write_node, local_node, &bytes_written, 1) == 0) {
		goto exit;
	}

	int hash_correct = 1;
	for(int i = 0; i < 10; i++) {
		if (write_node->hash[i] != cid_test[i]) {
			printf("Hashes should be the same each time, and do not match at position %d, should be %02x but is %02x\n", i, cid_test[i], write_node->hash[i]);
			hash_correct = 0;
		}
	}

	if (!hash_correct)
		goto exit;

	// make sure all went okay
	if (ipfs_merkledag_get(write_node->hash, write_node->hash_size, &read_node, local_node->repo) == 0) {
		goto exit;
	}

	// the second block should be there
	if (ipfs_merkledag_get(read_node->head_link->hash, read_node->head_link->hash_size, &read_node2, local_node->repo) == 0) {
		printf("Unable to find the linked node.\n");
		goto exit;
	}

	// compare data
	if (write_node->data_size != read_node->data_size) {
		printf("Data size of nodes are not equal. Should be %lu but are %lu\n", write_node->data_size, read_node->data_size);
		goto exit;
	}

	for(int i = 0; i < write_node->data_size; i++) {
		if (write_node->data[i] != read_node->data[i]) {
			printf("Data within node is different at position %d. The value should be %02x, but was %02x.\n", i, write_node->data[i], read_node->data[i]);
			goto exit;
		}
	}

	// convert cid to multihash
	if ( ipfs_cid_hash_to_base58(read_node->hash, read_node->hash_size, base58, base58_size) == 0) {
		printf("Unable to convert cid to multihash\n");
		goto exit;
	}

	// attempt to write file
	if (ipfs_exporter_to_file(base58, "/tmp/test_import_large_file.rsl", local_node) == 0) {
		printf("Unable to write file.\n");
		goto exit;
	}

	// compare original with new
	size_t new_file_size = os_utils_file_size("/tmp/test_import_large_file.rsl");
	if (new_file_size != bytes_size) {
		printf("File sizes are different. Should be %lu but the new one is %lu\n", bytes_size, new_file_size);
		goto exit;
	}

	FILE* f1 = fopen("/tmp/test_import_large.tmp", "rb");
	FILE* f2 = fopen("/tmp/test_import_large_file.rsl", "rb");

	// compare bytes of files
	while (bytes_read1 != 0 && bytes_read2 != 0) {
		bytes_read1 = fread(buf1, 1, 100, f1);
		bytes_read2 = fread(buf2, 1, 100, f2);
		if (bytes_read1 != bytes_read2) {
			printf("Error reading files for comparison. Read %lu bytes of file 1, but %lu bytes of file 2\n", bytes_read1, bytes_read2);
			fclose(f1);
			fclose(f2);
			goto exit;
		}
		if (memcmp(buf1, buf2, bytes_read1) != 0) {
			printf("The bytes between the files are different\n");
			fclose(f1);
			fclose(f2);
			goto exit;
		}
	}

	fclose(f1);
	fclose(f2);

	retVal = 1;
	exit:

	if (local_node != NULL)
		ipfs_node_free(local_node);
	if (write_node != NULL)
		ipfs_hashtable_node_free(write_node);
	if (read_node != NULL)
		ipfs_hashtable_node_free(read_node);
	if (read_node2 != NULL)
		ipfs_hashtable_node_free(read_node2);

	return retVal;

}

int test_import_trickle_file() {
	size_t bytes_size = 1000000; //1mb
	unsigned char file_bytes[bytes_size];
	const char* fileName = "/tmp/test_import_trickle.tmp";
	const char* repo_dir = "/tmp/ipfs_trickle";
	struct IpfsNode* local_node = NULL;
	int retVal = 0;
	struct HashtableNode* write_node = NULL;
	size_t bytes_written = 0;
	size_t bytes_read1 = 1;
	size_t bytes_read2 = 1;
	unsigned char buf1[100];
	unsigned char buf2[100];
	unsigned char base58[55];
	size_t base58_size = 55;

	create_bytes(file_bytes, bytes_size);
	create_file(fileName, file_bytes, bytes_size);

	if (!drop_and_build_repository(repo_dir, 4002, NULL, NULL)) {
		fprintf(stderr, "Unable to drop and build test repository at %s\n", repo_dir);
		goto exit;
	}

	if (!ipfs_node_offline_new(repo_dir, &local_node)) {
		fprintf(stderr, "Unable to create new IpfsNode\n");
		goto exit;
	}

	if (ipfs_import_file_with_layout(NULL, fileName, &write_node, local_node, &bytes_written, 1, UNIXFS_LAYOUT_TRICKLE) == 0) {
		goto exit;
	}

	// export via base58 hash
	if (ipfs_cid_hash_to_base58(write_node->hash, write_node->hash_size, base58, base58_size) == 0) {
		goto exit;
	}

	if (ipfs_exporter_to_file(base58, "/tmp/test_import_trickle.rsl", local_node) == 0) {
		goto exit;
	}

	size_t new_file_size = os_utils_file_size("/tmp/test_import_trickle.rsl");
	if (new_file_size != bytes_size) {
		printf("Trickle file sizes are different. Should be %lu but the new one is %lu\n", bytes_size, new_file_size);
		goto exit;
	}

	FILE* f1 = fopen("/tmp/test_import_trickle.tmp", "rb");
	FILE* f2 = fopen("/tmp/test_import_trickle.rsl", "rb");
	while (bytes_read1 != 0 && bytes_read2 != 0) {
		bytes_read1 = fread(buf1, 1, 100, f1);
		bytes_read2 = fread(buf2, 1, 100, f2);
		if (bytes_read1 != bytes_read2) {
			printf("Error reading files for comparison.\n");
			fclose(f1);
			fclose(f2);
			goto exit;
		}
		if (memcmp(buf1, buf2, bytes_read1) != 0) {
			printf("The bytes between the trickle files are different\n");
			fclose(f1);
			fclose(f2);
			goto exit;
		}
	}
	fclose(f1);
	fclose(f2);

	retVal = 1;
exit:
	if (local_node != NULL)
		ipfs_node_free(local_node);
	if (write_node != NULL)
		ipfs_hashtable_node_free(write_node);
	return retVal;
}

int test_import_small_file() {
	size_t bytes_size = 1000;
	unsigned char file_bytes[bytes_size];
	const char* fileName = "/tmp/test_import_small.tmp";
	const char* repo_path = "/tmp/ipfs_1";
	struct IpfsNode *local_node = NULL;
	int retVal = 0;

	// create the necessary file
	create_bytes(file_bytes, bytes_size);
	create_file(fileName, file_bytes, bytes_size);

	// get the repo
	drop_and_build_repository(repo_path, 4001, NULL, NULL);

	ipfs_node_offline_new(repo_path, &local_node);

	// write to ipfs
	struct HashtableNode* write_node;
	size_t bytes_written;
	if (ipfs_import_file(NULL, fileName, &write_node, local_node, &bytes_written, 1) == 0) {
		ipfs_node_free(local_node);
		return 0;
	}

	// cid should be the same each time
	unsigned char cid_test[10] = { 0x1e, 0xcf, 0x04, 0xce, 0x6a, 0xe8, 0xbf, 0xc0, 0xeb, 0xe4 };


	for(int i = 0; i < 10; i++) {
		if (write_node->hash[i] != cid_test[i]) {
			printf("Hashes do not match at position %d, should be %02x but is %02x\n", i, cid_test[i], write_node->hash[i]);
			ipfs_node_free(local_node);
			ipfs_hashtable_node_free(write_node);
			return 0;
		}
	}

	// make sure all went okay
	struct HashtableNode* read_node;
	if (ipfs_merkledag_get(write_node->hash, write_node->hash_size, &read_node, local_node->repo) == 0) {
		ipfs_node_free(local_node);
		ipfs_hashtable_node_free(write_node);
		return 0;
	}

	// compare data
	if (write_node->data_size != bytes_size + 8 || write_node->data_size != read_node->data_size) {
		printf("Data size of nodes are not equal or are incorrect. Should be %lu but are %lu\n", write_node->data_size, read_node->data_size);
		ipfs_node_free(local_node);
		ipfs_hashtable_node_free(write_node);
		ipfs_hashtable_node_free(read_node);
		return 0;
	}

	for(int i = 0; i < bytes_size; i++) {
		if (write_node->data[i] != read_node->data[i]) {
			printf("Data within node is different at position %d\n", i);
			ipfs_node_free(local_node);
			ipfs_hashtable_node_free(write_node);
			ipfs_hashtable_node_free(read_node);
			return 0;
		}
	}

	// attempt to look in the journal for the entry
	struct lmdb_context *context = (struct lmdb_context*)local_node->repo->config->datastore->datastore_context;
	struct JournalRecord* record = NULL;
	struct lmdb_trans_cursor *cursor = lmdb_trans_cursor_new();
	// get a transaction
	if (mdb_txn_begin(context->db_environment, context->current_transaction, MDB_RDONLY, &cursor->parent_transaction) != 0) {
		libp2p_logger_error("test_importer", "Unable to create db transaction.\n");
		goto exit;
	}
	cursor->environment = context->db_environment;
	cursor->database = context->journal_db;
	if (mdb_cursor_open(cursor->parent_transaction, *cursor->database, &cursor->cursor) != 0) {
		libp2p_logger_error("test_importer", "Unable to open cursor.\n");
		goto exit;
	} else if (!lmdb_journalstore_cursor_get(cursor, CURSOR_FIRST, &record)) {
		libp2p_logger_error("test_importer", "Unable to find any records in the database.\n");
		goto exit;
	}
	mdb_txn_commit(cursor->parent_transaction);

	retVal = 1;

	exit:

	ipfs_node_free(local_node);
	ipfs_hashtable_node_free(write_node);
	ipfs_hashtable_node_free(read_node);

	return retVal;
}
