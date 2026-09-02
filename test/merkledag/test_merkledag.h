#include "ipfs/merkledag/merkledag.h"
#include "ipfs/merkledag/node.h"
#include "../test_helper.h"

struct FSRepo* createAndOpenRepo(const char* dir) {
	int retVal = 1;
	// create a fresh repo
	retVal = drop_and_build_repository(dir, 4001, NULL, NULL);
	if (retVal == 0)
		return NULL;

	// open the fs repo
	struct RepoConfig* repo_config = NULL;
	struct FSRepo* fs_repo;

	// create the struct
	retVal = ipfs_repo_fsrepo_new(dir, repo_config, &fs_repo);
	if (retVal == 0)
		return NULL;

	// open the repository and read the config file
	retVal = ipfs_repo_fsrepo_open(fs_repo);
	if (retVal == 0) {
		ipfs_repo_fsrepo_free(fs_repo);
		return NULL;
	}
	return fs_repo;
}

int test_merkledag_get_data() {
	int retVal = 0;

	struct FSRepo* fs_repo = createAndOpenRepo("/tmp/.ipfs");

	// create data for node
	size_t binary_data_size = 256;
	unsigned char binary_data[binary_data_size];
	for(int i = 0; i < binary_data_size; i++) {
		binary_data[i] = i;
	}

	// create a node
	struct HashtableNode* node1;
	retVal = ipfs_hashtable_node_new_from_data(binary_data, binary_data_size, &node1);
	size_t bytes_written = 0;
	retVal = ipfs_merkledag_add(node1, fs_repo, &bytes_written);
	if (retVal == 0) {
		ipfs_hashtable_node_free(node1);
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}

	// now retrieve it
	struct HashtableNode* results_node;
	retVal = ipfs_merkledag_get(node1->hash, node1->hash_size, &results_node, fs_repo);
	if (retVal == 0) {
		ipfs_hashtable_node_free(node1);
		ipfs_hashtable_node_free(results_node);
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}

	if (results_node->data_size != 256) {
		ipfs_hashtable_node_free(node1);
		ipfs_hashtable_node_free(results_node);
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}

	// the data should be the same
	for(int i = 0; i < results_node->data_size; i++) {
		if (results_node->data[i] != node1->data[i]) {
			ipfs_hashtable_node_free(node1);
			ipfs_hashtable_node_free(results_node);
			ipfs_repo_fsrepo_free(fs_repo);
			return 0;
		}
	}

	ipfs_hashtable_node_free(node1);
	ipfs_hashtable_node_free(results_node);
	ipfs_repo_fsrepo_free(fs_repo);

	return retVal;
}

int test_merkledag_add_data() {
	int retVal = 0;

	struct FSRepo* fs_repo = createAndOpenRepo("/tmp/.ipfs");
	if (fs_repo == NULL)
		return 0;

	// get the size of the database
	int start_file_size = os_utils_file_size("/tmp/.ipfs/datastore/data.mdb");

	// create data for node
	size_t binary_data_size = 256;
	unsigned char binary_data[binary_data_size];
	for(int i = 0; i < binary_data_size; i++) {
		binary_data[i] = i;
	}

	// create a node
	struct HashtableNode* node1;
	retVal = ipfs_hashtable_node_new_from_data(binary_data, binary_data_size, &node1);

	size_t bytes_written = 0;
	retVal = ipfs_merkledag_add(node1, fs_repo, &bytes_written);
	if (retVal == 0) {
		ipfs_hashtable_node_free(node1);
		return 0;
	}

	// make sure everything is correct
	if (node1->hash == NULL)
		return 0;

	int first_add_size = os_utils_file_size("/tmp/.ipfs/datastore/data.mdb");
	if (first_add_size == start_file_size) { // uh oh, database should have increased in size
		ipfs_hashtable_node_free(node1);
		return 0;
	}

	// adding the same binary again should do nothing (the hash should be the same)
	struct HashtableNode* node2;
	retVal = ipfs_hashtable_node_new_from_data(binary_data, binary_data_size, &node2);
	retVal = ipfs_merkledag_add(node2, fs_repo, &bytes_written);
	if (retVal == 0) {
		ipfs_hashtable_node_free(node1);
		ipfs_hashtable_node_free(node2);
		return 0;
	}

	// make sure everything is correct
	if (node2->hash == NULL) {
		ipfs_hashtable_node_free(node1);
		ipfs_hashtable_node_free(node2);
		return 0;
	}
	for(int i = 0; i < node1->hash_size; i++) {
		if (node1->hash[i] != node2->hash[i]) {
			printf("hash of node1 does not match node2 at position %d\n", i);
			ipfs_hashtable_node_free(node1);
			ipfs_hashtable_node_free(node2);
			return 0;
		}
	}

	int second_add_size = os_utils_file_size("/tmp/.ipfs/datastore/data.mdb");
	if (first_add_size != second_add_size) { // uh oh, the database shouldn't have changed size
		printf("looks as if a new record was added when it shouldn't have. Old file size: %d, new file size: %d\n", first_add_size, second_add_size);
		ipfs_hashtable_node_free(node1);
		ipfs_hashtable_node_free(node2);
		return 0;
	}

	// now change 1 byte, which should change the hash
	binary_data[10] = 0;
	// create a node
	struct HashtableNode* node3;
	retVal = ipfs_hashtable_node_new_from_data(binary_data, binary_data_size, &node3);

	retVal = ipfs_merkledag_add(node3, fs_repo, &bytes_written);
	if (retVal == 0) {
		ipfs_hashtable_node_free(node1);
		ipfs_hashtable_node_free(node2);
		ipfs_hashtable_node_free(node3);
		return 0;
	}

	// make sure everything is correct
	if (node3->hash == NULL) {
		ipfs_hashtable_node_free(node1);
		ipfs_hashtable_node_free(node2);
		ipfs_hashtable_node_free(node3);
		return 0;
	}

	ipfs_hashtable_node_free(node1);
	ipfs_hashtable_node_free(node2);
	ipfs_hashtable_node_free(node3);
	int third_add_size = os_utils_file_size("/tmp/.ipfs/datastore/data.mdb");
	if (third_add_size == second_add_size || third_add_size < second_add_size) {// uh oh, it didn't add it
		printf("Node 3 should have been added, but the file size did not change from %d.\n", third_add_size);
		return 0;
	}

	ipfs_repo_fsrepo_free(fs_repo);

	return 1;
}

int test_merkledag_add_node() {
	int retVal = 0;
	struct HashtableNode* node1 = NULL;

	struct FSRepo* fs_repo = createAndOpenRepo("/tmp/.ipfs");
	if (fs_repo == NULL) {
		printf("Unable to create repo\n");
		return 0;
	}

	retVal = ipfs_hashtable_node_new(&node1);
	if (retVal == 0) {
		printf("Unable to make node\n");
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}

	size_t bytes_written = 0;
	retVal = ipfs_merkledag_add(node1, fs_repo, &bytes_written);
	if (retVal == 0) {
		ipfs_repo_fsrepo_free(fs_repo);
		ipfs_hashtable_node_free(node1);
		printf("Unable to add node\n");
		return 0;
	}

	ipfs_hashtable_node_free(node1);
	ipfs_repo_fsrepo_free(fs_repo);

	return 1;
}

/**
 * Should save links
 */
int test_merkledag_add_node_with_links() {
	int retVal = 0;
	struct NodeLink* link = NULL;
	struct HashtableNode* node1 = NULL;
	struct HashtableNode* node2 = NULL;

	struct FSRepo* fs_repo = createAndOpenRepo("/tmp/.ipfs");
	if (fs_repo == NULL) {
		printf("Unable to create repo\n");
		return 0;
	}

	// make link
	retVal = ipfs_node_link_create("", (unsigned char*)"abc123", 6, &link);
	if (retVal == 0) {
		printf("Unable to make new link\n");
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}
	retVal = ipfs_hashtable_node_new_from_link(link, &node1);
	if (retVal == 0) {
		printf("Unable to make node\n");
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}

	size_t bytes_written = 0;
	retVal = ipfs_merkledag_add(node1, fs_repo, &bytes_written);
	if (retVal == 0) {
		ipfs_repo_fsrepo_free(fs_repo);
		ipfs_hashtable_node_free(node1);
		printf("Unable to add node\n");
		return 0;
	}

	// now look for it
	retVal = ipfs_merkledag_get(node1->hash, node1->hash_size, &node2, fs_repo);
	if (retVal == 0) {
		ipfs_repo_fsrepo_free(fs_repo);
		ipfs_hashtable_node_free(node1);
		return 0;
	}

	struct NodeLink* node1_link = node1->head_link;
	struct NodeLink* node2_link = node2->head_link;

	if (node1_link->hash_size != node2_link->hash_size) {
		printf("Hashes are not of the same length. Hash1: %lu, Hash2: %lu\n", node1_link->hash_size, node2_link->hash_size);
		ipfs_repo_fsrepo_free(fs_repo);
		ipfs_hashtable_node_free(node1);
		ipfs_hashtable_node_free(node2);
		return 0;
	}
	while(node1_link != NULL) {
		for(int i = 0; i < node1_link->hash_size; i++) {
			if(node1_link->hash[i] != node2_link->hash[i]) {
				printf("Hashes do not match for node %s\n", node1_link->name);
				ipfs_repo_fsrepo_free(fs_repo);
				ipfs_hashtable_node_free(node1);
				ipfs_hashtable_node_free(node2);
				return 0;
			}
		}
		node1_link = node1_link->next;
		node2_link = node2_link->next;
	}

	ipfs_hashtable_node_free(node1);
	ipfs_hashtable_node_free(node2);
	ipfs_repo_fsrepo_free(fs_repo);

	return 1;
}

static int visit_count = 0;
static int visit_depths[4] = {0};

static int test_traverse_visit(struct HashtableNode* node, int depth, void* ctx) {
	(void)node;
	(void)ctx;
	if (visit_count < 4)
		visit_depths[visit_count] = depth;
	visit_count++;
	return 1;
}

int test_merkledag_traverse() {
	struct FSRepo* fs_repo = createAndOpenRepo("/tmp/.ipfs");
	if (fs_repo == NULL)
		return 0;

	// Create root node with two child links
	struct HashtableNode* child1;
	struct HashtableNode* child2;
	ipfs_hashtable_node_new_from_data((unsigned char*)"child1", 6, &child1);
	ipfs_hashtable_node_new_from_data((unsigned char*)"child2", 6, &child2);

	size_t bytes_written = 0;
	if (!ipfs_merkledag_add(child1, fs_repo, &bytes_written) ||
	    !ipfs_merkledag_add(child2, fs_repo, &bytes_written)) {
		ipfs_hashtable_node_free(child1);
		ipfs_hashtable_node_free(child2);
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}

	struct HashtableNode* root;
	ipfs_hashtable_node_new_from_data((unsigned char*)"root", 4, &root);
	struct NodeLink* link1;
	struct NodeLink* link2;
	ipfs_node_link_create("a", child1->hash, child1->hash_size, &link1);
	ipfs_node_link_create("b", child2->hash, child2->hash_size, &link2);
	ipfs_hashtable_node_add_link(root, link1);
	ipfs_hashtable_node_add_link(root, link2);

	if (!ipfs_merkledag_add(root, fs_repo, &bytes_written)) {
		ipfs_hashtable_node_free(child1);
		ipfs_hashtable_node_free(child2);
		ipfs_hashtable_node_free(root);
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}

	// Test depth-limited traversal (max_depth=1 should visit root + 2 children = 3 nodes)
	visit_count = 0;
	memset(visit_depths, 0, sizeof(visit_depths));
	if (!ipfs_merkledag_traverse(fs_repo, root->hash, root->hash_size, 1, test_traverse_visit, NULL)) {
		ipfs_hashtable_node_free(child1);
		ipfs_hashtable_node_free(child2);
		ipfs_hashtable_node_free(root);
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}
	if (visit_count != 3 || visit_depths[0] != 0 || visit_depths[1] != 1 || visit_depths[2] != 1) {
		printf("traverse: expected 3 nodes (0,1,1), got %d (%d,%d,%d)\n",
			visit_count, visit_depths[0], visit_depths[1], visit_depths[2]);
		ipfs_hashtable_node_free(child1);
		ipfs_hashtable_node_free(child2);
		ipfs_hashtable_node_free(root);
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}

	// Test max_depth=0 (root only)
	visit_count = 0;
	if (!ipfs_merkledag_traverse(fs_repo, root->hash, root->hash_size, 0, test_traverse_visit, NULL)) {
		ipfs_hashtable_node_free(child1);
		ipfs_hashtable_node_free(child2);
		ipfs_hashtable_node_free(root);
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}
	if (visit_count != 1) {
		printf("traverse depth 0: expected 1 node, got %d\n", visit_count);
		ipfs_hashtable_node_free(child1);
		ipfs_hashtable_node_free(child2);
		ipfs_hashtable_node_free(root);
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}

	ipfs_hashtable_node_free(child1);
	ipfs_hashtable_node_free(child2);
	ipfs_hashtable_node_free(root);
	ipfs_repo_fsrepo_free(fs_repo);
	return 1;
}

int test_merkledag_link_ordering() {
	struct FSRepo* fs_repo = createAndOpenRepo("/tmp/.ipfs");
	if (fs_repo == NULL)
		return 0;

	// Create three child nodes
	struct HashtableNode* child_z;
	struct HashtableNode* child_a;
	struct HashtableNode* child_m;
	ipfs_hashtable_node_new_from_data((unsigned char*)"z", 1, &child_z);
	ipfs_hashtable_node_new_from_data((unsigned char*)"a", 1, &child_a);
	ipfs_hashtable_node_new_from_data((unsigned char*)"m", 1, &child_m);

	size_t bytes_written = 0;
	if (!ipfs_merkledag_add(child_z, fs_repo, &bytes_written) ||
	    !ipfs_merkledag_add(child_a, fs_repo, &bytes_written) ||
	    !ipfs_merkledag_add(child_m, fs_repo, &bytes_written)) {
		ipfs_hashtable_node_free(child_z);
		ipfs_hashtable_node_free(child_a);
		ipfs_hashtable_node_free(child_m);
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}

	// Build root with links in non-alphabetical order: z, a, m
	struct HashtableNode* root;
	ipfs_hashtable_node_new_from_data((unsigned char*)"root", 4, &root);
	struct NodeLink* link_z;
	struct NodeLink* link_a;
	struct NodeLink* link_m;
	ipfs_node_link_create("z", child_z->hash, child_z->hash_size, &link_z);
	ipfs_node_link_create("a", child_a->hash, child_a->hash_size, &link_a);
	ipfs_node_link_create("m", child_m->hash, child_m->hash_size, &link_m);
	ipfs_hashtable_node_add_link(root, link_z);
	ipfs_hashtable_node_add_link(root, link_a);
	ipfs_hashtable_node_add_link(root, link_m);

	// Encode the node
	size_t enc_size = ipfs_hashtable_node_protobuf_encode_size(root);
	unsigned char enc[enc_size];
	size_t enc_written = 0;
	if (!ipfs_hashtable_node_protobuf_encode(root, enc, enc_size, &enc_written)) {
		ipfs_hashtable_node_free(child_z);
		ipfs_hashtable_node_free(child_a);
		ipfs_hashtable_node_free(child_m);
		ipfs_hashtable_node_free(root);
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}

	// Decode and verify link order is alphabetical: a, m, z
	struct HashtableNode* decoded;
	if (!ipfs_hashtable_node_protobuf_decode(enc, enc_written, &decoded)) {
		ipfs_hashtable_node_free(child_z);
		ipfs_hashtable_node_free(child_a);
		ipfs_hashtable_node_free(child_m);
		ipfs_hashtable_node_free(root);
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}

	struct NodeLink* first = decoded->head_link;
	struct NodeLink* second = first != NULL ? first->next : NULL;
	struct NodeLink* third = second != NULL ? second->next : NULL;

	int ret = 1;
	if (first == NULL || strcmp(first->name, "a") != 0) {
		printf("link ordering: expected first='a', got '%s'\n", first ? first->name : "null");
		ret = 0;
	} else if (second == NULL || strcmp(second->name, "m") != 0) {
		printf("link ordering: expected second='m', got '%s'\n", second ? second->name : "null");
		ret = 0;
	} else if (third == NULL || strcmp(third->name, "z") != 0) {
		printf("link ordering: expected third='z', got '%s'\n", third ? third->name : "null");
		ret = 0;
	}

	ipfs_hashtable_node_free(decoded);
	ipfs_hashtable_node_free(child_z);
	ipfs_hashtable_node_free(child_a);
	ipfs_hashtable_node_free(child_m);
	ipfs_hashtable_node_free(root);
	ipfs_repo_fsrepo_free(fs_repo);
	return ret;
}
