/***
 * Here are the wrappers for the lightning database
 * NOTE: In this implementation, the database will contain the base32 encoded value
 * of the multihash key if the file exists on disk.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "lmdb.h"
#include "libp2p/utils/logger.h"
#include "libp2p/crypto/encoding/base58.h"
#include "libp2p/os/utils.h"
#include "libp2p/db/datastore.h"
#include "ipfs/repo/fsrepo/lmdb_datastore.h"
#include "ipfs/repo/fsrepo/journalstore.h"
#include "libp2p/db/datastore.h"
#include "varint.h"

/**
 * Build a "value" section for a datastore record
 * @param record the data
 * @param result the data (usually a base32 of the cid hash) + the timestamp as varint
 * @param result_size the size of the result
 * @returns true(1) on success, otherwise 0
 */
int repo_fsrepo_lmdb_encode_record(struct DatastoreRecord* record, uint8_t **result, size_t *result_size) {
	// turn timestamp into varint
	uint8_t ts_varint[8];
	size_t num_bytes;
	if (varint_encode(record->timestamp, &ts_varint[0], 8, &num_bytes) == NULL) {
		return 0;
	}
	// make new structure
	*result = (uint8_t *) malloc(num_bytes + record->value_size);
	if (*result == NULL) {
		return 0;
	}
	memcpy(*result, ts_varint, num_bytes);
	memcpy(&(*result)[num_bytes], record->value, record->value_size);
	*result_size = record->value_size + num_bytes;
	return 1;
}

/**
 * turn lmdb components into a DatastoreRecord structure
 * @param key the key that we searched for in the database
 * @param value the result of the search
 * @param record the complete structure
 * @returns true(1) on success, false(0) otherwise
 */
int repo_fsrepo_lmdb_build_record(MDB_val *key, MDB_val *value, struct DatastoreRecord** record) {
	*record = libp2p_datastore_record_new();
	if (*record != NULL) {
		size_t varint_size = 0;
		struct DatastoreRecord *rec = *record;
		// set key
		rec->key_size = key->mv_size;
		rec->key = (uint8_t *) malloc(rec->key_size);
		if (rec->key == NULL) {
			libp2p_datastore_record_free(*record);
			*record = NULL;
			return 0;
		}
		memcpy(rec->key, key->mv_data, key->mv_size);
		// set value
		rec->timestamp = varint_decode(value->mv_data, value->mv_size, &varint_size);
		rec->value_size = value->mv_size - varint_size;
		rec->value = (uint8_t *) malloc(rec->value_size);
		if (rec->value == NULL) {
			libp2p_datastore_record_free(*record);
			*record = NULL;
			return 0;
		}
		uint8_t *val = (uint8_t*) value->mv_data;
		memcpy(rec->value, &val[varint_size], rec->value_size);
	}
	return 1;
}

/***
 * retrieve a record from the database and put in a pre-sized buffer
 * using an already allocated transaction, and with an already opened
 * database
 * @param key the key to look for
 * @param key_size the length of the key
 * @param record where to put the results
 * @param datastore where to look for the data
 * @param mdb_txn the already opened db transaction
 * @param datastore_table the reference to the already opened datastore table (database)
 * @returns true(1) on success
 */
int repo_fsrepo_lmdb_get_with_transaction(const unsigned char* key, size_t key_size, struct DatastoreRecord** record, MDB_txn *mdb_txn, MDB_dbi *datastore_table) {
	struct MDB_val db_key;
	struct MDB_val db_value;

	// check parameters passed in
	if (mdb_txn == NULL || datastore_table == NULL) {
		libp2p_logger_error("lmdb_datastore", "get_w_tx: invalid transaction or table reference.\n");
		return 0;
	}

	// prepare data
	db_key.mv_size = key_size;
	db_key.mv_data = (char*)key;

	if (mdb_get(mdb_txn, *datastore_table, &db_key, &db_value) != 0) {
		return 0;
	}

	if (!repo_fsrepo_lmdb_build_record(&db_key, &db_value, record)) {
		return 0;
	}

	return 1;

}

/***
 * retrieve a record from the database and put in a pre-sized buffer
 * @param key the key to look for
 * @param key_size the length of the key
 * @param record where to put the results
 * @param datastore where to look for the data
 * @returns true(1) on success
 */
int repo_fsrepo_lmdb_get(const unsigned char* key, size_t key_size, struct DatastoreRecord **record, const struct Datastore* datastore) {
	MDB_txn* mdb_txn;

	if (datastore == NULL || datastore->datastore_context == NULL) {
		libp2p_logger_error("lmdb_datastore", "get: datastore not initialized.\n");
		return 0;
	}
	struct lmdb_context *db_context = (struct lmdb_context*) datastore->datastore_context;
	if (db_context->db_environment == NULL) {
		libp2p_logger_error("lmdb_datastore", "get: datastore environment not initialized.\n");
		return 0;
	}

	// open transaction
	if (mdb_txn_begin(db_context->db_environment, db_context->current_transaction, 0, &mdb_txn) != 0)
		return 0;

	int retVal = repo_fsrepo_lmdb_get_with_transaction(key, key_size, record, mdb_txn, db_context->datastore_db);

	mdb_txn_commit(mdb_txn);

	return retVal;
}

/**
 * Open the database and create a new transaction
 * @param mdb_env the database handle
 * @param mdb_dbi  the table handle to be created
 * @param mdb_txn the transaction to be created
 * @returns true(1) on success, false(0) otherwise
 */
int lmdb_datastore_create_transaction(struct lmdb_context *db_context, MDB_txn **mdb_txn) {
	// open transaction
	int retVal = mdb_txn_begin(db_context->db_environment, db_context->current_transaction, 0, mdb_txn);
	if (retVal != 0) {
		libp2p_logger_error("lmdb_datastore", "Unable to create transaction. Error code %d.\n", retVal);
		return 0;
	}
	return 1;
}

/**
 * Write (or update) data in the datastore with the specified key
 * @param key the key
 * @param key_size the length of the key
 * @param data the data to be written
 * @param data_size the length of the data to be written
 * @param datastore the datastore to write to
 * @returns true(1) on success
 */
int repo_fsrepo_lmdb_put(struct DatastoreRecord* datastore_record, const struct Datastore* datastore) {
	int retVal;
	struct MDB_txn *child_transaction;
	struct MDB_val datastore_key;
	struct MDB_val datastore_value;
	struct DatastoreRecord* existingRecord = NULL;
	struct JournalRecord *journalstore_record = NULL;
	struct lmdb_trans_cursor *journalstore_cursor = NULL;

	if (datastore == NULL || datastore->datastore_context == NULL)
		return 0;

	struct lmdb_context *db_context = (struct lmdb_context*)datastore->datastore_context;

	if (db_context->db_environment == NULL) {
		libp2p_logger_error("lmdb_datastore", "put: invalid datastore handle.\n");
		return 0;
	}

	// open a transaction to the databases
	if (!lmdb_datastore_create_transaction(db_context, &child_transaction)) {
		libp2p_logger_error("lmdb_datastore", "put: Unable to create db transaction.\n");
		return 0;
	}

	// build the journalstore connectivity stuff
	lmdb_journalstore_cursor_open(datastore->datastore_context, &journalstore_cursor, child_transaction);
	if (journalstore_cursor == NULL) {
		libp2p_logger_error("lmdb_datastore", "put: Unable to allocate memory for journalstore cursor.\n");
		return 0;
	}

	// see if what we want is already in the datastore
	repo_fsrepo_lmdb_get_with_transaction(datastore_record->key, datastore_record->key_size, &existingRecord, child_transaction, db_context->datastore_db);
	if (existingRecord != NULL) {
		// overwrite the timestamp of the incoming record if what we have is older than what is coming in
		if ( existingRecord->timestamp != 0 && datastore_record->timestamp > existingRecord->timestamp) {
			datastore_record->timestamp = existingRecord->timestamp;
		}
		// build the journalstore_record with the search criteria
		journalstore_record = lmdb_journal_record_new();
		journalstore_record->hash_size = datastore_record->key_size;
		journalstore_record->hash = malloc(datastore_record->key_size);
		if (journalstore_record->hash == NULL) {
			libp2p_logger_error("lmdb_datastore", "put: Unable to allocate memory for key.\n");
			return 0;
		}
		memcpy(journalstore_record->hash, datastore_record->key, datastore_record->key_size);
		journalstore_record->timestamp = datastore_record->timestamp;
		// look up the corresponding journalstore record for possible updating
		lmdb_journalstore_get_record(db_context, journalstore_cursor, &journalstore_record);
	}

	// Put in the timestamp if it isn't there already (or is newer)
	unsigned long long now = os_utils_gmtime();
	if (datastore_record->timestamp == 0 || datastore_record->timestamp > now) {
		datastore_record->timestamp = now;
	}

	// If an identical record already exists, skip the rewrite
	if (existingRecord != NULL) {
		if (existingRecord->timestamp == datastore_record->timestamp &&
		    existingRecord->value_size == datastore_record->value_size &&
		    memcmp(existingRecord->value, datastore_record->value, existingRecord->value_size) == 0) {
			mdb_txn_commit(child_transaction);
			libp2p_datastore_record_free(existingRecord);
			lmdb_journalstore_cursor_close(journalstore_cursor, 0);
			if (journalstore_record != NULL) {
				lmdb_journal_record_free(journalstore_record);
			}
			return 1;
		}
	}

	// convert it into a byte array
	size_t record_size = 0;
	uint8_t *record;
	repo_fsrepo_lmdb_encode_record(datastore_record, &record, &record_size);

	// prepare data
	datastore_key.mv_size = datastore_record->key_size;
	datastore_key.mv_data = (char*)datastore_record->key;

	// delete any existing value(s) for this key before writing
	mdb_del(child_transaction, *db_context->datastore_db, &datastore_key, NULL);

	// write
	datastore_value.mv_size = record_size;
	datastore_value.mv_data = record;

	retVal = mdb_put(child_transaction, *db_context->datastore_db, &datastore_key, &datastore_value, 0);

	if (retVal == 0) {
		// Successfully added the datastore record. Now work with the journalstore.
		if (journalstore_record != NULL) {
			if (journalstore_record->timestamp != datastore_record->timestamp) {
				// we need to update
				journalstore_record->timestamp = datastore_record->timestamp;
				retVal = lmdb_journalstore_cursor_put(journalstore_cursor, journalstore_record);
				lmdb_journalstore_cursor_close(journalstore_cursor, 0);
				lmdb_journal_record_free(journalstore_record);
			}
		} else {
			// add it to the journalstore
			journalstore_record = lmdb_journal_record_new();
			journalstore_record->hash = (uint8_t*) malloc(datastore_record->key_size);
			if (journalstore_record->hash == NULL) {
				libp2p_logger_error("lmdb_datastore", "Unable to allocate memory to add record to journalstore.\n");
				lmdb_journalstore_cursor_close(journalstore_cursor, 0);
				lmdb_journal_record_free(journalstore_record);
			} else {
				memcpy(journalstore_record->hash, datastore_record->key, datastore_record->key_size);
				journalstore_record->hash_size = datastore_record->key_size;
				journalstore_record->timestamp = datastore_record->timestamp;
				journalstore_record->pending = 1; /* New record: pending replication until sync confirmed */
				journalstore_record->pin = 1;
				if (!lmdb_journalstore_journal_add(journalstore_cursor, journalstore_record)) {
					libp2p_logger_error("lmdb_datastore", "Datastore record was added, but problem adding Journalstore record. Continuing.\n");
				}
				lmdb_journalstore_cursor_close(journalstore_cursor, 0);
				lmdb_journal_record_free(journalstore_record);
				retVal = 1;
			}
		}
	} else {
		// datastore record was unable to be added.
		if (retVal == MDB_KEYEXIST) {
			// duplicate key.. Is this an error?
		} else {
			libp2p_logger_error("lmdb_datastore", "mdb_put returned %d.\n", retVal);
			retVal = 0;
		}
	}

	// cleanup
	if (mdb_txn_commit(child_transaction) != 0) {
		libp2p_logger_error("lmdb_datastore", "lmdb_put: transaction commit failed.\n");
	}
	free(record);
	libp2p_datastore_record_free(existingRecord);
	return retVal;
}

/**
 * Open an lmdb database with the given parameters.
 * Note: for now, the parameters are not used
 * @param argc number of parameters in the following array
 * @param argv an array of parameters
 * @param datastore the datastore struct
 * @returns true(1) on success, false(0) otherwise
 */
int repo_fsrepro_lmdb_open(int argc, char** argv, struct Datastore* datastore) {
	// create environment
	struct MDB_env* mdb_env;
	if (mdb_env_create(&mdb_env) < 0) {
		mdb_env_close(mdb_env);
		return 0;
	}

	// at most, 2 databases will be opened. The datastore and the journal.
	MDB_dbi dbs = 2;
	if (mdb_env_set_maxdbs(mdb_env, dbs) != 0) {
		mdb_env_close(mdb_env);
		return 0;
	}

	// open the environment
	if (mdb_env_open(mdb_env, datastore->path, 0, S_IRWXU) < 0) {
		mdb_env_close(mdb_env);
		return 0;
	}

	struct lmdb_context *db_context = (struct lmdb_context *) malloc(sizeof(struct lmdb_context));
	if (db_context == NULL) {
		mdb_env_close(mdb_env);
		return 0;
	}
	datastore->datastore_context = (void*) db_context;
	db_context->db_environment = (void*)mdb_env;
	db_context->datastore_db = (MDB_dbi*) malloc(sizeof(MDB_dbi));
	if (db_context->datastore_db == NULL) {
		mdb_env_close(mdb_env);
		free(db_context);
		return 0;
	}
	db_context->journal_db = (MDB_dbi*) malloc(sizeof(MDB_dbi));
	if (db_context->journal_db == NULL) {
		free(db_context->datastore_db);
		free(db_context);
		mdb_env_close(mdb_env);
		return 0;
	}

	// open the 2 databases
	if (mdb_txn_begin(mdb_env, NULL, 0, &db_context->current_transaction) != 0) {
		mdb_env_close(mdb_env);
		db_context->db_environment = NULL;
		return 0;
	}
	if (mdb_dbi_open(db_context->current_transaction, "DATASTORE", MDB_DUPSORT | MDB_CREATE, db_context->datastore_db ) != 0) {
		mdb_txn_abort(db_context->current_transaction);
		mdb_env_close(mdb_env);
		db_context->db_environment = NULL;
		return 0;
	}
	if (mdb_dbi_open(db_context->current_transaction, "JOURNALSTORE", MDB_DUPSORT | MDB_CREATE, db_context->journal_db) != 0) {
		mdb_txn_abort(db_context->current_transaction);
		mdb_env_close(mdb_env);
		db_context->db_environment = NULL;
		return 0;
	}
	mdb_txn_commit(db_context->current_transaction);
	db_context->current_transaction = NULL;
	return 1;
}

/***
 * Close an LMDB database
 * @param datastore the datastore struct that contains information about the opened database
 * @returns true(1) on success, otherwise false(0)
 */
int repo_fsrepo_lmdb_close(struct Datastore* datastore) {
	// check parameters
	if (datastore == NULL || datastore->datastore_context == NULL)
		return 0;

	// close the db environment
	struct lmdb_context *db_context = (struct lmdb_context*) datastore->datastore_context;
	if (db_context->current_transaction != NULL) {
		mdb_txn_commit(db_context->current_transaction);
	}
	mdb_env_close(db_context->db_environment);

	free(db_context->datastore_db);
	free(db_context->journal_db);

	free(db_context);

	return 1;
}

/***
 * Places the LMDB methods into the datastore's function pointers
 * @param datastore the datastore to fill
 * @returns true(1) on success;
 */
int repo_fsrepo_lmdb_cast(struct Datastore* datastore) {
	datastore->datastore_open = &repo_fsrepro_lmdb_open;
	datastore->datastore_close = &repo_fsrepo_lmdb_close;
	datastore->datastore_put = &repo_fsrepo_lmdb_put;
	datastore->datastore_get = &repo_fsrepo_lmdb_get;
	return 1;
}

/***
 * Creates the directory
 * @param datastore contains the path that needs to be created
 * @returns true(1) on success
 */
int repo_fsrepo_lmdb_create_directory(struct Datastore* datastore) {
#ifdef __MINGW32__
	return mkdir(datastore->path) == 0;
#else
	return mkdir(datastore->path, S_IRWXU) == 0;
#endif
}

