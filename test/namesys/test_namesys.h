#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>

#include <openssl/evp.h>

#include "../test_helper.h"
#include "ipfs/cid/cid.h"
#include "ipfs/core/ipfs_node.h"
#include "ipfs/crypto/verify.h"
#include "ipfs/namesys/publisher.h"
#include "ipfs/namesys/resolver.h"
#include "ipfs/namesys/pb.h"
#include "ipfs/namesys/verify.h"
#include "ipfs/util/time.h"
#include "libp2p/crypto/key.h"
#include "libp2p/crypto/rsa.h"
#include "libp2p/crypto/sha256.h"
#include "libp2p/db/datastore.h"

int test_namesys_publisher_publish() {
	int retVal = 0;
	struct IpfsNode* local_node = NULL;
	char* hash_text = "/ipns/QmZtAEqmnXMZkwVPKdyMGxUoo35cQMzNhmq6CN3DvgRwAD";
	char* repo_path = "./tmp/ipfs_1";
	char* peer_id = NULL;

	if (!drop_and_build_repository(repo_path, 4001, NULL, &peer_id)) {
		libp2p_logger_error("test_publisher", "Unable to build repository at %s.\n", repo_path);
		goto exit;
	}

	// get a local node
	if (!ipfs_node_offline_new(repo_path, &local_node)) {
		libp2p_logger_error("test_publisher", "publish: Unable to open ipfs repository.\n");
		goto exit;
	}

	// attempt to publish
	if (!ipfs_namesys_publisher_publish(local_node, hash_text)) {
		libp2p_logger_error("test_publisher", "publish: Unable to publish %s.\n", hash_text);
		goto exit;
	}

	retVal = 1;
	exit:
	ipfs_node_free(local_node);
	return retVal;
}

int test_namesys_resolver_resolve() {
	int retVal = 0;
	struct IpfsNode* local_node = NULL;
	char* hash_text = "QmZtAEqmnXMZkwVPKdyMGxUoo35cQMzNhmq6CN3DvgRwAD"; // the hash of a hello, world file
	char ipns_path[512] = ""; // the Peer ID of config.test1
	char* repo_path = "./tmp/ipfs_1";
	char* peer_id = NULL;
	char* result = NULL;

	drop_and_build_repository(repo_path, 4001, NULL, &peer_id);

	// get a local node
	if (!ipfs_node_offline_new(repo_path, &local_node)) {
		libp2p_logger_error("test_publisher", "publish: Unable to open ipfs repository.\n");
		goto exit;
	}

	// set ipns_path
	sprintf(ipns_path, "/ipfs/%s", peer_id);

	// attempt to publish
	if (!ipfs_namesys_publisher_publish(local_node, hash_text)) {
		libp2p_logger_error("test_publisher", "publish: Unable to publish %s.\n", hash_text);
		goto exit;
	}

	// attempt to retrieve
	if (!ipfs_namesys_resolver_resolve(local_node, ipns_path, 1, &result)) {
		libp2p_logger_error("test_namesys", "Could not resolve %s.\n", hash_text);
		goto exit;
	}

	if (strcmp(result, hash_text) != 0) {
		libp2p_logger_error("test_namesys", "Retrieve wrong result. %s should be %s.\n", result, hash_text);
		goto exit;
	}

	libp2p_logger_error("test_namesys", "Asked for %s and received %s. Success!\n", ipns_path, result);

	retVal = 1;
	exit:
	ipfs_node_free(local_node);
	if (result != NULL)
		free(result);
	return retVal;
}

int test_ipns_signed_record_roundtrip() {
	int retVal = 0;
	struct IpfsNode* local_node = NULL;
	char* hash_text = "/ipfs/QmZtAEqmnXMZkwVPKdyMGxUoo35cQMzNhmq6CN3DvgRwAD";
	char ipns_path[512] = "";
	char* repo_path = "./tmp/ipfs_ipns_roundtrip";
	char* peer_id = NULL;
	char* result = NULL;

	if (!drop_and_build_repository(repo_path, 4002, NULL, &peer_id)) {
		libp2p_logger_error("test_ipns_roundtrip", "Unable to build repository at %s.\n", repo_path);
		goto exit;
	}

	if (!ipfs_node_offline_new(repo_path, &local_node)) {
		libp2p_logger_error("test_ipns_roundtrip", "Unable to open ipfs repository.\n");
		goto exit;
	}

	sprintf(ipns_path, "/ipns/%s", peer_id);

	// Publish a signed IPNS record
	if (!ipfs_namesys_publisher_publish(local_node, hash_text)) {
		libp2p_logger_error("test_ipns_roundtrip", "Unable to publish %s.\n", hash_text);
		goto exit;
	}

	// Resolve the IPNS name
	if (!ipfs_namesys_resolver_resolve(local_node, ipns_path, 1, &result)) {
		libp2p_logger_error("test_ipns_roundtrip", "Could not resolve %s.\n", ipns_path);
		goto exit;
	}

	if (strcmp(result, hash_text) != 0) {
		libp2p_logger_error("test_ipns_roundtrip", "Wrong result: %s should be %s.\n", result, hash_text);
		goto exit;
	}

	libp2p_logger_error("test_ipns_roundtrip", "Roundtrip OK: %s -> %s\n", ipns_path, result);
	retVal = 1;

exit:
	ipfs_node_free(local_node);
	if (result != NULL)
		free(result);
	return retVal;
}

/**
 * Helpers for resolver-level IPNS V1/V2 conformance tests.
 */

static int encode_pubkey_for_ipns(struct PublicKey* pk, uint8_t* out, size_t out_size, size_t* out_len) {
	size_t sz = libp2p_crypto_public_key_protobuf_encode_size(pk);
	if (sz > out_size)
		return 0;
	return libp2p_crypto_public_key_protobuf_encode(pk, out, out_size, out_len);
}

static void free_ipns_entry(struct ipns_entry* entry) {
	if (entry) {
		ipfs_namesys_ipnsentry_reset(entry);
		free(entry);
	}
}

static int set_ipns_entry_common(struct ipns_entry* entry, const char* value, int expired) {
	entry->value = strdup(value);
	if (!entry->value)
		return 0;
	entry->validityType = malloc(sizeof(int32_t));
	if (!entry->validityType) {
		free(entry->value);
		entry->value = NULL;
		return 0;
	}
	*entry->validityType = IpnsEntry_EOL;
	struct timespec now;
	if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
		struct timeval tv;
		gettimeofday(&tv, NULL);
		now.tv_sec = tv.tv_sec;
		now.tv_nsec = tv.tv_usec * 1000;
	}
	if (expired)
		now.tv_sec -= 60;
	else
		now.tv_sec += 24 * 60 * 60;
	entry->validity = ipfs_util_time_format_RFC3339(&now);
	if (!entry->validity) {
		free(entry->validityType);
		entry->validityType = NULL;
		free(entry->value);
		entry->value = NULL;
		return 0;
	}
	entry->sequence = malloc(sizeof(uint64_t));
	if (!entry->sequence) {
		free(entry->validity);
		entry->validity = NULL;
		free(entry->validityType);
		entry->validityType = NULL;
		free(entry->value);
		entry->value = NULL;
		return 0;
	}
	*entry->sequence = 1;
	return 1;
}

static int store_ipns_entry(struct IpfsNode* node, const char* peer_id, struct ipns_entry* entry) {
	unsigned char pb_buffer[4096];
	size_t pb_size = 0;
	if (!ipfs_namesys_pb_ipns_entry_encode(entry, pb_buffer, sizeof(pb_buffer), &pb_size))
		return 0;
	struct Cid* cid = NULL;
	if (!ipfs_cid_decode_hash_from_base58((const unsigned char*)peer_id, strlen(peer_id), &cid))
		return 0;
	struct DatastoreRecord* record = libp2p_datastore_record_new();
	if (!record) {
		ipfs_cid_free(cid);
		return 0;
	}
	record->key = (uint8_t*)malloc(cid->hash_length);
	if (!record->key) {
		libp2p_datastore_record_free(record);
		ipfs_cid_free(cid);
		return 0;
	}
	memcpy(record->key, cid->hash, cid->hash_length);
	record->key_size = cid->hash_length;
	record->value = (uint8_t*)malloc(pb_size);
	if (!record->value) {
		libp2p_datastore_record_free(record);
		ipfs_cid_free(cid);
		return 0;
	}
	memcpy(record->value, pb_buffer, pb_size);
	record->value_size = pb_size;
	int ok = node->repo->config->datastore->datastore_put(record, node->repo->config->datastore);
	libp2p_datastore_record_free(record);
	ipfs_cid_free(cid);
	return ok;
}

static int resolve_ipns_path(struct IpfsNode* node, const char* peer_id, char** result) {
	char ipns_path[512];
	snprintf(ipns_path, sizeof(ipns_path), "/ipns/%s", peer_id);
	return ipfs_namesys_resolver_resolve(node, ipns_path, 0, result);
}

static int build_rsa_ipns_record(const char* value, int v2, int expired, char** out_peer_id, struct ipns_entry* entry) {
	int retVal = 0;
	struct RsaPrivateKey private_key;
	memset(&private_key, 0, sizeof(private_key));
	char* peer_id = NULL;
	unsigned char* sig = NULL;
	char* sig_data = NULL;
	char* msg = NULL;

	if (!libp2p_crypto_rsa_generate_keypair(&private_key, 2048))
		goto exit;

	struct PublicKey public_key;
	memset(&public_key, 0, sizeof(public_key));
	public_key.type = KEYTYPE_RSA;
	public_key.data = (unsigned char*)private_key.public_key_der;
	public_key.data_size = private_key.public_key_length;
	if (!libp2p_crypto_public_key_to_peer_id(&public_key, &peer_id))
		goto exit;

	if (!set_ipns_entry_common(entry, value, expired))
		goto exit;

	sig_data = ipns_entry_data_for_sig(entry);
	if (!sig_data)
		goto exit;

	size_t sig_data_len = strlen(sig_data);
	size_t msg_len = v2 ? IPNS_SIG_PREFIX_LEN + sig_data_len : sig_data_len;
	msg = (char*)malloc(msg_len);
	if (!msg)
		goto exit;
	if (v2) {
		memcpy(msg, IPNS_SIG_PREFIX, IPNS_SIG_PREFIX_LEN);
		memcpy(msg + IPNS_SIG_PREFIX_LEN, sig_data, sig_data_len);
	} else {
		memcpy(msg, sig_data, sig_data_len);
	}

	size_t sig_len = 0;
	if (!libp2p_crypto_rsa_sign(&private_key, (const char*)msg, msg_len, &sig, &sig_len))
		goto exit;
	entry->signature = (char*)sig;
	entry->signature_size = sig_len;

	if (v2) {
		entry->data = sig_data;
		entry->data_size = sig_data_len;
		sig_data = NULL;
	}

	uint8_t pubkey_pb[1024];
	size_t pubkey_pb_len = 0;
	if (!encode_pubkey_for_ipns(&public_key, pubkey_pb, sizeof(pubkey_pb), &pubkey_pb_len))
		goto exit;
	entry->pubkey = (char*)malloc(pubkey_pb_len);
	if (!entry->pubkey)
		goto exit;
	memcpy(entry->pubkey, pubkey_pb, pubkey_pb_len);
	entry->pubkey_size = pubkey_pb_len;

	*out_peer_id = peer_id;
	peer_id = NULL;
	retVal = 1;

exit:
	if (private_key.der)
		free(private_key.der);
	if (private_key.public_key_der)
		free(private_key.public_key_der);
	if (sig_data)
		free(sig_data);
	if (msg)
		free(msg);
	if (peer_id)
		free(peer_id);
	return retVal;
}

static int build_ed25519_ipns_record(const char* value, int v2, char** out_peer_id, struct ipns_entry* entry) {
	int retVal = 0;
	EVP_PKEY* pkey = NULL;
	EVP_PKEY_CTX* gen_ctx = NULL;
	EVP_MD_CTX* ctx = NULL;
	char* peer_id = NULL;
	char* sig_data = NULL;
	uint8_t* msg = NULL;
	uint8_t* sig = NULL;

	gen_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
	if (!gen_ctx || EVP_PKEY_keygen_init(gen_ctx) <= 0 || EVP_PKEY_keygen(gen_ctx, &pkey) <= 0 || !pkey)
		goto exit;

	uint8_t pubkey[32];
	size_t pubkey_len = 32;
	if (EVP_PKEY_get_raw_public_key(pkey, pubkey, &pubkey_len) != 1 || pubkey_len != 32)
		goto exit;

	struct PublicKey public_key;
	memset(&public_key, 0, sizeof(public_key));
	public_key.type = KEYTYPE_ED25519;
	public_key.data = pubkey;
	public_key.data_size = pubkey_len;
	if (!libp2p_crypto_public_key_to_peer_id(&public_key, &peer_id))
		goto exit;

	if (!set_ipns_entry_common(entry, value, 0))
		goto exit;

	sig_data = ipns_entry_data_for_sig(entry);
	if (!sig_data)
		goto exit;

	size_t sig_data_len = strlen(sig_data);
	size_t msg_len = v2 ? IPNS_SIG_PREFIX_LEN + sig_data_len : sig_data_len;
	msg = (uint8_t*)malloc(msg_len);
	if (!msg)
		goto exit;
	if (v2) {
		memcpy(msg, IPNS_SIG_PREFIX, IPNS_SIG_PREFIX_LEN);
		memcpy(msg + IPNS_SIG_PREFIX_LEN, sig_data, sig_data_len);
	} else {
		memcpy(msg, sig_data, sig_data_len);
	}

	ctx = EVP_MD_CTX_new();
	if (!ctx)
		goto exit;
	size_t sig_len = 64;
	sig = (uint8_t*)malloc(sig_len);
	if (!sig)
		goto exit;
	if (EVP_DigestSignInit(ctx, NULL, NULL, NULL, pkey) != 1)
		goto exit;
	if (EVP_DigestSign(ctx, sig, &sig_len, msg, msg_len) != 1)
		goto exit;
	entry->signature = (char*)sig;
	entry->signature_size = sig_len;
	sig = NULL;

	if (v2) {
		entry->data = sig_data;
		entry->data_size = sig_data_len;
		sig_data = NULL;
	}

	uint8_t pubkey_pb[64];
	size_t pubkey_pb_len = 0;
	if (!encode_pubkey_for_ipns(&public_key, pubkey_pb, sizeof(pubkey_pb), &pubkey_pb_len))
		goto exit;
	entry->pubkey = (char*)malloc(pubkey_pb_len);
	if (!entry->pubkey)
		goto exit;
	memcpy(entry->pubkey, pubkey_pb, pubkey_pb_len);
	entry->pubkey_size = pubkey_pb_len;

	*out_peer_id = peer_id;
	peer_id = NULL;
	retVal = 1;

exit:
	if (gen_ctx)
		EVP_PKEY_CTX_free(gen_ctx);
	if (pkey)
		EVP_PKEY_free(pkey);
	if (ctx)
		EVP_MD_CTX_free(ctx);
	if (sig_data)
		free(sig_data);
	if (msg)
		free(msg);
	if (sig)
		free(sig);
	if (peer_id)
		free(peer_id);
	return retVal;
}

static struct IpfsNode* make_test_node_for_ipns(const char* repo_path, int port) {
	char* peer_id = NULL;
	if (!drop_and_build_repository(repo_path, port, NULL, &peer_id)) {
		free(peer_id);
		return NULL;
	}
	free(peer_id);
	struct IpfsNode* node = NULL;
	if (!ipfs_node_offline_new(repo_path, &node))
		return NULL;
	return node;
}

int test_namesys_resolver_v1_rsa_roundtrip() {
	int retVal = 0;
	struct IpfsNode* node = NULL;
	struct ipns_entry* entry = NULL;
	char* peer_id = NULL;
	char* result = NULL;

	node = make_test_node_for_ipns("./tmp/ipfs_ipns_v1_rsa", 4101);
	if (!node)
		goto exit;

	entry = ipfs_namesys_pb_new_ipns_entry();
	if (!entry)
		goto exit;

	if (!build_rsa_ipns_record("/ipfs/QmIpnsV1RsaValue", 0, 0, &peer_id, entry))
		goto exit;

	if (!store_ipns_entry(node, peer_id, entry))
		goto exit;

	if (!resolve_ipns_path(node, peer_id, &result))
		goto exit;

	if (strcmp(result, "/ipfs/QmIpnsV1RsaValue") != 0)
		goto exit;

	retVal = 1;

exit:
	ipfs_node_free(node);
	free_ipns_entry(entry);
	if (peer_id)
		free(peer_id);
	if (result)
		free(result);
	return retVal;
}

int test_namesys_resolver_v2_rsa_roundtrip() {
	int retVal = 0;
	struct IpfsNode* node = NULL;
	struct ipns_entry* entry = NULL;
	char* peer_id = NULL;
	char* result = NULL;

	node = make_test_node_for_ipns("./tmp/ipfs_ipns_v2_rsa", 4102);
	if (!node)
		goto exit;

	entry = ipfs_namesys_pb_new_ipns_entry();
	if (!entry)
		goto exit;

	if (!build_rsa_ipns_record("/ipfs/QmIpnsV2RsaValue", 1, 0, &peer_id, entry))
		goto exit;

	if (!store_ipns_entry(node, peer_id, entry))
		goto exit;

	if (!resolve_ipns_path(node, peer_id, &result))
		goto exit;

	if (strcmp(result, "/ipfs/QmIpnsV2RsaValue") != 0)
		goto exit;

	retVal = 1;

exit:
	ipfs_node_free(node);
	free_ipns_entry(entry);
	if (peer_id)
		free(peer_id);
	if (result)
		free(result);
	return retVal;
}

int test_namesys_resolver_v2_ed25519_roundtrip() {
	int retVal = 0;
	struct IpfsNode* node = NULL;
	struct ipns_entry* entry = NULL;
	char* peer_id = NULL;
	char* result = NULL;

	node = make_test_node_for_ipns("./tmp/ipfs_ipns_v2_ed25519", 4103);
	if (!node)
		goto exit;

	entry = ipfs_namesys_pb_new_ipns_entry();
	if (!entry)
		goto exit;

	if (!build_ed25519_ipns_record("/ipfs/QmIpnsV2EdValue", 1, &peer_id, entry))
		goto exit;

	if (!store_ipns_entry(node, peer_id, entry))
		goto exit;

	if (!resolve_ipns_path(node, peer_id, &result))
		goto exit;

	if (strcmp(result, "/ipfs/QmIpnsV2EdValue") != 0)
		goto exit;

	retVal = 1;

exit:
	ipfs_node_free(node);
	free_ipns_entry(entry);
	if (peer_id)
		free(peer_id);
	if (result)
		free(result);
	return retVal;
}

int test_namesys_resolver_expired_record_rejected() {
	int retVal = 0;
	struct IpfsNode* node = NULL;
	struct ipns_entry* entry = NULL;
	char* peer_id = NULL;
	char* result = NULL;

	node = make_test_node_for_ipns("./tmp/ipfs_ipns_expired", 4104);
	if (!node)
		goto exit;

	entry = ipfs_namesys_pb_new_ipns_entry();
	if (!entry)
		goto exit;

	if (!build_rsa_ipns_record("/ipfs/QmIpnsExpiredValue", 1, 1, &peer_id, entry))
		goto exit;

	if (!store_ipns_entry(node, peer_id, entry))
		goto exit;

	if (resolve_ipns_path(node, peer_id, &result)) {
		fprintf(stderr, "expired IPNS record should have been rejected\n");
		goto exit;
	}

	retVal = 1;

exit:
	ipfs_node_free(node);
	free_ipns_entry(entry);
	if (peer_id)
		free(peer_id);
	if (result)
		free(result);
	return retVal;
}

int test_namesys_resolver_bad_signature_rejected() {
	int retVal = 0;
	struct IpfsNode* node = NULL;
	struct ipns_entry* entry = NULL;
	char* peer_id = NULL;
	char* result = NULL;

	node = make_test_node_for_ipns("./tmp/ipfs_ipns_badsig", 4105);
	if (!node)
		goto exit;

	entry = ipfs_namesys_pb_new_ipns_entry();
	if (!entry)
		goto exit;

	if (!build_rsa_ipns_record("/ipfs/QmIpnsBadSigValue", 1, 0, &peer_id, entry))
		goto exit;

	if (entry->signature_size > 0)
		entry->signature[0] ^= 0xFF;

	if (!store_ipns_entry(node, peer_id, entry))
		goto exit;

	if (resolve_ipns_path(node, peer_id, &result)) {
		fprintf(stderr, "IPNS record with bad signature should have been rejected\n");
		goto exit;
	}

	retVal = 1;

exit:
	ipfs_node_free(node);
	free_ipns_entry(entry);
	if (peer_id)
		free(peer_id);
	if (result)
		free(result);
	return retVal;
}
