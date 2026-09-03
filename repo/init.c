#include <dirent.h>
#include <sys/stat.h>
#include <string.h>

#include "libp2p/os/utils.h"
#include "ipfs/repo/config/config.h"
#include "ipfs/repo/fsrepo/fs_repo.h"

/**
 * The basic functions for initializing an IPFS repo
 */

static char config_dir_path[512];

/**
 * Get the correct repo home directory. This first looks at the
 * command line, then the IPFS_PATH environment variable,
 * then the user's home directory. This should come back with a
 * directory that contains a config file.
 * @param argc number of command line parameters
 * @param argv command line parameters
 * @returns the repo home directory
 */
char* ipfs_repo_get_home_directory(int argc, char** argv) {
	char *result = NULL;
	// first check the command line
	for(int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
			if (i + 1 < argc) {
				result = argv[i+1];
				break;
			}
		}
	}
	if (result == NULL) { // we didn't pass it on the command line
		// check IPFS_PATH
		result = os_utils_getenv("IPFS_PATH");
	}
	if (result == NULL) { // not on command line nor environment var.
		// get user's home directory (or current directory depending on platform),
		// and add .ipfs to it
		result = os_utils_get_homedir();
		strcpy(config_dir_path, result);
		strcat(config_dir_path, "/.ipfs");
		result = config_dir_path;
	}
	return result;
}

/**
 * Get the correct repo directory. Looks in all the appropriate places
 * for the ipfs directory.
 * @param argc number of command line arguments
 * @param argv command line arguments
 * @param repo_dir the results. This will point to the [IPFS_PATH]/.ipfs directory
 * @returns true(1) if the directory is there, false(0) if it is not.
 */
int ipfs_repo_get_directory(int argc, char** argv, char** repo_dir) {
	*repo_dir = ipfs_repo_get_home_directory(argc, argv);
	return os_utils_directory_exists(*repo_dir);
}

/**
 * Make an IPFS directory at the passed in path
 * @param path the path
 * @param swarm_port the port that the swarm will run on
 * @param bootstrap_peers a Vector of MultiAddress of fellow peers
 * @param peer_id the peer id generated
 * @returns true(1) on success, false(0) on failure
 */
int make_ipfs_repository(const char* path, int swarm_port, struct Libp2pVector* bootstrap_peers, char **peer_id, const char* config_import_file) {
	int retVal = 0;
	char currDirectory[1024];
	struct RepoConfig* repo_config = NULL;
	struct FSRepo* fs_repo = NULL;

	printf("initializing ipfs node at %s\n", path);
	// build a default repo config
	if (!ipfs_repo_config_new(&repo_config))
		goto exit;

	// Handle config file imports if provided
	if (config_import_file != NULL && strlen(config_import_file) > 0) {
		FILE* config_file = fopen(config_import_file, "r");
		if (config_file != NULL) {
			fseek(config_file, 0, SEEK_END);
			long fsize = ftell(config_file);
			fseek(config_file, 0, SEEK_SET);
			char* config_str = malloc(fsize + 1);
			if (config_str != NULL) {
				size_t read_bytes = fread(config_str, 1, fsize, config_file);
				config_str[read_bytes] = '\0';
				// TODO: parse JSON config and merge into conf
				// For now we read and discard; future work: jsmn parse + merge fields
				free(config_str);
			}
			fclose(config_file);
		}
	}

	printf("generating 2048-bit RSA keypair...");
	if (!ipfs_repo_config_init(repo_config, 2048, path, swarm_port, bootstrap_peers)) {
		fprintf(stderr, "Unable to initialize repository at %s\n", path);
		goto exit;
	}
	printf("done\n");
	// now the fs_repo
	if (!ipfs_repo_fsrepo_new(path, repo_config, &fs_repo))
		goto exit;
	// this builds a new repo
	if (!ipfs_repo_fsrepo_init(fs_repo))
		goto exit;

	// give some results to the user
	printf("peer identity: %s\n", fs_repo->config->identity->peer->id);
	if (peer_id != NULL) {
		*peer_id = malloc(fs_repo->config->identity->peer->id_size + 1);
		if (*peer_id != NULL)
			strcpy(*peer_id, fs_repo->config->identity->peer->id);
	}

	// make sure the repository exists
	if (!os_utils_filepath_join(path, "config", currDirectory, 1024))
		goto exit;

	if (!os_utils_file_exists(currDirectory))
		goto exit;

	// cleanup
	retVal = 1;
	exit:
	if (fs_repo != NULL)
		ipfs_repo_fsrepo_free(fs_repo);

	return retVal;
}

/**
 * Initialize a repository
 * @param argc number of command line arguments
 * @param argv command line arguments
 * @returns true(1) on succes, false(0) otherwise
 */
int ipfs_repo_init(int argc, char** argv) {
	char* repo_directory = NULL;
	if (ipfs_repo_get_directory(argc, argv, &repo_directory)) {
		printf("Directory already exists: %s\n", repo_directory);
		return 0;
	}
	// make the directory
	if (!os_mkdir(repo_directory)) {
		return 0;
	}
	// look for optional config import file (first non-switch argument after init)
	const char* config_import = NULL;
	int found_init = 0;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "init") == 0) {
			found_init = 1;
			continue;
		}
		if (found_init && argv[i][0] != '-') {
			config_import = argv[i];
			break;
		}
	}
	// make the repository
	return make_ipfs_repository(repo_directory, 4001, NULL, NULL, config_import);
}
