#include "libp2p/crypto/encoding/base32.h"
#include "ipfs/datastore/ds_helper.h"
#include "ipfs/blocks/block.h"
#include "ipfs/repo/config/config.h"
#include "ipfs/repo/fsrepo/fs_repo.h"
#include "ipfs/repo/fsrepo/journalstore.h"

#include "../test_helper.h"

#include <dirent.h>
#include <sys/stat.h>

/**
 * create a repository and put a record in the datastore and a block in the blockstore
 */
int test_ipfs_datastore_put() {
	struct Block* block;
	int retVal = 0;
	const unsigned char* input = (unsigned char*)"Hello, world!";

	// build the ipfs repository, then shut it down, so we can start fresh
	retVal = drop_and_build_repository("./tmp/.ipfs", 4001, NULL, NULL);
	if (retVal == 0)
		return 0;

	// build the block
	block = ipfs_block_new();
	if (block == NULL)
		return 0;

	retVal = ipfs_blocks_block_add_data(input, strlen((char*)input), block);
	if (retVal == 0) {
		ipfs_block_free(block);
		return 0;
	}

	// generate the key
	size_t key_length = libp2p_crypto_encoding_base32_encode_size(block->data_length);
	unsigned char key[key_length];
	retVal = ipfs_datastore_helper_ds_key_from_binary(block->data, block->data_length, &key[0], key_length, &key_length);
	if (retVal == 0) {
		ipfs_block_free(block);
		return 0;
	}

	// open the repository
	struct FSRepo* fs_repo;
	retVal = ipfs_repo_fsrepo_new("./tmp/.ipfs", NULL, &fs_repo);
	if (retVal == 0) {
		ipfs_block_free(block);
		return 0;
	}
	retVal = ipfs_repo_fsrepo_open(fs_repo);
	if (retVal == 0) {
		ipfs_block_free(block);
		return 0;
	}
	// send to Put with key
	retVal = ipfs_datastore_helper_add_block_to_datastore(block, fs_repo->config->datastore);
	if (retVal == 0) {
		ipfs_block_free(block);
		return 0;
	}

	// clean up
	ipfs_repo_fsrepo_free(fs_repo);
	ipfs_block_free(block);

	return 1;
}

/**
 * List what is in the journal
 */
int test_datastore_list_journal() {
	int recCount = 0;
	libp2p_logger_add_class("test_datastore");
	libp2p_logger_add_class("lmdb_datastore");

	// need to run test_import_small_file first

	// open database
	struct FSRepo* fs_repo;
	if (ipfs_repo_fsrepo_new("./tmp/.ipfs", NULL, &fs_repo) == 0) {
		return 0;
	}
	if (ipfs_repo_fsrepo_open(fs_repo) == 0) {
		return 0;
	}
	// open cursor
	struct lmdb_trans_cursor *crsr = NULL;
	if (!lmdb_journalstore_cursor_open(fs_repo->config->datastore->datastore_context, &crsr, NULL)) {
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}
	// grab records
	struct JournalRecord* record = NULL;
	enum DatastoreCursorOp op = CURSOR_FIRST;
	do {
		if (lmdb_journalstore_cursor_get(crsr, op, &record) == 0) {
			lmdb_journal_record_free(record);
			record = NULL;
			break;
		}
		recCount++;
		// display record
		libp2p_logger_debug("test_datastore", "Timestamp: %llu.\n", record->timestamp);
		libp2p_logger_debug("test_datastore", "Pin: %s.\n", record->pin == 1 ? "Y" : "N");
		// free record
		lmdb_journal_record_free(record);
		record = NULL;
		op = CURSOR_NEXT;
	} while (record != NULL);
	libp2p_logger_error("test_datastore", "Found %d records.\n", recCount);
	return 1;
}
