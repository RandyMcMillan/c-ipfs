#include <stdlib.h>

#include "../test_helper.h"
#include "ipfs/routing/routing.h"
#include "ipfs/repo/fsrepo/fs_repo.h"
#include "libp2p/net/multistream.h"
#include "libp2p/nodeio/nodeio.h"
#include "libp2p/utils/vector.h"
#include "libp2p/utils/linked_list.h"
#include "libp2p/peer/peerstore.h"
#include "libp2p/peer/providerstore.h"
#include "libp2p/crypto/encoding/base58.h"

void stop_kademlia(void);

int test_routing_supernode_start() {
	/* not working with supernode for now
	int retVal = 0;
	struct FSRepo* fs_repo = NULL;
	struct IpfsNode* ipfs_node = NULL;
	struct Stream* stream = NULL;

	if (!drop_build_and_open_repo("./tmp/.ipfs", &fs_repo))
		goto exit;

	ipfs_node = (struct IpfsNode*)malloc(sizeof(struct IpfsNode));
	ipfs_node->mode = MODE_ONLINE;
	ipfs_node->identity = fs_repo->config->identity;
	ipfs_node->repo = fs_repo;
	ipfs_node->routing = ipfs_routing_new_kademlia(ipfs_node, &fs_repo->config->identity->private_key, stream);

	if (ipfs_node->routing == NULL)
		goto exit;

	//TODO ping kademlia

	retVal = 1;
	exit:
	if (ipfs_node != NULL) {
		if (ipfs_node->routing != NULL)
			stop_kademlia();
		ipfs_node_free(ipfs_node);
	}
	return retVal;
	 */
	return 1;
}

void* start_daemon(void* path) {
	char* repo_path = (char*)path;
	ipfs_daemon_start(repo_path);
	return NULL;
}

int test_routing_supernode_get_remote_value() {
	return 0;
	/*
	// a remote machine has a file. Let's see if we can get it.
	// the key is QmYAXgX8ARiriupMQsbGXtKdDyGzWry1YV3sycKw1qqmgH, which is the test_file.txt
	int retVal = 0;
	struct FSRepo* fs_repo = NULL;
	struct IpfsNode* ipfs_node = NULL;
	struct Libp2pPeer this_peer;
	const unsigned char* orig_multihash = (unsigned char*)"QmYAXgX8ARiriupMQsbGXtKdDyGzWry1YV3sycKw1qqmgH";
	size_t hash_size = 100;
	unsigned char hash[hash_size];
	unsigned char* hash_ptr = &hash[0];
	struct Libp2pVector* multiaddresses = NULL;
	struct MultiAddress* addr = NULL;
	char* ip = NULL;
	struct SessionContext context;
	unsigned char* results = NULL;
	size_t results_size = 0;
	struct HashtableNode* node;

	// unencode the base58
	if (!libp2p_crypto_encoding_base58_decode(orig_multihash, strlen((char*)orig_multihash), &hash_ptr, &hash_size))
		goto exit;

	// fire things up
	if (!drop_build_and_open_repo("./tmp/.ipfs", &fs_repo))
		goto exit;

	ipfs_node = (struct IpfsNode*)malloc(sizeof(struct IpfsNode));
	ipfs_node->mode = MODE_ONLINE;
	ipfs_node->identity = fs_repo->config->identity;
	ipfs_node->repo = fs_repo;
	ipfs_node->providerstore = libp2p_providerstore_new(fs_repo->config->datastore, fs_repo->config->identity->peer);
	ipfs_node->peerstore = libp2p_peerstore_new(ipfs_node->identity->peer);
	// add the local peer to the peerstore
	this_peer.id = fs_repo->config->identity->peer->id;
	this_peer.id_size = fs_repo->config->identity->peer->id_size;
	this_peer.addr_head = libp2p_utils_linked_list_new();
	this_peer.addr_head->item = multiaddress_new_from_string("/ip4/127.0.0.1/tcp/4001");
	libp2p_peerstore_add_peer(ipfs_node->peerstore, &this_peer);
	// set a different port for the dht/kademlia stuff
	strcpy(ipfs_node->repo->config->addresses->api, "/ip4/127.0.0.1/udp/5002");
	// add bootstrap peer for kademlia
	struct MultiAddress* remote = multiaddress_new_from_string("/ip4/127.0.0.1/udp/5001");
	libp2p_utils_vector_add(ipfs_node->repo->config->bootstrap_peers, remote);
	ipfs_node->routing = ipfs_routing_new_kademlia(ipfs_node, &fs_repo->config->identity->private_key);


	if (ipfs_node->routing == NULL)
		goto exit;

	// ask the network who can provide this
	if (!ipfs_node->routing->FindProviders(ipfs_node->routing, hash, hash_size, &multiaddresses))
		goto exit;

	// get the file
	for(int i = 0; i < multiaddresses->total; i++) {
		addr = (struct MultiAddress*) libp2p_utils_vector_get(multiaddresses, i);
		if (multiaddress_is_ip4(addr)) {
			break;
		}
		addr = NULL;
	}

	if (addr == NULL)
		goto exit;

	// Connect to server
	multiaddress_get_ip_address(addr, &ip);
	struct Stream* file_stream = libp2p_net_multistream_connect(ip, multiaddress_get_ip_port(addr));

	if (file_stream == NULL)
		goto exit;

	context.insecure_stream = file_stream;
	context.default_stream = file_stream;
	// Switch from multistream to NodeIO
	if (!libp2p_nodeio_upgrade_stream(&context))
		goto exit;

	// Ask for file
	if (!libp2p_nodeio_get(&context, hash, hash_size, &results, &results_size))
		goto exit;

	if (!ipfs_hashtable_node_protobuf_decode(results, results_size, &node))
		goto exit;

	//we got it
	if (node->data_size < 100)
		goto exit;
	// make sure we got what we should have gotten
	// clean up
	retVal = 1;
	exit:
	if (fs_repo != NULL)
		ipfs_repo_fsrepo_free(fs_repo);
	if (ipfs_node != NULL)
		free(ipfs_node);
	if (multiaddresses != NULL)
		libp2p_utils_vector_free(multiaddresses);
	return retVal;
	*/
}

int test_routing_supernode_get_value() {
	return 0;
	/*
	int retVal = 0;
	struct FSRepo* fs_repo = NULL;
	struct IpfsNode* ipfs_node = NULL;
	int file_size = 1000;
	unsigned char bytes[file_size];
	char* fullFileName = "./tmp/temp_file.bin";
	struct HashtableNode* write_node = NULL;
	size_t bytes_written = 0;
	struct Libp2pVector* multiaddresses = NULL;
	unsigned char* results;
	size_t results_size = 0;
	struct HashtableNode* node = NULL;
	char* ip = NULL;
	pthread_t thread;
	int thread_started = 0;
	struct MultiAddress* addr = NULL;


	if (!drop_build_and_open_repo("./tmp/.ipfs", &fs_repo))
		goto exit;

	// start daemon
	pthread_create(&thread, NULL, start_daemon, (void*)"./tmp/.ipfs");
	thread_started = 1;

	ipfs_node = (struct IpfsNode*)malloc(sizeof(struct IpfsNode));
	ipfs_node->mode = MODE_ONLINE;
	ipfs_node->identity = fs_repo->config->identity;
	ipfs_node->repo = fs_repo;
	ipfs_node->providerstore = libp2p_providerstore_new(fs_repo->config->datastore, fs_repo->config->identity->peer);
	ipfs_node->peerstore = libp2p_peerstore_new(ipfs_node->identity->peer);
	struct Libp2pPeer this_peer;
	this_peer.id = fs_repo->config->identity->peer->id;
	this_peer.id_size = fs_repo->config->identity->peer->id_size;
	this_peer.addr_head = libp2p_utils_linked_list_new();
	this_peer.addr_head->item = multiaddress_new_from_string("/ip4/127.0.0.1/tcp/4001");
	libp2p_peerstore_add_peer(ipfs_node->peerstore, &this_peer);
	ipfs_node->routing = ipfs_routing_new_kademlia(ipfs_node, &fs_repo->config->identity->private_key);

	if (ipfs_node->routing == NULL)
		goto exit;

	// start listening

	// create a file
	create_bytes(&bytes[0], file_size);
	create_file(fullFileName, bytes, file_size);

	// write to ipfs
	if (ipfs_import_file("/tmp", fullFileName, &write_node, ipfs_node, &bytes_written, 1) == 0) {
		goto exit;
	}

	// announce to network that this can be provided
	//if (!ipfs_node->routing->Provide(ipfs_node->routing, (unsigned char*)write_node->hash, write_node->hash_size))
	//	goto exit;

	// ask the network who can provide this
	if (!ipfs_node->routing->FindProviders(ipfs_node->routing, write_node->hash, write_node->hash_size, &multiaddresses))
		goto exit;

	for(int i = 0; i < multiaddresses->total; i++) {
		addr = (struct MultiAddress*) libp2p_utils_vector_get(multiaddresses, i);
		if (multiaddress_is_ip4(addr)) {
			break;
		}
		addr = NULL;
	}

	if (addr == NULL)
		goto exit;

	// Connect to server
	multiaddress_get_ip_address(addr, &ip);
	struct Stream* file_stream = libp2p_net_multistream_connect(ip, multiaddress_get_ip_port(addr));

	if (file_stream == NULL)
		goto exit;

	struct SessionContext context;
	context.insecure_stream = file_stream;
	context.default_stream = file_stream;
	// Switch from multistream to NodeIO
	if (!libp2p_nodeio_upgrade_stream(&context))
		goto exit;

	// Ask for file
	if (!libp2p_nodeio_get(&context, write_node->hash, write_node->hash_size, &results, &results_size))
		goto exit;

	if (!ipfs_hashtable_node_protobuf_decode(results, results_size, &node))
		goto exit;

	//we got it
	if (node->data_size != write_node->data_size)
		goto exit;

	retVal = 1;
	exit:
	ipfs_daemon_stop();
	if (thread_started)
		pthread_join(thread, NULL);
	if (ipfs_node->routing != NULL)
		stop_kademlia();
	if (fs_repo != NULL)
		ipfs_repo_fsrepo_free(fs_repo);
	if (multiaddresses != NULL)
		libp2p_utils_vector_free(multiaddresses);
	return retVal;
	*/
}
