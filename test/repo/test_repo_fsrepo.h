#include "ipfs/repo/fsrepo/fs_repo.h"
#include "ipfs/repo/init.h"
#include "../test_helper.h"

int test_repo_fsrepo_open_config() {
	struct FSRepo* fs_repo = NULL;

	const char* path = "./tmp/.ipfs";

	if (!drop_build_and_open_repo(path, &fs_repo))
		return 0;

	if (!ipfs_repo_fsrepo_free(fs_repo))
		return 0;

	return 1;
}

int test_repo_fsrepo_build() {
	const char* path = "./tmp/.ipfs";
	char* peer_id = NULL;

	int retVal = drop_and_build_repository(path, 4001, NULL, &peer_id);
	if (peer_id != NULL)
		free(peer_id);
	return retVal;
}

int test_repo_init_config_import() {
	const char* path = "./tmp/.ipfs_import_test";
	drop_repository(path);

	// Create a minimal config file for import
	char config_path[256];
	snprintf(config_path, sizeof(config_path), "%s/import_config.json", path);
	os_mkdir(path);
	FILE* f = fopen(config_path, "w");
	if (!f) return 0;
	fputs("{\"Identity\":{\"PeerID\":\"QmTest\",\"PrivKey\":\"CAASpgkwggSiAgEAAoIBAQC...\"}}", f);
	fclose(f);

	char* peer_id = NULL;
	int retVal = make_ipfs_repository(path, 4001, NULL, &peer_id, config_path);
	if (peer_id != NULL)
		free(peer_id);
	return retVal;
}

int test_repo_config_merge_json() {
	struct RepoConfig* config = NULL;
	if (!ipfs_repo_config_new(&config))
		return 0;

	// Initialize with defaults
	if (!ipfs_repo_config_init(config, 2048, "./tmp/.ipfs_merge_test", 4001, NULL)) {
		ipfs_repo_config_free(config);
		return 0;
	}

	// Verify defaults before merge
	if (config->addresses->api == NULL || strcmp(config->addresses->api, "/ip4/127.0.0.1/tcp/5001") != 0) {
		ipfs_repo_config_free(config);
		return 0;
	}

	const char* import_json = "{"
		"\"Addresses\":{"
			"\"Swarm\":[\"/ip4/0.0.0.0/tcp/9999\"],"
			"\"API\":\"/ip4/127.0.0.1/tcp/9998\","
			"\"Gateway\":\"/ip4/127.0.0.1/tcp/9997\""
		"},"
		"\"Datastore\":{"
			"\"Type\":\"flatfs\","
			"\"StorageMax\":\"5GB\""
		"},"
		"\"Bootstrap\":[\"/ip4/104.131.131.82/tcp/4001\"]"
	"}";

	if (!repo_config_merge_json(config, import_json)) {
		ipfs_repo_config_free(config);
		return 0;
	}

	// Verify merged Addresses
	if (config->addresses->api == NULL || strcmp(config->addresses->api, "/ip4/127.0.0.1/tcp/9998") != 0) {
		fprintf(stderr, "API merge failed: expected /ip4/127.0.0.1/tcp/9998, got %s\n", config->addresses->api ? config->addresses->api : "(null)");
		ipfs_repo_config_free(config);
		return 0;
	}
	if (config->addresses->gateway == NULL || strcmp(config->addresses->gateway, "/ip4/127.0.0.1/tcp/9997") != 0) {
		fprintf(stderr, "Gateway merge failed: expected /ip4/127.0.0.1/tcp/9997, got %s\n", config->addresses->gateway ? config->addresses->gateway : "(null)");
		ipfs_repo_config_free(config);
		return 0;
	}
	if (config->addresses->swarm_head == NULL || config->addresses->swarm_head->item == NULL ||
	    strcmp((char*)config->addresses->swarm_head->item, "/ip4/0.0.0.0/tcp/9999") != 0) {
		fprintf(stderr, "Swarm merge failed\n");
		ipfs_repo_config_free(config);
		return 0;
	}

	// Verify merged Datastore
	if (config->datastore->type == NULL || strcmp(config->datastore->type, "flatfs") != 0) {
		fprintf(stderr, "Datastore type merge failed: expected flatfs, got %s\n", config->datastore->type ? config->datastore->type : "(null)");
		ipfs_repo_config_free(config);
		return 0;
	}
	if (config->datastore->storage_max == NULL || strcmp(config->datastore->storage_max, "5GB") != 0) {
		fprintf(stderr, "Datastore StorageMax merge failed: expected 5GB, got %s\n", config->datastore->storage_max ? config->datastore->storage_max : "(null)");
		ipfs_repo_config_free(config);
		return 0;
	}

	// Verify merged Bootstrap
	if (config->bootstrap_peers == NULL || config->bootstrap_peers->total != 1) {
		fprintf(stderr, "Bootstrap merge failed: expected 1 peer, got %d\n", config->bootstrap_peers ? config->bootstrap_peers->total : -1);
		ipfs_repo_config_free(config);
		return 0;
	}

	ipfs_repo_config_free(config);
	return 1;
}

int test_repo_fsrepo_write_read_block() {
	struct Block* block = NULL;
	struct FSRepo* fs_repo = NULL;
	int retVal = 0;

	// freshen the repository
	retVal = drop_build_and_open_repo("./tmp/.ipfs", &fs_repo);
	if (retVal == 0)
		return 0;

	// make some data
	size_t data_size = 10000;
	unsigned char data[data_size];

	int counter = 0;
	for(int i = 0; i < data_size; i++) {
		data[i] = counter++;
		if (counter > 15)
			counter = 0;
	}

	// create and write the block
	block = ipfs_block_new();
	if (block == NULL) {
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}
	retVal = ipfs_blocks_block_add_data(data, data_size, block);
	if (retVal == 0) {
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}

	size_t bytes_written;
	retVal = ipfs_repo_fsrepo_block_write(block, fs_repo, &bytes_written);
	if (retVal == 0) {
		ipfs_repo_fsrepo_free(fs_repo);
		ipfs_block_free(block);
		return 0;
	}

	// retrieve the block
	struct Block* results;
	retVal = ipfs_repo_fsrepo_block_read(block->cid->hash, block->cid->hash_length, &results, fs_repo);
	if (retVal == 0) {
		ipfs_repo_fsrepo_free(fs_repo);
		ipfs_block_free(block);
		return 0;
	}

	// compare the two blocks
	retVal = 1;
	if (block->data_length != results->data_length || block->data_length != data_size) {
		printf("block data is of different length: %lu vs %lu\n", results->data_length, block->data_length);
		retVal = 0;
	}

	for(size_t i = 0; i < block->data_length; i++) {
		if (block->data[i] != results->data[i]) {
			printf("Data is different at position %lu. Should be %02x but is %02x\n", i, block->data[i], results->data[i]);
			retVal = 0;
			break;
		}
	}

	ipfs_repo_fsrepo_free(fs_repo);
	ipfs_block_free(block);
	ipfs_block_free(results);
	return retVal;
}
