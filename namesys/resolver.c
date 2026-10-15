#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "libp2p/utils/logger.h"
#include "libp2p/crypto/rsa.h"
#include "ipfs/util/time.h"
#include "ipfs/namesys/resolver.h"
#include "ipfs/namesys/pb.h"
#include "ipfs/namesys/verify.h"
#include "ipfs/crypto/verify.h"
#include "libp2p/crypto/sha256.h"
#include "libp2p/crypto/key.h"

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

/**
 * Map our IPNS key-type enum to the libp2p KeyType enum.
 */
static enum KeyType ipns_key_type_to_libp2p(enum IpfsKeyType t) {
	switch (t) {
		case IPFS_KEY_TYPE_RSA:
			return KEYTYPE_RSA;
		case IPFS_KEY_TYPE_ED25519:
			return KEYTYPE_ED25519;
		default:
			return KEYTYPE_INVALID;
	}
}

/**
 * Check that an embedded public key hashes to the peer ID in the IPNS name.
 */
static int ipns_embedded_pubkey_matches_name(const struct IpnsVerifyEntry *entry, struct Cid *cid) {
	if (entry->pubkey_bytes_len == 0)
		return 0;
	enum KeyType kt = ipns_key_type_to_libp2p(entry->key_type);
	if (kt == KEYTYPE_INVALID)
		return 0;
	struct PublicKey pk;
	memset(&pk, 0, sizeof(pk));
	pk.type = kt;
	pk.data = (unsigned char *)entry->pubkey_bytes;
	pk.data_size = entry->pubkey_bytes_len;
	unsigned char envelope[8192];
	size_t envelope_size = 0;
	if (!libp2p_crypto_public_key_protobuf_encode(&pk, envelope, sizeof(envelope), &envelope_size))
		return 0;
	unsigned char hash[32];
	if (!libp2p_crypto_hashing_sha256(envelope, envelope_size, hash))
		return 0;
	return cid->hash_length == 32 && memcmp(cid->hash, hash, 32) == 0;
}

/**
 * Verify a V1 IPNS signature (signed over value+validity+validityType).
 */
static int ipns_verify_v1_signature(const struct IpnsVerifyEntry *entry, struct ipns_entry *pb_entry) {
	char *sig_data = ipns_entry_data_for_sig(pb_entry);
	if (!sig_data)
		return 0;
	size_t sig_data_len = strlen(sig_data);
	int ok = 0;
	switch (entry->key_type) {
		case IPFS_KEY_TYPE_RSA: {
			struct RsaPublicKey rsa_pk;
			rsa_pk.der = (unsigned char *)entry->pubkey_bytes;
			rsa_pk.der_length = entry->pubkey_bytes_len;
			ok = libp2p_crypto_rsa_verify(&rsa_pk, (unsigned char *)sig_data, sig_data_len, entry->signature);
			break;
		}
		case IPFS_KEY_TYPE_ED25519:
			ok = ipfs_crypto_verify_ed25519(entry->pubkey_bytes, entry->pubkey_bytes_len,
			                                (unsigned char *)sig_data, sig_data_len,
			                                entry->signature, entry->sig_len);
			break;
		case IPFS_KEY_TYPE_SECP256K1:
			ok = ipfs_crypto_verify_secp256k1(entry->pubkey_bytes, entry->pubkey_bytes_len,
			                                  (unsigned char *)sig_data, sig_data_len,
			                                  entry->signature, entry->sig_len);
			break;
		default:
			break;
	}
	free(sig_data);
	return ok;
}

/**
 * Verify the signature and name binding of an IPNS record.
 * @param require_embedded_pubkey when true (network path) the record must carry its own public key.
 */
static int ipns_entry_valid(struct IpfsNode* local_node, const char* path, struct Cid* cid,
                            const uint8_t* record, size_t record_size, struct ipns_entry* entry,
                            int require_embedded_pubkey) {
	struct IpnsVerifyEntry verify_entry;
	memset(&verify_entry, 0, sizeof(verify_entry));
	int has_embedded = ipfs_namesys_verify_entry_parse(record, record_size, &verify_entry) &&
	                   verify_entry.pubkey_bytes_len > 0 && verify_entry.sig_len > 0;

	if (has_embedded) {
		if (!ipns_embedded_pubkey_matches_name(&verify_entry, cid)) {
			libp2p_logger_error("resolver", "IPNS public key does not match record name for %s.\n", path);
			return 0;
		}
		if (entry->data && entry->data_size > 0) {
			if (!ipfs_namesys_verify_entry_signature(&verify_entry)) {
				libp2p_logger_error("resolver", "IPNS V2 signature verification failed for %s.\n", path);
				return 0;
			}
		} else {
			if (!ipns_verify_v1_signature(&verify_entry, entry)) {
				libp2p_logger_error("resolver", "IPNS V1 signature verification failed for %s.\n", path);
				return 0;
			}
		}
		return 1;
	}

	if (require_embedded_pubkey) {
		libp2p_logger_error("resolver", "Remote IPNS record is missing an embedded public key for %s.\n", path);
		return 0;
	}

	/* Local fallback for old records that were signed with the node's own key. */
	if (!local_node->identity || !local_node->identity->private_key.public_key_der)
		return 0;

	struct PublicKey pk;
	memset(&pk, 0, sizeof(pk));
	pk.type = KEYTYPE_RSA;
	pk.data = (unsigned char *)local_node->identity->private_key.public_key_der;
	pk.data_size = local_node->identity->private_key.public_key_length;
	unsigned char envelope[8192];
	size_t envelope_size = 0;
	unsigned char hash[32];
	if (!libp2p_crypto_public_key_protobuf_encode(&pk, envelope, sizeof(envelope), &envelope_size) ||
	    !libp2p_crypto_hashing_sha256(envelope, envelope_size, hash) ||
	    cid->hash_length != 32 || memcmp(cid->hash, hash, 32) != 0) {
		libp2p_logger_error("resolver", "IPNS local record name does not match node identity for %s.\n", path);
		return 0;
	}

	char *sig_data = ipns_entry_data_for_sig(entry);
	if (!sig_data)
		return 0;
	size_t sig_data_len = strlen(sig_data);
	struct RsaPublicKey pub_key;
	pub_key.der = (unsigned char *)local_node->identity->private_key.public_key_der;
	pub_key.der_length = local_node->identity->private_key.public_key_length;
	int ok = libp2p_crypto_rsa_verify(&pub_key, (unsigned char *)sig_data, sig_data_len, (unsigned char *)entry->signature);
	free(sig_data);
	if (!ok) {
		libp2p_logger_error("resolver", "IPNS local signature verification failed for %s.\n", path);
	}
	return ok;
}

/**
 * Check that an IPNS record has not expired.
 */
static int ipns_entry_not_expired(struct ipns_entry* entry) {
	if (entry->validityType && *entry->validityType == IpnsEntry_EOL && entry->validity) {
		struct timespec ts, now;
		if (ipfs_util_time_parse_RFC3339(&ts, entry->validity) != 0)
			return 0;
		if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
			struct timeval tv;
			gettimeofday(&tv, NULL);
			now.tv_sec = tv.tv_sec;
			now.tv_nsec = tv.tv_usec * 1000;
		}
		if (now.tv_sec > ts.tv_sec || (now.tv_sec == ts.tv_sec && now.tv_nsec > ts.tv_nsec))
			return 0;
	}
	return 1;
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
	int retVal = 0;

	if (!local_node || !path || !results) {
		return 0;
	}

	if (!ipfs_cid_decode_hash_from_ipfs_ipns_string(path, &cid)) {
		return 0;
	}

	// look locally
	struct DatastoreRecord* record = NULL;
	if (local_node->repo->config->datastore->datastore_get(cid->hash, cid->hash_length, &record, local_node->repo->config->datastore)) {
		if (!ipfs_namesys_pb_ipns_entry_decode(record->value, record->value_size, &entry)) {
			libp2p_logger_error("resolver", "Failed to decode IPNS entry for %s.\n", path);
			goto local_cleanup;
		}

		if (!entry->signature || entry->signature_size <= 0) {
			libp2p_logger_error("resolver", "IPNS record has no signature for %s.\n", path);
			goto local_cleanup;
		}

		if (!ipns_entry_valid(local_node, path, cid, record->value, record->value_size, entry, 0)) {
			goto local_cleanup;
		}

		if (!ipns_entry_not_expired(entry)) {
			libp2p_logger_error("resolver", "IPNS record expired for %s.\n", path);
			goto local_cleanup;
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
		ipfs_cid_free(cid);
		return retVal;
	}

	// Fallback: ask the network (DHT or routing)
	if (local_node->routing && local_node->routing->GetValue) {
		void* net_record = NULL;
		size_t net_record_size = 0;
		if (local_node->routing->GetValue(local_node->routing, cid->hash, cid->hash_length, &net_record, &net_record_size)) {
			struct ipns_entry* net_entry = NULL;
			if (ipfs_namesys_pb_ipns_entry_decode(net_record, net_record_size, &net_entry)) {
				if (net_entry->signature && net_entry->signature_size > 0 &&
				    ipns_entry_valid(local_node, path, cid, net_record, net_record_size, net_entry, 1) &&
				    ipns_entry_not_expired(net_entry) &&
				    net_entry->value) {
					*results = (char*)malloc(strlen(net_entry->value) + 1);
					if (*results != NULL) {
						strcpy(*results, net_entry->value);
						retVal = 1;
					}
				} else {
					libp2p_logger_error("resolver", "Remote IPNS record failed validation for %s.\n", path);
				}
				ipfs_namesys_ipnsentry_reset(net_entry);
				free(net_entry);
			}
			if (net_record)
				free(net_record);
		}
	}
	ipfs_cid_free(cid);
	return retVal;
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
