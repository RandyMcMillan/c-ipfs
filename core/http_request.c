#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "libp2p/crypto/encoding/base64.h"
#include "libp2p/os/memstream.h"
#include "libp2p/utils/vector.h"
#include "libp2p/utils/logger.h"
#include "ipfs/blocks/block.h"
#include "ipfs/blocks/blockstore.h"
#include "ipfs/cid/cid.h"
#include "ipfs/core/http_request.h"
#include "ipfs/importer/exporter.h"
#include "ipfs/namesys/resolver.h"
#include "ipfs/namesys/publisher.h"
#include "ipfs/routing/routing.h"

#ifndef C_IPFS_VERSION
#define C_IPFS_VERSION "0.0.0-dev"
#endif

/**
 * Handles HttpRequest and HttpParam
 */

/***
 * Build a new HttpRequest
 * @returns the newly allocated HttpRequest struct
 */
struct HttpRequest* ipfs_core_http_request_new() {
	struct HttpRequest* result = (struct HttpRequest*) malloc(sizeof(struct HttpRequest));
	if (result != NULL) {
		result->command = NULL;
		result->sub_command = NULL;
		result->params = libp2p_utils_vector_new(1);
		result->arguments = libp2p_utils_vector_new(1);
	}
	return result;
}

/***
 * Clean up resources of a HttpRequest struct
 * @param request the struct to destroy
 */
void ipfs_core_http_request_free(struct HttpRequest* request) {
	if (request != NULL) {
		//if (request->command != NULL)
		//	free(request->command);
		//if (request->sub_command != NULL)
		//	free(request->sub_command);
		if (request->params != NULL) {
			for(int i = 0; i < request->params->total; i++) {
				struct HttpParam* curr_param = (struct HttpParam*) libp2p_utils_vector_get(request->params, i);
				ipfs_core_http_param_free(curr_param);
			}
			libp2p_utils_vector_free(request->params);
		}
		if (request->arguments != NULL) {
			//for(int i = 0; i < request->arguments->total; i++) {
			//	free((char*)libp2p_utils_vector_get(request->arguments, i));
			//}
			libp2p_utils_vector_free(request->arguments);
		}
		if (request->data != NULL)
			free(request->data);
		free(request);
	}
}

struct HttpResponse* ipfs_core_http_response_new() {
	struct HttpResponse* response = (struct HttpResponse*) malloc(sizeof(struct HttpResponse));
	if (response != NULL) {
		response->content_type = NULL;
		response->bytes = NULL;
		response->bytes_size = 0;
	}
	return response;
}

void ipfs_core_http_response_free(struct HttpResponse* response) {
	if (response != NULL) {
		// NOTE: content_type should not be dynamically allocated
		if (response->bytes != NULL)
			free(response->bytes);
		free(response);
	}
}

/***
 * Build a new HttpParam
 * @returns a newly allocated HttpParam struct
 */
struct HttpParam* ipfs_core_http_param_new() {
	struct HttpParam* param = (struct HttpParam*) malloc(sizeof(struct HttpParam));
	if (param != NULL) {
		param->name = NULL;
		param->value = NULL;
	}
	return param;
}

/***
 * Clean up resources allocated by a HttpParam struct
 * @param param the struct to destroy
 */
void ipfs_core_http_param_free(struct HttpParam* param) {
	if (param != NULL) {
		if (param->name != NULL)
			free(param->name);
		if (param->value != NULL)
			free(param->value);
		free(param);
	}
}

/***
 * Handle processing of the "name" command
 * @param local_node the context
 * @param request the incoming request
 * @param response the response
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_core_http_process_name(struct IpfsNode* local_node, struct HttpRequest* request, struct HttpResponse** response) {
	int retVal = 0;
	char* path = NULL;
	char* result = NULL;
	if (request->arguments == NULL || request->arguments->total == 0) {
		libp2p_logger_error("http_request", "process_name: empty arguments\n");
		return 0;
	}
	path = (char*) libp2p_utils_vector_get(request->arguments, 0);
	if (strcmp(request->sub_command, "resolve") == 0) {
		retVal = ipfs_namesys_resolver_resolve(local_node, path, 1, &result);
		if (retVal) {
			*response = ipfs_core_http_response_new();
			struct HttpResponse* res = *response;
			res->content_type = "application/json";
			res->bytes = (uint8_t*) malloc(strlen(local_node->identity->peer->id) + strlen(path) + 30);
			if (res->bytes == NULL) {
				free(result);
				return 0;
			}
			sprintf((char*)res->bytes, "{ \"Path\": \"%s\" }", result);
			res->bytes_size = strlen((char*)res->bytes);
		}
		free(result);
	} else if (strcmp(request->sub_command, "publish") == 0) {
		retVal = ipfs_namesys_publisher_publish(local_node, path);
		if (retVal) {
			*response = ipfs_core_http_response_new();
			struct HttpResponse* res = *response;
			res->content_type = "application/json";
			res->bytes = (uint8_t*) malloc(strlen(local_node->identity->peer->id) + strlen(path) + 30);
			if (res->bytes == NULL)
				return 0;
			sprintf((char*)res->bytes, "{ \"Name\": \"%s\"\n \"Value\": \"%s\" }", local_node->identity->peer->id, path);
			res->bytes_size = strlen((char*)res->bytes);
		}
	}
	return retVal;
}

/***
 * Handle processing of the "object" command
 * @param local_node the context
 * @param request the incoming request
 * @param response the response
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_core_http_process_object(struct IpfsNode* local_node, struct HttpRequest* request, struct HttpResponse** response) {
	int retVal = 0;
	if (strcmp(request->sub_command, "get") == 0) {
		// do an object_get
		if (request->arguments->total == 1) {
			char* hash = (char*)libp2p_utils_vector_get(request->arguments, 0);
			struct Cid* cid = NULL;
			if (!ipfs_cid_decode_hash_from_base58((unsigned char*)hash, strlen(hash), &cid)) {
				ipfs_cid_free(cid);
				cid = NULL;
				return 0;
			}
			*response = ipfs_core_http_response_new();
			struct HttpResponse* res = *response;
			res->content_type = "application/json";
			FILE* response_file = open_memstream((char**)&res->bytes, &res->bytes_size);
			retVal = ipfs_exporter_object_cat_to_file(local_node, cid->hash, cid->hash_length, response_file);
			ipfs_cid_free(cid);
			fclose(response_file);
		}
	}
	return retVal;
}

int ipfs_core_http_process_dht_provide(struct IpfsNode* local_node, struct HttpRequest* request, struct HttpResponse** response) {
	int failedCount = 0;
	for (int i = 0; i < request->arguments->total; i++) {
		char* hash = (char*)libp2p_utils_vector_get(request->arguments, i);
		struct Cid* cid;
		if (!ipfs_cid_decode_hash_from_base58((unsigned char*)hash, strlen(hash), &cid)) {
			ipfs_cid_free(cid);
			cid = NULL;
			failedCount++;
			continue;
		}
		if (!local_node->routing->Provide(local_node->routing, cid->hash, cid->hash_length)) {
			ipfs_cid_free(cid);
			cid = NULL;
			failedCount++;
			continue;
		}
		ipfs_cid_free(cid);
	}
	*response = ipfs_core_http_response_new();
	struct HttpResponse* res = *response;
	res->content_type = "application/json";
	res->bytes = (uint8_t*) malloc(1024);
	if (res->bytes == NULL) {
		res->bytes_size = 0;
	} else {
		if (!failedCount) {
			// complete success
			// TODO: do the right thing
			snprintf((char*)res->bytes, 1024,  "{\n\t\"ID\": \"<string>\"\n" \
					"\t\"Type\": \"<int>\"\n"
					"\t\"Responses\": [\n"
					"\t\t{\n"
					"\t\t\t\"ID\": \"<string>\"\n"
					"\t\t\t\"Addrs\": [\n"
					"\t\t\t\t\"<object>\"\n"
					"\t\t\t]\n"
					"\t\t}\n"
					"\t]\n"
					"\t\"Extra\": \"<string>\"\n"
					"}\n"
					);
		} else {
			// at least some failed
			// TODO: do the right thing
			snprintf((char*)res->bytes, 1024,  "{\n\t\"ID\": \"<string>\",\n" \
					"\t\"Type\": \"<int>\",\n"
					"\t\"Responses\": [\n"
					"\t\t{\n"
					"\t\t\t\"ID\": \"<string>\",\n"
					"\t\t\t\"Addrs\": [\n"
					"\t\t\t\t\"<object>\"\n"
					"\t\t\t]\n"
					"\t\t}\n"
					"\t],\n"
					"\t\"Extra\": \"<string>\"\n"
					"}\n"
					);
		}
		res->bytes_size = strlen((char*)res->bytes);
	}
	return failedCount < request->arguments->total;
}

int ipfs_core_http_process_dht_get(struct IpfsNode* local_node, struct HttpRequest* request, struct HttpResponse** response) {
	int failedCount = 0;
	// for now, we can only handle 1 argument at a time
	if (request->arguments != NULL && request->arguments->total != 1)
		return 0;

	*response = ipfs_core_http_response_new();
	struct HttpResponse* res = *response;
	res->content_type = "application/octet-stream";

	for (int i = 0; i < request->arguments->total; i++) {
		char* hash = (char*)libp2p_utils_vector_get(request->arguments, i);
		struct Cid* cid;
		if (!ipfs_cid_decode_hash_from_base58((unsigned char*)hash, strlen(hash), &cid)) {
			ipfs_cid_free(cid);
			cid = NULL;
			failedCount++;
			continue;
		}
		if (!local_node->routing->GetValue(local_node->routing, cid->hash, cid->hash_length, (void**)&res->bytes, &res->bytes_size)) {
			ipfs_cid_free(cid);
			cid = NULL;
			failedCount++;
			continue;
		}
		ipfs_cid_free(cid);
		//TODO: we need to handle multiple arguments
	}
	return failedCount < request->arguments->total;
}

/***
 * process dht commands
 * @param local_node the context
 * @param request the request
 * @param response where to put the results
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_core_http_process_dht(struct IpfsNode* local_node, struct HttpRequest* request, struct HttpResponse** response) {
	int retVal = 0;
	if (strcmp(request->sub_command, "provide") == 0) {
		// do a dht provide
		retVal = ipfs_core_http_process_dht_provide(local_node, request, response);
	} else if (strcmp(request->sub_command, "get") == 0) {
		// do a dht get
		retVal = ipfs_core_http_process_dht_get(local_node, request, response);
	}
	return retVal;
}




int ipfs_core_http_process_swarm_connect(struct IpfsNode* local_node, struct HttpRequest* request, struct HttpResponse** resp) {
	// get the address
	if (request->arguments == NULL || request->arguments->total < 0)
		return 0;
	const char* address = (char*) libp2p_utils_vector_get(request->arguments, 0);
	if (address == NULL)
		return 0;
	// TODO: see if we are already connected, or at least already have this peer in our peerstore
	// attempt to connect
	struct MultiAddress* ma = multiaddress_new_from_string(address);
	if (ma == NULL) {
		libp2p_logger_error("http_request", "swarm_connect: Unable to convert %s to a MultiAddress.\n", address);
		return 0;
	}
	struct Libp2pPeer* new_peer = libp2p_peer_new_from_multiaddress(ma);
	if (!libp2p_peer_connect(local_node->dialer, new_peer, local_node->peerstore, local_node->repo->config->datastore, 30)) {
		libp2p_logger_error("http_request", "swarm_connect: Unable to connect to peer %s.\n", libp2p_peer_id_to_string(new_peer));
		libp2p_peer_free(new_peer);
		multiaddress_free(ma);
		return 0;
	}
	*resp = ipfs_core_http_response_new();
	struct HttpResponse* response = *resp;
	if (response == NULL) {
		libp2p_logger_error("http_response", "swarm_connect: Unable to allocate memory for the response.\n");
		libp2p_peer_free(new_peer);
		multiaddress_free(ma);
		return 0;
	}
	response->content_type = "application/json";
	char* json = "{ \"Strings\": [ \"%s\"] }";
	response->bytes_size =  strlen(json) + strlen(address) + 1;
	response->bytes = (uint8_t*) malloc(response->bytes_size);
	if (response->bytes == NULL) {
		response->bytes_size = 0;
		response->content_type = NULL;
		libp2p_peer_free(new_peer);
		multiaddress_free(ma);
		return 0;
	}
	sprintf((char*)response->bytes, json, address);
	// getting rid of the peer here will close the connection. That's not what we want
	//libp2p_peer_free(new_peer);
	multiaddress_free(ma);
	return 1;

}

int ipfs_core_http_process_swarm(struct IpfsNode* local_node, struct HttpRequest* request, struct HttpResponse** response) {
	int retVal = 0;
	if (strcmp(request->sub_command, "connect") == 0) {
		// connect to a peer
		retVal = ipfs_core_http_process_swarm_connect(local_node, request, response);
	}
	return retVal;
}

static int ipfs_core_http_process_id(struct IpfsNode* local_node, struct HttpRequest* request, struct HttpResponse** response) {
	(void)request;
	*response = ipfs_core_http_response_new();
	if (!*response) return 0;
	struct HttpResponse* res = *response;
	res->content_type = "application/json";

	// Base64-encode the public key DER
	size_t pk_b64_max = libp2p_crypto_encoding_base64_encode_size(local_node->identity->private_key.public_key_length);
	char* pk_b64 = malloc(pk_b64_max + 1);
	if (!pk_b64) {
		ipfs_core_http_response_free(res);
		*response = NULL;
		return 0;
	}
	size_t pk_b64_len = 0;
	libp2p_crypto_encoding_base64_encode(
		(const unsigned char*)local_node->identity->private_key.public_key_der,
		local_node->identity->private_key.public_key_length,
		(unsigned char*)pk_b64, pk_b64_max, &pk_b64_len);
	pk_b64[pk_b64_len] = '\0';

	// Build addresses array from swarm_head linked list
	char addr_buf[1024] = "";
	struct Libp2pLinkedList* curr = local_node->repo->config->addresses->swarm_head;
	int first = 1;
	while (curr) {
		if (!first) strncat(addr_buf, ",", sizeof(addr_buf) - strlen(addr_buf) - 1);
		strncat(addr_buf, "\"", sizeof(addr_buf) - strlen(addr_buf) - 1);
		strncat(addr_buf, (char*)curr->item, sizeof(addr_buf) - strlen(addr_buf) - 1);
		strncat(addr_buf, "\"", sizeof(addr_buf) - strlen(addr_buf) - 1);
		first = 0;
		curr = curr->next;
	}

	int bytes_needed = 256 + strlen(local_node->identity->peer->id) + pk_b64_len + strlen(addr_buf);
	res->bytes = (uint8_t*)malloc(bytes_needed);
	if (!res->bytes) {
		free(pk_b64);
		ipfs_core_http_response_free(res);
		*response = NULL;
		return 0;
	}
	snprintf((char*)res->bytes, bytes_needed,
		"{"
		"\"ID\":\"%s\","
		"\"PublicKey\":\"%s\","
		"\"Addresses\":[%s],"
		"\"AgentVersion\":\"c-ipfs/%s\","
		"\"ProtocolVersion\":\"ipfs/0.1.0\""
		"}",
		local_node->identity->peer->id,
		pk_b64,
		addr_buf,
		C_IPFS_VERSION);
	res->bytes_size = strlen((char*)res->bytes);
	free(pk_b64);
	return 1;
}

static int ipfs_core_http_process_version(struct IpfsNode* local_node, struct HttpRequest* request, struct HttpResponse** response) {
	(void)local_node;
	(void)request;
	*response = ipfs_core_http_response_new();
	if (!*response) return 0;
	struct HttpResponse* res = *response;
	res->content_type = "application/json";
	res->bytes = (uint8_t*)strdup("{\"Version\":\"" C_IPFS_VERSION "\",\"Commit\":\"\",\"Repo\":\"12\",\"System\":\"amd64/darwin\",\"Golang\":\"\"}");
	res->bytes_size = strlen((char*)res->bytes);
	return 1;
}

static int ipfs_core_http_process_block_get(struct IpfsNode* local_node, struct HttpRequest* request, struct HttpResponse** response) {
	if (!request->arguments || request->arguments->total < 1) return 0;
	char* hash = (char*)libp2p_utils_vector_get(request->arguments, 0);
	struct Cid* cid = NULL;
	if (!ipfs_cid_decode_hash_from_base58((unsigned char*)hash, strlen(hash), &cid)) {
		return 0;
	}
	struct Block* block = NULL;
	int retVal = ipfs_blockstore_get(local_node->blockstore->blockstoreContext, cid, &block);
	ipfs_cid_free(cid);
	if (!retVal || !block) {
		return 0;
	}
	*response = ipfs_core_http_response_new();
	if (!*response) {
		ipfs_block_free(block);
		return 0;
	}
	struct HttpResponse* res = *response;
	res->content_type = "application/vnd.ipld.raw";
	res->bytes = (uint8_t*)malloc(block->data_length);
	if (!res->bytes) {
		ipfs_core_http_response_free(res);
		*response = NULL;
		ipfs_block_free(block);
		return 0;
	}
	memcpy(res->bytes, block->data, block->data_length);
	res->bytes_size = block->data_length;
	ipfs_block_free(block);
	return 1;
}

static int ipfs_core_http_process_block_put(struct IpfsNode* local_node, struct HttpRequest* request, struct HttpResponse** response) {
	if (!request->data || request->data_size == 0) return 0;

	struct Block* block = ipfs_block_new_raw(request->data, request->data_size);
	if (!block) return 0;

	size_t written = 0;
	if (!ipfs_blockstore_put(local_node->blockstore->blockstoreContext, block, &written)) {
		ipfs_block_free(block);
		return 0;
	}

	char hash_str[256] = {0};
	if (!ipfs_cid_hash_to_base58(block->cid->hash, block->cid->hash_length, (unsigned char*)hash_str, 256)) {
		ipfs_block_free(block);
		return 0;
	}
	ipfs_block_free(block);

	*response = ipfs_core_http_response_new();
	if (!*response) return 0;
	struct HttpResponse* res = *response;
	res->content_type = "application/json";
	int bytes_needed = 64 + strlen(hash_str);
	res->bytes = (uint8_t*)malloc(bytes_needed);
	if (!res->bytes) {
		ipfs_core_http_response_free(res);
		*response = NULL;
		return 0;
	}
	snprintf((char*)res->bytes, bytes_needed, "{\"Key\":\"%s\",\"Size\":%zu}", hash_str, request->data_size);
	res->bytes_size = strlen((char*)res->bytes);
	return 1;
}

static int ipfs_core_http_process_cat(struct IpfsNode* local_node, struct HttpRequest* request, struct HttpResponse** response) {
	if (!request->arguments || request->arguments->total < 1) return 0;
	char* hash = (char*)libp2p_utils_vector_get(request->arguments, 0);
	struct Cid* cid = NULL;
	if (!ipfs_cid_decode_hash_from_base58((unsigned char*)hash, strlen(hash), &cid)) {
		return 0;
	}
	*response = ipfs_core_http_response_new();
	if (!*response) {
		ipfs_cid_free(cid);
		return 0;
	}
	struct HttpResponse* res = *response;
	res->content_type = "application/octet-stream";
	FILE* response_file = open_memstream((char**)&res->bytes, &res->bytes_size);
	int retVal = ipfs_exporter_object_cat_to_file(local_node, cid->hash, cid->hash_length, response_file);
	ipfs_cid_free(cid);
	fclose(response_file);
	if (!retVal) {
		ipfs_core_http_response_free(res);
		*response = NULL;
		return 0;
	}
	return 1;
}

/***
 * Process the parameters passed in from an http request
 * @param local_node the context
 * @param request the request
 * @param response the response
 * @returns true(1) on success, false(0) otherwise.
 */
int ipfs_core_http_request_process(struct IpfsNode* local_node, struct HttpRequest* request, struct HttpResponse** response) {
	if (request == NULL)
		return 0;
	int retVal = 0;

	if (strcmp(request->command, "name") == 0) {
		retVal = ipfs_core_http_process_name(local_node, request, response);
	} else if (strcmp(request->command, "object") == 0) {
		retVal = ipfs_core_http_process_object(local_node, request, response);
	} else if (strcmp(request->command, "dht") == 0) {
		retVal = ipfs_core_http_process_dht(local_node, request, response);
	} else if (strcmp(request->command, "swarm") == 0) {
		retVal = ipfs_core_http_process_swarm(local_node, request, response);
	} else if (strcmp(request->command, "id") == 0) {
		retVal = ipfs_core_http_process_id(local_node, request, response);
	} else if (strcmp(request->command, "version") == 0) {
		retVal = ipfs_core_http_process_version(local_node, request, response);
	} else if (strcmp(request->command, "block") == 0 && request->sub_command && strcmp(request->sub_command, "get") == 0) {
		retVal = ipfs_core_http_process_block_get(local_node, request, response);
	} else if (strcmp(request->command, "block") == 0 && request->sub_command && strcmp(request->sub_command, "put") == 0) {
		retVal = ipfs_core_http_process_block_put(local_node, request, response);
	} else if (strcmp(request->command, "cat") == 0) {
		retVal = ipfs_core_http_process_cat(local_node, request, response);
	}
	return retVal;
}

/**
 * just builds the basics of an http api url
 * @param local_node where to get the ip and port
 * @returns a string in the form of "http://127.0.0.1:5001/api/v0" (or whatever was in the config file for the API)
 */
char* ipfs_core_http_request_build_url_start(struct IpfsNode* local_node) {
	char* host = NULL;
	char port[10] = "";
	struct MultiAddress* ma = multiaddress_new_from_string(local_node->repo->config->addresses->api);
	if (ma == NULL)
		return NULL;
	if (!multiaddress_get_ip_address(ma, &host)) {
		multiaddress_free(ma);
		return 0;
	}
	int portInt = multiaddress_get_ip_port(ma);
	sprintf(port, "%d", portInt);
	int len = 18 + strlen(host) + strlen(port);
	char* retVal = malloc(len);
	if (retVal != NULL) {
		sprintf(retVal, "http://%s:%s/api/v0", host, port);
	}
	free(host);
	multiaddress_free(ma);
	return retVal;
}

/***
 * Adds commands to url
 * @param request the request
 * @param url the starting url, and will hold the results (i.e. http://127.0.0.1:5001/api/v0/<command>/<sub_command>
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_core_http_request_add_commands(struct HttpRequest* request, char** url) {
	// command
	int addl_length = strlen(request->command) + 2;
	char* string1 = (char*) malloc(strlen(*url) + addl_length);
	if (string1 != NULL) {
		sprintf(string1, "%s/%s", *url, request->command);
		free(*url);
		*url = string1;
		// sub_command
		if (request->sub_command != NULL) {
			addl_length = strlen(request->sub_command) + 2;
			string1 = (char*) malloc(strlen(*url) + addl_length);
			sprintf(string1, "%s/%s", *url, request->sub_command);
			free(*url);
			*url = string1;
		}
	}
	return string1 != NULL;
}

/***
 * Add parameters and arguments to the URL
 * @param request the request
 * @param url the url to continue to build
 */
int ipfs_core_http_request_add_parameters(struct HttpRequest* request, char** url) {
	// params
	if (request->params != NULL) {
		for (int i = 0; i < request->params->total; i++) {
			struct HttpParam* curr_param = (struct HttpParam*) libp2p_utils_vector_get(request->params, i);
			int len = strlen(curr_param->name) + strlen(curr_param->value) + 2;
			char* str = (char*) malloc(strlen(*url) + len);
			if (str == NULL) {
				return 0;
			}
			sprintf(str, "%s/%s=%s", *url, curr_param->name, curr_param->value);
			free(*url);
			*url = str;
		}
	}
	// args
	if (request->arguments != NULL) {
		int isFirst = 1;
		for(int i = 0; i < request->arguments->total; i++) {
			char* arg = (char*) libp2p_utils_vector_get(request->arguments, i);
			int len = strlen(arg) + strlen(*url) + 6;
			char* str = (char*) malloc(len);
			if (str == NULL)
				return 0;
			if (isFirst)
				sprintf(str, "%s?arg=%s", *url, arg);
			else
				sprintf(str, "%s&arg=%s", *url, arg);
			free(*url);
			*url = str;
		}
	}
	return 1;
}

struct curl_string {
	char* ptr;
	size_t len;
};

size_t curl_cb(void* ptr, size_t size, size_t nmemb, struct curl_string* str) {
	size_t new_len = str->len + size * nmemb;
	str->ptr = realloc(str->ptr, new_len + 1);
	if (str->ptr != NULL) {
		memcpy(str->ptr + str->len, ptr, size*nmemb);
		str->ptr[new_len] = '\0';
		str->len = new_len;
	}
	return size * nmemb;
}

/**
 * Do an HTTP Get to the local API
 * @param local_node the context
 * @param request the request
 * @param result the results
 * @param result_size the size of the results
 * @returns true(1) on success, false(0) on error
 */
int ipfs_core_http_request_get(struct IpfsNode* local_node, struct HttpRequest* request, char** result, size_t *result_size) {
	if (request == NULL || request->command == NULL)
		return 0;

	char* url = ipfs_core_http_request_build_url_start(local_node);
	if (url == NULL)
		return 0;

	if (!ipfs_core_http_request_add_commands(request, &url)) {
		free(url);
		return 0;
	}

	if (!ipfs_core_http_request_add_parameters(request, &url)) {
		free(url);
		return 0;
	}

	// do the GET using libcurl
	CURL *curl;
	CURLcode res;
	struct curl_string s;
	s.len = 0;
	s.ptr = malloc(1);
	s.ptr[0] = '\0';

	curl = curl_easy_init();
	if (!curl) {
		return 0;
	}
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if (res == CURLE_OK) {
		if (strcmp(s.ptr, "404 page not found") != 0) {
			*result = s.ptr;
			*result_size = s.len;
		}
		else
			res = -1;
	} else {
		libp2p_logger_error("http_request", "Results of [%s] returned failure. Return value: %d.\n", url, res);
		if (s.ptr != NULL)
			free(s.ptr);
	}
	return res == CURLE_OK;
}

/**
 * Do an HTTP Post to the local API
 * @param local_node the context
 * @param request the request
 * @param result the results
 * @param result_size the size of the results
 * @param data the array with post data
 * @param data_size the data length
 * @returns true(1) on success, false(0) on error
 */
int ipfs_core_http_request_post(struct IpfsNode* local_node, struct HttpRequest* request, char** result, size_t* result_size, char *data, size_t data_size) {
	if (request == NULL || request->command == NULL || data == NULL)
		return 0;

	char* url = ipfs_core_http_request_build_url_start(local_node);
	if (url == NULL)
		return 0;

	if (!ipfs_core_http_request_add_commands(request, &url)) {
		free(url);
		return 0;
	}

	if (!ipfs_core_http_request_add_parameters(request, &url)) {
		free(url);
		return 0;
	}

	// do the POST using libcurl
	CURL *curl;
	CURLcode res;
	struct curl_string s;
	s.len = 0;
	s.ptr = malloc(1);
	s.ptr[0] = '\0';

	struct curl_httppost *post = NULL, *last = NULL;
	CURLFORMcode curl_form_ret = curl_formadd(&post,		&last,
						CURLFORM_COPYNAME,	"filename",
						CURLFORM_PTRCONTENTS,	data,
						CURLFORM_CONTENTTYPE,	"application/octet-stream",
						CURLFORM_FILENAME,	"",
						CURLFORM_CONTENTSLENGTH,	data_size,
						CURLFORM_END);



	if (CURL_FORMADD_OK != curl_form_ret) {
		// i'm always getting curl_form_ret == 4 here
		// it means CURL_FORMADD_UNKNOWN_OPTION
		// what i'm doing wrong?
		fprintf(stderr, "curl_form_ret = %d\n", (int)curl_form_ret);
		return 0;
	}

	curl = curl_easy_init();
	if (!curl) {
		return 0;
	}
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
	curl_easy_setopt(curl, CURLOPT_HTTPPOST, post);
	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if (res == CURLE_OK) {
		if (strcmp(s.ptr, "404 page not found") != 0) {
			*result = s.ptr;
			*result_size = s.len;
		}
		else
			res = -1;
	} else {
		//libp2p_logger_error("http_request", "Results of [%s] returned failure. Return value: %d.\n", url, res);
		fprintf(stderr, "Results of [%s] returned failure. Return value: %d.\n", url, res);
		if (s.ptr != NULL)
			free(s.ptr);
	}
	return res == CURLE_OK;
}
