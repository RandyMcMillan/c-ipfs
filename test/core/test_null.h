#include <stdlib.h>
#include <pthread.h>
#include "ipfs/importer/importer.h"

int test_null_add_provider() {
	int retVal = 0;
	char* peer_id_1;
	char* peer_id_2;
	struct IpfsNode *local_node2 = NULL;
	pthread_t thread1, thread2;
	int thread1_started = 0, thread2_started = 0;
	struct MultiAddress* ma_peer1;
	char* ipfs_path = "./tmp/test1";

	// create peer 1 that will be the "server" for this test
	os_utils_setenv("IPFS_PATH", ipfs_path, 1);
	drop_and_build_repository(ipfs_path, 4001, NULL, &peer_id_1);
	char multiaddress_string[255];
	sprintf(multiaddress_string, "/ip4/127.0.0.1/tcp/4001/ipfs/%s", peer_id_1);
	ma_peer1 = multiaddress_new_from_string(multiaddress_string);
	// start the daemon in a separate thread
	if (pthread_create(&thread1, NULL, test_daemon_start, (void*)ipfs_path) < 0)
		goto exit;
	thread1_started = 1;

	// create peer 2 that will be the "client" for this test
	ipfs_path = "./tmp/test2";
	os_utils_setenv("IPFS_PATH", ipfs_path, 1);
	struct Libp2pVector* ma_vector = libp2p_utils_vector_new(1);
	libp2p_utils_vector_add(ma_vector, ma_peer1);
	drop_and_build_repository(ipfs_path, 4002, ma_vector, &peer_id_2);
	// add a file, to prime the connection to peer 1
	//TODO: Find a better way to do this...
	size_t bytes_written = 0;
	ipfs_node_online_new(ipfs_path, &local_node2);
	struct HashtableNode* node = NULL;
	ipfs_import_file(NULL, "/home/parallels/ipfstest/hello_world.txt", &node, local_node2, &bytes_written, 0);
	ipfs_node_free(local_node2);
	// start the daemon in a separate thread
	if (pthread_create(&thread2, NULL, test_daemon_start, (void*)ipfs_path) < 0)
		goto exit;
	thread2_started = 1;
    // wait for everything to start up
    // JMJ debugging
    sleep(60);

	//TODO: verify that the server (peer 1) has the client and his file

	retVal = 1;
	exit:
	if (local_node2 != NULL)
		ipfs_node_free(local_node2);
	if (ma_peer1 != NULL)
		multiaddress_free(ma_peer1);
	ipfs_daemon_stop();
	if (thread1_started)
		pthread_join(thread1, NULL);
	if (thread2_started)
		pthread_join(thread2, NULL);
	return retVal;
}
