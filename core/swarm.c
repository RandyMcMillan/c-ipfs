#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libp2p/utils/logger.h"
#include "ipfs/core/ipfs_node.h"
#include "ipfs/core/swarm.h"
#include "ipfs/core/http_request.h"

static void print_swarm_help(FILE* out) {
	fprintf(out, "Usage: ipfs swarm <command> [<args>]\n");
	fprintf(out, "\n");
	fprintf(out, "Available commands:\n");
	fprintf(out, "  connect <multiaddress>     Connect to a peer\n");
	fprintf(out, "  disconnect <multiaddress>  Disconnect from a peer (not implemented yet)\n");
	fprintf(out, "  peers                      List connected peers\n");
	fprintf(out, "\n");
	fprintf(out, "Examples:\n");
	fprintf(out, "  ipfs swarm connect /ip4/127.0.0.1/tcp/4001/p2p/QmPeerID\n");
	fprintf(out, "  ipfs swarm peers\n");
}

/***
 * Connect to a swarm
 * @param local_node the local node
 * @param address the address of the remote
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_swarm_connect(struct IpfsNode* local_node, const char* address) {
	char* response = NULL;
	size_t response_size;
	// use the API to connect
	struct HttpRequest* request = ipfs_core_http_request_new();
	if (request == NULL)
		return 0;
	request->command = "swarm";
	request->sub_command = "connect";
	libp2p_utils_vector_add(request->arguments, address);
	int retVal = ipfs_core_http_request_get(local_node, request, &response, &response_size);
	if (response != NULL && response_size > 0) {
		fwrite(response, 1, response_size, stdout);
		free(response);
	}
	ipfs_core_http_request_free(request);
	return retVal;
}

/***
 * List swarm peers
 * @param local_node the local node
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_swarm_peers(struct IpfsNode* local_node) {
	char* response = NULL;
	size_t response_size;
	struct HttpRequest* request = ipfs_core_http_request_new();
	if (request == NULL)
		return 0;
	request->command = "swarm";
	request->sub_command = "peers";
	int retVal = ipfs_core_http_request_get(local_node, request, &response, &response_size);
	if (response != NULL && response_size > 0) {
		fwrite(response, 1, response_size, stdout);
		free(response);
	}
	ipfs_core_http_request_free(request);
	return retVal;
}

/***
 * Handle command line swarm call
 */
int ipfs_swarm (struct CliArguments* args) {
	int retVal = 0;
	struct IpfsNode* client_node = NULL;

	if (args->argc < (args->verb_index + 2)) {
		print_swarm_help(stdout);
		retVal = 1;
		goto exit;
	}

	const char* which = args->argv[args->verb_index + 1];
	if (strcmp(which, "help") == 0 || strcmp(which, "--help") == 0 || strcmp(which, "-h") == 0) {
		print_swarm_help(stdout);
		retVal = 1;
		goto exit;
	}

	// make sure API is running
	if (!ipfs_node_offline_new(args->config_dir, &client_node)) {
		libp2p_logger_error("swarm", "Unable to create offline node.\n");
		goto exit;
	}
	if (client_node->mode != MODE_API_AVAILABLE) {
		libp2p_logger_error("swarm", "API must be running.\n");
		goto exit;
	}

	const char* path = NULL;
	if (args->argc > args->verb_index + 2)
		path = args->argv[args->verb_index + 2];

	// determine what we're doing
	if (strcmp(which, "connect") == 0) {
		if (!path) {
			print_swarm_help(stderr);
			goto exit;
		}
		retVal = ipfs_swarm_connect(client_node, path);
	} else if (strcmp(which, "peers") == 0) {
		retVal = ipfs_swarm_peers(client_node);
	} else if (strcmp(which, "disconnect") == 0) {
		libp2p_logger_error("swarm", "Swarm disconnect not implemented yet.\n");
		retVal = 0;
	} else {
		print_swarm_help(stderr);
		goto exit;
	}

	exit:
	// shut everything down
	ipfs_node_free(client_node);

	return retVal;
}
