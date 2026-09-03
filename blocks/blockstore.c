/***
 * a thin wrapper over a datastore for getting and putting block objects
 */
#include <sys/stat.h>
#include "libp2p/crypto/encoding/base32.h"
#include "ipfs/cid/cid.h"
#include "ipfs/blocks/block.h"
#include "ipfs/blocks/blockstore.h"
#include "ipfs/datastore/ds_helper.h"
#include "ipfs/repo/fsrepo/fs_repo.h"
#include "libp2p/os/utils.h"

/* forward declarations of helpers */
unsigned char* ipfs_blockstore_cid_to_base32(const struct Cid* cid);
char* ipfs_blockstore_path_get(const struct FSRepo* fs_repo, const char* filename);


/***
 * Create a new Blockstore struct
 * @param fs_repo the FSRepo to use
 * @returns the new Blockstore struct, or NULL if there was a problem.
 */
struct Blockstore* ipfs_blockstore_new(const struct FSRepo* fs_repo) {
	struct Blockstore* blockstore = (struct Blockstore*) malloc(sizeof(struct Blockstore));
	if(blockstore != NULL) {
		blockstore->blockstoreContext = (struct BlockstoreContext*) malloc(sizeof(struct BlockstoreContext));
		if (blockstore->blockstoreContext == NULL) {
			free(blockstore);
			return NULL;
		}
		blockstore->blockstoreContext->fs_repo = fs_repo;
		blockstore->Delete = ipfs_blockstore_delete;
		blockstore->Get = ipfs_blockstore_get;
		blockstore->Has = ipfs_blockstore_has;
		blockstore->Put = ipfs_blockstore_put;
	}
	return blockstore;
}

/**
 * Release resources of a Blockstore struct
 * @param blockstore the struct to free
 * @returns true(1)
 */
int ipfs_blockstore_free(struct Blockstore* blockstore) {
	if (blockstore != NULL) {
		if (blockstore->blockstoreContext != NULL)
			free(blockstore->blockstoreContext);
		free(blockstore);
	}
	return 1;
}

static int blockstore_process_file(const char* blockstore_path, const char* rel_path, const char* file_name,
		struct BlockstoreEntry** entries, struct BlockstoreEntry** last, int* count) {
	if (strstr(file_name, ".data") == NULL)
		return 0;

	char* dot = strchr(file_name, '.');
	char key_name[128];
	if (dot && (size_t)(dot - file_name) < sizeof(key_name)) {
		memcpy(key_name, file_name, dot - file_name);
		key_name[dot - file_name] = '\0';
	} else {
		strncpy(key_name, file_name, sizeof(key_name) - 1);
		key_name[sizeof(key_name) - 1] = '\0';
	}

	size_t hash_len = 0;
	unsigned char hash[64];
	if (!ipfs_datastore_helper_binary_from_ds_key((unsigned char*)key_name, strlen(key_name),
		hash, sizeof(hash), &hash_len)) {
		return 0;
	}

	struct BlockstoreEntry* entry = (struct BlockstoreEntry*)calloc(1, sizeof(struct BlockstoreEntry));
	if (!entry) return -1;
	entry->hash = (unsigned char*)malloc(hash_len);
	if (!entry->hash) {
		free(entry);
		return -1;
	}
	memcpy(entry->hash, hash, hash_len);
	entry->hash_size = hash_len;
	entry->next = NULL;

	char full_file_path[strlen(blockstore_path) + strlen(rel_path) + strlen(file_name) + 4];
	if (os_utils_filepath_join(blockstore_path, rel_path, full_file_path, sizeof(full_file_path))) {
		char tmp[sizeof(full_file_path)];
		if (os_utils_filepath_join(full_file_path, file_name, tmp, sizeof(tmp))) {
			struct stat st;
			if (stat(tmp, &st) == 0)
				entry->block_size = (size_t)st.st_size;
		}
	}

	if (*last == NULL) {
		*entries = entry;
	} else {
		(*last)->next = entry;
	}
	*last = entry;
	(*count)++;
	return 0;
}

static int blockstore_list_dir_recursive(const char* blockstore_path, const char* rel_path,
		struct BlockstoreEntry** entries, struct BlockstoreEntry** last, int* count) {
	char full_path[strlen(blockstore_path) + strlen(rel_path) + 2];
	if (!os_utils_filepath_join(blockstore_path, rel_path, full_path, sizeof(full_path)))
		return 0;

	struct FileList* files = os_utils_list_directory(full_path);
	struct FileList* current = files;
	while (current != NULL) {
		if (strcmp(current->file_name, ".") == 0 || strcmp(current->file_name, "..") == 0) {
			current = current->next;
			continue;
		}

		char child_path[strlen(full_path) + strlen(current->file_name) + 2];
		if (!os_utils_filepath_join(full_path, current->file_name, child_path, sizeof(child_path))) {
			current = current->next;
			continue;
		}

		struct stat st;
		if (stat(child_path, &st) == 0 && S_ISDIR(st.st_mode)) {
			char child_rel[strlen(rel_path) + strlen(current->file_name) + 2];
			if (os_utils_filepath_join(rel_path, current->file_name, child_rel, sizeof(child_rel))) {
				if (blockstore_list_dir_recursive(blockstore_path, child_rel, entries, last, count) != 0) {
					os_utils_free_file_list(files);
					return -1;
				}
			}
		} else {
			char file_rel[strlen(rel_path) + strlen(current->file_name) + 2];
			if (os_utils_filepath_join(rel_path, current->file_name, file_rel, sizeof(file_rel))) {
				if (blockstore_process_file(blockstore_path, rel_path, current->file_name, entries, last, count) != 0) {
					os_utils_free_file_list(files);
					return -1;
				}
			}
		}
		current = current->next;
	}
	os_utils_free_file_list(files);
	return 0;
}

/***
 * List all blocks in the blockstore.
 */
int ipfs_blockstore_list(const struct FSRepo* fs_repo, struct BlockstoreEntry** entries) {
	int count = 0;
	*entries = NULL;
	struct BlockstoreEntry* last = NULL;

	char blockstore_path[strlen(fs_repo->path) + 12];
	if (!os_utils_filepath_join(fs_repo->path, "blockstore", blockstore_path, sizeof(blockstore_path)))
		return -1;

	if (blockstore_list_dir_recursive(blockstore_path, "", entries, &last, &count) != 0)
		return -1;

	return count;
}

void ipfs_blockstore_list_free(struct BlockstoreEntry* entries) {
	while (entries != NULL) {
		struct BlockstoreEntry* next = entries->next;
		free(entries->hash);
		free(entries);
		entries = next;
	}
}

/**
 * Delete a block based on its Cid
 * @param cid the Cid to look for
 * @param returns true(1) on success
 */
int ipfs_blockstore_delete(const struct BlockstoreContext* context, struct Cid* cid) {
	unsigned char* key = ipfs_blockstore_cid_to_base32(cid);
	if (key == NULL)
		return 0;
	char* filename = ipfs_blockstore_path_get(context->fs_repo, (char*)key);
	if (filename == NULL) {
		free(key);
		return 0;
	}
	int ret = remove(filename);
	free(key);
	free(filename);
	return ret == 0;
}

/***
 * Determine if the Cid can be found
 * @param cid the Cid to look for
 * @returns true(1) if found
 */
int ipfs_blockstore_has(const struct BlockstoreContext* context, struct Cid* cid) {
	return 0;
}

unsigned char* ipfs_blockstore_cid_to_base32(const struct Cid* cid) {
	size_t key_length = libp2p_crypto_encoding_base32_encode_size(cid->hash_length);
	unsigned char* buffer = (unsigned char*)malloc(key_length + 1);
	if (buffer == NULL)
		return NULL;
	int retVal = ipfs_datastore_helper_ds_key_from_binary(cid->hash, cid->hash_length, &buffer[0], key_length, &key_length);
	if (retVal == 0) {
		free(buffer);
		return NULL;
	}
	buffer[key_length] = 0;
	return buffer;
}

unsigned char* ipfs_blockstore_hash_to_base32(const unsigned char* hash, size_t hash_length) {
	size_t key_length = libp2p_crypto_encoding_base32_encode_size(hash_length);
	unsigned char* buffer = (unsigned char*)malloc(key_length + 1);
	if (buffer == NULL)
		return NULL;
	int retVal = ipfs_datastore_helper_ds_key_from_binary(hash, hash_length, &buffer[0], key_length, &key_length);
	if (retVal == 0) {
		free(buffer);
		return NULL;
	}
	buffer[key_length] = 0;
	return buffer;
}

static int blockstore_mkdir_p(const char *path) {
	char tmp[512];
	char *p = NULL;
	size_t len = strlen(path);
	if (len >= sizeof(tmp)) return 0;
	memcpy(tmp, path, len + 1);
	if (tmp[len - 1] == '/')
		tmp[len - 1] = '\0';
	for (p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	mkdir(tmp, 0755);
	return 1;
}

char* ipfs_blockstore_path_get(const struct FSRepo* fs_repo, const char* filename) {
	int filepath_size = strlen(fs_repo->path) + 12;
	char filepath[filepath_size];
	int retVal = os_utils_filepath_join(fs_repo->path, "blockstore", filepath, filepath_size);
	if (retVal == 0) {
		return NULL;
	}

	// 2-level shard: blockstore/AB/CD/ABCD...data
	char shard1[4] = {filename[0], filename[1], '\0'};
	char shard2[4] = {filename[2], filename[3], '\0'};
	int shard_path_size = strlen(filepath) + 16;
	char shard_path[shard_path_size];
	char shard2_path[shard_path_size];
	if (!os_utils_filepath_join(filepath, shard1, shard_path, shard_path_size))
		return NULL;
	if (!os_utils_filepath_join(shard_path, shard2, shard2_path, shard_path_size))
		return NULL;
	blockstore_mkdir_p(shard2_path);

	int complete_filename_size = strlen(shard2_path) + strlen(filename) + 8;
	char* complete_filename = (char*)malloc(complete_filename_size);
	if (complete_filename == NULL)
		return NULL;
	retVal = os_utils_filepath_join(shard2_path, filename, complete_filename, complete_filename_size);
	if (retVal) {
		strcat(complete_filename, ".data");
	}
	return complete_filename;
}

/***
 * Find a block based on its Cid
 * @param cid the Cid to look for
 * @param block where to put the data to be returned
 * @returns true(1) on success
 */
int ipfs_blockstore_get(const struct BlockstoreContext* context, struct Cid* cid, struct Block** block) {
	int retVal = 0;
	// get datastore key, which is a base32 key of the multihash
	unsigned char* key = ipfs_blockstore_hash_to_base32(cid->hash, cid->hash_length);

	char* filename = ipfs_blockstore_path_get(context->fs_repo, (char*)key);

	size_t file_size = os_utils_file_size(filename);
	unsigned char buffer[file_size];

	FILE* file = fopen(filename, "rb");
	if (file == NULL)
		goto exit;

	size_t bytes_read = fread(buffer, 1, file_size, file);
	fclose(file);

	if (!ipfs_blocks_block_protobuf_decode(buffer, bytes_read, block))
		goto exit;

	(*block)->cid = ipfs_cid_copy(cid);

	if (!ipfs_block_validate(*block))
		goto exit;

	retVal = 1;
	exit:
	free(key);
	free(filename);

	return retVal;
}

/***
 * Put a block in the blockstore
 * @param block the block to store
 * @returns true(1) on success
 */
int ipfs_blockstore_put(const struct BlockstoreContext* context, struct Block* block, size_t* bytes_written) {
	// from blockstore.go line 118
	int retVal = 0;

	if (!ipfs_block_validate(block))
		return 0;

	// Get Datastore key, which is a base32 key of the multihash,
	unsigned char* key = ipfs_blockstore_cid_to_base32(block->cid);
	if (key == NULL) {
		free(key);
		return 0;
	}

	// turn the block into a binary array
	size_t protobuf_len = ipfs_blocks_block_protobuf_encode_size(block);
	unsigned char protobuf[protobuf_len];
	retVal = ipfs_blocks_block_protobuf_encode(block, protobuf, protobuf_len, &protobuf_len);
	if (retVal == 0) {
		free(key);
		return 0;
	}

	// now write byte array to file
	char* filename = ipfs_blockstore_path_get(context->fs_repo, (char*)key);
	if (filename == NULL) {
		free(key);
		return 0;
	}

	FILE* file = fopen(filename, "wb");
	*bytes_written = fwrite(protobuf, 1, protobuf_len, file);
	fclose(file);
	if (*bytes_written != protobuf_len) {
		free(key);
		free(filename);
		return 0;
	}

	// send to Put with key (this is now done separately)
	//fs_repo->config->datastore->datastore_put(key, key_length, block->data, block->data_length, fs_repo->config->datastore);

	free(key);
	free(filename);
	return 1;
}

/***
 * Put a struct UnixFS in the blockstore
 * @param unix_fs the structure
 * @param fs_repo the repo to place the strucure in
 * @param bytes_written the number of bytes written to the blockstore
 * @returns true(1) on success
 */
int ipfs_blockstore_put_unixfs(const struct UnixFS* unix_fs, const struct FSRepo* fs_repo, size_t* bytes_written) {
	// from blockstore.go line 118
	int retVal = 0;

	// Get Datastore key, which is a base32 key of the multihash,
	unsigned char* key = ipfs_blockstore_hash_to_base32(unix_fs->hash, unix_fs->hash_length);
	if (key == NULL) {
		free(key);
		return 0;
	}

	// turn the block into a binary array
	size_t protobuf_len = ipfs_unixfs_protobuf_encode_size(unix_fs);
	unsigned char protobuf[protobuf_len];
	retVal = ipfs_unixfs_protobuf_encode(unix_fs, protobuf, protobuf_len, &protobuf_len);
	if (retVal == 0) {
		free(key);
		return 0;
	}

	// now write byte array to file
	char* filename = ipfs_blockstore_path_get(fs_repo, (char*)key);
	if (filename == NULL) {
		free(key);
		return 0;
	}

	FILE* file = fopen(filename, "wb");
	*bytes_written = fwrite(protobuf, 1, protobuf_len, file);
	fclose(file);
	if (*bytes_written != protobuf_len) {
		free(key);
		free(filename);
		return 0;
	}

	free(key);
	free(filename);
	return 1;
}

/***
 * Find a UnixFS struct based on its hash
 * @param hash the hash to look for
 * @param hash_length the length of the hash
 * @param unix_fs the struct to fill
 * @param fs_repo where to look for the data
 * @returns true(1) on success
 */
int ipfs_blockstore_get_unixfs(const unsigned char* hash, size_t hash_length, struct UnixFS** block, const struct FSRepo* fs_repo) {
	// get datastore key, which is a base32 key of the multihash
	unsigned char* key = ipfs_blockstore_hash_to_base32(hash, hash_length);

	char* filename = ipfs_blockstore_path_get(fs_repo, (char*)key);

	size_t file_size = os_utils_file_size(filename);
	unsigned char buffer[file_size];

	FILE* file = fopen(filename, "rb");
	size_t bytes_read = fread(buffer, 1, file_size, file);
	fclose(file);

	int retVal = ipfs_unixfs_protobuf_decode(buffer, bytes_read, block);

	free(key);
	free(filename);

	return retVal;
}

/***
 * Put a struct Node in the blockstore
 * @param node the structure
 * @param fs_repo the repo to place the strucure in
 * @param bytes_written the number of bytes written to the blockstore
 * @returns true(1) on success
 */
int ipfs_blockstore_put_node(const struct HashtableNode* node, const struct FSRepo* fs_repo, size_t* bytes_written) {
	// from blockstore.go line 118
	int retVal = 0;

	// Get Datastore key, which is a base32 key of the multihash,
	unsigned char* key = ipfs_blockstore_hash_to_base32(node->hash, node->hash_size);
	if (key == NULL) {
		free(key);
		return 0;
	}

	// turn the block into a binary array
	size_t protobuf_len = ipfs_hashtable_node_protobuf_encode_size(node);
	unsigned char protobuf[protobuf_len];
	retVal = ipfs_hashtable_node_protobuf_encode(node, protobuf, protobuf_len, &protobuf_len);
	if (retVal == 0) {
		free(key);
		return 0;
	}

	// now write byte array to file
	char* filename = ipfs_blockstore_path_get(fs_repo, (char*)key);
	if (filename == NULL) {
		free(key);
		return 0;
	}

	FILE* file = fopen(filename, "wb");
	*bytes_written = fwrite(protobuf, 1, protobuf_len, file);
	fclose(file);
	if (*bytes_written != protobuf_len) {
		free(key);
		free(filename);
		return 0;
	}

	free(key);
	free(filename);
	return 1;
}

/***
 * Find a UnixFS struct based on its hash
 * @param hash the hash to look for
 * @param hash_length the length of the hash
 * @param unix_fs the struct to fill
 * @param fs_repo where to look for the data
 * @returns true(1) on success
 */
int ipfs_blockstore_get_node(const unsigned char* hash, size_t hash_length, struct HashtableNode** node, const struct FSRepo* fs_repo) {
	// get datastore key, which is a base32 key of the multihash
	unsigned char* key = ipfs_blockstore_hash_to_base32(hash, hash_length);

	char* filename = ipfs_blockstore_path_get(fs_repo, (char*)key);

	size_t file_size = os_utils_file_size(filename);
	unsigned char buffer[file_size];

	FILE* file = fopen(filename, "rb");
	if (file == NULL) {
		free(key);
		free(filename);
		return 0;
	}
	size_t bytes_read = fread(buffer, 1, file_size, file);
	fclose(file);

	// now we have the block, convert it to a node
	struct Block* block;
	if (!ipfs_blocks_block_protobuf_decode(buffer, bytes_read, &block)) {
		free(key);
		free(filename);
		ipfs_block_free(block);
		return 0;
	}

	int retVal = ipfs_hashtable_node_protobuf_decode(block->data, block->data_length, node);

	free(key);
	free(filename);
	ipfs_block_free(block);

	return retVal;
}

