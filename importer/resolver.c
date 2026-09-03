#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include "ipfs/importer/resolver.h"
#include "libp2p/utils/logger.h"
#include "libp2p/crypto/encoding/base58.h"
#include "libp2p/conn/session.h"
#include "libp2p/routing/dht_protocol.h"
#include "ipfs/merkledag/node.h"
#include "ipfs/merkledag/merkledag.h"
#include "ipfs/repo/fsrepo/fs_repo.h"
#include "libp2p/net/multistream.h"
#include "libp2p/record/message.h"
#include "multiaddr/multiaddr.h"
#include "libp2p/record/message.h"
#include "libp2p/conn/dialer.h"

/**
 * return the next chunk of a path
 * @param path the path
 * @param next_part a pointer to a string NOTE: don't forget to free
 * @returns true(1) on success, false(0) on error, or no more parts
 */
int ipfs_resolver_next_path(const char* path, char** next_part) {
	for (int i = 0; i < strlen(path); i++) {
		if (path[i] != '/') { // we have the next section
			char* pos = strchr(&path[i+1], '/');
			if (pos == NULL) {
				*next_part = (char*)malloc(strlen(path) + 1);
				if ( *next_part == NULL) {
					// memory issue
					return 0;
				}
				strcpy(*next_part, path);
			} else {
				*next_part = (char*)malloc(pos - &path[i] + 1);
				if (*next_part == NULL) {
					return 0;
				}
				strncpy(*next_part, &path[i], pos-&path[i]);
				(*next_part)[pos-&path[i]] = 0;
			}
			return 1;
		}
	}
	return 0;
}

/**
 * Remove preceding slash and "/ipfs/" or "/ipns/" as well as the local multihash (if it is local)
 * @param path the path from the command line
 * @param fs_repo the local repo
 * @returns the modified path
 */
const char* ipfs_resolver_remove_path_prefix(const char* path, const struct FSRepo* fs_repo) {
	int pos = 0;
	int first_non_slash = -1;
	while(&path[pos] != NULL) {
		if (path[pos] == '/') {
			pos++;
			continue;
		} else {
			if (first_non_slash == -1)
				first_non_slash = pos;
			if (pos == first_non_slash && (strncmp(&path[pos], "ipfs", 4) == 0 || strncmp(&path[pos], "ipns", 4) == 0) ) {
				// ipfs or ipns should be up front. Otherwise, it could be part of the path
				pos += 4;
			} else if (strncmp(&path[pos], fs_repo->config->identity->peer->id, fs_repo->config->identity->peer->id_size) == 0) {
				pos += fs_repo->config->identity->peer->id_size + 1; // the slash
			} else {
				return &path[pos];
			}
		}
	}
	return NULL;
}

/**
 * Determine if this path is a remote path
 * @param path the path to examine
 * @param fs_repo the local repo
 * @returns true(1) if this path is a remote path
 */
int ipfs_resolver_is_remote(const char* path, const struct FSRepo* fs_repo) {
	int pos = 0;

	// skip the first slash
	while (&path[pos] != NULL && path[pos] == '/') {
		pos++;
	}
	if (&path[pos] == NULL)
		return 0;

	// skip the ipfs prefix
	if (strncmp(&path[pos], "ipfs/", 5) == 0 || strncmp(&path[pos], "ipns/", 5) == 0) {
		pos += 5; //the word plus the slash
	} else
		return 0;

	// if this is a Qm code, see if it is a local Qm code
	if (path[pos] == 'Q' && path[pos+1] == 'm') {
		if (strncmp(&path[pos], fs_repo->config->identity->peer->id, fs_repo->config->identity->peer->id_size) != 0) {
			return 1;
		}
	}
	return 0;

}

/**
 * Retrieve a node from a remote source
 * @param path the path to retrieve
 * @param from where to start
 * @param fs_repo the local repo
 * @returns the node, or NULL if not found
 */
struct HashtableNode* ipfs_resolver_remote_get(const char* path, struct HashtableNode* from, const struct IpfsNode* ipfs_node) {
	// parse the path
	const char* temp = ipfs_resolver_remove_path_prefix(path, ipfs_node->repo);
	if (temp == NULL)
		return NULL;
	char* pos = strchr(temp, '/');
	if (pos == NULL || pos - temp > 254)
		return NULL;
	char id[255];
	size_t id_size = pos - temp;
	strncpy(id, temp, id_size);
	id[id_size] = 0;
	char* key = &pos[1];
	pos = strchr(key, '/');
	if (pos == NULL || pos - key > 254)
		return NULL;
	pos[0] = '\0';
	// get the multiaddress for this
	struct Libp2pPeer* peer = libp2p_peerstore_get_peer(ipfs_node->peerstore, (unsigned char*)id, id_size);
	if (peer == NULL) {
		// Attempt to discover peer via routing (DHT/kademlia)
		if (ipfs_node->routing != NULL && ipfs_node->routing->FindPeer != NULL) {
			if (ipfs_node->routing->FindPeer(ipfs_node->routing, (unsigned char*)id, id_size, &peer)) {
				libp2p_peerstore_add_peer(ipfs_node->peerstore, peer);
			} else {
				return NULL;
			}
		} else {
			return NULL;
		}
	}
	if (!libp2p_peer_connect(ipfs_node->dialer, peer, ipfs_node->peerstore, ipfs_node->repo->config->datastore, 10))
		return NULL;
	struct Stream* kademlia_stream = libp2p_conn_dialer_get_stream(ipfs_node->dialer, peer, "kademlia");
	if (kademlia_stream == NULL)
		return NULL;
	// build the request
	struct KademliaMessage* message = libp2p_message_new();
	message->message_type = MESSAGE_TYPE_GET_VALUE;
	message->key = key;
	message->key_size = strlen(key);
	size_t b58size = 100;
	uint8_t *b58key = (uint8_t*) malloc(b58size);
	if (b58key == NULL) {
		libp2p_crypto_encoding_base58_encode((unsigned char*)message->key, message->key_size, (unsigned char**) &b58key, &b58size);
		libp2p_logger_debug("resolver", "Attempting to use kademlia to get key %s.\n", b58key);
		free(b58key);
	}
	size_t message_protobuf_size = libp2p_message_protobuf_encode_size(message);
	unsigned char message_protobuf[message_protobuf_size];
	libp2p_message_protobuf_encode(message, message_protobuf, message_protobuf_size, &message_protobuf_size);
	libp2p_message_free(message);
	struct StreamMessage outgoing;
	outgoing.data = message_protobuf;
	outgoing.data_size = message_protobuf_size;
	kademlia_stream->write(kademlia_stream->stream_context, &outgoing);
	struct StreamMessage* response;
	// we should get back a protobuf'd record
	kademlia_stream->read(kademlia_stream->stream_context, &response, 5);
	if (response->data_size == 1)
		return NULL;
	// turn the protobuf into a Node
	struct HashtableNode* node;
	ipfs_hashtable_node_protobuf_decode(response->data, response->data_size, &node);
	libp2p_stream_message_free(response);
	return node;
}

/**
 * Interogate the path and the current node, looking
 * for the desired node.
 * @param path the current path
 * @param from the current node (or NULL if it is the first call)
 * @returns what we are looking for, or NULL if it wasn't found
 */
struct HashtableNode* ipfs_resolver_get(const char* path, struct HashtableNode* from, const struct IpfsNode* ipfs_node) {

	struct FSRepo* fs_repo = ipfs_node->repo;

	// shortcut for remote files
	if (from == NULL && ipfs_resolver_is_remote(path, fs_repo)) {
		return ipfs_resolver_remote_get(path, from, ipfs_node);
	}
	/**
	 * Memory management notes:
	 * If we find what we're looking for, we clean up "from" and return the object
	 * If we don't find what we're looking for, but we can continue the search, we clean up "from"
	 * If we don't find what we're looking for, and we cannot continue, we do not clean up "from"
	 */
	// remove unnecessary stuff
	if (from == NULL)
		path = ipfs_resolver_remove_path_prefix(path, fs_repo);
	// grab the portion of the path to work with
	char* path_section;
	if (ipfs_resolver_next_path(path, &path_section) == 0)
		return NULL;
	struct HashtableNode* current_node = NULL;
	if (from == NULL) {
		// this is the first time around. Grab the root node
		if (path_section[0] == 'Q' && path_section[1] == 'm') {
			// we have a hash. Convert to a real hash, and find the node
			size_t hash_length = libp2p_crypto_encoding_base58_decode_size(strlen(path_section));
			unsigned char hash[hash_length];
			unsigned char* ptr = &hash[0];
			if (libp2p_crypto_encoding_base58_decode((unsigned char*)path_section, strlen(path_section), &ptr, &hash_length) == 0) {
				free(path_section);
				return NULL;
			}
			if (ipfs_merkledag_get_by_multihash(hash, hash_length, &current_node, fs_repo) == 0) {
				free(path_section);
				return NULL;
			}
			// we have the root node, now see if we want this or something further down
			int pos = strlen(path_section);
			if (pos == strlen(path)) {
				free(path_section);
				return current_node;
			} else {
				// look on...
				free(path_section);
				struct HashtableNode* newNode = ipfs_resolver_get(&path[pos+1], current_node, ipfs_node); // the +1 is the slash
				return newNode;
			}
		} else {
			// we don't have a current node, and we don't have a hash. Something is wrong
			free(path_section);
			return NULL;
		}
	} else {
		// we were passed a node. If it is a directory, see if what we're looking for is in it
		if (ipfs_hashtable_node_is_directory(from)) {
			struct NodeLink* curr_link = from->head_link;
			while (curr_link != NULL) {
				// if it matches the name, we found what we're looking for.
				// If so, load up the node by its hash
				if (strcmp(curr_link->name, path_section) == 0) {
					if (ipfs_merkledag_get(curr_link->hash, curr_link->hash_size, &current_node, fs_repo) == 0) {
						free(path_section);
						return NULL;
					}
					if (strlen(path_section) == strlen(path)) {
						// we are at the end of our search
						ipfs_hashtable_node_free(from);
						from = NULL;
						free(path_section);
						return current_node;
					} else {
						char* next_path_section;
						ipfs_resolver_next_path(&path[strlen(path_section)], &next_path_section);
						free(path_section);
						// if we're at the end of the path, return the node
						// continue looking for the next part of the path
						ipfs_hashtable_node_free(from);
						from = NULL;
						struct HashtableNode* newNode = ipfs_resolver_get(next_path_section, current_node, ipfs_node);
						return newNode;
					}
				}
				curr_link = curr_link->next;
			}
		} else {
			// we're asking for a file from an object that is not a directory. Bail.
			free(path_section);
			return NULL;
		}
	}
	// it should never get here
	free(path_section);
	if (from != NULL)
		ipfs_hashtable_node_free(from);
	return NULL;
}

/**
 * Interrogate the path, looking for the peer.
 * NOTE: only called locally. Not for remote callers
 * @param path the peer path to search for in the form like "/ipfs/QmKioji..."
 * @param ipfs_node the context
 * @returns a peer struct, or NULL if not found
 */
struct Libp2pPeer* ipfs_resolver_find_peer(const char* path, const struct IpfsNode* ipfs_node) {
	struct FSRepo* fs_repo = ipfs_node->repo;
	struct Libp2pLinkedList *addresses = NULL;
	struct Libp2pPeer* peer = NULL;

	// shortcut for if this node is the node we're looking for
	if (!ipfs_resolver_is_remote(path, fs_repo)) {
		// turn the string list into a multiaddress list
		struct Libp2pLinkedList* current_list_string = fs_repo->config->addresses->swarm_head;
		struct Libp2pLinkedList* current_list_ma = addresses;
		while(current_list_string != NULL) {
			struct Libp2pLinkedList* item = libp2p_utils_linked_list_new();
			item->item = multiaddress_new_from_string(current_list_string->item);
			if (addresses == NULL) {
				addresses = item;
			} else {
				current_list_ma->next = item;
			}
			current_list_ma = item;
			current_list_string = current_list_string->next;
		}
	}

	// ask the swarm for the peer
	const char* address_string = ipfs_resolver_remove_path_prefix(path, fs_repo);
	ipfs_node->routing->FindPeer(ipfs_node->routing, (const unsigned char*)address_string, strlen(address_string), &peer);

	return peer;
}

