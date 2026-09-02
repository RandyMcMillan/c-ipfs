#include <string.h>
#include <errno.h>

#include "varint.h"
#include "lmdb.h"
#include "libp2p/utils/logger.h"
#include "libp2p/crypto/encoding/base58.h"
#include "ipfs/repo/fsrepo/journalstore.h"
#include "ipfs/repo/fsrepo/lmdb_datastore.h"

struct JournalRecord* lmdb_journal_record_new() {
	struct JournalRecord* rec = (struct JournalRecord*) malloc(sizeof(struct JournalRecord));
	if (rec != NULL) {
		rec->hash = NULL;
		rec->hash_size = 0;
		rec->pending = 0;
		rec->pin = 0;
		rec->timestamp = 0;
	}
	return rec;
}

int lmdb_journal_record_free(struct JournalRecord* rec) {
	if (rec != NULL) {
		if (rec->hash != NULL)
			free(rec->hash);
		rec->hash = NULL;
		free(rec);
	}
	return 1;
}

int lmdb_journalstore_generate_key(const struct JournalRecord* journal_record, struct MDB_val *db_key) {
	// build the key
	uint8_t *time_varint = (uint8_t*) malloc(8);
	if (time_varint == NULL) {
		return 0;
	}
	size_t time_varint_size = 0;
	varint_encode(journal_record->timestamp, time_varint, 8, &time_varint_size);

	db_key->mv_size = time_varint_size;
	db_key->mv_data = time_varint;
	return 1;
}

/***
 * Convert the JournalRec struct into a lmdb key and lmdb value
 * @param journal_record the record to convert
 * @param db_key where to store the key information
 * @param db_value where to store the value information
 */
int lmdb_journalstore_build_key_value_pair(const struct JournalRecord* journal_record, struct MDB_val* db_key, struct MDB_val *db_value) {
	// build the record, which is a timestamp as a key

	// build the key
	if (!lmdb_journalstore_generate_key(journal_record, db_key)) {
		return 0;
	}

	// build the value
	size_t record_size = journal_record->hash_size + 2;
	uint8_t *record = (uint8_t*) malloc(record_size);
	if (record == NULL) {
		free(db_key->mv_data);
		return 0;
	}
	// Field 1: pin flag
	record[0] = journal_record->pin;
	// Field 2: pending flag
	record[1] = journal_record->pending;
	// field 3: hash
	memcpy(&record[2], journal_record->hash, journal_record->hash_size);

	db_value->mv_size = record_size;
	db_value->mv_data = record;

	return 1;
}

/***
 * Build a JournalRecord from a key/value pair from the db
 * @param db_key the key
 * @param db_value the value
 * @param journal_record where to store the results
 * @reutrns true(1) on success, false(0) on error
 */
int lmdb_journalstore_build_record(const struct MDB_val* db_key, const struct MDB_val *db_value, struct JournalRecord **journal_record) {
	if (*journal_record == NULL) {
		*journal_record = lmdb_journal_record_new();
		if (*journal_record == NULL) {
			libp2p_logger_error("lmdb_journalstore", "build_record: Unable to allocate memory for new journal record.\n");
			return 0;
		}
	}

	struct JournalRecord *rec = *journal_record;
	// timestamp
	size_t varint_size = 0;
	rec->timestamp = varint_decode(db_key->mv_data, db_key->mv_size, &varint_size);
	// pin flag
	rec->pin = ((uint8_t*)db_value->mv_data)[0];
	// pending flag
	rec->pending = ((uint8_t*)db_value->mv_data)[1];
	// hash
	if (rec->hash != NULL) {
		free(rec->hash);
		rec->hash = NULL;
		rec->hash_size = 0;
	}
	rec->hash_size = db_value->mv_size - 2;
	rec->hash = malloc(rec->hash_size);
	if (rec->hash != NULL) {
		uint8_t *val = (uint8_t*)db_value->mv_data;
		memcpy(rec->hash, &val[2], rec->hash_size);
	} else {
		return 0;
	}

	return 1;
}

/***
 * Write a journal record
 * @param mbd_txn the transaction
 * @param timestamp the timestamp
 * @param hash the hash
 * @param hash_size the size of the hash
 * @returns true(1) on success, false(0) otherwise
 */
int lmdb_journalstore_journal_add(struct lmdb_trans_cursor *journalstore_cursor, struct JournalRecord *journalstore_record) {

	MDB_val journalstore_key;
	MDB_val journalstore_value;
	int createdTransaction = 0;

	if (!lmdb_journalstore_build_key_value_pair(journalstore_record, &journalstore_key, &journalstore_value)) {
		libp2p_logger_error("lmdbd_journalstore", "add: Unable to convert journalstore record to key/value.\n");
		return 0;
	}

	// create transaction if necessary
	if (journalstore_cursor->transaction == NULL) {
		mdb_txn_begin(journalstore_cursor->environment, journalstore_cursor->parent_transaction, 0, &journalstore_cursor->transaction);
		createdTransaction = 1;
	}

	int retVal = mdb_put(journalstore_cursor->transaction, *journalstore_cursor->database, &journalstore_key, &journalstore_value, 0);

	free(journalstore_key.mv_data);
	free(journalstore_value.mv_data);

	if (retVal != 0) {
		libp2p_logger_error("lmdb_journalstore", "Unable to add to JOURNALSTORE database.\n");
		return 0;
	}

	if (createdTransaction) {
		if (mdb_txn_commit(journalstore_cursor->transaction) != 0) {
			libp2p_logger_error("lmdb_journalstore", "Unable to commit JOURNALSTORE transaction.\n");
			return 0;
		}
	}

	return 1;
}

/***
 * Attempt to get a specific record identified by its timestamp and bytes
 * @param handle a handle to the database engine
 * @param journalstore_cursor the cursor (will be returned as a cursor that points to the record found)
 * @param journalstore_record where to put the results (can pass null). If data is within the struct, will use it as search criteria
 * @returns true(1) on success, false(0) otherwise
 */
int lmdb_journalstore_get_record(void* handle, struct lmdb_trans_cursor *journalstore_cursor, struct JournalRecord **journalstore_record)
{

	if (handle == NULL) {
		libp2p_logger_error("lmdb_journalstore", "get_record: database environment not set up.\n");
		return 0;
	}
	struct lmdb_context *db_context = (struct lmdb_context*)handle;

	// create a new transaction if necessary
	if (journalstore_cursor->transaction == NULL) {
		if (mdb_txn_begin(db_context->db_environment, journalstore_cursor->parent_transaction, 0, &journalstore_cursor->transaction) != 0) {
			libp2p_logger_error("lmdb_journanstore", "get_record: Attempt to begin transaction failed.\n");
			return 0;
		}
	}

	if (journalstore_cursor->cursor == NULL) {
		if (!lmdb_journalstore_cursor_open(handle, &journalstore_cursor, NULL)) {
			libp2p_logger_error("lmdb_journalstore", "Unable to open cursor in get_record.\n");
			return 0;
		}
	}
	// search for the timestamp
	if (!lmdb_journalstore_cursor_get(journalstore_cursor, CURSOR_FIRST, journalstore_record)) {
		libp2p_logger_debug("lmdb_journalstore", "Unable to find any records in table.\n");
		return 0;
	}

	return 1;
}

/**
 * Open a cursor to the journalstore table
 * @param db_handle a handle to the database (an MDB_env pointer)
 * @param cursor where to place the results
 * @returns true(1) on success, false(0) otherwise
 */
int lmdb_journalstore_cursor_open(void *handle, struct lmdb_trans_cursor **crsr, struct MDB_txn* trans_to_use) {
	if (handle != NULL) {
		struct lmdb_context *db_context = (struct lmdb_context*)handle;
		struct lmdb_trans_cursor *cursor = *crsr;
		if (cursor == NULL ) {
			cursor = lmdb_trans_cursor_new();
			if (cursor == NULL)
				return 0;
			*crsr = cursor;
		}
		cursor->database = db_context->journal_db;
		cursor->environment = db_context->db_environment;
		cursor->parent_transaction = db_context->current_transaction;

		if (cursor->transaction == NULL) {
			if (trans_to_use != NULL)
				cursor->transaction = trans_to_use;
			else {
				// open transaction
				if (mdb_txn_begin(db_context->db_environment, db_context->current_transaction, 0, &cursor->transaction) != 0) {
					libp2p_logger_error("lmdb_journalstore", "cursor_open: Unable to begin a transaction.\n");
					return 0;
				}
			}
		}
		if (cursor->cursor == NULL) {
			// open cursor
			if (mdb_cursor_open(cursor->transaction, *cursor->database, &cursor->cursor) != 0) {
				libp2p_logger_error("lmdb_journalstore", "cursor_open: Unable to open cursor.\n");
				mdb_txn_commit(cursor->transaction);
				return 0;
			}
			return 1;
		}
	} else {
		libp2p_logger_error("lmdb_journalstore", "Unable to open cursor on null db handle.\n");
	}
	return 0;

}

int lmdb_journalstore_composite_key_compare(struct JournalRecord *a, struct JournalRecord *b) {
	if (a == NULL && b == NULL)
		return 0;
	if (a == NULL && b != NULL)
		return 1;
	if (a != NULL && b == NULL)
		return -1;
	if (a->timestamp != b->timestamp) {
		if (a->timestamp > b->timestamp)
			return -1;
		else
			return 1;
	}
	if (a->hash_size != b->hash_size) {
		if (a->hash_size > b->hash_size)
			return -1;
		else
			return 1;
	}
	for(int i = 0; i < a->hash_size; i++) {
		if (a->hash[i] != b->hash[i]) {
			return b->hash[i] - a->hash[i];
		}
	}
	return 0;
}

/**
 * Read a record from the cursor. If (record) contains a key, it will look for the exact record.
 * If not, it will return the first matching record.
 * @param crsr the lmdb_trans_cursor
 * @param op the cursor operation (i.e. CURSOR_FIRST, CURSOR_NEXT, CURSOR_LAST, CURSOR_PREVIOUS)
 * @param record the record (will allocate a new one if *record is NULL)
 * @returns true(1) if something was found, false(0) otherwise)
 */
int lmdb_journalstore_cursor_get(struct lmdb_trans_cursor *tc, enum DatastoreCursorOp op, struct JournalRecord** record) {
	if (tc != NULL) {
		MDB_val mdb_key;
		MDB_val mdb_value;
		MDB_cursor_op co = MDB_FIRST;

		if (op == CURSOR_FIRST)
			co = MDB_FIRST;
		else if (op == CURSOR_NEXT)
			co = MDB_NEXT;
		else if (op == CURSOR_LAST)
			co = MDB_LAST;
		else if (op == CURSOR_PREVIOUS)
			co = MDB_PREV;

		int retVal = mdb_cursor_get(tc->cursor, &mdb_key, &mdb_value, co);
		if (retVal != 0) {
			if (retVal == MDB_NOTFOUND) {
				libp2p_logger_debug("lmdb_journalstore", "cursor_get: No records found in db.\n");
			} else if (retVal == EINVAL) {
				libp2p_logger_debug("lmdb_journalstore", "cursor_get: Invalid parameter specified.\n");
			}
			return 0;
		}

		if (*record == NULL) {
			// make a new record and pass it back
			if (!lmdb_journalstore_build_record(&mdb_key, &mdb_value, record))
				return 0;
			return 1;
		}

		// see if the passed in record has a specific record in mind (take care of duplicate keys)
		if ( (*record)->hash_size > 0) {
			struct JournalRecord* curr_record = NULL;
			if (!lmdb_journalstore_build_record(&mdb_key, &mdb_value, &curr_record)) {
				libp2p_logger_error("lmdb_journalstore", "Unable to convert journalstore record into a JournalRecord struct.\n");
				return 0;
			}
			// we are looking for a specific record. Flip through the records looking for the exact record
			while (lmdb_journalstore_composite_key_compare(*record, curr_record) != 0) {
				if ( (*record)->timestamp != curr_record->timestamp) {
					//we've exhausted all records for this timestamp. Exit.
					lmdb_journal_record_free(curr_record);
					curr_record = NULL;
					break;
				}
				// we did not find the exact record. Skip to the next one
				lmdb_journal_record_free(curr_record);
				curr_record = NULL;
				retVal = mdb_cursor_get(tc->cursor, &mdb_key, &mdb_value, MDB_NEXT);
				if (retVal != 0) {
					if (retVal == MDB_NOTFOUND) {
						libp2p_logger_debug("lmdb_journalstore", "cursor_get: Reached end of records without finding match.\n");
					}
					break;
				}
				if (!lmdb_journalstore_build_record(&mdb_key, &mdb_value, &curr_record)) {
					libp2p_logger_error("lmdb_journalstore", "Unable to convert journalstore record into a JournalRecord struct.\n");
					return 0;
				}
			}
			if (curr_record != NULL) {
				// we found the exact record. merge it into the *record
				(*record)->pending = curr_record->pending;
				(*record)->pin = curr_record->pin;
				lmdb_journal_record_free(curr_record);
				return 1;
			}
		} else {
			// we're not looking for any particular record. Return the first one found.
			return lmdb_journalstore_build_record(&mdb_key, &mdb_value, record);
		}
	}
	return 0;
}

/***
 * Write the record at the cursor
 * @param crsr the cursor
 * @param journal_record the record to write
 * @returns true(1) on success, false(0) otherwise
 */
int lmdb_journalstore_cursor_put(struct lmdb_trans_cursor *crsr, struct JournalRecord* journal_record) {
	struct MDB_cursor* cursor = crsr->cursor;
	struct MDB_val db_key;
	struct MDB_val db_value;

	if (!lmdb_journalstore_build_key_value_pair(journal_record, &db_key, &db_value)) {
		libp2p_logger_error("lmdb_journalstore", "Unable to create journalstore record.\n");
		return 0;
	}
	int retVal = mdb_cursor_put(cursor, &db_key, &db_value, 0);

	free(db_key.mv_data);
	free(db_value.mv_data);

	if (retVal != 0) {
		char* result = "";
		switch (retVal) {
		case(MDB_MAP_FULL):
				result = "Database Full";
		break;
		case (MDB_TXN_FULL):
				result = "Transaction has too many dirty pages";
		break;
		case (EACCES) :
				result = "Attempt was made to write in a read only transaction";
		break;
		case (EINVAL) :
				result = "An invalid parameter was specified";
		break;
		}
		libp2p_logger_error("lmdb_journalstore", "Put failed with error message %d [%s].\n", retVal, result);
		return 0;
	}

	return 1;
}

/**
 * Close the cursor and commits the transaction.
 * @param crsr a lmdb_trans_cursor pointer
 * @returns true(1)
 */
int lmdb_journalstore_cursor_close(struct lmdb_trans_cursor *cursor, int commitTransaction) {
	if (cursor != NULL) {
		if (cursor->cursor != NULL) {
			//mdb_cursor_close(cursor->cursor);
		}
		if (cursor->transaction != NULL && commitTransaction) {
			mdb_txn_commit(cursor->transaction);
		}
		cursor->cursor = NULL;
		cursor->transaction = NULL;
		lmdb_trans_cursor_free(cursor);
	}
	return 1;
}
