#pragma once

#include "libp2p/utils/vector.h"

/**
 * Make an IPFS directory at the passed in path
 * @param path the path
 * @param swarm_port the port that the swarm will run on
 * @param bootstrap_peers a Vector of MultiAddress of fellow peers
 * @param peer_id the peer id generated
 * @returns true(1) on success, false(0) on failure
 */
int make_ipfs_repository(const char* path, int swarm_port, struct Libp2pVector* bootstrap_peers, char **peer_id);

/**
 * Initialize a repository, called from the command line
 * @param argc number of arguments
 * @param argv arguments
 * @returns true(1) on success
 */
int ipfs_repo_init(int argc, char** argv);

/**
 * Get the correct repo directory. Looks in all the appropriate places
 * for the ipfs directory.
 * @param argc number of command line arguments
 * @param argv command line arguments
 * @param repo_dir the results. This will point to the [IPFS_PATH]/.ipfs directory
 * @returns true(1) if the directory is there, false(0) if it is not.
 */
/**
 * Get the correct repo home directory. This first looks at the
 * command line, then the IPFS_PATH environment variable,
 * then the user's home directory.
 * @param argc number of command line parameters
 * @param argv command line parameters
 * @returns the repo home directory
 */
char* ipfs_repo_get_home_directory(int argc, char** argv);

int ipfs_repo_get_directory(int argc, char** argv, char** repo_dir);

