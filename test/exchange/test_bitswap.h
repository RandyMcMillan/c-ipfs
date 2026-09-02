#include <pthread.h>
#include <signal.h>
#include "../test_helper.h"
#include "../routing/test_routing.h" // for test_routing_daemon_start
#include "libp2p/utils/vector.h"
#include "libp2p/utils/logger.h"
#include "ipfs/merkledag/merkledag.h" // for block to node conversion
#include "ipfs/exchange/bitswap/bitswap.h"
#include "ipfs/exchange/bitswap/message.h"
#include "ipfs/importer/importer.h"

uint8_t* generate_bytes(size_t size) {
	uint8_t* buffer = (uint8_t*) malloc(size);
	for(size_t i = 0; i < size; i++) {
		buffer[i] = i % 255;
	}
	return buffer;
}

int compare_generated_bytes(uint8_t* buffer, size_t size) {
	for(size_t i = 0; i < size; i++) {
		if (buffer[i] != (i % 255)) {
			fprintf(stderr, "compare_generated_bytes: Mismatch in position %lu", i);
			return 0;
		}
	}
	return 1;
}

/**
 * Test the protobufing of a BitswapMessage
 * this should be run with valgrind
 */
int test_bitswap_new_free() {
	int retVal = 0;
	struct BitswapMessage* message = NULL;
	struct WantlistEntry* wantlist_entry = NULL;
	struct Block* block = NULL;

	// Test 1, create a simple BitswapMessage
	message = ipfs_bitswap_message_new();
	ipfs_bitswap_message_free(message);
	message = NULL;

	// Test 2, okay, that worked, now make one more complicated
	message = ipfs_bitswap_message_new();
	message->wantlist = ipfs_bitswap_wantlist_new();
	ipfs_bitswap_message_free(message);
	message = NULL;

	// Test 3, now add some more
	message = ipfs_bitswap_message_new();
	// wantlist
	message->wantlist = ipfs_bitswap_wantlist_new();
	wantlist_entry = ipfs_bitswap_wantlist_entry_new();
	wantlist_entry->priority = 24;
	message->wantlist->entries = libp2p_utils_vector_new(1);
	libp2p_utils_vector_add(message->wantlist->entries, wantlist_entry);
	wantlist_entry = NULL;
	wantlist_entry = (struct WantlistEntry*)libp2p_utils_vector_get(message->wantlist->entries, 0);
	if (wantlist_entry == NULL) {
		fprintf(stderr, "Vector didn't have entry\n");
		goto exit;
	}
	if (wantlist_entry->priority != 24) {
		fprintf(stderr, "Unable to retrieve item from vector after an external free\n");
		goto exit;
	}
	// payload
	message->payload = libp2p_utils_vector_new(1);
	block = ipfs_block_new();
	block->data_length = 25;
	libp2p_utils_vector_add(message->payload, block);
	block = (struct Block*)libp2p_utils_vector_get(message->payload, 0);
	if (block == NULL) {
		fprintf(stderr, "Vector didn't have payload entry\n");
		goto exit;
	}
	if (block->data_length != 25) {
		fprintf(stderr, "Unable to retrieve block->data_length\n");
	}

	retVal = 1;
	exit:

	if (message != NULL)
	{
		ipfs_bitswap_message_free(message);
	}

	return retVal;
}

int test_bitswap_protobuf() {
	int retVal = 0;

	// create a complicated BitswapMessage
	// first the basics...
	struct BitswapMessage* message = ipfs_bitswap_message_new();
	// add a WantList
	struct BitswapWantlist* want_list = ipfs_bitswap_wantlist_new();
	message->wantlist = want_list;
	// add something to the WantList
	message->wantlist->entries = libp2p_utils_vector_new(1);
	struct WantlistEntry* wantlist_entry = ipfs_bitswap_wantlist_entry_new();
	wantlist_entry->block_size = 1;
	wantlist_entry->priority = 100;
	wantlist_entry->block = generate_bytes(100);
	wantlist_entry->block_size = 100;
	libp2p_utils_vector_add(message->wantlist->entries, wantlist_entry);
	message->wantlist->full = 1;

	retVal = 1;
	return retVal;
}

/**
 * Test Bitswap 1.2.0 message round-trip with wantType, sendDontHave,
 * BlockPresence, and pendingBytes.
 */
int test_bitswap_message_120() {
	int retVal = 0;

	struct BitswapMessage* message = ipfs_bitswap_message_new();
	if (message == NULL)
		return 0;

	// Wantlist with 1.2.0 fields
	message->wantlist = ipfs_bitswap_wantlist_new();
	message->wantlist->entries = libp2p_utils_vector_new(2);
	message->wantlist->full = 1;

	struct WantlistEntry* entry1 = ipfs_bitswap_wantlist_entry_new();
	entry1->block = generate_bytes(32);
	entry1->block_size = 32;
	entry1->priority = 10;
	entry1->cancel = 0;
	entry1->want_type = WANT_TYPE_HAVE;     // 1.2.0
	entry1->send_dont_have = 1;              // 1.2.0
	libp2p_utils_vector_add(message->wantlist->entries, entry1);

	struct WantlistEntry* entry2 = ipfs_bitswap_wantlist_entry_new();
	entry2->block = generate_bytes(32);
	entry2->block_size = 32;
	entry2->priority = 5;
	entry2->cancel = 1;
	entry2->want_type = WANT_TYPE_BLOCK;    // 1.2.0
	entry2->send_dont_have = 0;              // 1.2.0
	libp2p_utils_vector_add(message->wantlist->entries, entry2);

	// BlockPresences (1.2.0)
	message->block_presences = libp2p_utils_vector_new(2);
	struct BitswapBlockPresence* bp1 = ipfs_bitswap_block_presence_new();
	bp1->cid = generate_bytes(32);
	bp1->cid_size = 32;
	bp1->type = BLOCK_PRESENCE_HAVE;
	libp2p_utils_vector_add(message->block_presences, bp1);

	struct BitswapBlockPresence* bp2 = ipfs_bitswap_block_presence_new();
	bp2->cid = generate_bytes(32);
	bp2->cid_size = 32;
	bp2->type = BLOCK_PRESENCE_DONT_HAVE;
	libp2p_utils_vector_add(message->block_presences, bp2);

	// pendingBytes (1.2.0)
	message->pending_bytes = 4096;

	// Encode
	size_t buf_size = ipfs_bitswap_message_protobuf_encode_size(message);
	uint8_t* buf = (uint8_t*) malloc(buf_size);
	if (buf == NULL)
		goto exit;
	size_t bytes_written = 0;
	if (!ipfs_bitswap_message_protobuf_encode(message, buf, buf_size, &bytes_written))
		goto exit;

	// Decode
	struct BitswapMessage* decoded = NULL;
	if (!ipfs_bitswap_message_protobuf_decode(buf, bytes_written, &decoded))
		goto exit;

	// Verify wantlist
	if (decoded->wantlist == NULL || decoded->wantlist->entries == NULL || decoded->wantlist->entries->total != 2)
		goto exit;
	struct WantlistEntry* de1 = (struct WantlistEntry*) libp2p_utils_vector_get(decoded->wantlist->entries, 0);
	struct WantlistEntry* de2 = (struct WantlistEntry*) libp2p_utils_vector_get(decoded->wantlist->entries, 1);
	if (de1->want_type != WANT_TYPE_HAVE || de1->send_dont_have != 1)
		goto exit;
	if (de2->want_type != WANT_TYPE_BLOCK || de2->send_dont_have != 0 || de2->cancel != 1)
		goto exit;

	// Verify block presences
	if (decoded->block_presences == NULL || decoded->block_presences->total != 2)
		goto exit;
	struct BitswapBlockPresence* dbp1 = (struct BitswapBlockPresence*) libp2p_utils_vector_get(decoded->block_presences, 0);
	struct BitswapBlockPresence* dbp2 = (struct BitswapBlockPresence*) libp2p_utils_vector_get(decoded->block_presences, 1);
	if (dbp1->type != BLOCK_PRESENCE_HAVE || dbp1->cid_size != 32)
		goto exit;
	if (dbp2->type != BLOCK_PRESENCE_DONT_HAVE || dbp2->cid_size != 32)
		goto exit;

	// Verify pending bytes
	if (decoded->pending_bytes != 4096)
		goto exit;

	retVal = 1;
exit:
	if (buf != NULL) free(buf);
	if (decoded != NULL) ipfs_bitswap_message_free(decoded);
	ipfs_bitswap_message_free(message);
	return retVal;
}

/***
 * Put a file in ipfs and attempt to retrieve it using bitswap's Exchange interface
 */
int test_bitswap_retrieve_file()
{
	int retVal = 0;
	struct IpfsNode* localNode = NULL;
	const char* ipfs_path = "/tmp/ipfstest1";
	struct HashtableNode* node = NULL; // the node created by adding the file
	size_t bytes_written = 0;
	struct Block* block = NULL;
	struct Cid* cid = NULL;

	// build and open the new IPFS repository with no bootstrap peers
	drop_and_build_repository(ipfs_path, 4001, NULL, NULL);
	ipfs_node_offline_new(ipfs_path, &localNode);

	// add a file
	unsigned char bytes[256];
	create_bytes(&bytes[0], 256);
	create_file("/tmp/test_file.bin", &bytes[0], 256);
	ipfs_import_file(NULL, "/tmp/test_file.bin", &node, localNode, &bytes_written, 0);

	// build the Cid from the node information
	cid = ipfs_cid_new(0, node->hash, node->hash_size, CID_DAG_PROTOBUF);
	if (cid == NULL)
		goto exit;

	// attempt to retrieve the file
	if (!localNode->exchange->GetBlock(localNode->exchange, cid, &block)) {
		goto exit;
	}

	retVal = 1;
	exit:
	// cleanup
	if (block != NULL)
		ipfs_block_free(block);
	if (cid != NULL)
		ipfs_cid_free(cid);
	if (node != NULL)
		ipfs_hashtable_node_free(node);
	ipfs_node_free(localNode);
	return retVal;
}

/***
 * Attempt to retrieve a file from a known node
 */
int test_bitswap_retrieve_file_remote() {
	int retVal = 0;

	/*
	libp2p_logger_add_class("dht_protocol");
	libp2p_logger_add_class("providerstore");
	libp2p_logger_add_class("peerstore");
	libp2p_logger_add_class("exporter");
	libp2p_logger_add_class("peer");
	*/
	libp2p_logger_add_class("test_bitswap");
	libp2p_logger_add_class("null");
	libp2p_logger_add_class("online");
	libp2p_logger_add_class("multistream");

	// clean out repository
	char* ipfs_path = "/tmp/test1";
	char* peer_id_1 = NULL, *peer_id_2 = NULL;
	struct IpfsNode* ipfs_node1 = NULL, *ipfs_node2 = NULL;
	pthread_t thread1;
	int thread1_started = 0;
	struct MultiAddress* ma_peer1 = NULL;
	struct Libp2pVector* ma_vector2 = NULL;
	struct HashtableNode* node = NULL; // the file that was put in
	struct Block* result = NULL; // the block that comes back
	struct HashtableNode* result_node = NULL; // the HashtableNode of the block that comes back
	struct Cid* cid = NULL;

	// create peer 1
	libp2p_logger_debug("test_bitswap", "Firing up daemon 1.\n");
	drop_and_build_repository(ipfs_path, 4001, NULL, &peer_id_1);
	char multiaddress_string[255];
	sprintf(multiaddress_string, "/ip4/127.0.0.1/tcp/4001/ipfs/%s", peer_id_1);
	ma_peer1 = multiaddress_new_from_string(multiaddress_string);
	// add a file
	size_t bytes_written = 0;
	ipfs_node_offline_new(ipfs_path, &ipfs_node1);
	unsigned char bytes[256];
	create_bytes(bytes, 256);
	create_file("/tmp/hello.bin", bytes, 256);
	ipfs_import_file(NULL, "/tmp/hello.bin", &node, ipfs_node1, &bytes_written, 0);
	// start the daemon in a separate thread
	if (pthread_create(&thread1, NULL, test_daemon_start, (void*)ipfs_path) < 0) {
		libp2p_logger_error("test_bitswap", "Unable to start thread 1\n");
		goto exit;
	}
	thread1_started = 1;
    // wait for everything to start up
    sleep(3);

    // create my peer, peer 2
    libp2p_logger_debug("test_bitswap", "Firing up the client\n");
	ipfs_path = "/tmp/test2";
	ma_peer1 = multiaddress_new_from_string(multiaddress_string);
	ma_vector2 = libp2p_utils_vector_new(1);
	libp2p_utils_vector_add(ma_vector2, ma_peer1);
	drop_and_build_repository(ipfs_path, 4002, ma_vector2, &peer_id_2);
	multiaddress_free(ma_peer1);
	ipfs_node_offline_new(ipfs_path, &ipfs_node2);
    ipfs_node2->routing->Bootstrap(ipfs_node2->routing);

    // this does the heavy lifting...
    cid = ipfs_cid_new(0, node->hash, node->hash_size, CID_DAG_PROTOBUF);
    if (!ipfs_node2->exchange->GetBlock(ipfs_node2->exchange, cid, &result)) {
    		libp2p_logger_error("test_bitswap", "GetBlock returned false\n");
    		goto exit;
    }

    if (node->hash_size != result->cid->hash_length) {
    		libp2p_logger_error("test_bitswap", "Node hash sizes do not match. Should be %lu but is %lu\n", node->hash_size, result->cid->hash_length);
    		goto exit;
    }

    // to compare sizes, we must convert the block back to a hashtable node
    ipfs_merkledag_convert_block_to_node(result, &result_node);
    if (node->data_size != result_node->data_size) {
    		libp2p_logger_error("test_bitswap", "Result sizes do not match. Should be %lu but is %lu\n", node->data_size, result->data_length);
    		goto exit;
    }

	retVal = 1;
	exit:
	ipfs_daemon_stop();
	if (thread1_started)
		pthread_join(thread1, NULL);
	if (peer_id_1 != NULL)
		free(peer_id_1);
	if (peer_id_2 != NULL)
		free(peer_id_2);
	if (ma_vector2 != NULL) {
		libp2p_utils_vector_free(ma_vector2);
	}
	if (node != NULL)
		ipfs_hashtable_node_free(node);
	if (result_node != NULL)
		ipfs_hashtable_node_free(result_node);
	if (result != NULL)
		ipfs_block_free(result);
	if (cid != NULL)
		ipfs_cid_free(cid);
	return retVal;
}


/***
 * Attempt to retrieve a file from a known node
 */
int test_bitswap_retrieve_file_known_remote() {
	int retVal = 0;
	signal(SIGPIPE, SIG_IGN);
	/***
	 * This assumes a remote server with the hello_world.txt file already in its database
	 */
	int remote_port = 4001;
	// mac
	char* remote_peer_id = "QmPTDqsHMn3GkLBUHBofB5M6wBGzLwHAxqZpLvcda6hF6Q";
	char* remote_ip = "10.211.55.2";
	// linux
	//char* remote_peer_id = "QmRKm1d9kSCRpMFtLYpfhhCQ3DKuSSPJa3qn9wWXfwnWnY";
	//char* remote_ip = "10.211.55.4";
	char* hello_world_hash = "QmTUFTVgkHT3Qdd9ospVjSLi2upd6VdkeNXZQH66cVmzja";

	/*
	libp2p_logger_add_class("dht_protocol");
	libp2p_logger_add_class("providerstore");
	libp2p_logger_add_class("peerstore");
	libp2p_logger_add_class("exporter");
	libp2p_logger_add_class("peer");
	*/
	libp2p_logger_add_class("test_bitswap");
	libp2p_logger_add_class("null");
	libp2p_logger_add_class("online");
	libp2p_logger_add_class("multistream");
	libp2p_logger_add_class("secio");
	libp2p_logger_add_class("bitswap");
	libp2p_logger_add_class("bitswap_engine");
	libp2p_logger_add_class("bitswap_network");

	char* ipfs_path = "/tmp/test1";
	char* peer_id_1 = NULL, *peer_id_2 = NULL;
	struct IpfsNode* ipfs_node2 = NULL;
	struct MultiAddress* ma_peer1 = NULL;
	struct Libp2pVector* ma_vector2 = NULL;
	struct Block* result = NULL;
	struct Cid* cid = NULL;

	// create peer 1
	char multiaddress_string[255];
	sprintf(multiaddress_string, "/ip4/%s/tcp/%d/ipfs/%s", remote_ip, remote_port, remote_peer_id);
	ma_peer1 = multiaddress_new_from_string(multiaddress_string);

    // create my peer, peer 2
    libp2p_logger_debug("test_routing", "Firing up the client\n");
	ipfs_path = "/tmp/test2";
	ma_vector2 = libp2p_utils_vector_new(1);
	libp2p_utils_vector_add(ma_vector2, ma_peer1);
	drop_and_build_repository(ipfs_path, 4002, ma_vector2, &peer_id_2);
	multiaddress_free(ma_peer1);
	ipfs_node_online_new(ipfs_path, &ipfs_node2);

    if (!ipfs_cid_decode_hash_from_base58((unsigned char*)hello_world_hash, strlen(hello_world_hash), &cid))
    		goto exit;

    sleep(3);

    // this does the heavy lifting...
    if (!ipfs_node2->exchange->GetBlock(ipfs_node2->exchange, cid, &result)) {
    	libp2p_logger_error("test_bitswap", "GetBlock returned false\n");
    	goto exit;
    }

    if (result == NULL) {
    	libp2p_logger_error("test_bitswap", "GetBlock returned NULL");
    	goto exit;
    }

    if (result->cid == NULL) {
    	libp2p_logger_error("test_bitswap", "GetBlock returned an object with no CID");
    	goto exit;
    }

    if (cid->hash_length != result->cid->hash_length) {
    	libp2p_logger_error("test_bitswap", "Node hash sizes do not match. Should be %lu but is %lu\n", strlen(hello_world_hash), result->cid->hash_length);
    	goto exit;
    }

	retVal = 1;
	exit:
	if (peer_id_1 != NULL)
		free(peer_id_1);
	if (peer_id_2 != NULL)
		free(peer_id_2);
	if (ma_vector2 != NULL) {
		libp2p_utils_vector_free(ma_vector2);
	}
	// this is freed by ipfs_node_free
	//if (result != NULL)
	//	ipfs_block_free(result);
	if (cid != NULL)
		ipfs_cid_free(cid);
	ipfs_node_free(ipfs_node2);
	return retVal;
}




/***
 * Attempt to retrieve a file from a previously unknown node
 */
int test_bitswap_retrieve_file_third_party() {
	int retVal = 0;

	/*
	libp2p_logger_add_class("dht_protocol");
	libp2p_logger_add_class("providerstore");
	libp2p_logger_add_class("peerstore");
	libp2p_logger_add_class("exporter");
	*/
	libp2p_logger_add_class("peer");
	libp2p_logger_add_class("bitswap_network");
	libp2p_logger_add_class("test_bitswap");
	libp2p_logger_add_class("null");
	libp2p_logger_add_class("online");
	libp2p_logger_add_class("multistream");

	// clean out repository
	char* ipfs_path = "/tmp/test1";
	char* peer_id_1 = NULL, *peer_id_2 = NULL, *peer_id_3 = NULL;
	struct IpfsNode* ipfs_node2 = NULL, *ipfs_node3 = NULL;
	pthread_t thread1, thread2;
	int thread1_started = 0, thread2_started = 0;
	struct MultiAddress* ma_peer1 = NULL;
	struct Libp2pVector* ma_vector2 = NULL, *ma_vector3 = NULL;
	struct HashtableNode* node = NULL;
	struct Block* result = NULL;
	struct HashtableNode* result_node = NULL;
	struct Cid* cid = NULL;

	// create peer 1
	libp2p_logger_debug("test_bitswap", "Firing up daemon 1.\n");
	drop_and_build_repository(ipfs_path, 4001, NULL, &peer_id_1);
	char multiaddress_string[255];
	sprintf(multiaddress_string, "/ip4/127.0.0.1/tcp/4001/ipfs/%s", peer_id_1);
	ma_peer1 = multiaddress_new_from_string(multiaddress_string);
	// start the daemon in a separate thread
	if (pthread_create(&thread1, NULL, test_daemon_start, (void*)ipfs_path) < 0) {
		libp2p_logger_error("test_bitswap", "Unable to start thread 1\n");
		goto exit;
	}
	thread1_started = 1;
    // wait for everything to start up
    sleep(3);

    // create peer 2
	libp2p_logger_debug("test_routing", "Firing up daemon 2.\n");
	ipfs_path = "/tmp/test2";
	// create a vector to hold peer1's multiaddress so we can connect as a peer
	ma_vector2 = libp2p_utils_vector_new(1);
	libp2p_utils_vector_add(ma_vector2, ma_peer1);
	// note: this destroys some things, as it frees the fs_repo_3:
	drop_and_build_repository(ipfs_path, 4002, ma_vector2, &peer_id_2);
	multiaddress_free(ma_peer1);
	// add a file, to prime the connection to peer 1
	//TODO: Find a better way to do this...
	size_t bytes_written = 0;
	if (!ipfs_node_online_new(ipfs_path, &ipfs_node2))
		goto exit;
	ipfs_node2->routing->Bootstrap(ipfs_node2->routing);
	unsigned char bytes[256];
	create_bytes(&bytes[0], 256);
	create_file("/tmp/hello.bin", &bytes[0], 256);
	ipfs_import_file(NULL, "/tmp/hello.bin", &node, ipfs_node2, &bytes_written, 0);
	ipfs_node_free(ipfs_node2);
	// start the daemon in a separate thread
	if (pthread_create(&thread2, NULL, test_daemon_start, (void*)ipfs_path) < 0) {
		libp2p_logger_error("test_bitswap", "Unable to start thread 2\n");
		goto exit;
	}
	thread2_started = 1;
    // wait for everything to start up
    sleep(3);

    // create my peer, peer 3
    libp2p_logger_debug("test_routing", "Firing up the 3rd client\n");
	ipfs_path = "/tmp/test3";
	ma_peer1 = multiaddress_new_from_string(multiaddress_string);
	ma_vector3 = libp2p_utils_vector_new(1);
	libp2p_utils_vector_add(ma_vector3, ma_peer1);
	drop_and_build_repository(ipfs_path, 4003, ma_vector3, &peer_id_3);
	multiaddress_free(ma_peer1);
	ipfs_node_offline_new(ipfs_path, &ipfs_node3);

    ipfs_node3->routing->Bootstrap(ipfs_node3->routing);

    // this does the heavy lifting...
    cid = ipfs_cid_new(0, node->hash, node->hash_size, CID_DAG_PROTOBUF);
    if (!ipfs_node3->exchange->GetBlock(ipfs_node3->exchange, cid, &result)) {
    	libp2p_logger_error("test_bitswap", "GetBlock returned false\n");
    	goto exit;
    }

    if (node->hash_size != result->cid->hash_length) {
    	libp2p_logger_error("test_bitswap", "Node hash sizes do not match. Should be %lu but is %lu\n", node->hash_size, result->cid->hash_length);
    	goto exit;
    }

    ipfs_merkledag_convert_block_to_node(result, &result_node);
    if (node->data_size != result_node->data_size) {
    	libp2p_logger_error("test_bitswap", "Result sizes do not match. Should be %lu but is %lu\n", node->data_size, result->data_length);
    	goto exit;
    }

	retVal = 1;
	exit:
	ipfs_daemon_stop();
	if (thread1_started)
		pthread_join(thread1, NULL);
	if (thread2_started)
		pthread_join(thread2, NULL);
	if (ipfs_node3 != NULL)
		ipfs_node_free(ipfs_node3);
	if (peer_id_1 != NULL)
		free(peer_id_1);
	if (peer_id_2 != NULL)
		free(peer_id_2);
	if (peer_id_3 != NULL)
		free(peer_id_3);
	if (ma_vector2 != NULL) {
		libp2p_utils_vector_free(ma_vector2);
	}
	if (ma_vector3 != NULL) {
		libp2p_utils_vector_free(ma_vector3);
	}
	if (node != NULL)
		ipfs_hashtable_node_free(node);
	if (result != NULL)
		ipfs_block_free(result);
	if (result_node != NULL)
		ipfs_hashtable_node_free(result_node);
	if (cid != NULL)
		ipfs_cid_free(cid);
	return retVal;

}

/***
 * Attempt to retrieve a file from a known node
 */
int test_bitswap_retrieve_file_go_remote() {
	int retVal = 0;
	signal(SIGPIPE, SIG_IGN);
	/***
	 * This assumes a remote server with the hello_world.txt file already in its database
	 */
	int remote_port = 4001;
	// mac
	char* remote_peer_id = "QmNgGtj3Dk7RPRFgyp9exmq2WjaUf3gkwhMF1vkmcyXcBD";
	char* remote_ip = "10.211.55.2";
	// linux
	//char* remote_peer_id = "QmRKm1d9kSCRpMFtLYpfhhCQ3DKuSSPJa3qn9wWXfwnWnY";
	//char* remote_ip = "10.211.55.4";
	char* hello_world_hash = "QmYAXgX8ARiriupMQsbGXtKdDyGzWry1YV3sycKw1qqmgH";

	/*
	libp2p_logger_add_class("dht_protocol");
	libp2p_logger_add_class("providerstore");
	libp2p_logger_add_class("peerstore");
	libp2p_logger_add_class("exporter");
	libp2p_logger_add_class("peer");
	*/
	libp2p_logger_add_class("test_bitswap");
	libp2p_logger_add_class("null");
	libp2p_logger_add_class("online");
	libp2p_logger_add_class("multistream");
	libp2p_logger_add_class("secio");
	libp2p_logger_add_class("bitswap");
	libp2p_logger_add_class("bitswap_engine");
	libp2p_logger_add_class("bitswap_network");

	char* ipfs_path = "/tmp/test1";
	char peer_ma_1[80] = "", *peer_id_2 = NULL;
	struct IpfsNode* ipfs_node2 = NULL;
	struct MultiAddress* ma_peer1 = NULL;
	struct Libp2pVector* ma_vector2 = NULL;
	struct Block* result = NULL;
	struct Cid* cid = NULL;

	// create a MultiAddress of the GO peer (peer 1)
	sprintf(peer_ma_1, "/ip4/%s/tcp/%d/ipfs/%s", remote_ip, remote_port, remote_peer_id);
	ma_peer1 = multiaddress_new_from_string(peer_ma_1);

    // create my peer, peer 2
    libp2p_logger_debug("test_routing", "Firing up the client\n");
	ipfs_path = "/tmp/test2";
	ma_vector2 = libp2p_utils_vector_new(1);
	libp2p_utils_vector_add(ma_vector2, ma_peer1);
	drop_and_build_repository(ipfs_path, 4002, ma_vector2, &peer_id_2);
	multiaddress_free(ma_peer1);
	ipfs_node_online_new(ipfs_path, &ipfs_node2);

    if (!ipfs_cid_decode_hash_from_base58((unsigned char*)hello_world_hash, strlen(hello_world_hash), &cid))
    		goto exit;

    sleep(3);

    // this does the heavy lifting...
    if (!ipfs_node2->exchange->GetBlock(ipfs_node2->exchange, cid, &result)) {
    	libp2p_logger_error("test_bitswap", "GetBlock returned false\n");
    	goto exit;
    }

    if (result == NULL) {
    	libp2p_logger_error("test_bitswap", "GetBlock returned NULL");
    	goto exit;
    }

    if (result->cid == NULL) {
    	libp2p_logger_error("test_bitswap", "GetBlock returned an object with no CID");
    	goto exit;
    }

    if (cid->hash_length != result->cid->hash_length) {
    	libp2p_logger_error("test_bitswap", "Node hash sizes do not match. Should be %lu but is %lu\n", strlen(hello_world_hash), result->cid->hash_length);
    	goto exit;
    }

	retVal = 1;
	exit:
	if (peer_id_2 != NULL)
		free(peer_id_2);
	if (ma_vector2 != NULL) {
		libp2p_utils_vector_free(ma_vector2);
	}
	if (cid != NULL)
		ipfs_cid_free(cid);
	ipfs_node_free(ipfs_node2);
	if (ma_peer1 != NULL)
		multiaddress_free(ma_peer1);
	return retVal;
}

/***
 * Attempt to retrieve a file from a known node
 */
int test_bitswap_serve_file_go_remote() {
	int retVal = 0;
	signal(SIGPIPE, SIG_IGN);
	/***
	 * This assumes a remote client asking for hello.txt
	 */
	// linux
	//char* remote_peer_id = "QmRKm1d9kSCRpMFtLYpfhhCQ3DKuSSPJa3qn9wWXfwnWnY";
	//char* remote_ip = "10.211.55.4";
	//char* hello_world_hash = "QmYAXgX8ARiriupMQsbGXtKdDyGzWry1YV3sycKw1qqmgH";

	/*
	libp2p_logger_add_class("dht_protocol");
	libp2p_logger_add_class("providerstore");
	libp2p_logger_add_class("peerstore");
	libp2p_logger_add_class("exporter");
	libp2p_logger_add_class("peer");
	*/
	libp2p_logger_add_class("test_bitswap");
	libp2p_logger_add_class("null");
	libp2p_logger_add_class("online");
	libp2p_logger_add_class("multistream");
	libp2p_logger_add_class("secio");
	libp2p_logger_add_class("bitswap");
	libp2p_logger_add_class("bitswap_engine");
	libp2p_logger_add_class("bitswap_network");

	char* ipfs_path = "/tmp/ipfs_2";
	char *peer_id_2 = NULL;
	struct FSRepo* fs_repo;
	pthread_t thread2;

    // create my peer, peer 2
    libp2p_logger_debug("test_bitswap", "Firing up the client\n");
    drop_build_open_repo(ipfs_path, &fs_repo, "config.test2.wo_journal");
    ipfs_repo_fsrepo_free(fs_repo);

    // add file
    char* bytes = "Hello, World!";
    char* filename = "hello.txt";
    create_file(filename, (unsigned char*)bytes, strlen(bytes));
    char* argv[] = { "ipfs" , "--config", ipfs_path, "add", filename};
    struct CliArguments args;
    args.argc = 5;
    args.argv = argv;
    args.config_dir = ipfs_path;
    args.verb_index = 3;
    ipfs_import_files(&args);

	if (pthread_create(&thread2, NULL, test_daemon_start, (void*)ipfs_path) < 0) {
		libp2p_logger_error("test_bitswap", "Unable to start thread 2\n");
		goto exit;
	}

    sleep(120);

	retVal = 1;
	exit:
	ipfs_daemon_stop();
	if (peer_id_2 != NULL)
		free(peer_id_2);
	return retVal;
}

