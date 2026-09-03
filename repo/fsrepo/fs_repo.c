#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <errno.h>

#include "libp2p/utils/logger.h"
#include "libp2p/crypto/encoding/base64.h"
#include "libp2p/crypto/key.h"
#include "libp2p/peer/peer.h"
#include "libp2p/utils/vector.h"
#include "ipfs/blocks/blockstore.h"
#include "ipfs/datastore/ds_helper.h"
#include "libp2p/db/datastore.h"
#include "libp2p/db/filestore.h"
#include "ipfs/repo/fsrepo/fs_repo.h"
#include "libp2p/os/utils.h"
#include "ipfs/repo/fsrepo/lmdb_datastore.h"
#include "ipfs/repo/fsrepo/jsmn.h"
#include "multiaddr/multiaddr.h"

/** 
 * private methods
 */

static int fs_repo_acquire_lock(struct FSRepo* repo) {
	if (repo->lock_fd >= 0)
		return 1;
	char lock_path[512];
	snprintf(lock_path, sizeof(lock_path), "%s/repo.lock", repo->path);
	repo->lock_fd = open(lock_path, O_RDWR | O_CREAT, 0600);
	if (repo->lock_fd < 0)
		return 0;
	if (flock(repo->lock_fd, LOCK_EX | LOCK_NB) != 0) {
		close(repo->lock_fd);
		repo->lock_fd = -1;
		return 0;
	}
	return 1;
}

static void fs_repo_release_lock(struct FSRepo* repo) {
	if (repo->lock_fd >= 0) {
		flock(repo->lock_fd, LOCK_UN);
		close(repo->lock_fd);
		repo->lock_fd = -1;
	}
}

static int fs_repo_check_writable(const char* path) {
	return access(path, W_OK) == 0;
}

static int fs_repo_read_version(const char* path) {
	char version_path[512];
	snprintf(version_path, sizeof(version_path), "%s/version", path);
	FILE* f = fopen(version_path, "r");
	if (!f) return -1;
	int version = -1;
	if (fscanf(f, "%d", &version) != 1) {
		fclose(f);
		return -1;
	}
	fclose(f);
	return version;
}

static int fs_repo_write_version(const char* path, int version) {
	char version_path[512];
	snprintf(version_path, sizeof(version_path), "%s/version", path);
	FILE* f = fopen(version_path, "w");
	if (!f) return 0;
	fprintf(f, "%d\n", version);
	fclose(f);
	return 1;
}

/**
 * writes the config file atomically (temp file + rename)
 * @param full_filename the full filename of the config file in the OS
 * @param config the details to put into the file
 * @returns true(1) on success, else false(0)
 */
int repo_config_write_config_file(char* full_filename, struct RepoConfig* config) {
	// build temp filename: full_filename + ".tmp"
	size_t tmp_len = strlen(full_filename) + 5;
	char* tmp_filename = malloc(tmp_len);
	if (tmp_filename == NULL)
		return 0;
	snprintf(tmp_filename, tmp_len, "%s.tmp", full_filename);

	FILE* out_file = fopen(tmp_filename, "w");
	if (out_file == NULL) {
		free(tmp_filename);
		return 0;
	}

	int write_ok = 1;
	if (write_ok)
		write_ok = (fprintf(out_file, "{\n") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, " \"Identity\": {\n") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"PeerID\": \"%s\",\n", config->identity->peer->id) > 0);
	// print correct format of private key
	// first put it in a protobuf
	struct PrivateKey* priv_key = NULL;
	unsigned char* protobuf = NULL;
	unsigned char* encoded_buffer = NULL;
	if (write_ok) {
		priv_key = libp2p_crypto_private_key_new();
		if (priv_key == NULL)
			write_ok = 0;
	}
	if (write_ok) {
		priv_key->data_size = config->identity->private_key.der_length;
		priv_key->data = (unsigned char*)malloc(priv_key->data_size);
		if (priv_key->data == NULL) {
			libp2p_crypto_private_key_free(priv_key);
			write_ok = 0;
		} else {
			memcpy(priv_key->data, config->identity->private_key.der, priv_key->data_size);
			priv_key->type = KEYTYPE_RSA;
			size_t protobuf_size = libp2p_crypto_private_key_protobuf_encode_size(priv_key);
			protobuf = (unsigned char*)malloc(protobuf_size);
			if (protobuf == NULL) {
				libp2p_crypto_private_key_free(priv_key);
				write_ok = 0;
			} else {
				size_t encoded_size = libp2p_crypto_encoding_base64_encode_size(protobuf_size);
				encoded_buffer = (unsigned char*)malloc(encoded_size + 1);
				if (encoded_buffer == NULL) {
					libp2p_crypto_private_key_free(priv_key);
					free(protobuf);
					write_ok = 0;
				} else {
					size_t out_size = 0;
					libp2p_crypto_private_key_protobuf_encode(priv_key, protobuf, protobuf_size, &out_size);
					int retVal = libp2p_crypto_encoding_base64_encode(protobuf, protobuf_size, encoded_buffer, encoded_size, &encoded_size);
					encoded_buffer[encoded_size] = 0;
					if (retVal == 0)
						write_ok = 0;
					else
						write_ok = (fprintf(out_file, "  \"PrivKey\": \"%s\"\n", encoded_buffer) > 0);
				}
			}
		}
	}
	if (write_ok)
		write_ok = (fprintf(out_file, " },\n") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, " \"Datastore\": {\n") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"Type\": \"%s\",\n", config->datastore->type) > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"Path\": \"%s\",\n", config->datastore->path) > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"StorageMax\": \"%s\",\n", config->datastore->storage_max) > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"StorageGCWatermark\": %d,\n", config->datastore->storage_gc_watermark) > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"GCPeriod\": \"%s\",\n", config->datastore->gc_period) > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"Params\": null,\n") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"NoSync\": %s,\n", config->datastore->no_sync ? "true" : "false") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"HashOnRead\": %s,\n", config->datastore->hash_on_read ? "true" : "false") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"BloomFilterSize\": %d\n", config->datastore->bloom_filter_size) > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, " },\n \"Addresses\": {\n") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"Swarm\": [\n") > 0);
	if (write_ok) {
		struct Libp2pLinkedList* current = config->addresses->swarm_head;
		while (current != NULL && write_ok) {
			write_ok = (fprintf(out_file, "  \"%s\"", (char*)current->item) > 0);
			if (write_ok) {
				if (current->next == NULL)
					write_ok = (fprintf(out_file, "\n") > 0);
				else
					write_ok = (fprintf(out_file, ",\n") > 0);
			}
			current = current->next;
		}
	}
	if (write_ok)
		write_ok = (fprintf(out_file, "  ],\n") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"API\": \"%s\",\n", config->addresses->api) > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"Gateway\": \"%s\"\n", config->addresses->gateway) > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, " },\n  \"Mounts\": {\n") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"IPFS\": \"%s\",\n", config->mounts.ipfs) > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"IPNS\": \"%s\",\n", config->mounts.ipns) > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"FuseAllowOther\": %s\n", "false") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, " },\n  \"Discovery\": {\n   \"MDNS\": {\n") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "   \"Enabled\": %s,\n", config->discovery.mdns.enabled ? "true" : "false") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "   \"Interval\": %d\n  }\n },\n", config->discovery.mdns.interval) > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, " \"Ipns\": {\n") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"RepublishedPeriod\": \"\",\n") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"RecordLifetime\": \"\",\n") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"ResolveCacheSize\": %d\n", config->ipns.resolve_cache_size) > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, " },\n \"Bootstrap\": [\n") > 0);
	if (write_ok) {
		for(int i = 0; i < config->bootstrap_peers->total && write_ok; i++) {
			const struct MultiAddress* peer = (const struct MultiAddress*)libp2p_utils_vector_get(config->bootstrap_peers, i);
			write_ok = (fprintf(out_file, "  \"%s\"", peer->string) > 0);
			if (write_ok) {
				if (i < config->bootstrap_peers->total - 1)
					write_ok = (fprintf(out_file, ",\n") > 0);
				else
					write_ok = (fprintf(out_file, "\n") > 0);
			}
		}
	}
	if (write_ok)
		write_ok = (fprintf(out_file, " ],\n \"Tour\": {\n  \"Last\": \"\"\n },\n") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, " \"Gateway\": {\n") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"HTTPHeaders\": {\n") > 0);
	if (write_ok) {
		for (int i = 0; i < config->gateway->http_headers->num_elements && write_ok; i++) {
			write_ok = (fprintf(out_file, "   \"%s\": [\n    \"%s\"\n  ]", config->gateway->http_headers->headers[i]->header, config->gateway->http_headers->headers[i]->value) > 0);
			if (write_ok) {
				if (i < config->gateway->http_headers->num_elements - 1)
					write_ok = (fprintf(out_file, ",\n") > 0);
				else
					write_ok = (fprintf(out_file, "\n },\n") > 0);
			}
		}
	}
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"RootRedirect\": \"%s\",\n", config->gateway->root_redirect) > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"Writable\": %s,\n", config->gateway->writable ? "true" : "false") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"PathPrefixes\": []\n") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, " },\n \"SupernodeRouting\": {\n") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, "  \"Servers\": null\n },") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, " \"API\": {\n  \"HTTPHeaders\": null\n },\n") > 0);
	if (write_ok)
		write_ok = (fprintf(out_file, " \"Swarm\": {\n  \"AddrFilters\": null\n }\n}") > 0);

	int flush_ok = 0;
	if (write_ok) {
		flush_ok = (fflush(out_file) == 0);
		if (flush_ok) {
			int fd = fileno(out_file);
			flush_ok = (fsync(fd) == 0);
		}
	}
	fclose(out_file);

	int rename_ok = 0;
	if (flush_ok) {
		rename_ok = (rename(tmp_filename, full_filename) == 0);
	}

	if (!rename_ok && !flush_ok && !write_ok) {
		remove(tmp_filename);
	}

	if (priv_key != NULL)
		libp2p_crypto_private_key_free(priv_key);
	if (protobuf != NULL)
		free(protobuf);
	if (encoded_buffer != NULL)
		free(encoded_buffer);
	free(tmp_filename);
	return rename_ok;
}

// forward declaration, actual is in init.c
char* ipfs_repo_get_home_directory(int argc, char** argv);

/**
 * constructs the FSRepo struct.
 * Remember: ipfs_repo_fsrepo_free must be called
 * @param repo_path the path to the repo
 * @param config the optional config file. NOTE: if passed, fsrepo_free will free resources of the RepoConfig.
 * @param repo the struct to allocate memory for
 * @returns false(0) if something bad happened, otherwise true(1)
 */
int ipfs_repo_fsrepo_new(const char* repo_path, struct RepoConfig* config, struct FSRepo** repo) {
	*repo = (struct FSRepo*)malloc(sizeof(struct FSRepo));
	(*repo)->lock_fd = -1;
	(*repo)->closed = 0;

	if (repo_path == NULL) {
		char* ipfs_path = ipfs_repo_get_home_directory(0, NULL);
		(*repo)->path = malloc(strlen(ipfs_path) + 1);
		if ((*repo)->path == NULL) {
			free( (*repo));
			return 0;
		}
		strcpy((*repo)->path, ipfs_path);
	} else {
		int len = strlen(repo_path) + 1;
		(*repo)->path = (char*)malloc(len);
		if ( (*repo)->path != NULL)
			strncpy((*repo)->path, repo_path, len);
	}
	// allocate other structures
	if (config != NULL)
		(*repo)->config = config;
	else {
		if (ipfs_repo_config_new(&((*repo)->config)) == 0) {
			free((*repo)->path);
			return 0;
		}
	}
	return 1;
}

/**
 * Cleans up memory
 * @param repo the struct to clean up
 * @returns true(1) on success
 */
int ipfs_repo_fsrepo_free(struct FSRepo* repo) {
	if (repo != NULL) {
		if (repo->lock_fd >= 0) {
			close(repo->lock_fd);
			repo->lock_fd = -1;
		}
		if (repo->path != NULL)
			free(repo->path);
		if (repo->config != NULL)
			ipfs_repo_config_free(repo->config);
		free(repo);
	}
	return 1;
}

/**
 * checks to see if the repo is initialized at the given path
 * @param full_path the path to the repo
 * @returns true(1) if the config file is there, false(0) otherwise
 */
int repo_config_is_initialized(char* full_path) {
	char* config_file_full_path;
	int retVal = repo_config_get_file_name(full_path, &config_file_full_path);
	if (!retVal)
		return 0;
	
	if (os_utils_file_exists(config_file_full_path))
		retVal = 1;
	else
		retVal = 0;
	
	free(config_file_full_path);
	return retVal;
}

/***
 * Check to see if the repo is initialized
 * @param full_path the path to the repo
 * @returns true(1) if it is initialized, false(0) otherwise.
 */
int fs_repo_is_initialized_unsynced(char* full_path) {
	return repo_config_is_initialized(full_path);
}

/**
 * checks to see if the repo is initialized
 * @param full_path the full path to the repo
 * @returns true(1) if it is initialized, otherwise false(0)
 */
int repo_check_initialized(char* full_path) {
	// note the old version of this reported an error if the repo was a .go-ipfs repo (by looking at the path)
	// this version skips that step
	return fs_repo_is_initialized_unsynced(full_path);
}

/***
 * Reads the file, placing its contents in buffer
 * NOTE: this allocates memory for buffer, and should be freed
 * @param path the path to the config file
 * @param buffer where to put the contents
 * @returns true(1) on success
 */
int _read_file(const char* path, char** buffer) {
	int file_size = os_utils_file_size(path);
	if (file_size <= 0)
		return 0;
	// allocate memory
	*buffer = malloc(file_size + 1);
	if (*buffer == NULL) {
		return 0;
	}
	memset(*buffer, 0, file_size + 1);

	// open file
	FILE* in_file = fopen(path, "r");
	if (in_file == NULL) {
		free(*buffer);
		*buffer = NULL;
		return 0;
	}
	// read data
	size_t read_bytes = fread(*buffer, 1, file_size, in_file);
	(*buffer)[read_bytes] = '\0';

	// cleanup
	fclose(in_file);
	return read_bytes == (size_t)file_size;
}

/**
 * Find the position of a key
 * @param data the string that contains the json
 * @param tokens the tokens of the parsed string
 * @param tok_length the number of tokens there are
 * @param tag what we're looking for
 * @returns the position of the requested token in the array, or -1
 */
int _find_token(const char* data, const jsmntok_t* tokens, int tok_length, int start_from, const char* tag) {
	for(int i = start_from; i < tok_length; i++) {
		jsmntok_t curr_token = tokens[i];
		if ( curr_token.type == JSMN_STRING) {
			// convert to string
			int str_len = curr_token.end - curr_token.start;
			char str[str_len + 1];
			strncpy(str, &data[curr_token.start], str_len );
			str[str_len] = 0;
			if (strcmp(str, tag) == 0)
				return i;
		}
	}
	return -1;
}

/**
 * Retrieves the value of a key / value pair from the JSON data
 * @param data the full JSON string
 * @param tokens the array of tokens
 * @param tok_length the number of tokens
 * @param search_from start search from this token onward
 * @param tag what to search for (NOTE: If null, read from search_from)
 * @param result where to put the result. NOTE: allocates memory that must be freed
 * @returns true(1) on success
 */
int _get_json_string_value(char* data, const jsmntok_t* tokens, int tok_length, int search_from, const char* tag, char** result) {
	int pos = 0;
	jsmntok_t* curr_token = NULL;

	if (tag == NULL) {
		pos = search_from;
		if (pos >= 0)
			curr_token = (jsmntok_t*)&tokens[pos];
	}
	else {
		pos = _find_token(data, tokens, tok_length, search_from, tag);
		if (pos >= 0)
			curr_token = (jsmntok_t*)&tokens[pos + 1];
	}

	if (curr_token == NULL)
		return 0;

	if (curr_token->type == JSMN_PRIMITIVE) {
		// a null
		*result = NULL;
	}
	if (curr_token->type != JSMN_STRING)
		return 0;
	// allocate memory
	int str_len = curr_token->end - curr_token->start;
	*result = malloc(str_len + 1);
	if (*result == NULL)
		return 0;
	// copy in the string
	strncpy(*result, &data[curr_token->start], str_len);
	(*result)[str_len] = 0;
	return 1;
}

/**
 * Retrieves the value of a key / value pair from the JSON data
 * @param data the full JSON string
 * @param tokens the array of tokens
 * @param tok_length the number of tokens
 * @param search_from start search from this token onward
 * @param tag what to search for
 * @param result where to put the result
 * @returns true(1) on success
 */
int _get_json_int_value(char* data, const jsmntok_t* tokens, int tok_length, int search_from, const char* tag, int* result) {
	int pos = _find_token(data, tokens, tok_length, search_from, tag);
	if (pos < 0)
		return 0;
	jsmntok_t curr_token = tokens[pos+1];
	if (curr_token.type != JSMN_PRIMITIVE)
		return 0;
	// allocate memory
	int str_len = curr_token.end - curr_token.start;
	char str[str_len + 1];
	// copy in the string
	strncpy(str, &data[curr_token.start], str_len);
	str[str_len] = 0;
	if (strcmp(str, "true") == 0)
		*result = 1;
	else if (strcmp(str, "false") == 0)
		*result = 0;
	else if (strcmp(str, "null") == 0) // what should we do here?
		*result = 0;
	else // its a real number
		*result = atoi(str);
	return 1;
}

/***
 * Opens the config file and puts the data into the FSRepo struct
 * @param repo the FSRepo struct
 * @returns 0 on failure, otherwise 1
 */
int fs_repo_open_config(struct FSRepo* repo) {
	int retVal;
	char* data;
	size_t full_filename_length = strlen(repo->path) + 8;
	char full_filename[full_filename_length];
	retVal = os_utils_filepath_join(repo->path, "config", full_filename, full_filename_length);
	if (retVal == 0) {
		libp2p_logger_error("fs_repo", "fs_repo_open_config: filepath_join failed for %s/config\n", repo->path);
		return 0;
	}
	retVal = _read_file(full_filename, &data);
	if (retVal == 0) {
		libp2p_logger_error("fs_repo", "fs_repo_open_config: _read_file failed for %s\n", full_filename);
		return 0;
	}
	// parse the data
	jsmn_parser parser;
	jsmn_init(&parser);
	int num_tokens = 256;
	jsmntok_t tokens[num_tokens];
	num_tokens = jsmn_parse(&parser, data, strlen(data), tokens, 256);
	if (num_tokens <= 0) {
		libp2p_logger_error("fs_repo", "fs_repo_open_config: jsmn_parse failed with %d for %s\n", num_tokens, full_filename);
		free(data);
		return 0;
	}
	// fill FSRepo struct
	// allocation done by fsrepo_new... repo->config = malloc(sizeof(struct RepoConfig));
	// Identity
	int curr_pos = _find_token(data, tokens, num_tokens, 0, "Identity");
	if (curr_pos < 0) {
		libp2p_logger_error("fs_repo", "fs_repo_open_config: Identity token not found in %s\n", full_filename);
		free(data);
		return 0;
	}
	// the next should be the array, then string "PeerID"
	//NOTE: the code below compares the peer id of the file with the peer id generated
	// by the key. If they don't match, we fail.
	unsigned char* test_peer_id = NULL;
	_get_json_string_value(data, tokens, num_tokens, curr_pos, "PeerID", (char**)&test_peer_id);
	char* priv_key_base64;
	// then PrivKey
	_get_json_string_value(data, tokens, num_tokens, curr_pos, "PrivKey", &priv_key_base64);
	retVal = repo_config_identity_build_private_key(repo->config->identity, priv_key_base64);
	if (retVal == 0
			|| strlen((char*)test_peer_id) != repo->config->identity->peer->id_size
			|| strcmp((char*)test_peer_id, repo->config->identity->peer->id) != 0) {
		libp2p_logger_error("fs_repo", "fs_repo_open_config: identity mismatch or build failed in %s (retVal=%d, test_peer_id=%s, computed=%s)\n",
			full_filename, retVal, test_peer_id ? (char*)test_peer_id : "(null)",
			repo->config->identity->peer->id ? repo->config->identity->peer->id : "(null)");
		free(data);
		free(priv_key_base64);
		free(test_peer_id);
		return 0;
	}
	repo->config->identity->peer->is_local = 1;
	free(test_peer_id);

	// now the datastore
	//int datastore_position = _find_token(data, tokens, num_tokens, 0, "Datastore");
	_get_json_string_value(data, tokens, num_tokens, curr_pos, "Type", &repo->config->datastore->type);
	_get_json_string_value(data, tokens, num_tokens, curr_pos, "Path", &repo->config->datastore->path);
	_get_json_string_value(data, tokens, num_tokens, curr_pos, "StorageMax", &repo->config->datastore->storage_max);
	_get_json_int_value(data, tokens, num_tokens, curr_pos, "StorageGCWatermark", &repo->config->datastore->storage_gc_watermark);
	_get_json_string_value(data, tokens, num_tokens, curr_pos, "GCPeriod", &repo->config->datastore->gc_period);
	_get_json_string_value(data, tokens, num_tokens, curr_pos, "Params", &repo->config->datastore->params);
	_get_json_int_value(data, tokens, num_tokens, curr_pos, "NoSync", &repo->config->datastore->no_sync);
	_get_json_int_value(data, tokens, num_tokens, curr_pos, "HashOnRead", &repo->config->datastore->hash_on_read);
	_get_json_int_value(data, tokens, num_tokens, curr_pos, "BloomFilterSize", &repo->config->datastore->bloom_filter_size);
	// Kubo compatibility: provide defaults for fields Kubo doesn't include at top level
	if (repo->config->datastore->type == NULL) {
		repo->config->datastore->type = malloc(5);
		if (repo->config->datastore->type != NULL)
			strcpy(repo->config->datastore->type, "lmdb");
	}
	if (repo->config->datastore->path == NULL) {
		size_t path_len = strlen(repo->path) + 11;
		repo->config->datastore->path = malloc(path_len);
		if (repo->config->datastore->path != NULL)
			os_utils_filepath_join(repo->path, "datastore", repo->config->datastore->path, path_len);
	}
	if (repo->config->datastore->storage_max == NULL) {
		repo->config->datastore->storage_max = malloc(5);
		if (repo->config->datastore->storage_max != NULL)
			strcpy(repo->config->datastore->storage_max, "10GB");
	}
	if (repo->config->datastore->gc_period == NULL) {
		repo->config->datastore->gc_period = malloc(3);
		if (repo->config->datastore->gc_period != NULL)
			strcpy(repo->config->datastore->gc_period, "1h");
	}

	// get addresses. First is Swarm array, then Api, then Gateway
	curr_pos = _find_token(data, tokens, num_tokens, curr_pos, "Addresses");
	if (curr_pos < 0) {
		libp2p_logger_error("fs_repo", "fs_repo_open_config: Addresses token not found in %s\n", full_filename);
		free(data);
		return 0;
	}
	// get swarm addresses
	int swarm_pos = _find_token(data, tokens, num_tokens, curr_pos, "Swarm") + 1;
	if (tokens[swarm_pos].type != JSMN_ARRAY) {
		libp2p_logger_error("fs_repo", "fs_repo_open_config: Swarm is not an array in %s\n", full_filename);
		free(data);
		return 0;
	}
	int swarm_size = tokens[swarm_pos].size;
	swarm_pos++;
	repo->config->addresses->swarm_head = NULL;
	struct Libp2pLinkedList* last = NULL;
	struct Libp2pLinkedList* current_ma_pos = repo->config->identity->peer->addr_head;
	for(int i = 0; i < swarm_size; i++) {
		struct Libp2pLinkedList* current = libp2p_utils_linked_list_new();
		if (!_get_json_string_value(data, tokens, num_tokens, swarm_pos + i, NULL, (char**)&current->item))
			break;
		if (repo->config->addresses->swarm_head == NULL) {
			repo->config->addresses->swarm_head = current;
		} else {
			last->next = current;
		}
		last = current;
		// add current to peer too
		struct MultiAddress* ma = multiaddress_new_from_string(current->item);
		if (ma != NULL) {
			if (current_ma_pos == NULL) {
				repo->config->identity->peer->addr_head = libp2p_utils_linked_list_new();
				current_ma_pos = repo->config->identity->peer->addr_head;
			} else {
				struct Libp2pLinkedList* next_ma_pos = libp2p_utils_linked_list_new();
				current_ma_pos->next = next_ma_pos;
				current_ma_pos = next_ma_pos;
			}
			current_ma_pos->item = ma;
		}
	}
	_get_json_string_value(data, tokens, num_tokens, curr_pos, "API", &repo->config->addresses->api);
	_get_json_string_value(data, tokens, num_tokens, curr_pos, "Gateway", &repo->config->addresses->gateway);

	// bootstrap peers
	swarm_pos = _find_token(data, tokens, num_tokens, curr_pos, "Bootstrap");
	if (swarm_pos >= 0) {
		swarm_pos++;
		if (tokens[swarm_pos].type != JSMN_ARRAY) {
			free(data);
			return 0;
		}
		swarm_size = tokens[swarm_pos].size;
		repo->config->bootstrap_peers = libp2p_utils_vector_new(swarm_size);
		swarm_pos++;
		for(int i = 0; i < swarm_size; i++) {
			char* val = NULL;
			if (!_get_json_string_value(data, tokens, num_tokens, swarm_pos + i, NULL, &val))
				break;
			struct MultiAddress* cur = multiaddress_new_from_string(val);
			if (cur == NULL)
				continue;
			libp2p_utils_vector_add(repo->config->bootstrap_peers, cur);
			free(val);
		}
	}

	// replication
	curr_pos = _find_token(data, tokens, num_tokens, curr_pos, "Replication");
	if (curr_pos >= 0) {
		// announce minutes
		curr_pos++;
		_get_json_int_value(data, tokens, num_tokens, curr_pos, "AnnounceMinutes", &repo->config->replication->announce_minutes);
		_get_json_int_value(data, tokens, num_tokens, curr_pos, "Announce", &repo->config->replication->announce);
		// nodes list
		int nodes_pos = _find_token(data, tokens, num_tokens, curr_pos, "Peers");
		if (nodes_pos >= 0) {
			nodes_pos++;
			if (tokens[nodes_pos].type == JSMN_ARRAY) {
				int nodes_size = tokens[nodes_pos].size;
				repo->config->replication->replication_peers = libp2p_utils_vector_new(nodes_size);
				nodes_pos++;
				for(int i = 0; i < nodes_size; i++) {
					char* val = NULL;
					if (!_get_json_string_value(data, tokens, num_tokens, nodes_pos, NULL, &val))
						break;
					struct MultiAddress* cur = multiaddress_new_from_string(val);
					if (cur == NULL)
						continue;
					// make multiaddress a peer
					struct Libp2pPeer* peer = libp2p_peer_new_from_multiaddress(cur);
					multiaddress_free(cur);
					struct ReplicationPeer* rp = repo_config_replication_peer_new();
					rp->peer = peer;
					libp2p_logger_debug("fs_repo", "Adding %s to replication_peers.\n", libp2p_peer_id_to_string(rp->peer));
					libp2p_utils_vector_add(repo->config->replication->replication_peers, rp);
					free(val);
				}
			} else {
				libp2p_logger_debug("fs_repo", "Replication|Peers is not an array.\n");
			}
		} else {
			libp2p_logger_debug("fs_repo", "No replication peers found.\n");
		}
	}
	// free the memory used reading the json file
	free(data);
	free(priv_key_base64);
	return 1;
}

/***
 * set function pointers in the datastore struct to lmdb
 * @param repo contains the information
 * @returns true(1) on success
 */
int fs_repo_setup_lmdb_datastore(struct FSRepo* repo) {
	return repo_fsrepo_lmdb_cast(repo->config->datastore);
}

/***
 * opens the repo's datastore, and puts a reference to it in the FSRepo struct
 * @param repo the FSRepo struct
 * @returns 0 on failure, otherwise 1
 */
int fs_repo_open_datastore(struct FSRepo* repo) {
	int argc = 0;
	char** argv = NULL;

	if (repo->config->datastore->type == NULL) {
		libp2p_logger_error("fs_repo", "Datastore type is NULL; defaulting to lmdb\n");
		repo->config->datastore->type = malloc(5);
		if (repo->config->datastore->type != NULL)
			strcpy(repo->config->datastore->type, "lmdb");
	}

	if (strncmp(repo->config->datastore->type, "lmdb", 4) == 0) {
		// this is a LightningDB. Open it.
		int retVal = fs_repo_setup_lmdb_datastore(repo);
		if (retVal == 0)
			return 0;
	} else {
		// add new datastore types here
		return 0;
	}

	int retVal = repo->config->datastore->datastore_open(argc, argv, repo->config->datastore);

	// do specific datastore cleanup here if needed

	return retVal;
}

/**
 * For interface of Filestore. Retrieves a node from the filestore
 * @param hash the hash to pull
 * @param hash_length the length of the hash
 * @param node_obj where to put the results
 * @param filestore a reference to the filestore struct
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_repo_fsrepo_node_get(const unsigned char* hash, size_t hash_length, void** node_obj, size_t *node_size, const struct Filestore* filestore) {
	struct FSRepo* fs_repo = (struct FSRepo*)filestore->handle;
	struct HashtableNode* node = NULL;
	int retVal = ipfs_repo_fsrepo_node_read(hash, hash_length, &node, fs_repo);
	if (retVal == 1) {
		*node_size = ipfs_hashtable_node_protobuf_encode_size(node);
		*node_obj = malloc(*node_size);
		if (*node_obj == NULL) {
			ipfs_hashtable_node_free(node);
			return 0;
		}
		retVal = ipfs_hashtable_node_protobuf_encode(node, *node_obj, *node_size, node_size);
	}
	ipfs_hashtable_node_free(node);
	return retVal;
}

/**
 * public methods
 */

/**
 * opens a fsrepo
 * @param repo the repo struct. Should contain the path. This method will do the rest
 * @return 0 if there was a problem, otherwise 1
 */
int ipfs_repo_fsrepo_open(struct FSRepo* repo) {
	// check if initialized
	if (!repo_check_initialized(repo->path)) {
		return 0;
	}
	// acquire repo lock
	if (!fs_repo_acquire_lock(repo)) {
		libp2p_logger_error("fs_repo", "Unable to acquire repo lock for %s\n", repo->path);
		return 0;
	}
	// check the version, and make sure it is correct
	int version = fs_repo_read_version(repo->path);
	if (version >= 0 && version != IPFS_REPO_VERSION) {
		libp2p_logger_error("fs_repo", "Repo version mismatch: expected %d, got %d\n", IPFS_REPO_VERSION, version);
		fs_repo_release_lock(repo);
		return 0;
	}
	// make sure the directory is writable
	if (!fs_repo_check_writable(repo->path)) {
		libp2p_logger_error("fs_repo", "Repo directory is not writable: %s\n", repo->path);
		fs_repo_release_lock(repo);
		return 0;
	}
	// open the config
	if (!fs_repo_open_config(repo)) {
		fs_repo_release_lock(repo);
		return 0;
	}

	// open the datastore
	if (!fs_repo_open_datastore(repo)) {
		fs_repo_release_lock(repo);
		return 0;
	}
	
	// init the filestore
	repo->config->filestore->handle = repo;
	repo->config->filestore->node_get = ipfs_repo_fsrepo_node_get;

	return 1;
}

/***
 * checks to see if the repo is initialized
 * @param repo_path the path to the repo
 * @returns true(1) if it is initialized, otherwise false(0)
 */
int fs_repo_is_initialized(char* repo_path) {
	return fs_repo_is_initialized_unsynced(repo_path);
}

int ipfs_repo_fsrepo_datastore_init(struct FSRepo* fs_repo) {
	// make the directory
	if (repo_fsrepo_lmdb_create_directory(fs_repo->config->datastore) == 0)
		return 0;

	// fill in the function prototypes
	return repo_fsrepo_lmdb_cast(fs_repo->config->datastore);
}

int ipfs_repo_fsrepo_blockstore_init(const struct FSRepo* fs_repo) {
	size_t full_path_size = strlen(fs_repo->path) + 15;
	char full_path[full_path_size];
	int retVal = os_utils_filepath_join(fs_repo->path, "blockstore", full_path, full_path_size);
	if (retVal == 0)
		return 0;

#ifdef __MINGW32__
	if (mkdir(full_path) != 0)
#else
	if (mkdir(full_path, S_IRWXU) != 0)
#endif
		return 0;
	return 1;
}

/**
 * Initializes a new FSRepo at the given path with the provided config
 * @param path the path to use
 * @param config the information for the config file
 * @returns true(1) on success
 */
int ipfs_repo_fsrepo_init(struct FSRepo* repo) {
	// return error if this has already been done
	if (fs_repo_is_initialized_unsynced(repo->path))
		return 0;

	// acquire lock to prevent concurrent init
	if (!fs_repo_acquire_lock(repo)) {
		libp2p_logger_error("fs_repo", "Unable to acquire lock during init of %s\n", repo->path);
		return 0;
	}
	// double-check after locking
	if (fs_repo_is_initialized_unsynced(repo->path)) {
		fs_repo_release_lock(repo);
		return 0;
	}

	int retVal = fs_repo_write_config_file(repo->path, repo->config);
	if (retVal == 0) {
		fs_repo_release_lock(repo);
		return 0;
	}

	retVal = ipfs_repo_fsrepo_datastore_init(repo);
	if (retVal == 0) {
		fs_repo_release_lock(repo);
		return 0;
	}

	retVal = ipfs_repo_fsrepo_blockstore_init(repo);
	if (retVal == 0) {
		fs_repo_release_lock(repo);
		return 0;
	}

	// write the version to a file for migrations
	if (!fs_repo_write_version(repo->path, IPFS_REPO_VERSION)) {
		libp2p_logger_error("fs_repo", "Unable to write repo version file for %s\n", repo->path);
		fs_repo_release_lock(repo);
		return 0;
	}

	fs_repo_release_lock(repo);
	return 1;
}

/**
 * write the config file to disk
 * @param path the path to the file
 * @param config the config structure
 * @returns true(1) on success
 */
int fs_repo_write_config_file(char* path, struct RepoConfig* config) {
	if (fs_repo_is_initialized(path))
		return 0;
	
	char* buff = NULL;
	if (!repo_config_get_file_name(path, &buff))
		return 0;
	
	int retVal = repo_config_write_config_file(buff, config);
	
	free(buff);
	
	return retVal;
}

/***
 * Write a block to the datastore and blockstore
 * @param block the block to write
 * @param fs_repo the repo to write to
 * @returns true(1) on success
 */
int ipfs_repo_fsrepo_block_write(struct Block* block, const struct FSRepo* fs_repo, size_t* bytes_written) {
	int retVal = 1;
	struct Blockstore* blockstore = ipfs_blockstore_new(fs_repo);
	if (blockstore == NULL) {
		return 0;
	}
	retVal = ipfs_blockstore_put(blockstore->blockstoreContext, block, bytes_written);
	ipfs_blockstore_free(blockstore);
	if (retVal == 0) {
		return 0;
	}
	retVal = ipfs_datastore_helper_add_block_to_datastore(block, fs_repo->config->datastore);
	if (retVal == 0) {
		return 0;
	}
	return 1;
}

int ipfs_repo_fsrepo_node_read(const unsigned char* hash, size_t hash_length, struct HashtableNode** node, const struct FSRepo* fs_repo) {
	int retVal = 0;

	// get the base32 hash from the database
	// We do this only to see if it is in the database
	struct DatastoreRecord *datastore_record = NULL;
	if (!fs_repo->config->datastore->datastore_get(hash, hash_length, &datastore_record, fs_repo->config->datastore))
		return 0;
	libp2p_datastore_record_free(datastore_record);
	// now get the block from the blockstore
	retVal = ipfs_blockstore_get_node(hash, hash_length, node, fs_repo);
	return retVal;
}



int ipfs_repo_fsrepo_block_read(const unsigned char* hash, size_t hash_length, struct Block** block, const struct FSRepo* fs_repo) {
	int retVal = 0;

	// get the base32 hash from the database
	// We do this only to see if it is in the database
	struct DatastoreRecord *datastore_record = NULL;
	if (!fs_repo->config->datastore->datastore_get(hash, hash_length, &datastore_record, fs_repo->config->datastore))
		return 0;

	libp2p_datastore_record_free(datastore_record);

	// now get the block from the blockstore
	struct Cid* cid = ipfs_cid_new(0, hash, hash_length, CID_DAG_PROTOBUF);
	if (cid == NULL)
		return 0;
	struct Blockstore* blockstore = ipfs_blockstore_new(fs_repo);
	if (blockstore == NULL) {
		ipfs_cid_free(cid);
		return 0;
	}
	retVal = ipfs_blockstore_get(blockstore->blockstoreContext, cid, block);
	ipfs_blockstore_free(blockstore);
	ipfs_cid_free(cid);
	return retVal;
}

int ipfs_repo_fsrepo_unixfs_read(const unsigned char* hash, size_t hash_length, struct UnixFS** unix_fs, const struct FSRepo* fs_repo) {
	int retVal = 0;

	// get the base32 hash from the database
	// We do this only to see if it is in the database
	struct DatastoreRecord *datastore_record = NULL;
	if (!fs_repo->config->datastore->datastore_get(hash, hash_length, &datastore_record, fs_repo->config->datastore))
		return 0;
	libp2p_datastore_record_free(datastore_record);
	// now get the block from the blockstore
	retVal = ipfs_blockstore_get_unixfs(hash, hash_length, unix_fs, fs_repo);
	return retVal;
}

