/**
 * Helpers for testing
 */

/***
 * Drop a repository by deleting the directory
 * @param path the path of the repository
 * @returns true(1) on success, false(0) otherwise
 */
int drop_repository(const char* path);

/**
 * Create a new repository in the directory, erasing old one
 * NOTE: base directory must already exist
 */
int drop_and_build_repository(const char* dir, int swarm_port, struct Libp2pVector* bootstrap_peers, char** peer_id);

/**
 * Create a repository without bootstrap peers for isolated daemon tests.
 */
int drop_and_build_repository_without_bootstrap(const char* dir, int swarm_port, char** peer_id);

/***
 * Drop a repository and build a new one
 * @param path where to create it
 * @param fs_repo the results
 * @returns true(1) on success, otherwise false(0)
 */
int drop_build_and_open_repo(const char* path, struct FSRepo** fs_repo);

/***
 * Drop a repository and build a new one with the specified config file
 * @param path where to create it
 * @param fs_repo the results
 * @param config_file_to_copy where to find the config file to copy
 * @returns true(1) on success, otherwise false(0)
 */
int drop_build_open_repo(const char* path, struct FSRepo** fs_repo, const char* config_file_to_copy);

/***
 * Helper to create a test file in the OS
 * @pram fileName the resultant file name
 * @param bytes the data that goes in the file
 * @param num_bytes the size of the buffer
 */
int create_file(const char* fileName, unsigned char* bytes, size_t num_bytes);

/***
 * Create a buffer with some bytes
 * @param buffer where to put the bytes
 * @param num_bytes how much to fill of the buffer
 */
int create_bytes(unsigned char* buffer, size_t num_bytes);

/***
 * Start a daemon (usefull in a separate thread)
 * @param arg a char string of the repo path
 * @returns NULL
 */
void* test_daemon_start(void* arg);

/**
 * Determine if a file exists in the blockstore
 * @param dir the directory of the blockstore
 * @param filename the file name (a base32 hash)
 * @returns true(1) if the file exists, false(0) otherwise
 */
int have_file_in_blockstore(const char* dir, const char* filename);
