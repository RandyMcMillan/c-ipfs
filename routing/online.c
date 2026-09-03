#include <stdlib.h>
#include <math.h>
#include <pthread.h>

#include "libp2p/crypto/encoding/base58.h"
#include "ipfs/routing/routing.h"
#include "ipfs/core/null.h"
#include "libp2p/record/message.h"
#include "libp2p/net/stream.h"
#include "libp2p/conn/session.h"
#include "libp2p/routing/dht_protocol.h"
#include "libp2p/routing/dht_utils.h"
#include "libp2p/utils/logger.h"
#include "libp2p/conn/dialer.h"
#include "ipfs/core/null.h"

/**
 * Implements the routing interface for communicating with network clients
 */

/**
 * Helper method to send and receive a kademlia message
 * @param session_context the session context
 * @param message what to send
 * @returns what was received
 */
struct KademliaMessage* ipfs_routing_online_send_receive_message(struct SessionContext* sessionContext, struct KademliaMessage* message) {
	struct KademliaMessage* return_message = NULL;
	//unsigned char* protocol = (unsigned char*)"/ipfs/kad/1.0.0\n";

	// send the message, and expect the same back
	if (!libp2p_routing_dht_send_message(sessionContext, message)) {
		libp2p_logger_error("online", "Attempted to write to Kademlia stream, but could not.\n");
	} else {
		if (!libp2p_routing_dht_receive_message(sessionContext, &return_message)) {
			libp2p_logger_error("online", "Unable to receive kademlia message.\n");
		}
	}
	return return_message;
}

/***
 * Helper: check if a peer id is already in a vector of Libp2pPeer*
 */
static int peer_id_in_vector(struct Libp2pVector* vec, const unsigned char* peer_id, size_t peer_id_size) {
	if (vec == NULL) return 0;
	for (int i = 0; i < vec->total; i++) {
		struct Libp2pPeer* p = (struct Libp2pPeer*)libp2p_utils_vector_get(vec, i);
		if (p != NULL && p->id_size == peer_id_size && memcmp(p->id, peer_id, peer_id_size) == 0)
			return 1;
	}
	return 0;
}

/***
 * Ask the network for anyone that can provide a hash.
 * Uses iterative Kademlia-style lookup: queries connected peers,
 * accumulates providers, and follows closer peers one round.
 * @param routing the context
 * @param key the hash to look for
 * @param key_size the size of the hash
 * @param peers an array of Peer structs that can provide the hash
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_routing_online_find_remote_providers(struct IpfsRouting* routing, const unsigned char* key, size_t key_size, struct Libp2pVector** peers) {
	int found = 0;
	struct Libp2pVector* result_peers = libp2p_utils_vector_new(1);
	struct Libp2pVector* queried = libp2p_utils_vector_new(1);
	struct Libp2pVector* candidates = libp2p_utils_vector_new(1);

	// build the message to be transmitted
	struct KademliaMessage* message = libp2p_message_new();
	message->message_type = MESSAGE_TYPE_GET_PROVIDERS;
	message->key_size = key_size;
	message->key = malloc(message->key_size);
	if (message->key == NULL) {
		libp2p_message_free(message);
		libp2p_utils_vector_free(result_peers);
		libp2p_utils_vector_free(queried);
		libp2p_utils_vector_free(candidates);
		return 0;
	}
	memcpy(message->key, key, message->key_size);

	// Seed candidates with all connected peers
	struct Libp2pLinkedList* current_entry = routing->local_node->peerstore->head_entry;
	while (current_entry != NULL) {
		struct PeerEntry* entry = (struct PeerEntry*)current_entry->item;
		if (entry != NULL && entry->peer != NULL && entry->peer->connection_type == CONNECTION_TYPE_CONNECTED) {
			libp2p_utils_vector_add(candidates, entry->peer);
		}
		current_entry = current_entry->next;
	}

	int rounds = 0;
	while (candidates->total > 0 && rounds < 2) {
		struct Libp2pVector* next_candidates = libp2p_utils_vector_new(1);
		for (int i = 0; i < candidates->total; i++) {
			struct Libp2pPeer* peer = (struct Libp2pPeer*)libp2p_utils_vector_get(candidates, i);
			if (peer == NULL || peer_id_in_vector(queried, (unsigned char*)peer->id, peer->id_size))
				continue;
			libp2p_utils_vector_add(queried, peer);

			libp2p_logger_debug("online", "FindRemoteProviders: Asking peer %s for providers\n", libp2p_peer_id_to_string(peer));
			struct KademliaMessage* return_message = ipfs_routing_online_send_receive_message(peer->sessionContext, message);
			if (return_message == NULL)
				continue;

			// Collect providers
			struct Libp2pLinkedList* prov = return_message->provider_peer_head;
			while (prov != NULL) {
				struct Libp2pPeer* current_peer = (struct Libp2pPeer*)prov->item;
				if (current_peer != NULL && !peer_id_in_vector(result_peers, (unsigned char*)current_peer->id, current_peer->id_size)) {
					struct Libp2pPeer* peerstorePeer = libp2p_peerstore_get_peer(routing->local_node->peerstore, (unsigned char*)current_peer->id, current_peer->id_size);
					if (peerstorePeer == NULL) {
						libp2p_peerstore_add_peer(routing->local_node->peerstore, current_peer);
						peerstorePeer = libp2p_peerstore_get_peer(routing->local_node->peerstore, (unsigned char*)current_peer->id, current_peer->id_size);
					}
					if (peerstorePeer != NULL) {
						libp2p_utils_vector_add(result_peers, peerstorePeer);
						found = 1;
					}
				}
				prov = prov->next;
			}

			// Collect closer peers for next round
			struct Libp2pLinkedList* closer = return_message->closer_peer_head;
			while (closer != NULL) {
				struct Libp2pPeer* closer_peer = (struct Libp2pPeer*)closer->item;
				if (closer_peer != NULL && !peer_id_in_vector(queried, (unsigned char*)closer_peer->id, closer_peer->id_size)) {
					libp2p_peerstore_add_peer(routing->local_node->peerstore, closer_peer);
					struct Libp2pPeer* stored = libp2p_peerstore_get_peer(routing->local_node->peerstore, (unsigned char*)closer_peer->id, closer_peer->id_size);
					if (stored != NULL)
						libp2p_utils_vector_add(next_candidates, stored);
				}
				closer = closer->next;
			}
			libp2p_message_free(return_message);
		}
		libp2p_utils_vector_free(candidates);
		candidates = next_candidates;
		rounds++;
	}

	libp2p_message_free(message);
	libp2p_utils_vector_free(candidates);
	libp2p_utils_vector_free(queried);

	if (found) {
		*peers = result_peers;
	} else {
		libp2p_utils_vector_free(result_peers);
		*peers = NULL;
	}
	return found;
}

/**
 * Looking for a provider of a hash. This first looks locally, then asks the network
 * @param routing the context
 * @param key the hash to look for
 * @param key_size the size of the hash
 * @param peers the results
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_routing_online_find_providers(struct IpfsRouting* routing, const unsigned char* key, size_t key_size, struct Libp2pVector** peers) {
	unsigned char* peer_id;
	int peer_id_size;
	struct Libp2pPeer *peer;

	// see if we can find the key, and retrieve the peer who has it
	if (!libp2p_providerstore_get(routing->local_node->providerstore, key, key_size, &peer_id, &peer_id_size)) {
		libp2p_logger_debug("online", "%s: Unable to find provider locally... Asking network\n", libp2p_peer_id_to_string(routing->local_node->identity->peer));
		// we need to look remotely
		return ipfs_routing_online_find_remote_providers(routing, key, key_size, peers);
	}

	libp2p_logger_debug("online", "%s: Found provider locally. Searching for peer.\n", libp2p_peer_id_to_string(routing->local_node->identity->peer));
	// now translate the peer id into a peer to get the multiaddresses
	peer = libp2p_peerstore_get_peer(routing->local_node->peerstore, peer_id, peer_id_size);
	free(peer_id);
	if (peer == NULL) {
		libp2p_logger_error("online", "find_providers: We said we had the peer, but then we couldn't find it.\n");
		return 0;
	}

	libp2p_logger_debug("online", "%s: Found peer %s.\n", libp2p_peer_id_to_string(routing->local_node->identity->peer), libp2p_peer_id_to_string(peer));

	*peers = libp2p_utils_vector_new(1);
	libp2p_utils_vector_add(*peers, peer);
	return 1;
}

/***
 * helper method. Connect to a peer and ask it for information
 * about another peer
 */
int ipfs_routing_online_ask_peer_for_peer(struct Libp2pPeer* whoToAsk, const unsigned char* peer_id, size_t peer_id_size, struct Libp2pPeer **result) {
	int retVal = 0;
	struct KademliaMessage *message = NULL, *return_message = NULL;

	if (whoToAsk->connection_type == CONNECTION_TYPE_CONNECTED) {
		message = libp2p_message_new();
		if (message == NULL)
			goto exit;
		message->message_type = MESSAGE_TYPE_FIND_NODE;
		message->key_size = peer_id_size;
		message->key = malloc(peer_id_size);
		if (message->key == NULL)
			goto exit;
		memcpy(message->key, peer_id, peer_id_size);

		return_message = ipfs_routing_online_send_receive_message(whoToAsk->sessionContext, message);
		if (return_message == NULL) {
			// some kind of network error
			whoToAsk->connection_type = CONNECTION_TYPE_NOT_CONNECTED;
			char* id[whoToAsk->id_size + 1];
			memcpy(id, whoToAsk->id, whoToAsk->id_size);
			id[whoToAsk->id_size] = 0;
			libp2p_logger_error("online", "Connection to %s is broken\n", id);
			goto exit;
		}
		if ( return_message->provider_peer_head == NULL || return_message->provider_peer_head->item == NULL)
			goto exit;

		*result = libp2p_peer_copy(return_message->provider_peer_head->item);

	} else {
		goto exit;
	}

	retVal = 1;
	exit:
	if (return_message != NULL)
		libp2p_message_free(return_message);
	if (message != NULL)
		libp2p_message_free(message);

	return retVal;
}

/**
 * Find a peer using iterative Kademlia-style lookup.
 * @param routing the context
 * @param peer_id the id to look for
 * @param peer_id_size the size of the id
 * @param peer the result of the search
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_routing_online_find_peer(struct IpfsRouting* routing, const unsigned char* peer_id, size_t peer_id_size, struct Libp2pPeer **result) {
	// first look to see if we have it in the local peerstore
	struct Peerstore* peerstore = routing->local_node->peerstore;
	*result = libp2p_peerstore_get_peer(peerstore, (unsigned char*)peer_id, peer_id_size);
	if (*result != NULL) {
		return 1;
	}

	struct Libp2pVector* queried = libp2p_utils_vector_new(1);
	struct Libp2pVector* candidates = libp2p_utils_vector_new(1);

	// Seed candidates with all connected peers
	struct Libp2pLinkedList *current = peerstore->head_entry;
	while(current != NULL) {
		struct PeerEntry* entry = (struct PeerEntry*)current->item;
		if (entry != NULL && entry->peer != NULL && entry->peer->connection_type == CONNECTION_TYPE_CONNECTED) {
			libp2p_utils_vector_add(candidates, entry->peer);
		}
		current = current->next;
	}

	struct KademliaMessage* message = libp2p_message_new();
	message->message_type = MESSAGE_TYPE_FIND_NODE;
	message->key_size = peer_id_size;
	message->key = malloc(peer_id_size);
	if (message->key == NULL) {
		libp2p_message_free(message);
		libp2p_utils_vector_free(queried);
		libp2p_utils_vector_free(candidates);
		return 0;
	}
	memcpy(message->key, peer_id, peer_id_size);

	int rounds = 0;
	while (candidates->total > 0 && rounds < 2) {
		struct Libp2pVector* next_candidates = libp2p_utils_vector_new(1);
		for (int i = 0; i < candidates->total; i++) {
			struct Libp2pPeer* peer = (struct Libp2pPeer*)libp2p_utils_vector_get(candidates, i);
			if (peer == NULL || peer_id_in_vector(queried, (unsigned char*)peer->id, peer->id_size))
				continue;
			libp2p_utils_vector_add(queried, peer);

			if (peer->connection_type != CONNECTION_TYPE_CONNECTED)
				continue;

			struct KademliaMessage* ret = ipfs_routing_online_send_receive_message(peer->sessionContext, message);
			if (ret == NULL)
				continue;

			// Check if this peer knows the target directly
			if (ret->provider_peer_head != NULL && ret->provider_peer_head->item != NULL) {
				struct Libp2pPeer* found_peer = (struct Libp2pPeer*)ret->provider_peer_head->item;
				if (found_peer->id_size == peer_id_size && memcmp(found_peer->id, peer_id, peer_id_size) == 0) {
					*result = libp2p_peer_copy(found_peer);
					libp2p_message_free(ret);
					libp2p_utils_vector_free(next_candidates);
					libp2p_utils_vector_free(candidates);
					libp2p_utils_vector_free(queried);
					libp2p_message_free(message);
					return 1;
				}
			}

			// Collect closer peers for next round
			struct Libp2pLinkedList* closer = ret->closer_peer_head;
			while (closer != NULL) {
				struct Libp2pPeer* closer_peer = (struct Libp2pPeer*)closer->item;
				if (closer_peer != NULL && !peer_id_in_vector(queried, (unsigned char*)closer_peer->id, closer_peer->id_size)) {
					libp2p_peerstore_add_peer(routing->local_node->peerstore, closer_peer);
					struct Libp2pPeer* stored = libp2p_peerstore_get_peer(routing->local_node->peerstore, (unsigned char*)closer_peer->id, closer_peer->id_size);
					if (stored != NULL)
						libp2p_utils_vector_add(next_candidates, stored);
				}
				closer = closer->next;
			}
			libp2p_message_free(ret);
		}
		libp2p_utils_vector_free(candidates);
		candidates = next_candidates;
		rounds++;
	}

	libp2p_utils_vector_free(candidates);
	libp2p_utils_vector_free(queried);
	libp2p_message_free(message);
	return 0;
}

struct Libp2pPeer* ipfs_routing_online_build_local_peer(struct IpfsRouting* routing) {
	// create the local_peer to be attached to the message
	struct Libp2pPeer* local_peer = libp2p_peer_copy(routing->local_node->identity->peer);
	local_peer->connection_type = CONNECTION_TYPE_CONNECTED;
	local_peer->addr_head = libp2p_utils_linked_list_new();
	struct MultiAddress* temp_ma = multiaddress_new_from_string((char*)routing->local_node->repo->config->addresses->swarm_head->item);
	int port = multiaddress_get_ip_port(temp_ma);
	multiaddress_free(temp_ma);
	char str[255];
	sprintf(str, "/ip4/127.0.0.1/tcp/%d/ipfs/%s/", port, routing->local_node->identity->peer->id);
	struct MultiAddress* ma = multiaddress_new_from_string(str);
	libp2p_logger_debug("online", "Adding local MultiAddress %s to peer.\n", ma->string);
	local_peer->addr_head->item = ma;
	return local_peer;
}

/**
 * Notify the network that this host can provide this key
 * @param routing information about this host
 * @param key the key (hash) of the data
 * @param key_size the length of the key
 * @returns true(1) on success, otherwise false
 */
int ipfs_routing_online_provide(struct IpfsRouting* routing, const unsigned char* key, size_t key_size) {
	// build a Libp2pPeer that represents this peer
	struct Libp2pPeer* local_peer = ipfs_routing_online_build_local_peer(routing);

	// create the message
	struct KademliaMessage* msg = libp2p_message_new();
	msg->key_size = key_size;
	msg->key = malloc(msg->key_size);
	if (msg->key == NULL) {
		libp2p_message_free(msg);
		return 0;
	}
	memcpy(msg->key, key, msg->key_size);
	msg->message_type = MESSAGE_TYPE_ADD_PROVIDER;
	msg->provider_peer_head = libp2p_utils_linked_list_new();
	msg->provider_peer_head->item = local_peer;

	// loop through all peers in peerstore, and let them know (if we're still connected)
	struct Libp2pLinkedList *current = routing->local_node->peerstore->head_entry;
	while (current != NULL) {
		struct PeerEntry* current_peer_entry = (struct PeerEntry*)current->item;
		struct Libp2pPeer* current_peer = current_peer_entry->peer;
		if (current_peer->is_local) {
			// don't bother adding it
		} else {
			// notify everyone we're connected to
			if (current_peer->connection_type == CONNECTION_TYPE_CONNECTED) {
				// ignoring results is okay this time
				struct KademliaMessage* rslt = ipfs_routing_online_send_receive_message(current_peer->sessionContext, msg);
				if (rslt != NULL)
					libp2p_message_free(rslt);
			}
		}
		current = current->next;
	}

	// this will take care of freeing local_peer too
	libp2p_message_free(msg);

	return 1;
}

/**
 * Ping a remote
 * @param routing the context
 * @param message the message that we want to send
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_routing_online_ping(struct IpfsRouting* routing, struct Libp2pPeer* peer) {
	struct KademliaMessage *outMsg = NULL, *inMsg = NULL;
	int retVal = 0;

	if (peer->connection_type != CONNECTION_TYPE_CONNECTED) {
		if (!libp2p_peer_connect(routing->local_node->dialer, peer, routing->local_node->peerstore, routing->local_node->repo->config->datastore, 5))
			goto exit;
	}
	if (peer->connection_type == CONNECTION_TYPE_CONNECTED) {

		// build the message
		outMsg = libp2p_message_new();
		if (outMsg == NULL)
			goto exit;
		outMsg->message_type = MESSAGE_TYPE_PING;
		// send the message
		inMsg = ipfs_routing_online_send_receive_message(peer->sessionContext, outMsg);

		if (inMsg == NULL) {
			goto exit;
		}

		if (inMsg == NULL || inMsg->message_type != MESSAGE_TYPE_PING)
			goto exit;
	}

	retVal = 1;
	exit:
	if (inMsg != NULL)
		libp2p_message_free(inMsg);
	if (outMsg != NULL)
		libp2p_message_free(outMsg);

	return retVal;
}

/***
 * Ask a peer for a value
 * @param routing the context
 * @param peer the peer to ask
 * @param key the key to ask for
 * @param key_size the size of the key
 * @param buffer where to put the results
 * @param buffer_length the size of the buffer
 * @returns true(1) on success
 */
int ipfs_routing_online_get_peer_value(ipfs_routing* routing, const struct Libp2pPeer* peer, const unsigned char* key, size_t key_size, void** buffer, size_t *buffer_size) {
	// build message
	struct KademliaMessage* msg = libp2p_message_new();
	msg->key_size = key_size;
	msg->key = malloc(msg->key_size);
	if (msg->key == NULL) {
		libp2p_message_free(msg);
		return 0;
	}
	memcpy(msg->key, key, msg->key_size);
	msg->message_type = MESSAGE_TYPE_GET_VALUE;

	// send message and receive results
	struct KademliaMessage* ret_msg = ipfs_routing_online_send_receive_message(peer->sessionContext, msg);
	libp2p_message_free(msg);

	if (ret_msg == NULL)
		return 0;
	if (ret_msg->record == NULL || ret_msg->record->value_size <= 0) {
		libp2p_message_free(ret_msg);
		return 0;
	}
	// put message results into buffer
	*buffer_size = ret_msg->record->value_size;
	*buffer = malloc(*buffer_size);
	if (*buffer == NULL) {
		*buffer_size = 0;
		libp2p_message_free(ret_msg);
		return 0;
	}
	memcpy(*buffer, ret_msg->record->value, *buffer_size);
	libp2p_message_free(ret_msg);
	return 1;
}

/**
 * Retrieve a value from the dht
 * @param routing the context
 * @param key the key
 * @param key_size the size of the key
 * @param buffer where to put the results
 * @param buffer_size the length of the buffer
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_routing_online_get_value (ipfs_routing* routing, const unsigned char *key, size_t key_size, void **buffer, size_t *buffer_size)
{
	struct Libp2pVector *peers = NULL;
	int retVal = 0;

	// just to be sure
	*buffer_size = 0;

	// find a provider
	routing->FindProviders(routing, key, key_size, &peers);
	if (peers == NULL) {
		libp2p_logger_debug("online", "online_get_value returned no providers\n");
		goto exit;
	}

	libp2p_logger_debug("online", "FindProviders returned %d providers\n", peers->total);

	for(int i = 0; i < peers->total; i++) {
		struct Libp2pPeer* current_peer = libp2p_peerstore_get_or_add_peer(routing->local_node->peerstore, libp2p_utils_vector_get(peers, i));
		if (current_peer->is_local) {
			// it's a local fetch. Retrieve it
			libp2p_logger_debug("online", "It is a local fetch. Attempting get_value locally.\n");
			if (ipfs_routing_generic_get_value(routing, key, key_size, buffer, buffer_size)) {
				retVal = 1;
				break;
			}
		} else if (libp2p_peer_is_connected(current_peer)) {
			libp2p_logger_debug("online", "It is a remote fetch to a peer that is already connected. Attemping get_peer_value for peer %s.\n", libp2p_peer_id_to_string(current_peer));
			// ask a connected peer for the file. If unsuccessful, continue in the loop.
			if (ipfs_routing_online_get_peer_value(routing, current_peer, key, key_size, buffer, buffer_size)) {
				retVal = 1;
				goto exit;
			}
		}
	}
	if (!retVal) {
		// we didn't get the file. Try to connect to the peers we're not connected to, and ask for the file
		for(int i = 0; i < peers->total; i++) {
			struct Libp2pPeer* current_peer = (struct Libp2pPeer*)libp2p_utils_vector_get(peers, i);
			if (current_peer->is_local) {
				// we tried this once, it didn't work. Skip it.
				continue;
			}
			if (!libp2p_peer_is_connected(current_peer)) {
				// attempt to connect. If unsuccessful, continue in the loop.
				libp2p_logger_debug("online", "Attempting to connect to peer to retrieve file\n");
				if (libp2p_peer_connect(routing->local_node->dialer, current_peer, routing->local_node->peerstore, routing->local_node->repo->config->datastore, 5)) {
					libp2p_logger_debug("online", "Peer connected\n");
					if (ipfs_routing_online_get_peer_value(routing, current_peer, key, key_size, buffer, buffer_size)) {
						libp2p_logger_debug("online", "Retrieved a value\n");
						retVal = 1;
						goto exit;
					} else {
						libp2p_logger_debug("online", "Did not retrieve a value\n");
					}
				}
			}
		}
		if (buffer_size == 0)
			goto exit;
	}

	retVal = 1;
	exit:
	if (peers != NULL) {
		/* Free the vector, not the items
		for (int i = 0; i < peers->total; i++) {
			struct Libp2pPeer* current = libp2p_utils_vector_get(peers, i);
			libp2p_peer_free(current);
		}
		*/
		libp2p_utils_vector_free(peers);
	}
    return retVal;
}


/**
 * Connects to swarm
 * @param routing the routing struct
 * @returns 0 on success, otherwise error code
 */
int ipfs_routing_online_bootstrap(struct IpfsRouting* routing) {
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
			// Check if peer already exists to prevent duplicates
			peer = libp2p_peerstore_get_peer(routing->local_node->peerstore, (const unsigned char*)peer_id, peer_id_size);
			if (peer == NULL) {
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
			}
			free(peer_id);
			if (peer == NULL) {
				return -1; // this should never happen
			}
			if (peer->sessionContext == NULL) {
				if (!libp2p_peer_connect(routing->local_node->dialer, peer, routing->local_node->peerstore, routing->local_node->repo->config->datastore, 2)) {
					libp2p_logger_debug("online", "Attempted to bootstrap and connect to %s but failed. Continuing.\n", libp2p_peer_id_to_string(peer));
				}
			}
		}
	}

	return 0;
}

/**
 * Create a new ipfs_routing struct for online clients
 * @param fs_repo the repo
 * @param private_key the local private key
 * @reurns the ipfs_routing struct that handles messages
 */
ipfs_routing* ipfs_routing_new_online (struct IpfsNode* local_node, struct RsaPrivateKey *private_key) {
    ipfs_routing *onlineRouting = malloc (sizeof(ipfs_routing));

    if (onlineRouting) {
        onlineRouting->local_node     = local_node;
        onlineRouting->sk            = private_key;

        onlineRouting->PutValue      = ipfs_routing_generic_put_value;
        onlineRouting->GetValue      = ipfs_routing_online_get_value;
        onlineRouting->FindProviders = ipfs_routing_online_find_providers;
        onlineRouting->FindPeer      = ipfs_routing_online_find_peer;
        onlineRouting->Provide       = ipfs_routing_online_provide;
        onlineRouting->Ping          = ipfs_routing_online_ping;
        onlineRouting->Bootstrap     = ipfs_routing_online_bootstrap;
        onlineRouting->Listen        = ipfs_null_listen;
        onlineRouting->Shutdown      = ipfs_null_shutdown;
    }

    onlineRouting->local_node->mode = MODE_ONLINE;

    onlineRouting->Bootstrap(onlineRouting);

    return onlineRouting;
}

int ipfs_routing_online_free(ipfs_routing* incoming) {
	free(incoming);
	return 1;
}
