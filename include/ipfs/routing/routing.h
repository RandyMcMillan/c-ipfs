#pragma once

#include "libp2p/peer/peer.h"
#include "libp2p/crypto/rsa.h"
#include "libp2p/record/message.h"
#include "ipfs/core/ipfs_node.h"

// offlineRouting implements the IpfsRouting interface,
// but only provides the capability to Put and Get signed dht
// records to and from the local datastore.
struct IpfsRouting {
	struct IpfsNode* local_node;
	size_t ds_len;
	struct RsaPrivateKey* sk;

	/**
	 * Put a value in the datastore
	 * @param 1 the struct that contains connection information
	 * @param 2 the key
	 * @param 3 the size of the key
	 * @param 4 the value
	 * @param 5 the size of the value
	 * @returns 0 on success, otherwise -1
	 */
	int (*PutValue)      (struct IpfsRouting*, const unsigned char*, size_t, const void*, size_t);
	/**
	 * Get a value from the filestore
	 * @param 1 the struct that contains the connection information
	 * @param 2 the key to look for
	 * @param 3 the size of the key
	 * @param 4 a place to store the value
	 * @param 5 the size of the value
	 * @returns true(1) on success, false(0) otherwise
	 */
	int (*GetValue)      (struct IpfsRouting*, const unsigned char*, size_t, void**, size_t*);
	/**
	 * Find a provider
	 * @param routing the context
	 * @param key the information that is being looked for
	 * @param key_size the size of param 2
	 * @param peers a vector of Libp2pPeers found that can provide the value for the key
	 * @returns true(1) on success, otherwise false(0)
	 */
	int (*FindProviders) (struct IpfsRouting* routing, const unsigned char* key, size_t key_size, struct Libp2pVector** peers);
	/**
	 * Find a peer
	 * @param 1 the context
	 * @param 2 the peer to look for
	 * @param 3 the size of the peer char array
	 * @param 4 the results
	 * @param 5 the size of the results
	 * @returns 0 or error code
	 */
	int (*FindPeer)      (struct IpfsRouting*, const unsigned char*, size_t, struct Libp2pPeer** result);
	/**
	 * Announce to the network that this host can provide this key
	 * @param 1 the context
	 * @param 2 the key
	 * @param 3 the key size
	 * @returns true(1) on success, otherwise false(0)
	 */
	int (*Provide)       (struct IpfsRouting*, const unsigned char*, size_t);
	/**
	 * Ping
	 * @param routing the context
	 * @param peer the peer
	 * @returns true(1) on success, otherwise false(0)
	 */
	int (*Ping)          (struct IpfsRouting* routing, struct Libp2pPeer* peer);
	/**
	 * Get everything going
	 * @param routing the context
	 * @returns true(1) on success, otherwise false(0)
	 */
	int (*Bootstrap)     (struct IpfsRouting*);
	void* (*Listen)       (void*);
	int (*Shutdown)      ();
};
typedef struct IpfsRouting ipfs_routing;

// offline routing routines.
ipfs_routing* ipfs_routing_new_offline (struct IpfsNode* local_node, struct RsaPrivateKey *private_key);
// online using secio, should probably be deprecated
ipfs_routing* ipfs_routing_new_online (struct IpfsNode* local_node, struct RsaPrivateKey* private_key);
int ipfs_routing_online_free(ipfs_routing*);
int ipfs_routing_offline_free(ipfs_routing* incoming);
// online using DHT/kademlia, the recommended router
ipfs_routing* ipfs_routing_new_kademlia(struct IpfsNode* local_node, struct RsaPrivateKey* private_key);
int ipfs_routing_kademlia_free(ipfs_routing* incoming);
// generic routines
int ipfs_routing_generic_put_value (ipfs_routing* offlineRouting, const unsigned char *key, size_t key_size, const void *val, size_t vlen);
int ipfs_routing_generic_get_value (ipfs_routing* offlineRouting, const unsigned char *key, size_t key_size, void **val, size_t *vlen);

// supernode
int ipfs_routing_supernode_parse_provider(const unsigned char* in, size_t in_size, struct Libp2pLinkedList** multiaddresses);
