#include <stdlib.h>
#include <pthread.h>
#include <ipfs/routing/routing.h>
#include <ipfs/util/errs.h>
#include "libp2p/crypto/rsa.h"
#include "libp2p/utils/logger.h"
#include "libp2p/record/record.h"
#include "ipfs/core/http_request.h"
#include "ipfs/datastore/ds_helper.h"
#include "ipfs/merkledag/merkledag.h"
#include "ipfs/routing/routing.h"
#include "ipfs/importer/resolver.h"

int ipfs_routing_generic_put_value (ipfs_routing* offlineRouting, const unsigned char *key, size_t key_size, const void *val, size_t vlen)
{
    int err;
    char *record, *nkey;
    size_t len, nkey_len;

    err = libp2p_record_make_put_record (&record, &len, offlineRouting->sk, (const char*)key, val, vlen, 0);

    if (err) {
        return err;
    }

    size_t nkey_max = ((key_size + 4) / 5) * 8 + 1;
    nkey = malloc(nkey_max);
    if (!nkey) {
        free (record);
        return -1;
    }

    if (!ipfs_datastore_helper_ds_key_from_binary(key, key_size, (unsigned char*)nkey, nkey_max, &nkey_len)) {
        free (nkey);
        free (record);
        return -1;
    }

    struct DatastoreRecord* ds_record = libp2p_datastore_record_new();
    if (!ds_record) {
        free (nkey);
        free (record);
        return -1;
    }
    ds_record->key = (uint8_t*)nkey;
    ds_record->key_size = nkey_len;
    ds_record->value = (uint8_t*)record;
    ds_record->value_size = len;
    ds_record->timestamp = 0;

    struct Datastore* datastore = offlineRouting->local_node->repo->config->datastore;
    if (!datastore || !datastore->datastore_put) {
        libp2p_datastore_record_free(ds_record);
        free (record);
        return -1;
    }

    int put_ret = datastore->datastore_put(ds_record, datastore);
    libp2p_datastore_record_free(ds_record);
    return put_ret == 1 ? 0 : -1;
}

/***
 * Get a value from the merkledag
 * @param routing the context
 * @param key the key
 * @param key_size the size of the key
 * @param val where to put the results
 * @param vlen the size of the results
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_routing_generic_get_value (ipfs_routing* routing, const unsigned char *key, size_t key_size, void **val, size_t *vlen)
{
	int retVal = 0;

	if (routing->local_node->mode == MODE_API_AVAILABLE) {
		unsigned char buffer[256];
		if (!ipfs_cid_hash_to_base58(key, key_size, &buffer[0], 256)) {
			libp2p_logger_error("offline", "Unable to convert hash to its Base58 representation.\n");
			return 0;
		}

		libp2p_logger_debug("offline", "Attempting to ask API for dht get %s.\n", buffer);

		char* response;
		struct HttpRequest* req = ipfs_core_http_request_new();
		req->command = "dht";
		req->sub_command = "get";
		req->arguments = libp2p_utils_vector_new(1);
		libp2p_utils_vector_add(req->arguments, buffer);
		size_t response_size = 0;
		if (!ipfs_core_http_request_post(routing->local_node, req, &response, &response_size, "", 0)) {
			libp2p_logger_error("offline", "Unable to call API for dht get.\n");
			ipfs_core_http_request_free(req);
			return 0;
		}
		ipfs_core_http_request_free(req);
		*vlen = response_size;
		if (*vlen > 0) {
			*val = malloc(*vlen + 1);
			uint8_t* ptr = (uint8_t*)*val;
			if (ptr == NULL) {
				return 0;
			}
			memcpy(ptr, response, *vlen);
			ptr[*vlen] = 0;
			retVal = 1;
		}
	} else {
	    struct HashtableNode* node = NULL;
	    *val = NULL;

	    if (!ipfs_merkledag_get(key, key_size, &node, routing->local_node->repo)) {
			goto exit;
		}

	    // protobuf the node
	    int protobuf_size = ipfs_hashtable_node_protobuf_encode_size(node);
	    *val = malloc(protobuf_size);
	    if (*val == NULL)
	    	goto exit;

	    if (ipfs_hashtable_node_protobuf_encode(node, *val, protobuf_size, vlen) == 0) {
	    		goto exit;
	    }

	    retVal = 1;
	    exit:
		if (node != NULL)
			ipfs_hashtable_node_free(node);
		if (retVal != 1 && *val != NULL) {
			free(*val);
			*val = NULL;
		}
	}
    return retVal;
}

// forward declaration
int ipfs_routing_online_find_remote_providers(struct IpfsRouting* routing, const unsigned char* key, size_t key_size, struct Libp2pVector** peers);

int ipfs_routing_offline_find_providers (ipfs_routing* routing, const unsigned char *key, size_t key_size, struct Libp2pVector** peers)
{
	//if (routing->local_node->mode == MODE_API_AVAILABLE) {
		//TODO: we need to ask the api to do this for us
	//} else
	//{
		unsigned char* peer_id;
		int peer_id_size;
		struct Libp2pPeer *peer;

		// see if we can find the key, and retrieve the peer who has it
		if (!libp2p_providerstore_get(routing->local_node->providerstore, key, key_size, &peer_id, &peer_id_size)) {
			libp2p_logger_debug("offline", "%s: Unable to find provider locally... Asking network\n", libp2p_peer_id_to_string(routing->local_node->identity->peer));
			// we need to look remotely
			return ipfs_routing_online_find_remote_providers(routing, key, key_size, peers);
		}

		libp2p_logger_debug("offline", "%s: Found provider locally. Searching for peer.\n", libp2p_peer_id_to_string(routing->local_node->identity->peer));
		// now translate the peer id into a peer to get the multiaddresses
		peer = libp2p_peerstore_get_peer(routing->local_node->peerstore, peer_id, peer_id_size);
		free(peer_id);
		if (peer == NULL) {
			libp2p_logger_error("offline", "find_providers: We said we had the peer, but then we couldn't find it.\n");
			return 0;
		}

		libp2p_logger_debug("offline", "%s: Found peer %s.\n", libp2p_peer_id_to_string(routing->local_node->identity->peer), libp2p_peer_id_to_string(peer));

		*peers = libp2p_utils_vector_new(1);
		libp2p_utils_vector_add(*peers, peer);
	//}
	return 1;
}

int ipfs_routing_offline_find_peer (ipfs_routing* offlineRouting, const unsigned char *peer_id, size_t pid_size, struct Libp2pPeer **result)
{
    return 0;
}

/**
 * Attempt to publish that this node can provide a value
 * @param offlineRouting the context
 * @param incoming_hash the hash (in binary form)
 * @param incoming_hash_size the length of the hash array
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_routing_offline_provide (ipfs_routing* offlineRouting, const unsigned char *incoming_hash, size_t incoming_hash_size)
{
	if (offlineRouting->local_node->mode == MODE_API_AVAILABLE) {
		//TODO: publish this through the api
		unsigned char buffer[256];
		if (!ipfs_cid_hash_to_base58(incoming_hash, incoming_hash_size, &buffer[0], 256)) {
			libp2p_logger_error("offline", "Unable to convert hash to its Base58 representation.\n");
			return 0;
		}

		char* response;
		struct HttpRequest* request = ipfs_core_http_request_new();
		request->command = "dht";
		request->sub_command = "provide";
		libp2p_utils_vector_add(request->arguments, buffer);
		size_t response_size = 0;
		if (!ipfs_core_http_request_post(offlineRouting->local_node, request, &response, &response_size, "", 0)) {
			libp2p_logger_error("offline", "Unable to call API for dht publish.\n");
			ipfs_core_http_request_free(request);
			return 0;
		}
		ipfs_core_http_request_free(request);
		fwrite(response, 1, response_size, stdout);
		return 1;
	} else {
		libp2p_logger_debug("offline", "Unable to announce that I can provide the hash, as API not available.\n");
	}

    return 0;
}

int ipfs_routing_offline_ping (ipfs_routing* offlineRouting, struct Libp2pPeer* peer)
{
    return ErrOffline;
}

/**
 * Attempt to bootstrap, even if there is no API
 * @param offlineRouting the interface
 * @returns 0
 */
int ipfs_routing_offline_bootstrap (ipfs_routing* routing)
{
	if (routing->local_node->mode == MODE_API_AVAILABLE) {
		// do nothing
	} else {
		char* peer_id = NULL;
		int peer_id_size = 0;
		struct MultiAddress* address = NULL;
		struct Libp2pPeer *peer = NULL;

		// for each address in our bootstrap list, add info into the peerstore
		struct Libp2pVector* bootstrap_peers = routing->local_node->repo->config->bootstrap_peers;
		for(int i = 0; i < bootstrap_peers->total; i++) {
			address = (struct MultiAddress*)libp2p_utils_vector_get(bootstrap_peers, i);
			// attempt to get the peer ID
			peer_id = multiaddress_get_peer_id(address);
			if (peer_id != NULL) {
				peer_id_size = strlen(peer_id);
				peer = libp2p_peer_new();
				peer->id_size = peer_id_size;
				peer->id = malloc(peer->id_size + 1);
				if (peer->id == NULL) { // out of memory?
					libp2p_peer_free(peer);
					free(peer_id);
					return -1;
				}
				memcpy(peer->id, peer_id, peer->id_size);
				peer->id[peer->id_size] = 0;
				peer->addr_head = libp2p_utils_linked_list_new();
				if (peer->addr_head == NULL) { // out of memory?
					libp2p_peer_free(peer);
					free(peer_id);
					return -1;
				}
				peer->addr_head->item = multiaddress_copy(address);
				libp2p_peerstore_add_peer(routing->local_node->peerstore, peer);
				libp2p_peer_free(peer);
				// now find it and attempt to connect
				peer = libp2p_peerstore_get_peer(routing->local_node->peerstore, (const unsigned char*)peer_id, peer_id_size);
				free(peer_id);
				if (peer == NULL) {
					return -1; // this should never happen
				}
				if (peer->sessionContext == NULL) { // should always be true unless we added it twice (TODO: we should prevent that earlier)
					if (!libp2p_peer_connect(routing->local_node->dialer, peer, routing->local_node->peerstore, routing->local_node->repo->config->datastore, 2)) {
						libp2p_logger_debug("online", "Attempted to bootstrap and connect to %s but failed. Continuing.\n", libp2p_peer_id_to_string(peer));
					}
				}
			}
		}
	}

	return 0;
}

struct IpfsRouting* ipfs_routing_new_offline (struct IpfsNode* local_node, struct RsaPrivateKey *private_key)
{
    struct IpfsRouting *offlineRouting = malloc (sizeof(struct IpfsRouting));

    if (offlineRouting) {
        offlineRouting->local_node     = local_node;
        offlineRouting->sk            = private_key;

        offlineRouting->PutValue      = ipfs_routing_generic_put_value;
        offlineRouting->GetValue      = ipfs_routing_generic_get_value;
        offlineRouting->FindProviders = ipfs_routing_offline_find_providers;
        offlineRouting->FindPeer      = ipfs_routing_offline_find_peer;
        offlineRouting->Provide       = ipfs_routing_offline_provide;
        offlineRouting->Ping          = ipfs_routing_offline_ping;
        offlineRouting->Bootstrap     = ipfs_routing_offline_bootstrap;
    }

    return offlineRouting;
}

int ipfs_routing_offline_free(ipfs_routing* incoming) {
	free(incoming);
	return 1;
}
