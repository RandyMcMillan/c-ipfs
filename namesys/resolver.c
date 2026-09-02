#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "libp2p/utils/logger.h"
#include "libp2p/crypto/rsa.h"
#include "ipfs/util/time.h"
#include "ipfs/namesys/resolver.h"
#include "ipfs/namesys/pb.h"

/**
 * The opposite of publisher.c
 *
 * These are the resources for resolving an IPNS name, turning it into an ipfs path
 */

/**
 * Determine if the incoming path is in ipns format
 * @param path the path to check
 * @returns true(1) if the path begins with "/ipns/"
 */
int is_ipns_string(char* path) {
	if (path == NULL)
		return 0;
	if (strstr(path, "/ipns/") == path)
		return 1;
	return 0;
}

/***
 * Resolve an IPNS name, only to its next step
 * @param local_node the context
 * @param path the ipns_path (i.e. "ipns/Qm12345...")
 * @param results where to store the results (i.e. "ipns/Qm5678...")
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_namesys_resolver_resolve_once(struct IpfsNode* local_node, const char* path, char** results) {
	struct Cid* cid = NULL;
	struct ipns_entry* entry = NULL;
	char* sig_data = NULL;
	struct timespec ts, now;
	int retVal = 0;

	if (!local_node || !path || !results) {
		return 0;
	}

	if (!ipfs_cid_decode_hash_from_ipfs_ipns_string(path, &cid)) {
		return 0;
	}

	// look locally
	struct DatastoreRecord* record;
	if (local_node->repo->config->datastore->datastore_get(cid->hash, cid->hash_length, &record, local_node->repo->config->datastore)) {
		// Decode the IPNS entry from protobuf
		if (!ipfs_namesys_pb_ipns_entry_decode(record->value, record->value_size, &entry)) {
			libp2p_logger_error("resolver", "Failed to decode IPNS entry for %s.\n", path);
			goto local_cleanup;
		}

		// Validate signature using local node's public key
		if (entry->signature && entry->signature_size > 0 && local_node->identity) {
			sig_data = ipns_entry_data_for_sig(entry);
			if (sig_data) {
				struct RsaPublicKey pub_key;
				pub_key.der = (unsigned char*)local_node->identity->private_key.public_key_der;
				pub_key.der_length = local_node->identity->private_key.public_key_length;
				if (!libp2p_crypto_rsa_verify(&pub_key, (unsigned char*)sig_data, strlen(sig_data), (unsigned char*)entry->signature)) {
					libp2p_logger_error("resolver", "IPNS signature verification failed for %s.\n", path);
					goto local_cleanup;
				}
			}
		}

		// Check EOL
		if (entry->validityType && *entry->validityType == IpnsEntry_EOL && entry->validity) {
			if (ipfs_util_time_parse_RFC3339(&ts, entry->validity) != 0) {
				libp2p_logger_error("resolver", "Failed to parse IPNS validity for %s.\n", path);
				goto local_cleanup;
			}
			if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
				struct timeval tv;
				gettimeofday(&tv, NULL);
				now.tv_sec = tv.tv_sec;
				now.tv_nsec = tv.tv_usec * 1000;
			}
			if (now.tv_sec > ts.tv_sec || (now.tv_sec == ts.tv_sec && now.tv_nsec > ts.tv_nsec)) {
				libp2p_logger_error("resolver", "IPNS record expired for %s.\n", path);
				goto local_cleanup;
			}
		}

		if (!entry->value) {
			libp2p_logger_error("resolver", "IPNS entry has no value for %s.\n", path);
			goto local_cleanup;
		}

		*results = (char*) malloc(strlen(entry->value) + 1);
		if (*results == NULL) {
			goto local_cleanup;
		}
		strcpy(*results, entry->value);
		retVal = 1;

	local_cleanup:
		if (record) {
			libp2p_datastore_record_free(record);
		}
		if (entry) {
			ipfs_namesys_ipnsentry_reset(entry);
			free(entry);
		}
		if (sig_data) {
			free(sig_data);
		}
		ipfs_cid_free(cid);
		return retVal;
	}

	//TODO: ask the network
	ipfs_cid_free(cid);
	return 0;
}

/**
 * Resolve an IPNS name.
 * NOTE: if recursive is set to false, the result could be another ipns path
 * @param local_node the context
 * @param path the ipns path (i.e. "/ipns/Qm12345..")
 * @param recursive true to resolve until the result is not ipns, false to simply get the next step in the path
 * @param result the results (i.e. "/ipfs/QmAb12CD...")
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_namesys_resolver_resolve(struct IpfsNode* local_node, const char* path, int recursive, char** results) {
	char* result = NULL;
	char* current_path = (char*) malloc(strlen(path) + 1);
	if (current_path == NULL) {
		return 0;
	}
	strcpy(current_path, path);

	// if we go more than 10 deep, bail
	int counter = 0;

	do {
		if (counter > 10) {
			libp2p_logger_error("resolver", "Resolver looped %d times. Infinite loop? Last result: %s.\n", counter, current_path);
			free(current_path);
			return 0;
		}
		// resolve the current path
		if (!ipfs_namesys_resolver_resolve_once(local_node, current_path, &result)) {
			libp2p_logger_error("resolver", "Resolver returned false searching for %s.\n", current_path);
			free(current_path);
			return 0;
		}
		// result will not be NULL
		free(current_path);
		current_path = (char*) malloc(strlen(result)+1);
		if (current_path != NULL)
			strcpy(current_path, result);
		free(result);
		counter++;
	} while(recursive && is_ipns_string(current_path));

	*results = current_path;
	return 1;
}
