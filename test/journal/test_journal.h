#include <stdlib.h>
#include <pthread.h>

#include "ipfs/journal/journal_entry.h"
#include "ipfs/journal/journal_message.h"
#include "ipfs/repo/fsrepo/journalstore.h"

int test_journal_encode_decode() {
	int retVal = 0;
	struct JournalEntry* entry = ipfs_journal_entry_new();
	struct JournalMessage* message = ipfs_journal_message_new();
	struct JournalMessage* result_message = NULL;
	struct JournalEntry* result_entry = NULL;
	uint8_t *buffer;
	size_t buffer_size;

	// build entry
	entry->hash = malloc(1);
	entry->hash[0] = 1;
	entry->hash_size = 1;
	entry->pin = 1;
	entry->timestamp = 1;
	// build message
	message->current_epoch = 2;
	message->start_epoch = 3;
	message->end_epoch = 4;
	libp2p_utils_vector_add(message->journal_entries, entry);

	// protobuf it
	buffer_size = ipfs_journal_message_encode_size(message);
	buffer = malloc(buffer_size);
	if (!ipfs_journal_message_encode(message, buffer, buffer_size, &buffer_size))
		goto exit;

	// unprotobuf it
	if (!ipfs_journal_message_decode(buffer, buffer_size, &result_message))
		goto exit;

	// compare
	if (result_message->current_epoch != message->current_epoch)
		goto exit;
	if (result_message->start_epoch != message->start_epoch)
		goto exit;
	if (result_message->end_epoch != message->end_epoch)
		goto exit;
	if (result_message->journal_entries->total != message->journal_entries->total)
		goto exit;
	result_entry = (struct JournalEntry*) libp2p_utils_vector_get(message->journal_entries, 0);
	if (result_entry->timestamp != entry->timestamp)
		goto exit;
	if (result_entry->pin != entry->pin)
		goto exit;
	if (result_entry->hash_size != entry->hash_size)
		goto exit;
	for (int i = 0; i < result_entry->hash_size; i++) {
		if (result_entry->hash[i] != entry->hash[i])
			goto exit;
	}

	// cleanup
	retVal = 1;
	exit:
	if (buffer != NULL)
		free(buffer);
	ipfs_journal_message_free(message);
	ipfs_journal_message_free(result_message);
	// the above lines take care of these
	//ipfs_journal_entry_free(entry);
	//ipfs_journal_entry_free(result_entry);
	return retVal;
}

/***
 * Starts a server with a file in it with a specific set of files.
 * It also has a specific address, with a config file of another
 * known peer to replicate between
 */
int test_journal_server_1() {
	int retVal = 0;
	pthread_t daemon_thread;
	int thread_started = 0;
	char* ipfs_path = "./tmp/ipfs_1";
	char* config_file = "config.test1";
	struct FSRepo* fs_repo = NULL;

	libp2p_logger_add_class("test_journal");
	libp2p_logger_add_class("journal");
	libp2p_logger_add_class("daemon");
	libp2p_logger_add_class("online");
	libp2p_logger_add_class("peer");
	libp2p_logger_add_class("null");
	libp2p_logger_add_class("replication");
	libp2p_logger_add_class("fs_repo");
	libp2p_logger_add_class("lmdb_journalstore");
	libp2p_logger_add_class("lmdb_datastore");
	libp2p_logger_add_class("secio");
	libp2p_logger_add_class("socket");
	libp2p_logger_add_class("protocol");
	libp2p_logger_add_class("dht_protocol");
	libp2p_logger_add_class("resolver");
	libp2p_logger_add_class("unixfs");
	libp2p_logger_add_class("bitswap_engine");
	libp2p_logger_add_class("bitswap_network");

	if (!drop_build_open_repo(ipfs_path, &fs_repo, config_file)) {
		ipfs_repo_fsrepo_free(fs_repo);
		libp2p_logger_error("test_journal", "Unable to drop and build repository at %s\n", ipfs_path);
		goto exit;
	}

	libp2p_logger_debug("test_journal", "Changed the server id to %s.\n", fs_repo->config->identity->peer->id);

	ipfs_repo_fsrepo_free(fs_repo);

	// add some files to the datastore
	uint8_t *bytes = (unsigned char*)"hello, world!\n";
	char* filename = "test1.txt";
	create_file(filename, bytes, strlen((char*)bytes));
	struct HashtableNode* node;
	size_t bytes_written;
	struct IpfsNode *local_node = NULL;
	ipfs_node_offline_new(ipfs_path, &local_node);
	ipfs_import_file(NULL, filename, &node, local_node, &bytes_written, 0);
	ipfs_node_free(local_node);
	ipfs_hashtable_node_free(node);

	libp2p_logger_debug("test_journal", "*** Firing up daemon for server 1 ***\n");

	pthread_create(&daemon_thread, NULL, test_daemon_start, (void*)ipfs_path);
	thread_started = 1;

	sleep(45);

	libp2p_logger_error("test_journal", "Sleep is over. Shutting down.\n");

	retVal = 1;
	exit:
	ipfs_daemon_stop();
	if (thread_started)
		pthread_join(daemon_thread, NULL);
	return retVal;
}

/***
 * Starts a server with a specific set of files.
 * It also has a specific address, with a config file of another
 * known peer to replicate between
 */
int test_journal_server_2() {
	int retVal = 0;
	pthread_t daemon_thread;
	int thread_started = 0;
	char* ipfs_path = "./tmp/ipfs_2";
	char* config_file = "config.test2";
	struct FSRepo* fs_repo = NULL;

	libp2p_logger_add_class("test_journal");
	libp2p_logger_add_class("journal");
	libp2p_logger_add_class("daemon");
	libp2p_logger_add_class("online");
	libp2p_logger_add_class("peer");
	libp2p_logger_add_class("null");
	libp2p_logger_add_class("replication");
	libp2p_logger_add_class("fs_repo");
	libp2p_logger_add_class("lmdb_journalstore");
	libp2p_logger_add_class("lmdb_datastore");
	libp2p_logger_add_class("secio");
	libp2p_logger_add_class("protocol");
	libp2p_logger_add_class("dht_protocol");
	libp2p_logger_add_class("resolver");
	libp2p_logger_add_class("unixfs");
	libp2p_logger_add_class("bitswap_engine");
	libp2p_logger_add_class("bitswap_network");

	if (!drop_build_open_repo(ipfs_path, &fs_repo, config_file)) {
		ipfs_repo_fsrepo_free(fs_repo);
		libp2p_logger_error("test_journal", "Unable to drop and build repository at %s\n", ipfs_path);
		goto exit;
	}

	libp2p_logger_debug("test_journal", "Changed the server id to %s.\n", fs_repo->config->identity->peer->id);

	ipfs_repo_fsrepo_free(fs_repo);

	libp2p_logger_debug("test_journal", "*** Firing up daemon for server 2 ***\n");

	pthread_create(&daemon_thread, NULL, test_daemon_start, (void*)ipfs_path);
	thread_started = 1;

	sleep(45);

	// see if we have the file that we should have...
	if (!have_file_in_blockstore("./tmp/ipfs_2/blockstore", "2PD7A7OALR6OCEDZNKYAX363LMX3SBXZQPD3IAVTT")) {
		libp2p_logger_error("test_journal", "We don't have the file that we think we should.\n");
		goto exit;
	} else {
		libp2p_logger_debug("test_journal", "File is here. Success!\n");
	}

	retVal = 1;
	exit:
	ipfs_daemon_stop();
	if (thread_started)
		pthread_join(daemon_thread, NULL);
	return retVal;
}

#include "lmdb.h"

// test the lightning db process
int test_journal_db() {

	MDB_env *mdb_env = NULL;
	MDB_txn *mdb_txn = NULL;
	MDB_dbi datastore_db;
	MDB_dbi journalstore_db;
	MDB_val datastore_key;
	MDB_val datastore_value;
	MDB_val *journalstore_key;
	MDB_val *journalstore_value;
	MDB_val returned_value;

	// set up records
	char* key = "ABC123";
	char* value = "Hello, world!";
	datastore_key.mv_size = strlen(key);
	datastore_key.mv_data = (void*)key;
	datastore_value.mv_size = strlen(value);
	datastore_value.mv_data = (void*)value;
	journalstore_key = (MDB_val*) malloc(sizeof(MDB_val));
	journalstore_key->mv_size = strlen(key);
	journalstore_key->mv_data = (void*)key;
	journalstore_value = (MDB_val*) malloc(sizeof(MDB_val));
	journalstore_value->mv_size = strlen(value);
	journalstore_value->mv_data = (void*)value;

	// clean up the old stuff
	unlink ("./tmp/lock.mdb");
	unlink ("./tmp/data.mdb");

	// create environment
	if (mdb_env_create(&mdb_env) != 0)
		return 0;

	if (mdb_env_set_maxdbs(mdb_env, (MDB_dbi)2) != 0)
		return 0;

	if (mdb_env_open(mdb_env, "/tmp", 0, S_IRWXU) != 0) {
		fprintf(stderr, "Unable to open environment.\n");
		return 0;
	}

	// create a transaction
	if (mdb_txn_begin(mdb_env, NULL, 0, &mdb_txn) != 0) {
		fprintf(stderr, "Unable to open transaction.\n");
		return 0;
	}

	// open databases
	if (mdb_dbi_open(mdb_txn, "DATASTORE", MDB_DUPSORT | MDB_CREATE, &datastore_db) != 0)
		return 0;
	if (mdb_dbi_open(mdb_txn, "JOURNALSTORE", MDB_DUPSORT | MDB_CREATE, &journalstore_db) != 0)
		return 0;

	// search for a record in the datastore
	if (mdb_get(mdb_txn, datastore_db, &datastore_key, &returned_value) != MDB_NOTFOUND) {
		return 0;
	}

	// add record to datastore
	if (mdb_put(mdb_txn, datastore_db, &datastore_key, &datastore_value, 0) != 0)
		return 0;

	// add record to journalstore
	if (mdb_put(mdb_txn, journalstore_db, journalstore_key, journalstore_value, 0) != 0)
		return 0;

	// get rid of MDB_val values from journalstore to see if commit still works
	free(journalstore_key);
	free(journalstore_value);
	journalstore_key = NULL;
	journalstore_value = NULL;

	// close everything up
	if (mdb_txn_commit(mdb_txn) != 0)
		return 0;

	mdb_env_close(mdb_env);

	return 1;
}
