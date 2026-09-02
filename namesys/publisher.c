#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "libp2p/routing/dht_protocol.h"
#include "libp2p/crypto/rsa.h"
#include "ipfs/util/errs.h"
#include "ipfs/util/time.h"
#include "ipfs/namesys/pb.h"
#include "ipfs/namesys/publisher.h"

/**
 * Convert an ipns_entry into a char array for signing
 *
 * @param entry the ipns_entry
 * @returns a char array that contains the data from the ipns_entry
 */
char* ipns_entry_data_for_sig (struct ipns_entry *entry)
{
    char *ret;

    if (!entry || !entry->value || !entry->validity) {
        return NULL;
    }
    ret = calloc (1, strlen(entry->value) + strlen (entry->validity) + sizeof(IpnsEntry_ValidityType) + 1);
    if (ret) {
        strcpy(ret, entry->value);
        strcat(ret, entry->validity);
        if (entry->validityType) {
            memcpy(ret+strlen(entry->value)+strlen(entry->validity), entry->validityType, sizeof(IpnsEntry_ValidityType));
        } else {
            IpnsEntry_ValidityType default_type = IpnsEntry_EOL;
            memcpy(ret+strlen(entry->value)+strlen(entry->validity), &default_type, sizeof(IpnsEntry_ValidityType));
        }
    }
    return ret;
}

int ipns_selector_func (int *idx, struct ipns_entry ***recs, char *k, char **vals)
{
    int err = 0, i, c;

    if (!idx || !recs || !k || !vals) {
        return ErrInvalidParam;
    }

    for (c = 0 ; vals[c] ; c++); // count array

    *recs = calloc(c+1, sizeof (void*)); // allocate return array.
    if (!*recs) {
        return ErrAllocFailed;
    }
    for (i = 0 ; i < c ; i++) {
        (*recs)[i] = ipfs_namesys_pb_new_ipns_entry(); // alloc every record
        if (!(*recs)[i]) {
            return ErrAllocFailed;
        }
        // NOTE: vals are assumed to be null-terminated protobuf strings for now.
        // In a full implementation, lengths should be passed explicitly.
        if (!ipfs_namesys_pb_ipns_entry_decode((const unsigned char*)vals[i], strlen(vals[i]), &(*recs)[i])) {
            ipfs_namesys_ipnsentry_reset ((*recs)[i]); // make sure record is empty.
        }
    }
    return ipns_select_record(idx, *recs, vals);
}

/***
 * selects an ipns_entry record from a list
 *
 * @param idx the index of the found record
 * @param recs the records
 * @param vals the search criteria?
 * @returns 0 on success, otherwise error code
 */
int ipns_select_record (int *idx, struct ipns_entry **recs, char **vals)
{
    int i, best_i = -1;
    uint64_t best_seq = 0;
    struct timespec rt, bestt;

    if (!idx || !recs || !vals) {
        return ErrInvalidParam;
    }

    for (i = 0 ; recs[i] ; i++) {
        if (!(recs[i]->sequence) || *(recs[i]->sequence) < best_seq) {
            continue;
        }

        if (best_i == -1 || *(recs[i]->sequence) > best_seq) {
            best_seq = *(recs[i]->sequence);
            best_i = i;
        } else if (*(recs[i]->sequence) == best_seq) {
            if (ipfs_util_time_parse_RFC3339 (&rt, ipfs_namesys_pb_get_validity (recs[i])) != 0) {
                continue;
            }
            if (ipfs_util_time_parse_RFC3339 (&bestt, ipfs_namesys_pb_get_validity (recs[best_i])) != 0) {
                continue;
            }
            if (rt.tv_sec > bestt.tv_sec || (rt.tv_sec == bestt.tv_sec && rt.tv_nsec > bestt.tv_nsec)) {
                best_i = i;
            } else if (rt.tv_sec == bestt.tv_sec && rt.tv_nsec == bestt.tv_nsec) {
                if (strcmp(vals[i], vals[best_i]) > 0) {
                    best_i = i;
                }
            }
        }
    }
    if (best_i == -1) {
        return ErrNoRecord;
    }
    *idx = best_i;
    return 0;
}

/****
 * implements ValidatorFunc and verifies that the
 * given 'val' is an IpnsEntry and that that entry is valid.
 *
 * @param k
 * @param val
 * @returns 0 on success, otherwise an error code
 */
int ipns_validate_ipns_record (char *k, char *val)
{
    int err = 0;
    struct ipns_entry *entry = NULL;
    struct timespec ts, now;

    if (!k || !val) {
        return ErrInvalidParam;
    }

    if (!ipfs_namesys_pb_ipns_entry_decode((const unsigned char*)val, strlen(val), &entry)) {
        return ErrInvalidParam;
    }

    if (ipfs_namesys_pb_get_validity_type (entry) == IpnsEntry_EOL) {
        err = ipfs_util_time_parse_RFC3339 (&ts, ipfs_namesys_pb_get_validity (entry));
        if (err) {
            ipfs_namesys_ipnsentry_reset(entry);
            free(entry);
            return err;
        }
        if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
            timespec_get(&now, TIME_UTC);
        }
        if (now.tv_sec > ts.tv_sec || (now.tv_sec == ts.tv_sec && now.tv_nsec > ts.tv_nsec)) {
            ipfs_namesys_ipnsentry_reset(entry);
            free(entry);
            return ErrExpiredRecord;
        }
    } else {
        ipfs_namesys_ipnsentry_reset(entry);
        free(entry);
        return ErrUnrecognizedValidity;
    }

    ipfs_namesys_ipnsentry_reset(entry);
    free(entry);
    return 0;
}

/**
 * Helper to copy values from one to another, allocating memory
 *
 * @param from the value to copy
 * @param from_size the size of from
 * @param to where to allocate memory and copy
 * @param to_size where to put the value of from_size in the new structure
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_namesys_copy_bytes(uint8_t* from, size_t from_size, uint8_t** to, size_t* to_size) {
	*to = (uint8_t*) malloc(from_size);
	if (*to == NULL) {
		return 0;
	}
	memcpy(*to, from, from_size);
	*to_size = from_size;
	return 1;
}

/**
 * Store the hash locally, and notify the network
 *
 * @param local_node the context
 * @param path the path (could be "/ipns" or "/ipfs")
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_namesys_publisher_publish(struct IpfsNode* local_node, char* path) {
	int retVal = 0;
	struct ipns_entry* entry = NULL;
	unsigned char* pb_buffer = NULL;
	size_t pb_buffer_size = 0;
	char* sig_data = NULL;
	unsigned char* signature = NULL;
	size_t signature_size = 0;

	if (!local_node || !path) {
		return 0;
	}

	// Build the IPNS entry
	entry = ipfs_namesys_pb_new_ipns_entry();
	if (!entry) {
		goto exit;
	}

	// value
	entry->value = strdup(path);
	if (!entry->value) {
		goto exit;
	}

	// validityType = EOL
	entry->validityType = malloc(sizeof(int32_t));
	if (!entry->validityType) {
		goto exit;
	}
	*entry->validityType = IpnsEntry_EOL;

	// validity = current time + 24 hours, RFC3339 formatted
	struct timespec now;
	if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
		timespec_get(&now, TIME_UTC);
	}
	now.tv_sec += 24 * 60 * 60; // 24 hours
	entry->validity = ipfs_util_time_format_RFC3339(&now);
	if (!entry->validity) {
		goto exit;
	}

	// sequence = 1 (for now; should be persisted and incremented)
	entry->sequence = malloc(sizeof(uint64_t));
	if (!entry->sequence) {
		goto exit;
	}
	*entry->sequence = 1;

	// Sign the entry
	sig_data = ipns_entry_data_for_sig(entry);
	if (!sig_data) {
		goto exit;
	}
	if (!libp2p_crypto_rsa_sign(&local_node->identity->private_key, sig_data, strlen(sig_data), &signature, &signature_size)) {
		goto exit;
	}
	entry->signature = (char*)signature;
	entry->signature_size = signature_size;
	signature = NULL; // ownership transferred to entry

	// Encode to protobuf
	pb_buffer_size = 4096;
	pb_buffer = malloc(pb_buffer_size);
	if (!pb_buffer) {
		goto exit;
	}
	if (!ipfs_namesys_pb_ipns_entry_encode(entry, pb_buffer, pb_buffer_size, &pb_buffer_size)) {
		goto exit;
	}

	// store locally
	struct DatastoreRecord* record = libp2p_datastore_record_new();
	if (record == NULL)
		goto exit;

	// convert this peer id into a cid
	struct Cid* local_peer = NULL;

	if (!ipfs_cid_decode_hash_from_base58((unsigned char*)local_node->identity->peer->id, local_node->identity->peer->id_size, &local_peer)) {
		libp2p_datastore_record_free(record);
		goto exit;
	}

	// key, which is the peer id
	if (!ipfs_namesys_copy_bytes(local_peer->hash, local_peer->hash_length, &record->key, &record->key_size)) {
		ipfs_cid_free(local_peer);
		libp2p_datastore_record_free(record);
		goto exit;
	}
	// value, which is the protobuf-encoded IPNS entry
	if (!ipfs_namesys_copy_bytes(pb_buffer, pb_buffer_size, &record->value, &record->value_size)) {
		ipfs_cid_free(local_peer);
		libp2p_datastore_record_free(record);
		goto exit;
	}

	if (!local_node->repo->config->datastore->datastore_put(record, local_node->repo->config->datastore)) {
		ipfs_cid_free(local_peer);
		libp2p_datastore_record_free(record);
		goto exit;
	}
	libp2p_datastore_record_free(record);

	// for now, even if what is below fails because of not being connected, return TRUE
	retVal = 1;

	// propagate to network
	// build the KademliaMessage
	struct KademliaMessage* msg = libp2p_message_new();
	if (msg == NULL) {
		libp2p_message_free(msg);
		goto exit;
	}
	msg->message_type = MESSAGE_TYPE_PUT_VALUE;
	msg->provider_peer_head = libp2p_utils_linked_list_new();
	msg->provider_peer_head->item = libp2p_peer_copy(local_node->identity->peer);
	// msg->Libp2pRecord
	msg->record = libp2p_record_new();
	if (msg->record == NULL) {
		libp2p_message_free(msg);
		ipfs_cid_free(local_peer);
		goto exit;
	}
	// KademliaMessage->Libp2pRecord->author
	if (!ipfs_namesys_copy_bytes(local_peer->hash, local_peer->hash_length, (unsigned char**)&msg->record->author, &msg->record->author_size)) {
		libp2p_message_free(msg);
		ipfs_cid_free(local_peer);
		goto exit;
	}
	// KademliaMessage->Libp2pRecord->key
	if (!ipfs_namesys_copy_bytes(local_peer->hash, local_peer->hash_length, (unsigned char**)&msg->record->key, &msg->record->key_size)) {
		libp2p_message_free(msg);
		ipfs_cid_free(local_peer);
		goto exit;
	}
	// KademliaMessage->Libp2pRecord->value (protobuf-encoded IPNS entry)
	if (!ipfs_namesys_copy_bytes(pb_buffer, pb_buffer_size, &msg->record->value, &msg->record->value_size)) {
		libp2p_message_free(msg);
		ipfs_cid_free(local_peer);
		goto exit;
	}

	libp2p_routing_dht_send_message_nearest_x(local_node->dialer, local_node->peerstore, local_node->repo->config->datastore, msg, 10);

	libp2p_message_free(msg);
	ipfs_cid_free(local_peer);

exit:
	if (entry) {
		ipfs_namesys_ipnsentry_reset(entry);
		free(entry);
	}
	if (pb_buffer) {
		free(pb_buffer);
	}
	if (sig_data) {
		free(sig_data);
	}
	if (signature) {
		free(signature);
	}
	return retVal;
}
