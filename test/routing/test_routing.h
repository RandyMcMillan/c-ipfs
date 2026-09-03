#pragma once
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "libp2p/os/utils.h"
#include "libp2p/utils/logger.h"

#include "multiaddr/multiaddr.h"

#include "ipfs/cmd/cli.h"
#include "ipfs/core/daemon.h"
#include "ipfs/core/ipfs_node.h"
#include "ipfs/routing/routing.h"
#include "ipfs/importer/importer.h"
#include "ipfs/importer/exporter.h"

#include "../test_helper.h"

/***
 * "publish" a hash to the datastore (for ipns)
 */
int test_routing_put_value() {
	int retVal = 0;
	char* ipfs_path_publisher = "./tmp/ipfs_1";
	char* peer_id_publisher = NULL;
	struct MultiAddress* ma_publisher = NULL;
	pthread_t thread_publisher;
	int publisher_thread_started = 0;
	struct CliArguments* arguments = NULL;

	libp2p_logger_add_class("test_routing");
	libp2p_logger_add_class("api");
	// fire up the "publisher"
	if (!drop_and_build_repository(ipfs_path_publisher, 4001, NULL, &peer_id_publisher)) {
		libp2p_logger_error("test_routing", "Unable to drop and build repository.\n");
		goto exit;
	}

	char multiaddress_string[255];
	sprintf(multiaddress_string, "/ip4/127.0.0.1/tcp/4001/ipfs/%s", peer_id_publisher);
	ma_publisher = multiaddress_new_from_string(multiaddress_string);
	if (pthread_create(&thread_publisher, NULL, test_daemon_start, (void*)ipfs_path_publisher) < 0) {
		libp2p_logger_error("test_routing", "Unable to start first daemon.\n");
		goto exit;
	}
	publisher_thread_started = 1;

	sleep(3);

	// import the file into the "publisher"
	char* args[] = { "ipfs", "--config", ipfs_path_publisher, "add", "-r", "~/site"};
	arguments = cli_arguments_new(6, args);
	ipfs_import_files(arguments);
	cli_arguments_free(arguments);

	// now "publish" to publisher
	libp2p_logger_debug("test_routing", "About to publish to publisher.\n");
	char* args2[] = {"ipfs", "--config", ipfs_path_publisher, "name", "publish", "QmZtAEqmnXMZkwVPKdyMGxUoo35cQMzNhmq6CN3DvgRwAD" };
	arguments = cli_arguments_new(6, args2);
	if (!ipfs_name(arguments))
		goto exit;
	cli_arguments_free(arguments);
	arguments = NULL;

	// see if we have what we should...
	libp2p_logger_debug("test_routing", "About to ask for the server to resolve the publisher.\n");
	char ipns[126];
	sprintf(ipns, "/ipns/%s", peer_id_publisher);
	char* args3[] = {"ipfs", "--config", ipfs_path_publisher, "name", "resolve", ipns};
	arguments = cli_arguments_new(6, args3);
	if (!ipfs_name(arguments))
		goto exit;

	retVal = 1;
	exit:
	ipfs_daemon_stop();
	if (publisher_thread_started) {
		pthread_join(thread_publisher, NULL);
	}
	multiaddress_free(ma_publisher);
	cli_arguments_free(arguments);
	return retVal;
}

/***
 * "publish" a hash to the datastore (for ipns)
 */
int test_routing_put_value_and_resolve() {
	int retVal = 0;
	char* ipfs_path_publisher = "./tmp/ipfs_1";
	char* peer_id_publisher = NULL;
	struct MultiAddress* ma_publisher = NULL;
	pthread_t thread_publisher;
	int publisher_thread_started = 0;
	pthread_t thread_consumer;
	char* ipfs_path_consumer = "./tmp/ipfs_2";
	char* peer_id_consumer = NULL;
	int consumer_thread_started = 0;
	struct Libp2pVector* ma_vector = NULL;
	struct CliArguments* arguments = NULL;

	libp2p_logger_add_class("test_routing");

	// fire up the "publisher"
	if (!drop_and_build_repository(ipfs_path_publisher, 4001, NULL, &peer_id_publisher)) {
		libp2p_logger_error("test_routing", "Unable to drop and build repository.\n");
		goto exit;
	}

	char multiaddress_string[255];
	sprintf(multiaddress_string, "/ip4/127.0.0.1/tcp/4001/ipfs/%s", peer_id_publisher);
	ma_publisher = multiaddress_new_from_string(multiaddress_string);
	if (pthread_create(&thread_publisher, NULL, test_daemon_start, (void*)ipfs_path_publisher) < 0) {
		libp2p_logger_error("test_routing", "Unable to start first daemon.\n");
		goto exit;
	}
	publisher_thread_started = 1;

	sleep(3);

	// import the file into the "publisher"
	char* args[] = { "ipfs", "--config", ipfs_path_publisher, "add", "-r", "~/site"};
	arguments = cli_arguments_new(6, args);
	ipfs_import_files(arguments);
	cli_arguments_free(arguments);

	// fire up the "consumer"
	ma_vector = libp2p_utils_vector_new(1);
	libp2p_utils_vector_add(ma_vector, ma_publisher);
	drop_and_build_repository(ipfs_path_consumer, 4002, ma_vector, &peer_id_consumer);
	if (pthread_create(&thread_consumer, NULL, test_daemon_start, (void*)ipfs_path_consumer) < 0)
		goto exit;
	consumer_thread_started = 1;
	// wait for everything to fire up
	sleep(3);

	// now "publish" to publisher
	libp2p_logger_debug("test_routing", "About to publish to publisher.\n");
	char* args2[] = {"ipfs", "--config", ipfs_path_publisher, "name", "publish", "QmZtAEqmnXMZkwVPKdyMGxUoo35cQMzNhmq6CN3DvgRwAD" };
	arguments = cli_arguments_new(6, args2);
	if (!ipfs_name(arguments))
		goto exit;
	cli_arguments_free(arguments);
	arguments = NULL;

	// see if we have what we should...
	libp2p_logger_debug("test_routing", "About to ask for the server to resolve the publisher.\n");
	char* args3[] = {"ipfs", "--config", ipfs_path_consumer, "resolve", peer_id_publisher};
	//char* results = NULL;
	arguments = cli_arguments_new(5, args3);
	if (!ipfs_name(arguments))
		goto exit;

	retVal = 1;
	exit:
	ipfs_daemon_stop();
	if (publisher_thread_started) {
		pthread_join(thread_publisher, NULL);
	}
	if (consumer_thread_started) {
		pthread_join(thread_consumer, NULL);
	}
	multiaddress_free(ma_publisher);
	cli_arguments_free(arguments);
	return retVal;
}

int test_routing_find_peer() {
	int retVal = 0;
	char* ipfs_path1 = "./tmp/ipfs_1";
	char* ipfs_path2 = "./tmp/ipfs_2";
	char* ipfs_path3 = "./tmp/ipfs_3";
	pthread_t thread1;
	pthread_t thread2;
	pthread_t thread3;
	int thread1_started = 0, thread2_started = 0, thread3_started = 0;
	char* peer_id_1 = NULL;
	char* peer_id_2 = NULL;
	char* peer_id_3 = NULL;
	struct IpfsNode* local_node = NULL;
	struct IpfsNode* local_node2 = NULL;
	struct IpfsNode* local_node3 = NULL;
	struct FSRepo* fs_repo = NULL;
	struct MultiAddress* ma_peer1 = NULL;
	struct Libp2pVector *ma_vector = NULL;
	struct Libp2pPeer* result = NULL;
	struct HashtableNode *node = NULL;

	// create peer 1
	drop_and_build_repository(ipfs_path1, 4001, NULL, &peer_id_1);
	char multiaddress_string[255];
	sprintf(multiaddress_string, "/ip4/127.0.0.1/tcp/4001/ipfs/%s", peer_id_1);
	ma_peer1 = multiaddress_new_from_string(multiaddress_string);
	// start the daemon in a separate thread
	if (pthread_create(&thread1, NULL, test_daemon_start, (void*)ipfs_path1) < 0)
		goto exit;
	thread1_started = 1;

	sleep(3);

	// create peer 2
	ma_vector = libp2p_utils_vector_new(1);
	libp2p_utils_vector_add(ma_vector, ma_peer1);
	drop_and_build_repository(ipfs_path2, 4002, ma_vector, &peer_id_2);
	// start the daemon in a separate thread
	if (pthread_create(&thread2, NULL, test_daemon_start, (void*)ipfs_path2) < 0)
		goto exit;
	thread2_started = 1;

	// wait for everything to start up
	sleep(3);

	// add a file to peer 2
	char* hello_text = "Hello, World!";
	create_file("./tmp/hello.txt", (uint8_t*)hello_text, strlen(hello_text));
	size_t bytes_written = 0;
	if (!ipfs_node_offline_new(ipfs_path2, &local_node2)) {
		fprintf(stderr, "Unable to create offline node for %s\n", ipfs_path2);
		goto exit;
	}
	ipfs_import_file(NULL, "./tmp/hello.txt", &node, local_node2, &bytes_written, 0);
	ipfs_node_free(local_node2);

	// create my peer, peer 3
	libp2p_utils_vector_add(ma_vector, ma_peer1);
	drop_and_build_repository(ipfs_path3, 4003, ma_vector, &peer_id_3);
	// start the daemon in a separate thread
	if (pthread_create(&thread3, NULL, test_daemon_start, (void*)ipfs_path3) < 0)
		goto exit;
	thread3_started = 1;

	sleep(3);

	if (!ipfs_node_offline_new(ipfs_path3, &local_node3)) {
		fprintf(stderr, "Unable to create offline node for %s\n", ipfs_path3);
		goto exit;
	}
	int peer2_len = strlen(peer_id_2);

	if (!local_node3->routing->FindPeer(local_node3->routing, (unsigned char*)peer_id_2, peer2_len, &result)) {
		fprintf(stderr, "Unable to find peer %s by asking %s\n", peer_id_2, peer_id_1);
		goto exit;
	}

	if (result == NULL) {
		fprintf(stderr, "Result was NULL\n");
		goto exit;
	}

	struct MultiAddress *remote_address = result->addr_head->item;
	fprintf(stderr, "Remote address is: %s\n", remote_address->string);

	retVal = 1;
	exit:
	ipfs_daemon_stop();
	if (thread1_started)
		pthread_join(thread1, NULL);
	if (thread2_started)
		pthread_join(thread2, NULL);
	if (thread3_started)
		pthread_join(thread3, NULL);
	if (peer_id_1 != NULL)
		free(peer_id_1);
	if (peer_id_2 != NULL)
		free(peer_id_2);
	if (peer_id_3 != NULL)
		free(peer_id_3);
	if (fs_repo != NULL)
		ipfs_repo_fsrepo_free(fs_repo);
	if (ma_peer1 != NULL)
		multiaddress_free(ma_peer1);
	if (ma_vector != NULL)
		libp2p_utils_vector_free(ma_vector);
	if (node != NULL)
		ipfs_hashtable_node_free(node);
	if (local_node != NULL)
		ipfs_node_free(local_node);
	if (result != NULL)
		libp2p_peer_free(result);

	return retVal;
}

int test_routing_find_providers() {
	int retVal = 0;
	char* ipfs_path1 = "./tmp/test_findprov_1";
	char* ipfs_path2 = "./tmp/test_findprov_2";
	char* ipfs_path3 = "./tmp/test_findprov_3";
	char* peer_id_1 = NULL;
	char* peer_id_2 = NULL;
	char* peer_id_3 = NULL;
	//struct IpfsNode *local_node1 = NULL;
	struct IpfsNode *local_node2 = NULL;
	struct IpfsNode *local_node3 = NULL;
	char* remote_peer_id = NULL;
	pthread_t thread1, thread2;
	int thread1_started = 0, thread2_started = 0;
	struct MultiAddress* ma_peer1 = NULL;
	struct Libp2pVector* ma_vector2 = NULL, *ma_vector3 = NULL;
    struct FSRepo* fs_repo = NULL;
    struct HashtableNode* node = NULL;
    struct Libp2pVector* result = NULL;

	// create peer 1
	drop_and_build_repository(ipfs_path1, 4001, NULL, &peer_id_1);
	char multiaddress_string[255];
	sprintf(multiaddress_string, "/ip4/127.0.0.1/tcp/4001/ipfs/%s", peer_id_1);
	ma_peer1 = multiaddress_new_from_string(multiaddress_string);
	// start the daemon in a separate thread
	if (pthread_create(&thread1, NULL, test_daemon_start, (void*)ipfs_path1) < 0) {
		fprintf(stderr, "Unable to start thread 1\n");
		goto exit;
	}
	thread1_started = 1;

	sleep(3);

	// create peer 2
	// create a vector to hold peer1's multiaddress so we can connect as a peer
	ma_vector2 = libp2p_utils_vector_new(1);
	libp2p_utils_vector_add(ma_vector2, ma_peer1);
	// note: this destroys some things, as it frees the fs_repo:
	drop_and_build_repository(ipfs_path2, 4002, ma_vector2, &peer_id_2);
	// start the daemon in a separate thread
	if (pthread_create(&thread2, NULL, test_daemon_start, (void*)ipfs_path2) < 0) {
		fprintf(stderr, "Unable to start thread 2\n");
		goto exit;
	}
	thread2_started = 1;

    // wait for everything to start up
    // JMJ debugging =
    sleep(3);

	// add a file, to prime the connection to peer 1
	size_t bytes_written = 0;
	uint8_t *hello_bytes = (unsigned char*)"hello, world!\n";
	create_file("./tmp/hello_world.txt", hello_bytes, strlen((char*)hello_bytes));
	if (!ipfs_node_offline_new(ipfs_path2, &local_node2)) {
		fprintf(stderr, "Unable to create offline node for %s\n", ipfs_path2);
		goto exit;
	}
	ipfs_import_file(NULL, "./tmp/hello_world.txt", &node, local_node2, &bytes_written, 0);
	ipfs_node_free(local_node2);

	// create my peer, peer 3
	ma_peer1 = multiaddress_new_from_string(multiaddress_string);
	ma_vector3 = libp2p_utils_vector_new(1);
	libp2p_utils_vector_add(ma_vector3, ma_peer1);
	drop_and_build_repository(ipfs_path3, 4003, ma_vector3, &peer_id_3);

	if (!ipfs_node_offline_new(ipfs_path3, &local_node3)) {
		fprintf(stderr, "Unable to create offline node for %s\n", ipfs_path3);
		goto exit;
	}

    if (!local_node3->routing->FindProviders(local_node3->routing, node->hash, node->hash_size, &result)) {
    		fprintf(stderr, "Unable to find a provider\n");
    		goto exit;
    }

	if (result == NULL) {
		fprintf(stderr, "Provider array is NULL\n");
		goto exit;
	}

	// connect to peer 2
	struct Libp2pPeer *remote_peer = NULL;
	for(int i = 0; i < result->total; i++) {
		remote_peer = (struct Libp2pPeer*)libp2p_utils_vector_get(result, i);
		if (remote_peer->connection_type == CONNECTION_TYPE_CONNECTED
				|| libp2p_peer_connect(local_node3->dialer, remote_peer, local_node3->peerstore, local_node3->repo->config->datastore, 5)) {
			break;
		}
		remote_peer = NULL;
	}

	if (remote_peer == NULL) {
		fprintf(stderr, "Remote Peer is NULL\n");
		goto exit;
	}

	remote_peer_id = malloc(remote_peer->id_size + 1);
	memcpy(remote_peer_id, remote_peer->id, remote_peer->id_size);
	remote_peer_id[remote_peer->id_size] = 0;
	fprintf(stderr, "Remote address is: %s\n", remote_peer_id);
	free(remote_peer_id);
	remote_peer_id = NULL;

	retVal = 1;
	exit:
	ipfs_daemon_stop();
	if (fs_repo != NULL)
		ipfs_repo_fsrepo_free(fs_repo);
	if (peer_id_1 != NULL)
		free(peer_id_1);
	if (peer_id_2 != NULL)
		free(peer_id_2);
	if (peer_id_3 != NULL)
		free(peer_id_3);
	if (thread1_started)
		pthread_join(thread1, NULL);
	if (thread2_started)
		pthread_join(thread2, NULL);
	if (ma_vector2 != NULL) {
		libp2p_utils_vector_free(ma_vector2);
	}
	if (ma_vector3 != NULL) {
		libp2p_utils_vector_free(ma_vector3);
	}
	if (local_node3 != NULL) {
		ipfs_node_free(local_node3);
	}
	if (node != NULL)
		ipfs_hashtable_node_free(node);
	if (result != NULL) {
		// we have a vector of peers. Clean 'em up:
		/* free the vector, not the peers.
		for(int i = 0; i < result->total; i++) {
			struct Libp2pPeer* p = (struct Libp2pPeer*)libp2p_utils_vector_get(result, i);
			libp2p_peer_free(p);
		}
		*/
		libp2p_utils_vector_free(result);
	}
	return retVal;

}

/***
 * Fire up a "client" and "server" and let the client tell the server he's providing a file
 */
int test_routing_provide() {
	int retVal = 0;
	// clean out repository
	char* ipfs_path = "./tmp/test1";
	os_utils_setenv("IPFS_PATH", ipfs_path, 1);
	char* peer_id_1 = NULL;
	char* peer_id_2 = NULL;
	struct IpfsNode *local_node2 = NULL;
	pthread_t thread1, thread2;
	int thread1_started = 0, thread2_started = 0;
	struct MultiAddress* ma_peer1 = NULL;
	struct Libp2pVector* ma_vector2 = NULL;
	struct HashtableNode* node = NULL;

	libp2p_logger_add_class("daemon");
	libp2p_logger_add_class("null");

	// create peer 1
	drop_and_build_repository(ipfs_path, 4001, NULL, &peer_id_1);
	char multiaddress_string[255];
	sprintf(multiaddress_string, "/ip4/127.0.0.1/tcp/4001/ipfs/%s", peer_id_1);
	ma_peer1 = multiaddress_new_from_string(multiaddress_string);
	// start the daemon in a separate thread
	if (pthread_create(&thread1, NULL, test_daemon_start, (void*)ipfs_path) < 0) {
		fprintf(stderr, "Unable to start thread 1\n");
		goto exit;
	}
	thread1_started = 1;

	// create peer 2
	ipfs_path = "./tmp/test2";
	os_utils_setenv("IPFS_PATH", ipfs_path, 1);
	// create a vector to hold peer1's multiaddress so we can connect as a peer
	ma_vector2 = libp2p_utils_vector_new(1);
	libp2p_utils_vector_add(ma_vector2, ma_peer1);
	// note: this distroys some things, as it frees the fs_repo:
	drop_and_build_repository(ipfs_path, 4002, ma_vector2, &peer_id_2);
	// add a file, to prime the connection to peer 1
	//TODO: Find a better way to do this...
	size_t bytes_written = 0;
	if (!ipfs_node_offline_new(ipfs_path, &local_node2)) {
		fprintf(stderr, "Unable to create offline node for %s\n", ipfs_path);
		goto exit;
	}
	uint8_t *bytes = (unsigned char*)"hello, world!\n";
	char* filename = "test1.txt";
	create_file(filename, bytes, strlen((char*)bytes));
	ipfs_import_file(NULL, filename, &node, local_node2, &bytes_written, 0);
	ipfs_node_free(local_node2);
	// start the daemon in a separate thread
	if (pthread_create(&thread2, NULL, test_daemon_start, (void*)ipfs_path) < 0) {
		fprintf(stderr, "Unable to start thread 2\n");
		goto exit;
	}
	thread2_started = 1;

    // wait for everything to start up
    sleep(3);

	retVal = 1;
	exit:
	ipfs_daemon_stop();
	if (peer_id_1 != NULL)
		free(peer_id_1);
	if (peer_id_2 != NULL)
		free(peer_id_2);
	if (thread1_started)
		pthread_join(thread1, NULL);
	if (thread2_started)
		pthread_join(thread2, NULL);
	if (ma_vector2 != NULL) {
		libp2p_utils_vector_free(ma_vector2);
	}
	if (node != NULL)
		ipfs_hashtable_node_free(node);
	if (ma_peer1 != NULL)
		multiaddress_free(ma_peer1);
	return retVal;

}

/***
 * Attempt to retrieve a file from a previously unknown node
 */
int test_routing_retrieve_file_third_party() {
	int retVal = 0;
	char* ipfs_path_1 = "./tmp/ipfs_1", *ipfs_path_2 = "./tmp/ipfs_2", *ipfs_path_3 = "./tmp/ipfs_3";
	char* peer_id_1 = NULL, *peer_id_2 = NULL, *peer_id_3 = NULL;
	struct IpfsNode* ipfs_node2 = NULL, *ipfs_node3 = NULL;
	pthread_t thread1, thread2, thread3;
	int thread1_started = 0, thread2_started = 0, thread3_started = 0;
	struct MultiAddress* ma_peer1 = NULL;
	struct Libp2pVector* ma_vector2 = NULL, *ma_vector3 = NULL;
	struct HashtableNode* node = NULL, *result_node = NULL;
	char multiaddress_string[255] = "";
	char hash[256] = "";

	/*
	libp2p_logger_add_class("online");
	libp2p_logger_add_class("offline");
	libp2p_logger_add_class("multistream");
	libp2p_logger_add_class("null");
	libp2p_logger_add_class("dht_protocol");
	libp2p_logger_add_class("providerstore");
	libp2p_logger_add_class("peerstore");
	libp2p_logger_add_class("exporter");
	libp2p_logger_add_class("peer");
	libp2p_logger_add_class("test_routing");
	libp2p_logger_add_class("api");
	libp2p_logger_add_class("secio");
	*/

	// clean out repository

	// create peer 1
	if (!drop_and_build_repository(ipfs_path_1, 4001, NULL, &peer_id_1))
		goto exit;
	sprintf(multiaddress_string, "/ip4/127.0.0.1/tcp/4001/ipfs/%s", peer_id_1);
	ma_peer1 = multiaddress_new_from_string(multiaddress_string);
	// start the daemon in a separate thread
	libp2p_logger_debug("test_routing", "Firing up daemon 1, our central server. Named: %s.\n", peer_id_1);
	if (pthread_create(&thread1, NULL, test_daemon_start, (void*)ipfs_path_1) < 0) {
		libp2p_logger_error("test_routing", "Unable to start thread 1\n");
		goto exit;
	}
	thread1_started = 1;

    // create peer 2, that will host the file
	// create a vector to hold peer1's multiaddress so we can connect as a peer
	ma_vector2 = libp2p_utils_vector_new(1);
	libp2p_utils_vector_add(ma_vector2, ma_peer1);
	// note: this destroys some things, as it frees the fs_repo:
	drop_and_build_repository(ipfs_path_2, 4002, ma_vector2, &peer_id_2);
	multiaddress_free(ma_peer1);
	// start the daemon in a separate thread
	libp2p_logger_debug("test_routing", "Firing up daemon 2, which will host the file. Named %s.\n", peer_id_2);
	if (pthread_create(&thread2, NULL, test_daemon_start, (void*)ipfs_path_2) < 0) {
		libp2p_logger_error("test_routing", "Unable to start thread 2.\n");
		goto exit;
	}
	thread2_started = 1;

    // wait for everything to start up
    sleep(3);

	//TODO: add a file to server 2
	uint8_t *bytes = (unsigned char*)"hello, world!\n";
	char* filename = "test1.txt";
	create_file(filename, bytes, strlen((char*)bytes));
	size_t bytes_written;
	ipfs_node_offline_new(ipfs_path_2, &ipfs_node2);
	ipfs_import_file(NULL, filename, &node, ipfs_node2, &bytes_written, 0);
	memset(hash, 0, 256);
	ipfs_cid_hash_to_base58(node->hash, node->hash_size, (unsigned char*)hash, 256);
	libp2p_logger_debug("test_routing", "Inserted file with hash %s into server %s.\n", hash, peer_id_2);
	ipfs_node_free(ipfs_node2);

    // wait for everything to start up
    sleep(3);

    // create my peer, peer 3
	ma_peer1 = multiaddress_new_from_string(multiaddress_string);
	ma_vector3 = libp2p_utils_vector_new(1);
	libp2p_utils_vector_add(ma_vector3, ma_peer1);
	drop_and_build_repository(ipfs_path_3, 4003, ma_vector3, &peer_id_3);
	multiaddress_free(ma_peer1);
	libp2p_logger_debug("test_routing", "Firing up daemon 3, which is our leech. Named: %s.\n", peer_id_3);
	if (pthread_create(&thread3, NULL, test_daemon_start, (void*)ipfs_path_3) < 0) {
		libp2p_logger_error("test_routing", "Unable to start thread 3.\n");
		goto exit;
	}
	thread3_started = 1;

	sleep(3);

	//now have peer 3 ask for a file that is on peer 2, but peer 3 only knows of peer 1
	ipfs_node_offline_new(ipfs_path_3, &ipfs_node3);

	libp2p_logger_debug("test_routing", "Client of daemon 3 will now attempt to look for %s.\n", hash);

    if (!ipfs_exporter_get_node(ipfs_node3, node->hash, node->hash_size, &result_node)) {
    		libp2p_logger_error("test_routing", "Get_Node returned false\n");
    		goto exit;
    }

    if (node->hash_size != result_node->hash_size) {
    		fprintf(stderr, "Node hash sizes do not match. Should be %lu but is %lu\n", node->hash_size, result_node->hash_size);
    		goto exit;
    }

    if (node->data_size != result_node->data_size) {
    		fprintf(stderr, "Result sizes do not match. Should be %lu but is %lu\n", node->data_size, result_node->data_size);
    		goto exit;
    }

	retVal = 1;
	exit:
	ipfs_daemon_stop();
	if (thread1_started)
		pthread_join(thread1, NULL);
	if (thread2_started)
		pthread_join(thread2, NULL);
	if (thread3_started)
		pthread_join(thread3, NULL);
	if (ipfs_node3 != NULL)
		ipfs_node_free(ipfs_node3);
	if (peer_id_1 != NULL)
		free(peer_id_1);
	if (peer_id_2 != NULL)
		free(peer_id_2);
	if (peer_id_3 != NULL)
		free(peer_id_3);
	if (ma_vector2 != NULL)
		libp2p_utils_vector_free(ma_vector2);
	if (ma_vector3 != NULL)
		libp2p_utils_vector_free(ma_vector3);
	if (node != NULL)
		ipfs_hashtable_node_free(node);
	if (result_node != NULL)
		ipfs_hashtable_node_free(result_node);
	return retVal;

}

/***
 * Attempt to retrieve a large file from a previously unknown node
 */
int test_routing_retrieve_large_file() {
	int retVal = 0;

	/*
	libp2p_logger_add_class("multistream");
	libp2p_logger_add_class("null");
	libp2p_logger_add_class("dht_protocol");
	libp2p_logger_add_class("providerstore");
	libp2p_logger_add_class("peerstore");
	libp2p_logger_add_class("peer");
	libp2p_logger_add_class("test_routing");
	*/

	libp2p_logger_add_class("exporter");
	libp2p_logger_add_class("online");

	// clean out repository
	char* ipfs_path = "./tmp/test1";
	char* peer_id_1 = NULL, *peer_id_2 = NULL, *peer_id_3 = NULL;
	struct IpfsNode* ipfs_node2 = NULL, *ipfs_node3 = NULL;
	pthread_t thread1, thread2;
	int thread1_started = 0, thread2_started = 0;
	struct MultiAddress* ma_peer1 = NULL;
	struct Libp2pVector* ma_vector2 = NULL, *ma_vector3 = NULL;
	struct HashtableNode* node = NULL, *result_node = NULL;
	FILE *fd;
	char* temp_file_name = "./tmp/largefile.tmp";

	unlink(temp_file_name);

	// create peer 1
	drop_and_build_repository(ipfs_path, 4001, NULL, &peer_id_1);
	char multiaddress_string[255];
	sprintf(multiaddress_string, "/ip4/127.0.0.1/tcp/4001/ipfs/%s", peer_id_1);
	ma_peer1 = multiaddress_new_from_string(multiaddress_string);
	// start the daemon in a separate thread
	libp2p_logger_debug("test_routing", "Firing up daemon 1.\n");
	if (pthread_create(&thread1, NULL, test_daemon_start, (void*)ipfs_path) < 0) {
		fprintf(stderr, "Unable to start thread 1\n");
		goto exit;
	}
	thread1_started = 1;

    // wait for everything to start up
    // JMJ debugging =
    sleep(3);

    // create peer 2
	ipfs_path = "./tmp/test2";
	// create a vector to hold peer1's multiaddress so we can connect as a peer
	ma_vector2 = libp2p_utils_vector_new(1);
	libp2p_utils_vector_add(ma_vector2, ma_peer1);
	// note: this distroys some things, as it frees the fs_repo_3:
	drop_and_build_repository(ipfs_path, 4002, ma_vector2, &peer_id_2);
	multiaddress_free(ma_peer1);
	// add a file, to prime the connection to peer 1
	//TODO: Find a better way to do this...
	size_t bytes_written = 0;
	if (!ipfs_node_online_new(ipfs_path, &ipfs_node2))
		goto exit;
	ipfs_node2->routing->Bootstrap(ipfs_node2->routing);
	ipfs_import_file(NULL, "/home/parallels/ipfstest/test_import_large.tmp", &node, ipfs_node2, &bytes_written, 0);
	ipfs_node_free(ipfs_node2);
	// start the daemon in a separate thread
	libp2p_logger_debug("test_routing", "Firing up daemon 2.\n");
	if (pthread_create(&thread2, NULL, test_daemon_start, (void*)ipfs_path) < 0) {
		fprintf(stderr, "Unable to start thread 2\n");
		goto exit;
	}
	thread2_started = 1;

    // wait for everything to start up
    // JMJ debugging =
    sleep(3);

    // see if we get the entire file
    libp2p_logger_debug("test_routing", "Firing up the 3rd client\n");
    // create my peer, peer 3
	ipfs_path = "./tmp/test3";
	ma_peer1 = multiaddress_new_from_string(multiaddress_string);
	ma_vector3 = libp2p_utils_vector_new(1);
	libp2p_utils_vector_add(ma_vector3, ma_peer1);
	drop_and_build_repository(ipfs_path, 4003, ma_vector3, &peer_id_3);
	multiaddress_free(ma_peer1);
	ipfs_node_online_new(ipfs_path, &ipfs_node3);

    ipfs_node3->routing->Bootstrap(ipfs_node3->routing);


    fd = fopen(temp_file_name, "w+");
    ipfs_exporter_object_cat_to_file(ipfs_node3, node->hash, node->hash_size, fd);
    fclose(fd);

    struct stat buf;
    stat(temp_file_name, &buf);

    if (buf.st_size != 1000000) {
    	fprintf(stderr, "File size should be 1000000, but is %lu\n", (unsigned long)buf.st_size);
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
	if (result_node != NULL)
		ipfs_hashtable_node_free(result_node);
	return retVal;

}

#include "libp2p/routing/dht_utils.h"

int test_dht_utils_xor_distance() {
	unsigned char a[DHT_ID_SIZE] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		                               0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
		                               0x10, 0x11, 0x12, 0x13};
	unsigned char b[DHT_ID_SIZE] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		                               0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
		                               0x10, 0x11, 0x12, 0x13};
	unsigned char c[DHT_ID_SIZE] = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8,
		                               0xF7, 0xF6, 0xF5, 0xF4, 0xF3, 0xF2, 0xF1, 0xF0,
		                               0xEF, 0xEE, 0xED, 0xEC};
	unsigned char d[DHT_ID_SIZE] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		                               0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
		                               0x10, 0x11, 0x12, 0xFF};

	unsigned char dist_ab[DHT_ID_SIZE];
	unsigned char dist_ac[DHT_ID_SIZE];
	unsigned char dist_ad[DHT_ID_SIZE];

	libp2p_routing_dht_xor_distance(a, b, dist_ab);
	libp2p_routing_dht_xor_distance(a, c, dist_ac);
	libp2p_routing_dht_xor_distance(a, d, dist_ad);

	// a XOR a should be 0
	for (int i = 0; i < DHT_ID_SIZE; i++) {
		if (dist_ab[i] != 0) return 0;
	}

	// a XOR c should be all 0xFF
	for (int i = 0; i < DHT_ID_SIZE; i++) {
		if (dist_ac[i] != 0xFF) return 0;
	}

	// a XOR d should have only last byte non-zero
	for (int i = 0; i < DHT_ID_SIZE - 1; i++) {
		if (dist_ad[i] != 0) return 0;
	}
	if (dist_ad[DHT_ID_SIZE - 1] != (0x13 ^ 0xFF)) return 0;

	// Distance ordering: ad < ac
	if (libp2p_routing_dht_distance_cmp(dist_ad, dist_ac) >= 0) return 0;
	// ab < ad
	if (libp2p_routing_dht_distance_cmp(dist_ab, dist_ad) >= 0) return 0;

	return 1;
}
