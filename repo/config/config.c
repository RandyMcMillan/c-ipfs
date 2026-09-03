#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libp2p/utils/linked_list.h"
#include "ipfs/repo/config/config.h"
#include "libp2p/os/utils.h"
#include "ipfs/repo/config/bootstrap_peers.h"
#include "ipfs/repo/config/swarm.h"
#include "libp2p/db/filestore.h"
#include "multiaddr/multiaddr.h"

/***
 * public
 */

/***
 * gets the default path from the environment variable and/or the homedir struct
 * NOTE: this allocates memory for result. Clean up after yourself.
 * @param result where the result string will reside.
 * @returns true(1) on success, or false(0)
 */
int config_get_default_path_root(char** result) {
	char* root = os_utils_getenv("IPFS_PATH");
	if (root == NULL) {
		root = os_utils_getenv("HOME");
		*result = malloc( strlen(root) + 7);
		if (*result == NULL)
			return 0;
		strncpy(*result, root, strlen(root)+1);
		strncat(*result, "/.ipfs", 7);
	} else {
		*result = malloc(strlen(root)+1);
		if (*result == NULL)
			return 0;
		strncpy(*result, root, strlen(root)+1);
	}
	return 1;
}

/***
 * Returns the path "extension" relative to the configuration root.
 * If an empty string is provided for config_root, the default root
 * is used. NOTE: be sure to dispose of the memory allocated for result.
 * @param config_root the path to the root of the configuration
 * @param extension the extension to add to the path
 * @param result the result of config_root with extension appended
 * @returns true(1) if everything went okay, false(0) otherwise
 */
int config_path(char* config_root, char* extension, char* result, int max_len) {
	if (strlen(config_root) == 0) {
		char* default_path = NULL;
		int retVal = config_get_default_path_root(&default_path);
		if (!retVal)
			return retVal;
		retVal = os_utils_filepath_join(default_path, extension, result, max_len);
		free(default_path);
		return retVal;
	}
	return os_utils_filepath_join(config_root, extension, result, max_len);
}

/**
 * provide the full path of the config file, given the directory.
 * NOTE: This allocates memory for result. Make sure to clean up after yourself.
 * @param path the path to the config file (without the actual file name)
 * @param result the full filename including the path
 * @returns true(1) on success, false(0) otherwise
 */
int repo_config_get_file_name(char* path, char** result) {
	unsigned long max_len = strlen(path) + 8;
	*result = malloc(sizeof(char) * max_len);
	if (result == NULL)
		return 0;
	
	return os_utils_filepath_join(path, "config", *result, max_len);
}

int ipfs_repo_config_is_valid_identity(struct Identity* identity) {
	if (identity == NULL || identity->peer == NULL || identity->peer->id == NULL || identity->peer->id[0] != 'Q' || identity->peer->id[1] != 'm')
		return 0;
	return 1;
}

/***
 * create a configuration based on the passed in parameters
 * @param config the configuration struct to be filled in
 * @param num_bits_for_keypair number of bits for the key pair
 * @param repo_path the path to the configuration
 * @param swarm_port the port to run on
 * @param bootstrap_peers vector of Multiaddresses of fellow peers
 * @returns true(1) on success, otherwise 0
 */
int ipfs_repo_config_init(struct RepoConfig* config, unsigned int num_bits_for_keypair, const char* repo_path, int swarm_port, struct Libp2pVector *bootstrap_peers) {
	// identity
	int counter = 0;
	while (counter < 5) {
		if (counter > 0) {
			repo_config_identity_clear(config->identity);
		}
		if (!repo_config_identity_init(config->identity, num_bits_for_keypair))
			return 0;
		if (ipfs_repo_config_is_valid_identity(config->identity))
			break;
		// we didn't get it right, try again
		counter++;
	}

	if (counter == 5)
		return 0;
	
	// bootstrap peers
	if (bootstrap_peers != NULL) {
		config->bootstrap_peers = libp2p_utils_vector_new(bootstrap_peers->total);
		for(int i = 0; i < bootstrap_peers->total; i++) {
			const struct MultiAddress* orig = (const struct MultiAddress*) libp2p_utils_vector_get(bootstrap_peers, i);
			libp2p_utils_vector_add(config->bootstrap_peers, multiaddress_copy(orig));
		}
	}
	else {
		if (!repo_config_bootstrap_peers_retrieve(&(config->bootstrap_peers)))
			return 0;
	}
	
	// datastore
	if (!libp2p_datastore_init(config->datastore, repo_path))
		return 0;

	// swarm addresses
	char* addr1 = malloc(64);
	if (addr1 != NULL) {
		sprintf(addr1, "/ip4/0.0.0.0/tcp/%d", swarm_port);
		config->addresses->swarm_head = libp2p_utils_linked_list_new();
		if (config->addresses->swarm_head != NULL) {
			config->addresses->swarm_head->item = malloc(strlen(addr1) + 1);
			if (config->addresses->swarm_head->item != NULL) {
				strcpy(config->addresses->swarm_head->item, addr1);
			}

			sprintf(addr1, "/ip6/::/tcp/%d", swarm_port);
			config->addresses->swarm_head->next = libp2p_utils_linked_list_new();
			if (config->addresses->swarm_head->next != NULL) {
				config->addresses->swarm_head->next->item = malloc(strlen(addr1) + 1);
				if (config->addresses->swarm_head->next->item != NULL)
					strcpy(config->addresses->swarm_head->next->item, addr1);
			}
		}
		int port_adder = swarm_port - 4001;
		sprintf(addr1, "/ip4/127.0.0.1/tcp/%d", 5001 + port_adder);
		config->addresses->api = malloc(strlen(addr1)+1);
		if (config->addresses->api != NULL)
			strcpy(config->addresses->api, addr1);
		sprintf(addr1, "/ip4/127.0.0.1/tcp/%d", 8080 + port_adder);
		config->addresses->gateway = malloc(strlen(addr1)+1);
		if (config->addresses->gateway != NULL)
			strcpy(config->addresses->gateway, addr1);
		free(addr1);
	}

	config->discovery.mdns.enabled = 1;
	config->discovery.mdns.interval = 10;
	config->discovery.relay.enabled = 0;
	config->discovery.relay.hop = 0;
	config->discovery.autonat.enabled = 0;
	config->discovery.hole_punch.enabled = 0;
	
	config->mounts.ipfs = "/ipfs";
	config->mounts.ipns = "/ipns";
	
	config->ipns.resolve_cache_size = 128;
	
	config->reprovider.interval = "12h";
	
	config->gateway->root_redirect = "";
	config->gateway->writable = 0;
	
	config->gateway->path_prefixes.num_elements = 0;

	// gateway http headers
	char** header_array = (char * []) { "Access-Control-Allow-Origin", "Access-Control-Allow-Methods", "Access-Control-Allow-Headers" };
	char** header_values = (char*[])  { "*", "GET", "X-Requested-With" };
	if (!repo_config_gateway_http_header_init(config->gateway->http_headers, header_array, header_values, 3))
		return 0;
	
	return 1;
}

/***
 * Initialize memory for a RepoConfig struct
 * @param config the structure to initialize
 * @returns true(1) on success
 */
int ipfs_repo_config_new(struct RepoConfig** config) {
	*config = (struct RepoConfig*)malloc(sizeof(struct RepoConfig));
	if (*config == NULL)
		return 0;

	// set initial values
	(*config)->bootstrap_peers = NULL;

	int retVal = 1;
	retVal = repo_config_identity_new(&((*config)->identity));
	if (retVal == 0)
		return 0;

	retVal = libp2p_datastore_new(&((*config)->datastore));
	if (retVal == 0)
		return 0;

	(*config)->filestore = libp2p_filestore_new();

	retVal = repo_config_addresses_new(&((*config)->addresses));
	if (retVal == 0)
		return 0;

	retVal = repo_config_gateway_new(&((*config)->gateway));
	if (retVal == 0)
		return 0;

	if (!repo_config_replication_new(&((*config)->replication)))
		return 0;

	return 1;
}

/**
 * Free resources
 * @param config the struct to be freed
 * @returns true(1) on success
 */
int ipfs_repo_config_free(struct RepoConfig* config) {
	if (config != NULL) {
		if (config->identity != NULL)
			repo_config_identity_free(config->identity);
		if (&(config->bootstrap_peers) != NULL)
			repo_config_bootstrap_peers_free(config->bootstrap_peers);
		if (config->datastore != NULL)
			libp2p_datastore_free(config->datastore);
		if (config->filestore != NULL)
			libp2p_filestore_free(config->filestore);
		if (config->addresses != NULL)
			repo_config_addresses_free(config->addresses);
		if (config->gateway != NULL)
			repo_config_gateway_free(config->gateway);
		if (config->replication != NULL)
			repo_config_replication_free(config->replication);
		free(config);
	}
	return 1;
}

int repo_config_martial_to_json(struct RepoConfig* config) {
	return 0;
}
