#include <pthread.h>

#include "../test_helper.h"
#include "libp2p/utils/logger.h"
#include "ipfs/cmd/cli.h"
#include "ipfs/core/client_api.h"
#include "ipfs/core/daemon.h"
#include "ipfs/core/http_request.h"
#include "ipfs/importer/exporter.h"
#include "ipfs/importer/importer.h"
#include "ipfs/namesys/name.h"

int test_core_api_startup_shutdown() {
	char* repo_path = "/tmp/ipfs_1";
	char* peer_id = NULL;
	int retVal = 0;
	pthread_t daemon_thread = 0;
	struct IpfsNode* client_node = NULL;

	//libp2p_logger_add_class("api");

	if (!drop_and_build_repository(repo_path, 4001, NULL, &peer_id))
		goto exit;

	// this should start the api
	pthread_create(&daemon_thread, NULL, test_daemon_start, repo_path);
	sleep(3);

	// make a client to the api
	if (!ipfs_node_offline_new(repo_path, &client_node)) {
		goto exit;
	}

	// test to see if it is working
	if (client_node->mode != MODE_API_AVAILABLE) {
		libp2p_logger_error("test_api", "API Not available.\n");
		goto exit;
	}

	retVal = 1;
	// cleanup
	exit:
	ipfs_daemon_stop();
	pthread_join(daemon_thread, NULL);
	if (peer_id != NULL)
		free(peer_id);

	return retVal;
}

/***
 * Attempt to get a file over the "network" using the api
 */
int test_core_api_object_cat() {
	int retVal = 0;
	pthread_t daemon_thread1;
	pthread_t daemon_thread2;
	int thread_started1 = 0;
	int thread_started2 = 0;
	char* ipfs_path1 = "/tmp/ipfs_1";
	char* config_file1 = "config.test1.wo_journal";
	char* ipfs_path2 = "/tmp/ipfs_2";
	char* config_file2 = "config.test2.wo_journal";
	struct FSRepo* fs_repo = NULL;
	char hash[256] = "";
	char* args[] = {"ipfs", "--config", ipfs_path2, "cat", hash };
	struct CliArguments* arguments = NULL;

	// logging
	libp2p_logger_add_class("test_api");
	libp2p_logger_add_class("journal");
	libp2p_logger_add_class("daemon");
	libp2p_logger_add_class("online");
	libp2p_logger_add_class("peer");
	libp2p_logger_add_class("null");
	libp2p_logger_add_class("replication");
	libp2p_logger_add_class("fs_repo");
	libp2p_logger_add_class("lmdb_journalstore");
	libp2p_logger_add_class("lmdb_datastore");
	libp2p_logger_add_class("secio");
	libp2p_logger_add_class("socket");
	libp2p_logger_add_class("protocol");
	libp2p_logger_add_class("dht_protocol");
	libp2p_logger_add_class("resolver");
	libp2p_logger_add_class("unixfs");
	libp2p_logger_add_class("bitswap_engine");
	libp2p_logger_add_class("bitswap_network");
	libp2p_logger_add_class("exporter");

	// build 2 repos
	if (!drop_build_open_repo(ipfs_path1, &fs_repo, config_file1)) {
		ipfs_repo_fsrepo_free(fs_repo);
		libp2p_logger_error("test_api", "Unable to drop and build repository at %s\n", ipfs_path1);
		goto exit;
	}
	libp2p_logger_debug("test_api", "Changed the server id to %s.\n", fs_repo->config->identity->peer->id);
	ipfs_repo_fsrepo_free(fs_repo);
	if (!drop_build_open_repo(ipfs_path2, &fs_repo, config_file2)) {
		ipfs_repo_fsrepo_free(fs_repo);
		libp2p_logger_error("test_api", "Unable to drop and build repository at %s\n", ipfs_path2);
		goto exit;
	}
	libp2p_logger_debug("test_api", "Changed the server id to %s.\n", fs_repo->config->identity->peer->id);
	ipfs_repo_fsrepo_free(fs_repo);

	// add a file to the first repo
	uint8_t *bytes = (unsigned char*)"hello, world!\n";
	char* filename = "/tmp/test1.txt";
	create_file(filename, bytes, strlen((char*)bytes));
	struct HashtableNode* node;
	size_t bytes_written;
	struct IpfsNode *local_node = NULL;
	ipfs_node_offline_new(ipfs_path1, &local_node);
	ipfs_import_file(NULL, filename, &node, local_node, &bytes_written, 0);
	memset(hash, 0, 256);
	ipfs_cid_hash_to_base58(node->hash, node->hash_size, (unsigned char*)hash, 256);
	libp2p_logger_debug("test_api", "Inserted file with hash %s.\n", hash);
	ipfs_node_free(local_node);
	ipfs_hashtable_node_free(node);

	libp2p_logger_debug("test_api", "*** Firing up daemons ***\n");
	pthread_create(&daemon_thread1, NULL, test_daemon_start, (void*)ipfs_path1);
	thread_started1 = 1;
	sleep(3);
	pthread_create(&daemon_thread2, NULL, test_daemon_start, (void*)ipfs_path2);
	thread_started2 = 1;

	sleep(3);

	// use a client to ask for the file on server 1
	arguments = cli_arguments_new(5, args);
	if (ipfs_exporter_object_cat(arguments, stdout) == 0) {
		libp2p_logger_error("test_api", "ipfs_exporter_object_cat returned false.\n");
		goto exit;
	}

	retVal = 1;
	exit:
	ipfs_daemon_stop();
	if (thread_started1)
		pthread_join(daemon_thread1, NULL);
	if (thread_started2)
		pthread_join(daemon_thread2, NULL);
	cli_arguments_free(arguments);
	return retVal;
}

/***
 * Attempt to get a binary file over the api
 */
int test_core_api_object_cat_binary() {
	int retVal = 0;
	pthread_t daemon_thread1;
	int thread_started1 = 0;
	char* ipfs_path1 = "/tmp/ipfs_1";
	char* config_file1 = "config.test1.wo_journal";
	struct FSRepo* fs_repo = NULL;
	char hash[256] = "";
	char* args[] = {"ipfs", "--config", ipfs_path1, "cat", hash };
	struct CliArguments* arguments = NULL;

	// logging
	libp2p_logger_add_class("test_api");
	libp2p_logger_add_class("journal");
	libp2p_logger_add_class("daemon");
	libp2p_logger_add_class("online");
	libp2p_logger_add_class("peer");
	libp2p_logger_add_class("null");
	libp2p_logger_add_class("replication");
	libp2p_logger_add_class("fs_repo");
	libp2p_logger_add_class("lmdb_journalstore");
	libp2p_logger_add_class("lmdb_datastore");
	libp2p_logger_add_class("secio");
	libp2p_logger_add_class("socket");
	libp2p_logger_add_class("protocol");
	libp2p_logger_add_class("dht_protocol");
	libp2p_logger_add_class("resolver");
	libp2p_logger_add_class("unixfs");
	libp2p_logger_add_class("bitswap_engine");
	libp2p_logger_add_class("bitswap_network");
	libp2p_logger_add_class("exporter");
	libp2p_logger_add_class("api");

	// build repo
	if (!drop_build_open_repo(ipfs_path1, &fs_repo, config_file1)) {
		ipfs_repo_fsrepo_free(fs_repo);
		libp2p_logger_error("test_api", "Unable to drop and build repository at %s\n", ipfs_path1);
		goto exit;
	}
	libp2p_logger_debug("test_api", "Changed the server id to %s.\n", fs_repo->config->identity->peer->id);
	ipfs_repo_fsrepo_free(fs_repo);

	// add a file to the repo
	uint8_t bytes[256];
	for(int i = 0; i < 256; i++)
		bytes[i] = i;
	char* filename = "test1.txt";
	create_file(filename, bytes, 256);
	struct HashtableNode* node;
	size_t bytes_written;
	struct IpfsNode *local_node = NULL;
	ipfs_node_offline_new(ipfs_path1, &local_node);
	ipfs_import_file(NULL, filename, &node, local_node, &bytes_written, 0);
	memset(hash, 0, 256);
	ipfs_cid_hash_to_base58(node->hash, node->hash_size, (unsigned char*)hash, 256);
	libp2p_logger_debug("test_api", "Inserted file with hash %s.\n", hash);
	ipfs_node_free(local_node);
	ipfs_hashtable_node_free(node);

	libp2p_logger_debug("test_api", "*** Firing up daemons ***\n");
	pthread_create(&daemon_thread1, NULL, test_daemon_start, (void*)ipfs_path1);
	thread_started1 = 1;
	sleep(3);

	// use a client to ask for the file
	arguments = cli_arguments_new(5, args);
	if (ipfs_exporter_object_cat(arguments, stdout) == 0) {
		libp2p_logger_error("test_api", "ipfs_exporter_object_cat returned false.\n");
		goto exit;
	}

	retVal = 1;
	exit:
	ipfs_daemon_stop();
	if (thread_started1)
		pthread_join(daemon_thread1, NULL);
	cli_arguments_free(arguments);
	return retVal;
}

/***
 * Attempt to get a large (>256K) binary file over the api
 */
int test_core_api_object_cat_large_binary() {
	int retVal = 0;
	pthread_t daemon_thread1;
	int thread_started1 = 0;
	char* ipfs_path1 = "/tmp/ipfs_1";
	char* config_file1 = "config.test1.wo_journal";
	struct FSRepo* fs_repo = NULL;
	char hash[256] = "";
	char* args[] = {"ipfs", "--config", ipfs_path1, "cat", hash };
	struct CliArguments* arguments = NULL;

	// logging
	libp2p_logger_add_class("test_api");
	libp2p_logger_add_class("journal");
	libp2p_logger_add_class("daemon");
	libp2p_logger_add_class("online");
	libp2p_logger_add_class("peer");
	libp2p_logger_add_class("null");
	libp2p_logger_add_class("replication");
	libp2p_logger_add_class("fs_repo");
	libp2p_logger_add_class("lmdb_journalstore");
	libp2p_logger_add_class("lmdb_datastore");
	libp2p_logger_add_class("secio");
	libp2p_logger_add_class("socket");
	libp2p_logger_add_class("protocol");
	libp2p_logger_add_class("dht_protocol");
	libp2p_logger_add_class("resolver");
	libp2p_logger_add_class("unixfs");
	libp2p_logger_add_class("bitswap_engine");
	libp2p_logger_add_class("bitswap_network");
	libp2p_logger_add_class("exporter");
	libp2p_logger_add_class("api");

	// build repo
	if (!drop_build_open_repo(ipfs_path1, &fs_repo, config_file1)) {
		ipfs_repo_fsrepo_free(fs_repo);
		libp2p_logger_error("test_api", "Unable to drop and build repository at %s\n", ipfs_path1);
		goto exit;
	}
	libp2p_logger_debug("test_api", "Changed the server id to %s.\n", fs_repo->config->identity->peer->id);
	ipfs_repo_fsrepo_free(fs_repo);

	// add a file to the repo
	uint8_t bytes[300000];
	for(int i = 0; i < 300000; i++)
		bytes[i] = i % 255;
	char* filename = "test1.txt";
	create_file(filename, bytes, 300000);
	struct HashtableNode* node;
	size_t bytes_written;
	struct IpfsNode *local_node = NULL;
	ipfs_node_offline_new(ipfs_path1, &local_node);
	ipfs_import_file(NULL, filename, &node, local_node, &bytes_written, 0);
	memset(hash, 0, 256);
	ipfs_cid_hash_to_base58(node->hash, node->hash_size, (unsigned char*)hash, 256);
	libp2p_logger_debug("test_api", "Inserted file with hash %s.\n", hash);
	ipfs_node_free(local_node);
	ipfs_hashtable_node_free(node);

	libp2p_logger_debug("test_api", "*** Firing up daemons ***\n");
	pthread_create(&daemon_thread1, NULL, test_daemon_start, (void*)ipfs_path1);
	thread_started1 = 1;
	sleep(3);

	// use a client to ask for the file
	arguments = cli_arguments_new(5, args);
	char* filename2 = "test2.txt";
	unlink(filename2);
	FILE* fd = fopen(filename2, "w" );
	if (ipfs_exporter_object_cat(arguments, fd) == 0) {
		libp2p_logger_error("test_api", "ipfs_exporter_object_cat returned false.\n");
		fclose(fd);
		goto exit;
	}
	fclose(fd);

	retVal = 1;
	exit:
	ipfs_daemon_stop();
	if (thread_started1)
		pthread_join(daemon_thread1, NULL);
	cli_arguments_free(arguments);
	return retVal;
}


int test_core_api_name_resolve() {
	int retVal = 0;
	pthread_t daemon_thread1;
	pthread_t daemon_thread2;
	int thread_started1 = 0;
	int thread_started2 = 0;
	char* ipfs_path1 = "/tmp/ipfs_1";
	char* config_file1 = "config.test1.wo_journal";
	char* ipfs_path2 = "/tmp/ipfs_2";
	char* config_file2 = "config.test2.wo_journal";
	struct FSRepo* fs_repo = NULL;
	char hash[256] = "";
	char peer_id1[256] = "";
	char* publish_args[] = {"ipfs", "--config", ipfs_path1, "name", "publish", hash };
	char* resolve_args[] = {"ipfs", "--config", ipfs_path2, "name", "resolve", peer_id1 };
	struct CliArguments* args = NULL;

	//libp2p_logger_add_class("api");

	// build 2 repos... repo 1
	if (!drop_build_open_repo(ipfs_path1, &fs_repo, config_file1)) {
		ipfs_repo_fsrepo_free(fs_repo);
		libp2p_logger_error("test_api", "Unable to drop and build repository at %s\n", ipfs_path1);
		goto exit;
	}
	sprintf(peer_id1, "/ipns/%s", fs_repo->config->identity->peer->id);
	ipfs_repo_fsrepo_free(fs_repo);
	// build 2 repos... repo 2
	if (!drop_build_open_repo(ipfs_path2, &fs_repo, config_file2)) {
		ipfs_repo_fsrepo_free(fs_repo);
		libp2p_logger_error("test_api", "Unable to drop and build repository at %s\n", ipfs_path2);
		goto exit;
	}
	libp2p_logger_debug("test_api", "Changed the server id to %s.\n", fs_repo->config->identity->peer->id);
	ipfs_repo_fsrepo_free(fs_repo);

	// add a file to the first repo
	uint8_t *bytes = (unsigned char*)"hello, world!\n";
	char* filename = "test1.txt";
	create_file(filename, bytes, strlen((char*)bytes));
	struct HashtableNode* node;
	size_t bytes_written;
	struct IpfsNode *local_node = NULL;
	ipfs_node_offline_new(ipfs_path1, &local_node);
	ipfs_import_file(NULL, filename, &node, local_node, &bytes_written, 0);
	memset(hash, 0, 256);
	ipfs_cid_hash_to_base58(node->hash, node->hash_size, (unsigned char*)hash, 256);
	ipfs_node_free(local_node);
	ipfs_hashtable_node_free(node);

	libp2p_logger_debug("test_api", "*** Firing up daemons ***\n");
	pthread_create(&daemon_thread1, NULL, test_daemon_start, (void*)ipfs_path1);
	thread_started1 = 1;
	sleep(5);
	pthread_create(&daemon_thread2, NULL, test_daemon_start, (void*)ipfs_path2);
	thread_started2 = 1;
	sleep(10);

	// publish name on server 1
	args = cli_arguments_new(6, publish_args);
	ipfs_name(args);
	cli_arguments_free(args);
	args = NULL;

	sleep(3);

	// use a client of server2 to to ask for the "name resolve" on server 1
	args = cli_arguments_new(6, resolve_args);
	if (ipfs_name(args) == 0) {
		goto exit;
	}

	sleep(5);

	retVal = 1;
	exit:
	cli_arguments_free(args);
	ipfs_daemon_stop();
	if (thread_started1)
		pthread_join(daemon_thread1, NULL);
	if (thread_started2)
		pthread_join(daemon_thread2, NULL);
	return retVal;

}

/**
 * Like test_core_api_name_resolve, but split into 2 pieces
 */
int test_core_api_name_resolve_1() {
	int retVal = 0;
	pthread_t daemon_thread1;
	int thread_started1 = 0;
	char* ipfs_path1 = "/tmp/ipfs_1";
	char* config_file1 = "config.test1.wo_journal";
	struct FSRepo* fs_repo = NULL;
	char hash[256] = "";
	char peer_id1[256] = "";
	struct CliArguments* args = NULL;

	libp2p_logger_add_class("api");
	libp2p_logger_add_class("test_api");
	libp2p_logger_add_class("protocol");

	// repo 1
	if (!drop_build_open_repo(ipfs_path1, &fs_repo, config_file1)) {
		ipfs_repo_fsrepo_free(fs_repo);
		libp2p_logger_error("test_api", "Unable to drop and build repository at %s\n", ipfs_path1);
		goto exit;
	}
	sprintf(peer_id1, "/ipns/%s", fs_repo->config->identity->peer->id);
	ipfs_repo_fsrepo_free(fs_repo);

	// add a file to the first repo
	uint8_t *bytes = (unsigned char*)"hello, world!\n";
	char* filename = "test1.txt";
	create_file(filename, bytes, strlen((char*)bytes));
	struct HashtableNode* node;
	size_t bytes_written;
	struct IpfsNode *local_node = NULL;
	ipfs_node_offline_new(ipfs_path1, &local_node);
	ipfs_import_file(NULL, filename, &node, local_node, &bytes_written, 0);
	memset(hash, 0, 256);
	ipfs_cid_hash_to_base58(node->hash, node->hash_size, (unsigned char*)hash, 256);
	libp2p_logger_debug("test_api", "The hash is %s.\n", hash);
	ipfs_node_free(local_node);
	ipfs_hashtable_node_free(node);

	libp2p_logger_debug("test_api", "*** Firing up daemon ***\n");
	pthread_create(&daemon_thread1, NULL, test_daemon_start, (void*)ipfs_path1);
	thread_started1 = 1;


	sleep(45);

	retVal = 1;
	exit:
	cli_arguments_free(args);
	ipfs_daemon_stop();
	if (thread_started1)
		pthread_join(daemon_thread1, NULL);
	return retVal;
}

/***
 * This should already be running before you run test_core_api_name_resolve_2
 */
int test_core_api_name_resolve_2() {
	int retVal = 0;
	pthread_t daemon_thread2;
	int thread_started2 = 0;
	char* ipfs_path2 = "/tmp/ipfs_2";
	char* config_file2 = "config.test2.wo_journal";
	struct FSRepo* fs_repo = NULL;
	char peer_id1[256] = "/ipns/QmZVoAZGFfinB7MQQiDzB84kWaDPQ95GLuXdemJFM2r9b4";
	char* resolve_args[] = {"ipfs", "--config", ipfs_path2, "name", "resolve", peer_id1 };
	struct CliArguments* args = NULL;

	libp2p_logger_add_class("test_api");
	libp2p_logger_add_class("api");
	libp2p_logger_add_class("bitswap_engine");
	libp2p_logger_add_class("dht_protocol");

	// build 2 repos... repo 2
	if (!drop_build_open_repo(ipfs_path2, &fs_repo, config_file2)) {
		ipfs_repo_fsrepo_free(fs_repo);
		libp2p_logger_error("test_api", "Unable to drop and build repository at %s\n", ipfs_path2);
		goto exit;
	}
	libp2p_logger_debug("test_api", "Changed the server id to %s.\n", fs_repo->config->identity->peer->id);
	ipfs_repo_fsrepo_free(fs_repo);

	libp2p_logger_debug("test_api", "*** Firing up daemon ***\n");
	pthread_create(&daemon_thread2, NULL, test_daemon_start, (void*)ipfs_path2);
	thread_started2 = 1;

	libp2p_logger_debug("test_api", "Waiting 45 secs...\n");
	sleep(45);
	libp2p_logger_debug("test_api", "Wait is over\n");

	// use a client of server2 to to ask for the "name resolve" on server 1
	args = cli_arguments_new(6, resolve_args);
	if (ipfs_name(args) == 0) {
		goto exit;
	}

	retVal = 1;
	exit:
	cli_arguments_free(args);
	ipfs_daemon_stop();
	if (thread_started2)
		pthread_join(daemon_thread2, NULL);
	return retVal;
}

// publish a name to server 1
int test_core_api_name_resolve_3()
{
	char* hash = "QmcbMZW8hcd46AfUsJUxQYTanHYDpeUq3pBuX5nihPEKD9";
	char* publish_args[] = {"ipfs", "--config", "/tmp/ipfs_1", "name", "publish", hash };
	struct CliArguments* args = NULL;

	libp2p_logger_add_class("api");
	libp2p_logger_add_class("test_api");
	libp2p_logger_add_class("http_request");

	// publish name on server 1
	args = cli_arguments_new(6, publish_args);
	int retVal = ipfs_name(args);
	cli_arguments_free(args);
	args = NULL;
	return retVal;

}

int test_core_api_id() {
	int retVal = 0;
	struct IpfsNode* local_node = NULL;
	char* repo_path = "/tmp/ipfs_api_id";
	char* peer_id = NULL;

	if (!drop_and_build_repository(repo_path, 4001, NULL, &peer_id))
		goto exit;
	if (!ipfs_node_offline_new(repo_path, &local_node))
		goto exit;

	struct HttpRequest* req = ipfs_core_http_request_new();
	if (!req) goto exit;
	req->command = strdup("id");

	struct HttpResponse* resp = NULL;
	if (!ipfs_core_http_request_process(local_node, req, &resp) || !resp) {
		ipfs_core_http_request_free(req);
		goto exit;
	}

	if (strstr((char*)resp->bytes, local_node->identity->peer->id) != NULL) {
		retVal = 1;
	}

	ipfs_core_http_response_free(resp);
	ipfs_core_http_request_free(req);
exit:
	ipfs_node_free(local_node);
	if (peer_id) free(peer_id);
	return retVal;
}

int test_core_api_version() {
	int retVal = 0;
	struct IpfsNode* local_node = NULL;
	char* repo_path = "/tmp/ipfs_api_version";
	char* peer_id = NULL;

	if (!drop_and_build_repository(repo_path, 4001, NULL, &peer_id))
		goto exit;
	if (!ipfs_node_offline_new(repo_path, &local_node))
		goto exit;

	struct HttpRequest* req = ipfs_core_http_request_new();
	if (!req) goto exit;
	req->command = strdup("version");

	struct HttpResponse* resp = NULL;
	if (!ipfs_core_http_request_process(local_node, req, &resp) || !resp) {
		ipfs_core_http_request_free(req);
		goto exit;
	}

	if (strstr((char*)resp->bytes, "Version") != NULL) {
		retVal = 1;
	}

	ipfs_core_http_response_free(resp);
	ipfs_core_http_request_free(req);
exit:
	ipfs_node_free(local_node);
	if (peer_id) free(peer_id);
	return retVal;
}

int test_core_api_block_get() {
	int retVal = 0;
	struct IpfsNode* local_node = NULL;
	char* repo_path = "/tmp/ipfs_api_block";
	char* peer_id = NULL;
	struct Block* block = NULL;
	struct Cid* cid = NULL;

	if (!drop_and_build_repository(repo_path, 4001, NULL, &peer_id))
		goto exit;
	if (!ipfs_node_offline_new(repo_path, &local_node))
		goto exit;

	// Create and store a raw block
	const char* data = "hello block";
	block = ipfs_block_new_raw((const unsigned char*)data, strlen(data));
	if (!block) goto exit;

	size_t written = 0;
	if (!ipfs_blockstore_put(local_node->blockstore->blockstoreContext, block, &written))
		goto exit;

	// Get the CID as base58 string
	char hash_str[256] = {0};
	if (!ipfs_cid_hash_to_base58(block->cid->hash, block->cid->hash_length, (unsigned char*)hash_str, 256))
		goto exit;

	// Build HTTP request for block/get
	struct HttpRequest* req = ipfs_core_http_request_new();
	if (!req) goto exit;
	req->command = strdup("block");
	req->sub_command = strdup("get");
	libp2p_utils_vector_add(req->arguments, strdup(hash_str));

	struct HttpResponse* resp = NULL;
	if (!ipfs_core_http_request_process(local_node, req, &resp) || !resp) {
		ipfs_core_http_request_free(req);
		goto exit;
	}

	if (resp->bytes_size == strlen(data) && memcmp(resp->bytes, data, strlen(data)) == 0) {
		retVal = 1;
	}

	ipfs_core_http_response_free(resp);
	ipfs_core_http_request_free(req);
exit:
	if (block) ipfs_block_free(block);
	ipfs_node_free(local_node);
	if (peer_id) free(peer_id);
	return retVal;
}

int test_core_api_cat() {
	int retVal = 0;
	struct IpfsNode* local_node = NULL;
	char* repo_path = "/tmp/ipfs_api_cat";
	char* peer_id = NULL;
	struct HashtableNode* node = NULL;
	char hash_str[256] = {0};

	if (!drop_and_build_repository(repo_path, 4001, NULL, &peer_id))
		goto exit;
	if (!ipfs_node_offline_new(repo_path, &local_node))
		goto exit;

	// Import a small file
	const char* content = "hello, cat world!\n";
	char* filename = "/tmp/test_api_cat.txt";
	create_file(filename, (unsigned char*)content, strlen(content));

	size_t bytes_written = 0;
	if (!ipfs_import_file(NULL, filename, &node, local_node, &bytes_written, 0))
		goto exit;

	if (!ipfs_cid_hash_to_base58(node->hash, node->hash_size, (unsigned char*)hash_str, 256))
		goto exit;

	// Build HTTP request for cat
	struct HttpRequest* req = ipfs_core_http_request_new();
	if (!req) goto exit;
	req->command = strdup("cat");
	libp2p_utils_vector_add(req->arguments, strdup(hash_str));

	struct HttpResponse* resp = NULL;
	if (!ipfs_core_http_request_process(local_node, req, &resp) || !resp) {
		ipfs_core_http_request_free(req);
		goto exit;
	}

	if (resp->bytes_size == strlen(content) && memcmp(resp->bytes, content, strlen(content)) == 0) {
		retVal = 1;
	}

	ipfs_core_http_response_free(resp);
	ipfs_core_http_request_free(req);
exit:
	if (node) ipfs_hashtable_node_free(node);
	ipfs_node_free(local_node);
	if (peer_id) free(peer_id);
	return retVal;
}
