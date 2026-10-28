#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "ipfs/cid/cid.h"
#include "ipfs/core/http_request.h"
#include "ipfs/importer/exporter.h"
#include "ipfs/merkledag/merkledag.h"
#include "ipfs/merkledag/node.h"
#include "ipfs/repo/fsrepo/fs_repo.h"
#include "ipfs/repo/init.h"
#include "ipfs/core/ipfs_node.h"
#include "libp2p/utils/logger.h"
#include "ipfs/namesys/name.h"
#include "ipfs/repo/fsrepo/jsmn.h"

/**
 * pull objects from ipfs
 */

/***
 * Helper method to retrieve a protobuf'd Node from the router
 * @param local_node the context
 * @param hash the hash to retrieve
 * @param hash_size the length of the hash
 * @param result a place to store the Node
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_exporter_get_node(struct IpfsNode* local_node, const unsigned char* hash, const size_t hash_size,
		struct HashtableNode** result) {
	unsigned char *buffer = NULL;
	size_t buffer_size = 0;
	int retVal = 0;
	struct KademliaMessage* msg = NULL;
	struct Block* block = NULL;
	struct Cid* cid = NULL;

	if (result == NULL)
		return 0;
	*result = NULL;

	// 1. Try the local blockstore / datastore first.
	if (ipfs_merkledag_get(hash, hash_size, result, local_node->repo)) {
		return 1;
	}

	// 2. If we are online, ask the network via the Bitswap exchange.
	if (local_node->mode != MODE_OFFLINE && local_node->exchange != NULL && local_node->exchange->GetBlock != NULL) {
		cid = ipfs_cid_new(0, hash, hash_size, CID_DAG_PROTOBUF);
		if (cid != NULL && local_node->exchange->GetBlock(local_node->exchange, cid, &block) && block != NULL) {
			if (ipfs_hashtable_node_protobuf_decode(block->data, block->data_length, result)) {
				(*result)->hash_size = hash_size;
				(*result)->hash = (unsigned char*)malloc(hash_size);
				if ((*result)->hash != NULL) {
					memcpy((*result)->hash, hash, hash_size);
					retVal = 1;
				} else {
					ipfs_hashtable_node_free(*result);
					*result = NULL;
				}
			}
		}
	}

	// 3. Fall back to the legacy routing/DHT value path (used for IPNS names, etc.).
	if (!retVal && local_node->routing != NULL && local_node->routing->GetValue != NULL) {
		if (local_node->routing->GetValue(local_node->routing, hash, hash_size, (void**)&buffer, &buffer_size)) {
			if (ipfs_hashtable_node_protobuf_decode(buffer, buffer_size, result)) {
				(*result)->hash_size = hash_size;
				(*result)->hash = (unsigned char*)malloc(hash_size);
				if ((*result)->hash != NULL) {
					memcpy((*result)->hash, hash, hash_size);
					retVal = 1;
				} else {
					ipfs_hashtable_node_free(*result);
					*result = NULL;
				}
			}
		}
	}

	if (!retVal) {
		libp2p_logger_debug("exporter", "get_node could not retrieve hash locally or from network.\n");
	}

	exit:
	if (cid != NULL)
		ipfs_cid_free(cid);
	if (block != NULL)
		ipfs_block_free(block);
	if (buffer != NULL)
		free(buffer);
	if (msg != NULL)
		libp2p_message_free(msg);

	return retVal;
}

/***
 * Get a file by its hash, and write the data to a filestream
 * @param hash the base58 multihash of the cid
 * @param file_descriptor where to write
 * @param local_node the context
 */
int ipfs_exporter_to_filestream(const unsigned char* hash, FILE* file_descriptor, struct IpfsNode* local_node) {

	// convert hash to cid
	struct Cid* cid = NULL;
	if ( ipfs_cid_decode_hash_from_base58(hash, strlen((char*)hash), &cid) == 0) {
		return 0;
	}

	// find block
	struct HashtableNode* read_node = NULL;
	if (!ipfs_exporter_get_node(local_node, cid->hash, cid->hash_length, &read_node)) {
		ipfs_cid_free(cid);
		return 0;
	}

	// no longer need the cid
	ipfs_cid_free(cid);

	if (read_node->head_link == NULL) {
		// convert the node's data into a UnixFS data block
		struct UnixFS* unix_fs;
		ipfs_unixfs_protobuf_decode(read_node->data, read_node->data_size, &unix_fs);
		size_t bytes_written = fwrite(unix_fs->bytes, 1, unix_fs->bytes_size, file_descriptor);
		if (bytes_written != unix_fs->bytes_size) {
			ipfs_hashtable_node_free(read_node);
			ipfs_unixfs_free(unix_fs);
			return 0;
		}
		ipfs_unixfs_free(unix_fs);
	} else {
		struct NodeLink* link = read_node->head_link;
		struct HashtableNode* link_node = NULL;
		while (link != NULL) {
			if ( !ipfs_exporter_get_node(local_node, link->hash, link->hash_size, &link_node)) {
				ipfs_hashtable_node_free(read_node);
				return 0;
			}
			struct UnixFS* unix_fs;
			ipfs_unixfs_protobuf_decode(link_node->data, link_node->data_size, &unix_fs);
			size_t bytes_written = fwrite(unix_fs->bytes, 1, unix_fs->bytes_size, file_descriptor);
			if (bytes_written != unix_fs->bytes_size) {
				ipfs_hashtable_node_free(link_node);
				ipfs_hashtable_node_free(read_node);
				ipfs_unixfs_free(unix_fs);
				return 0;
			}
			ipfs_hashtable_node_free(link_node);
			ipfs_unixfs_free(unix_fs);
			link = link->next;
		}
	}

	if (read_node != NULL)
		ipfs_hashtable_node_free(read_node);

	return 1;
}


/**
 * get a file by its hash, and write the data to a file
 * @param hash the base58 multihash of the cid
 * @param file_name the file name to write to
 * @returns true(1) on success
 */
int ipfs_exporter_to_file(const unsigned char* hash, const char* file_name, struct IpfsNode *local_node) {
	// process blocks
	FILE* file = fopen(file_name, "wb");
	if (file == NULL) {
		return 0;
	}
	int retVal = ipfs_exporter_to_filestream(hash, file, local_node);
	fclose(file);
	return retVal;
}

/**
 * get a file by its hash, and write the data to a file
 * @param hash the base58 multihash of the cid
 * @param file_name the file name to write to
 * @returns true(1) on success
 */
int ipfs_exporter_to_console(const unsigned char* hash, struct IpfsNode *local_node) {
	// convert hash to cid
	struct Cid* cid = NULL;
	if ( ipfs_cid_decode_hash_from_base58(hash, strlen((char*)hash), &cid) == 0) {
		return 0;
	}

	// find block
	struct HashtableNode* read_node = NULL;
	if (!ipfs_exporter_get_node(local_node, cid->hash, cid->hash_length, &read_node)) {
		ipfs_cid_free(cid);
		return 0;
	}

	// no longer need the cid
	ipfs_cid_free(cid);

	// process blocks
	struct NodeLink* link = read_node->head_link;
	printf("{Links:[");
	while (link != NULL) {
		unsigned char b58[100];
		ipfs_cid_hash_to_base58(link->hash, link->hash_size, b58, 100);
		printf("{\"Name\":\"%s\",\"Hash\":\"%s\",\"Size\":%lu}", (link->name != NULL ? link->name : ""), (char*)b58, link->t_size);
		link = link->next;
	}
	printf("],\"Data\":\"");
	for(size_t i = 0LU; i < read_node->data_size; i++) {
		printf("%02x", read_node->data[i]);
	}
	printf("\"}\n");

	if (read_node != NULL)
		ipfs_hashtable_node_free(read_node);

	return 1;
}

/***
 * Called from the command line with ipfs object get [hash].
 * Retrieves the object pointed to by hash, and displays
 * its block data (links and data elements)
 * @param argc number of arguments
 * @param argv arguments
 * @returns true(1) on success
 */
int ipfs_exporter_object_get(int argc, char** argv) {
	char* repo_path = NULL;

	if (!ipfs_repo_get_directory(argc, argv, &repo_path)) {
		fprintf(stderr, "Unable to open repository: %s\n", repo_path);
		return 0;
	}

	struct IpfsNode* local_node = NULL;
	if (!ipfs_node_online_new(repo_path, &local_node))
		return 0;

	// find hash
	int retVal = ipfs_exporter_to_console((unsigned char*)argv[3], local_node);

	ipfs_node_free(local_node);

	return retVal;
}

/**
 * rebuild a file based on this HashtableNode, traversing links
 * @param node the HashtableNode to start with
 * @param local_node the context
 * @param file the filestream to fill
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_exporter_cat_node(struct HashtableNode* node, struct IpfsNode* local_node, FILE *file) {
	// process this node, then move on to the links

	// build the unixfs
	struct UnixFS* unix_fs;
	if (!ipfs_unixfs_protobuf_decode(node->data, node->data_size, &unix_fs)) {
		return 0;
	}
	for(size_t i = 0LU; i < unix_fs->bytes_size; i++) {
		fprintf(file, "%c", unix_fs->bytes[i]);
	}
	ipfs_unixfs_free(unix_fs);
	// process links
	struct NodeLink* current = node->head_link;
	while (current != NULL) {
		// find the node
		struct HashtableNode* child_node = NULL;
		if (!ipfs_exporter_get_node(local_node, current->hash, current->hash_size, &child_node)) {
			return 0;
		}
		ipfs_exporter_cat_node(child_node, local_node, file);
		ipfs_hashtable_node_free(child_node);
		current = current->next;
	}

	return 1;
}

int ipfs_exporter_object_cat_to_file(struct IpfsNode *local_node, unsigned char* hash, int hash_size, FILE* file) {
	struct HashtableNode* read_node = NULL;

	// find block
	if (!ipfs_exporter_get_node(local_node, hash, hash_size, &read_node)) {
		return 0;
	}

	int retVal = ipfs_exporter_cat_node(read_node, local_node, file);
	ipfs_hashtable_node_free(read_node);
	return retVal;
}

/***
 * Called from the command line with ipfs cat [hash]. Retrieves the object
 * pointed to by hash, and displays its raw block data to the console
 * @param argc number of arguments
 * @param argv arguments
 * @returns true(1) on success
 */
int ipfs_exporter_object_cat(struct CliArguments* args, FILE* output_file) {
	struct IpfsNode *local_node = NULL;
	char* repo_dir = NULL;

	if (!ipfs_repo_get_directory(args->argc, args->argv, &repo_dir)) {
		libp2p_logger_error("exporter", "Unable to open repo: %s\n", repo_dir);
		return 0;
	}

	if (!ipfs_node_offline_new(repo_dir, &local_node)) {
		libp2p_logger_error("exporter", "Unable to create new offline node based on config at %s.\n", repo_dir);
		return 0;
	}

	if (local_node->mode == MODE_API_AVAILABLE) {
		const char ipns_prefix[] = "/ipns/";
		const char ipfs_prefix[] = "/ipfs/";
		char* hash = args->argv[args->verb_index + 1];
		libp2p_logger_debug("exporter", "We're attempting to use the API for this object get of %s.\n", hash);
		if (memcmp(hash, ipfs_prefix, sizeof(ipfs_prefix)-1) == 0) {
			// skip ipfs_prefix;
			hash += sizeof(ipfs_prefix)-1;
		} else if (memcmp(hash, ipns_prefix, sizeof(ipns_prefix)-1) == 0) {
			char *response = NULL;
			size_t response_size;
			if (ipfs_name_resolve(local_node, hash, &response, &response_size) && response && response_size > 0) {
				hash = jsmn_simple_parser(response, response_size, "Path");
				if (!hash) {
					return 0;
				}
			}
		}
		struct HttpRequest* request = ipfs_core_http_request_new();
		char* response = NULL;
		request->command = "object";
		request->sub_command = "get";
		request->arguments = libp2p_utils_vector_new(1);
		libp2p_utils_vector_add(request->arguments, hash);
		size_t response_size = 0;
		int retVal = ipfs_core_http_request_post(local_node, request, &response, &response_size, "", 0);
		if (response != NULL && response_size > 0) {
			fwrite(response, 1, response_size, output_file);
			free(response);
		} else {
			retVal = 0;
		}
		ipfs_core_http_request_free(request);
		return retVal;
	} else {
		libp2p_logger_debug("exporter", "API not available, using direct access.\n");
		struct Cid* cid = NULL;
		if ( ipfs_cid_decode_hash_from_base58((unsigned char*)args->argv[args->verb_index+1], strlen(args->argv[args->verb_index+1]), &cid) == 0) {
			libp2p_logger_error("exporter", "Unable to decode hash from base58 [%s]\n", args->argv[args->verb_index+1]);
			return 0;
		}

		int retVal = ipfs_exporter_object_cat_to_file(local_node, cid->hash, cid->hash_length, output_file);
		ipfs_cid_free(cid);

		return retVal;
	}

	return 0;

}
