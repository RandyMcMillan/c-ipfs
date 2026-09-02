#include <pthread.h>

#include "ipfs/routing/routing.h"
#include "ipfs/core/null.h"
#include "libp2p/routing/kademlia.h"
#include "libp2p/routing/dht_protocol.h"
#include "libp2p/peer/providerstore.h"
#include "libp2p/utils/vector.h"
#include "ipfs/thirdparty/ipfsaddr/ipfs_addr.h"

// Forward declarations from online routing
int ipfs_routing_online_get_value(struct IpfsRouting*, const unsigned char*, size_t, void**, size_t*);
int ipfs_routing_online_find_providers(struct IpfsRouting*, const unsigned char*, size_t, struct Libp2pVector**);
int ipfs_routing_online_find_peer(struct IpfsRouting*, const unsigned char*, size_t, struct Libp2pPeer**);
int ipfs_routing_online_ping(struct IpfsRouting*, struct Libp2pPeer*);
int ipfs_routing_online_bootstrap(struct IpfsRouting*);
int ipfs_routing_online_provide(struct IpfsRouting*, const unsigned char*, size_t);

/**
 * Put a value in the datastore
 */
int ipfs_routing_kademlia_put_value(struct IpfsRouting* routing, const unsigned char* key, size_t key_size, const void* value, size_t value_size) {
	return ipfs_routing_generic_put_value(routing, key, key_size, value, value_size);
}

/**
 * Get a value from the datastore
 */
int ipfs_routing_kademlia_get_value(struct IpfsRouting* routing, const unsigned char* key, size_t key_size, void** value, size_t* value_size) {
	return ipfs_routing_online_get_value(routing, key, key_size, value, value_size);
}

/**
 * Find a provider that can provide a particular key
 */
int ipfs_routing_kademlia_find_providers(struct IpfsRouting* routing, const unsigned char* key, size_t key_size, struct Libp2pVector** results) {
	return ipfs_routing_online_find_providers(routing, key, key_size, results);
}

/**
 * Find a peer
 */
int ipfs_routing_kademlia_find_peer(struct IpfsRouting* routing, const unsigned char* param1, size_t param2, struct Libp2pPeer **result) {
	return ipfs_routing_online_find_peer(routing, param1, param2, result);
}

/**
 * Calling this method notifies the network that this peer can provide this key
 */
int ipfs_routing_kademlia_provide(struct IpfsRouting* routing, const unsigned char* key, size_t key_size) {
	// Store locally
	libp2p_providerstore_add(routing->local_node->providerstore, (unsigned char*)key, key_size,
		(unsigned char*)routing->local_node->identity->peer->id, routing->local_node->identity->peer->id_size);

	// Announce to nearest peers via DHT protocol
	struct KademliaMessage* msg = libp2p_message_new();
	msg->message_type = MESSAGE_TYPE_ADD_PROVIDER;
	msg->key_size = key_size;
	msg->key = malloc(key_size);
	if (msg->key != NULL) {
		memcpy(msg->key, key, key_size);
		struct Libp2pPeer* local_peer = libp2p_peer_copy(routing->local_node->identity->peer);
		if (local_peer != NULL) {
			local_peer->connection_type = CONNECTION_TYPE_CONNECTED;
			msg->provider_peer_head = libp2p_utils_linked_list_new();
			if (msg->provider_peer_head != NULL)
				msg->provider_peer_head->item = local_peer;
		}
		libp2p_routing_dht_send_message_nearest_x(routing->local_node->dialer, routing->local_node->peerstore,
			routing->local_node->repo->config->datastore, msg, 3);
		libp2p_message_free(msg);
	} else {
		libp2p_message_free(msg);
	}
	return 1;
}

/**
 * Ping this instance
 */
int ipfs_routing_kademlia_ping(struct IpfsRouting* routing, struct Libp2pPeer* peer) {
	return ipfs_routing_online_ping(routing, peer);
}

int ipfs_routing_kademlia_bootstrap(struct IpfsRouting* routing) {
	// First do the online bootstrap (connect to bootstrap peers)
	ipfs_routing_online_bootstrap(routing);

	struct IpfsNode *local_node = routing->local_node;
	// read the config file and get the bootstrap peers
	for(int i = 0; i < local_node->repo->config->bootstrap_peers->total; i++) {
		struct IPFSAddr* ipfs_addr = (struct IPFSAddr*) libp2p_utils_vector_get(local_node->repo->config->bootstrap_peers, i);
		struct MultiAddress* ma = multiaddress_new_from_string(ipfs_addr->entire_string);
		char* ptr;
		if ( (ptr = strstr(ipfs_addr->entire_string, "/ipfs/")) != NULL) {
			ptr += 6;
			if (ptr[0] == 'Q' && ptr[1] == 'm') {
				struct Libp2pPeer* peer = libp2p_peer_new_from_multiaddress(ma);
				if (peer) {
					peer->id = ptr;
					peer->id_size = strlen(ptr);
					libp2p_peerstore_add_peer(local_node->peerstore, peer);
				}
			}
		}
	}
	return 1;
}

int ipfs_routing_kademlia_free(struct IpfsRouting* incoming) {
	stop_kademlia();
	free(incoming);
	return 1;
}

struct IpfsRouting* ipfs_routing_new_kademlia(struct IpfsNode* local_node, struct RsaPrivateKey* private_key) {
	char kademlia_id[21];
	// generate kademlia compatible id by getting first 20 chars of peer id
	if (local_node->identity->peer->id == NULL || local_node->identity->peer->id_size < 20) {
		return NULL;
	}
	strncpy(kademlia_id, local_node->identity->peer->id, 20);
	kademlia_id[20] = 0;
	struct IpfsRouting* routing = (struct IpfsRouting*)malloc(sizeof(struct IpfsRouting));
	if (routing != NULL) {
		routing->local_node = local_node;
		routing->sk = private_key;
		routing->PutValue = ipfs_routing_kademlia_put_value;
		routing->GetValue = ipfs_routing_kademlia_get_value;
		routing->FindProviders = ipfs_routing_kademlia_find_providers;
		routing->FindPeer = ipfs_routing_kademlia_find_peer;
		routing->Provide = ipfs_routing_kademlia_provide;
		routing->Ping = ipfs_routing_kademlia_ping;
		routing->Bootstrap = ipfs_routing_kademlia_bootstrap;
		routing->Listen = ipfs_null_listen;
		routing->Shutdown = ipfs_null_shutdown;
	}
	// connect to nodes and listen for connections
	struct MultiAddress* address = multiaddress_new_from_string(local_node->repo->config->addresses->api);
	if (address != NULL && multiaddress_is_ip(address)) {
		start_kademlia_multiaddress(address, kademlia_id, 10, local_node->repo->config->bootstrap_peers);
	}
	local_node->routing = routing;
	return routing;
}
