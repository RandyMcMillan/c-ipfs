#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>

#include "ipfs/cmd/ipfs/init.h"
#include "ipfs/commands/request.h"
#include "ipfs/commands/command_option.h"
#include "libp2p/os/utils.h"
#include "ipfs/core/ipfs_node.h"
#include "ipfs/core/builder.h"
#include "ipfs/repo/config/config.h"
#include "ipfs/repo/fsrepo/fs_repo.h"
#include "ipfs/namesys/publisher.h"

const int nBitsForKeypairDefault = 2048;

/***
 * runs before major processing during initialization
 * @param request the request
 * @returns 0 if a problem, otherwise a 1
 */
int init_pre_run(struct Request* request) {
	// Check if repo is locked by another process (daemon running)
	char* repo_path = request->invoc_context->config_root;
	if (repo_path != NULL) {
		char lock_path[512];
		int len = snprintf(lock_path, sizeof(lock_path), "%s/repo.lock", repo_path);
		if (len > 0 && (size_t)len < sizeof(lock_path)) {
			int fd = open(lock_path, O_RDWR | O_CREAT, 0600);
			if (fd >= 0) {
				int locked = flock(fd, LOCK_EX | LOCK_NB);
				if (locked != 0) {
					close(fd);
					fprintf(stderr, "Error: ipfs daemon is already running in this repo\n");
					return 0;
				}
				flock(fd, LOCK_UN);
				close(fd);
			}
		}
	}
	return 1;
}

/**
 * This actually opens the repo and gets things set up
 * @param repo the repo information
 * @returns true(1) on success
 */
int initialize_ipns_keyspace(struct FSRepo* repo) {
	// open fs repo
	int retVal = ipfs_repo_fsrepo_open(repo);
	if (retVal == 0)
		return 0;

	// Build an offline node for IPNS keyspace initialization
	struct IpfsNode* ipfs_node = NULL;
	retVal = ipfs_node_offline_new(repo->path, &ipfs_node);
	if (retVal == 0 || ipfs_node == NULL)
		return 0;

	// Setup offline routing
	ipfs_node->routing = ipfs_routing_new_offline(ipfs_node, &repo->config->identity->private_key);
	if (ipfs_node->routing == NULL) {
		ipfs_node_free(ipfs_node);
		return 0;
	}

	/* Publish an empty directory to initialize the IPNS keyspace.
	 * Kubo uses /ipfs/QmUNLLsPACCz1vLxQVkX7LXxXzr6bFt8hehz5GXhPxCgTz */
	if (!ipfs_namesys_publisher_publish(ipfs_node, "/ipfs/QmUNLLsPACCz1vLxQVkX7LXxXzr6bFt8hehz5GXhPxCgTz")) {
		fprintf(stderr, "Warning: failed to initialize IPNS keyspace.\n");
	}
	ipfs_node_free(ipfs_node);
	return 1;
}

/**
 * called by init_run, to do the heavy lifting
 * @param out_file an output stream (stdout)
 * @param repo_root a path that is where the .ipfs directory will be put
 * @param empty true(1) if empty, false(0) if not
 * @param num_bits_for_keypair number of bits for key pair
 * @param conf the configuration struct
 * @returns 0 on error, 1 on success
 */
int do_init(FILE* out_file, char* repo_root, int empty, int num_bits_for_keypair, struct RepoConfig* conf) {
	// make sure the directory is writable
	if (!os_utils_directory_writeable(repo_root))
		return 0;
	// verify that it is not already initialized
	if (fs_repo_is_initialized(repo_root))
		return 0;
	// If the conf is null, make one
	if (conf == NULL) {
		if (ipfs_repo_config_new(&conf) == 0)
			return 0;
	}
	if (conf->identity == NULL || conf->identity->peer == NULL || conf->identity->peer->id == NULL) {
		int retVal = ipfs_repo_config_init(conf, num_bits_for_keypair, repo_root, 4001, NULL);
		if (retVal == 0)
			return 0;
	}
	// initialize the fs repo
	struct FSRepo* repo;
	int retVal = ipfs_repo_fsrepo_new(repo_root, conf, &repo);
	if (retVal == 0)
		return 0;
	retVal = ipfs_repo_fsrepo_init(repo);
	if (retVal == 0)
		return 0;

	// Add default assets (readme)
	if (!empty) {
		char readme_path[512];
		snprintf(readme_path, sizeof(readme_path), "%s/README.md", repo_root);
		FILE* readme = fopen(readme_path, "w");
		if (readme != NULL) {
			fprintf(readme, "# IPFS Repository\n\nThis is an IPFS repository.\n");
			fclose(readme);
		}
	}
	return initialize_ipns_keyspace(repo);
}

/***
 * does major processing during initialization
 * @param request the request
 * @returns 0 if a problem, otherwise a 1
 */
int init_run(struct Request* request) {
	if (!request || !request->invoc_context || !request->invoc_context->config_root) {
		fprintf(stderr, "Error: init requires a valid repository path.\n");
		return 0;
	}

	struct RepoConfig* conf;
	if (ipfs_repo_config_new(&conf) == 0)
		return 0;

	// Handle config file imports passed in request arguments
	if (request->arguments != NULL && strlen(request->arguments) > 0) {
		FILE* config_file = fopen(request->arguments, "r");
		if (config_file != NULL) {
			fseek(config_file, 0, SEEK_END);
			long fsize = ftell(config_file);
			fseek(config_file, 0, SEEK_SET);
			char* config_str = malloc(fsize + 1);
			if (config_str != NULL) {
				size_t read_bytes = fread(config_str, 1, fsize, config_file);
				config_str[read_bytes] = '\0';
				// TODO: parse JSON config and merge into conf
				free(config_str);
			}
			fclose(config_file);
		}
	}

	int num_bits_for_key_pair = request->cmd.options[0]->default_int_val;
	if (num_bits_for_key_pair < 1024) {
		fprintf(stderr, "Error: key size must be at least 1024 bits.\n");
		return 0;
	}

	return do_init(stdout, request->invoc_context->config_root, 1, num_bits_for_key_pair, conf);
}

/***
 * does the cleanup after major processing during initialization
 * @param request the request
 * @returns 0 if a problem, otherwise a 1
 */
int init_post_run(struct Request* request) {
	// nothing to do
	return 1;
}

int ipfs_cmd_ipfs_init_command_new(struct Command* cmd) {
	int retVal = 1;

	// help text
	cmd->help_text.tagline = "Initializes IPFS config file.";
	cmd->help_text.short_description = "\nInitializes IPFS configuration files and generates a new keypair.\n\nipfs uses a repository in the local file system. By default, the repo is\nlocated at ~/.ipfs. To change the repo location, set the $IPFS_PATH\nenvironment variable.:\n\n    export IPFS_PATH=/path/to/ipfsrepo";

	cmd->argument_count = 1;
	cmd->option_count = 2;
	commands_command_init(cmd);
	// allocate memory for array of pointers
	retVal = commands_argument_init(cmd->arguments[0], "default-config", 0, 0, "Initialize with the given configuration");
	if (retVal == 0)
		return 0;
	cmd->arguments[0]->enable_stdin = 1;
	
	// options
	cmd->options[0]->name_count = 2;
	retVal = commands_command_option_init(cmd->options[0], "Number of bits to use in the generated RSA private key");
	cmd->options[0]->names[0] = "bits";
	cmd->options[0]->names[1] = "b";
	cmd->options[0]->kind = integer;
	cmd->options[0]->default_int_val = nBitsForKeypairDefault;
	cmd->options[1]->name_count = 2;
	retVal = commands_command_option_init(cmd->options[1], "Don't add and pin help files to the local storage");
	cmd->options[1]->default_bool_val = 0;
	cmd->options[1]->names[0] = "empty-repo";
	cmd->options[1]->names[1] = "e";

	// function pointers
	cmd->pre_run = init_pre_run;
	cmd->run = init_run;
	cmd->post_run = init_post_run;
	
	return retVal;
}

/***
 * Uninitializes all the dynamic memory caused by get_init_command
 * @param command the struct
 * @returns 0 on failure, otherwise 1
 */
int ipfs_cmd_ipfs_init_command_free(struct Command* command) {
	// NOTE: commands_command_free takes care of arguments and command_options
	commands_command_free(command);
	return 1;
}
