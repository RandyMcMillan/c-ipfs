// these two for strdup
#define _GNU_SOURCE
#define __USE_GNU
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "ipfs/importer/importer.h"
#include "ipfs/merkledag/merkledag.h"
#include "libp2p/os/utils.h"
#include "ipfs/cmd/cli.h"
#include "ipfs/core/ipfs_node.h"
#include "ipfs/core/http_request.h"
#include "ipfs/repo/fsrepo/fs_repo.h"
#include "ipfs/repo/init.h"
#include "ipfs/unixfs/unixfs.h"

#define MAX_DATA_SIZE 262144 // 1024 * 256;

/***
 * Imports OS files into the datastore
 */

/***
 * adds a blocksize to the UnixFS structure stored in the data
 * element of a Node
 * @param node the node to work with
 * @param blocksize the blocksize to add
 * @returns true(1) on success
 */
int ipfs_importer_add_filesize_to_data_section(struct HashtableNode* node, size_t bytes_read) {
	// now add to the data section
	struct UnixFS* data_section = NULL;
	if (node->data == NULL) {
		// nothing in data section yet, create new UnixFS
		ipfs_unixfs_new(&data_section);
		data_section->data_type = UNIXFS_FILE;
	} else {
		ipfs_unixfs_protobuf_decode(node->data, node->data_size, &data_section);
	}
	struct UnixFSBlockSizeNode bs;
	bs.block_size = bytes_read;
	ipfs_unixfs_add_blocksize(&bs, data_section);
	data_section->file_size += bytes_read;
	// put the new data back in the data section
	size_t protobuf_size = ipfs_unixfs_protobuf_encode_size(data_section); //delay bytes_size entry
	unsigned char protobuf[protobuf_size];
	ipfs_unixfs_protobuf_encode(data_section, protobuf, protobuf_size, &protobuf_size);
	ipfs_unixfs_free(data_section);
	ipfs_hashtable_node_set_data(node, protobuf, protobuf_size);
	return 1;
}

/**
 * read the next chunk of bytes, create a node, and add a link to the node in the passed-in node
 * @param file the file handle
 * @param node the node to add to
 * @returns number of bytes read
 */
size_t ipfs_import_chunk(FILE* file, struct HashtableNode* parent_node, struct FSRepo* fs_repo, size_t* total_size, size_t* bytes_written) {
	unsigned char buffer[MAX_DATA_SIZE];
	size_t bytes_read = fread(buffer, 1, MAX_DATA_SIZE, file);

	// structs used by this method
	struct UnixFS* new_unixfs = NULL;
	struct HashtableNode* new_node = NULL;
	struct NodeLink* new_link = NULL;

	// put the file bits into a new UnixFS file
	if (ipfs_unixfs_new(&new_unixfs) == 0)
		return 0;
	new_unixfs->data_type = UNIXFS_FILE;
	new_unixfs->file_size = bytes_read;
	if (ipfs_unixfs_add_data(&buffer[0], bytes_read, new_unixfs) == 0) {
		ipfs_unixfs_free(new_unixfs);
		return 0;
	}
	// protobuf the UnixFS
	size_t protobuf_size = ipfs_unixfs_protobuf_encode_size(new_unixfs);
	if (protobuf_size == 0) {
		ipfs_unixfs_free(new_unixfs);
		return 0;
	}
	unsigned char protobuf[protobuf_size];
	*bytes_written = 0;
	if (ipfs_unixfs_protobuf_encode(new_unixfs, protobuf, protobuf_size, bytes_written) == 0) {
		ipfs_unixfs_free(new_unixfs);
		return 0;
	}

	// we're done with the UnixFS object
	ipfs_unixfs_free(new_unixfs);

	size_t size_of_node = 0;

	// if there is more to read, create a new node.
	if (bytes_read == MAX_DATA_SIZE) {
		// create a new node
		if (ipfs_hashtable_node_new_from_data(protobuf, *bytes_written, &new_node) == 0) {
			return 0;
		}
		// persist
		size_t size_of_node = 0;
		if (ipfs_merkledag_add(new_node, fs_repo, &size_of_node) == 0) {
			ipfs_hashtable_node_free(new_node);
			return 0;
		}

		// put link in parent node
		if (ipfs_node_link_create(NULL, new_node->hash, new_node->hash_size, &new_link) == 0) {
			ipfs_hashtable_node_free(new_node);
			return 0;
		}
		new_link->t_size = size_of_node;
		*total_size += new_link->t_size;
		// NOTE: disposal of this link object happens when the parent is disposed
		if (ipfs_hashtable_node_add_link(parent_node, new_link) == 0) {
			ipfs_hashtable_node_free(new_node);
			return 0;
		}
		ipfs_importer_add_filesize_to_data_section(parent_node, bytes_read);
		ipfs_hashtable_node_free(new_node);
		*bytes_written = size_of_node;
		size_of_node = 0;
	} else {
		// if there are no existing links, put what we pulled from the file into parent_node
		// otherwise, add it as a link
		if (parent_node->head_link == NULL) {
			ipfs_hashtable_node_set_data(parent_node, protobuf, *bytes_written);
		} else {
			// there are existing links. put the data in a new node, save it, then put the link in parent_node
			// create a new node
			if (ipfs_hashtable_node_new_from_data(protobuf, *bytes_written, &new_node) == 0) {
				return 0;
			}
			// persist
			if (ipfs_merkledag_add(new_node, fs_repo, &size_of_node) == 0) {
				ipfs_hashtable_node_free(new_node);
				return 0;
			}

			// put link in parent node
			if (ipfs_node_link_create(NULL, new_node->hash, new_node->hash_size, &new_link) == 0) {
				ipfs_hashtable_node_free(new_node);
				return 0;
			}
			new_link->t_size = size_of_node;
			*total_size += new_link->t_size;
			// NOTE: disposal of this link object happens when the parent is disposed
			if (ipfs_hashtable_node_add_link(parent_node, new_link) == 0) {
				ipfs_hashtable_node_free(new_node);
				return 0;
			}
			ipfs_importer_add_filesize_to_data_section(parent_node, bytes_read);
			ipfs_hashtable_node_free(new_node);
		}
		// persist the main node
		ipfs_merkledag_add(parent_node, fs_repo, bytes_written);
		*bytes_written += size_of_node;
	} // add to parent vs add as link

	return bytes_read;
}

/**
 * Prints to the console the results of a node import
 * @param node the node imported
 * @param file_name the name of the file
 * @returns true(1) if successful, false(0) if couldn't generate the MultiHash to be displayed
 */
int ipfs_import_print_node_results(const struct HashtableNode* node, const char* file_name) {
	// give some results to the user
	//TODO: if directory_entry is itself a directory, traverse and report files
	int buffer_len = 100;
	unsigned char buffer[buffer_len];
	if (ipfs_cid_hash_to_base58(node->hash, node->hash_size, buffer, buffer_len) == 0) {
		printf("Unable to generate hash for file %s.\n", file_name);
		return 0;
	}
	printf("added %s %s\n", buffer, file_name);
	return 1;
}


/**
 * Creates a node based on an incoming file or directory
 * NOTE: this can be called recursively for directories
 * NOTE: When this function completes, parent_node will be either:
 * 	1) the complete file, in the case of a small file (<256k-ish)
 * 	2) a node with links to the various pieces of a large file
 * 	3) a node with links to files and directories if 'fileName' is a directory
 * @param root_dir the directory for where to look for the file
 * @param file_name the file (or directory) to import
 * @param parent_node the root node (has links to others in case this is a large file and is split)
 * @param fs_repo the ipfs repository
 * @param bytes_written number of bytes written to disk
 * @param recursive true if we should navigate directories
 * @returns true(1) on success
 */
int ipfs_import_file(const char* root_dir, const char* fileName, struct HashtableNode** parent_node, struct IpfsNode* local_node, size_t* bytes_written, int recursive) {
	/**
	 * NOTE: When this function completes, parent_node will be either:
	 * 1) the complete file, in the case of a small file (<256k-ish)
	 * 2) a node with links to the various pieces of a large file
	 * 3) a node with links to files and directories if 'fileName' is a directory
	 */
	int retVal = 1;
	int bytes_read = MAX_DATA_SIZE;
	size_t total_size = 0;

	if (os_utils_is_directory(fileName)) {
		// calculate the new root_dir
		char* new_root_dir = (char*)root_dir;
		char* path = NULL;
		char* file = NULL;
		os_utils_split_filename(fileName, &path, &file);
		if (root_dir == NULL) {
			new_root_dir = file;
		} else {
			free(path);
			path = malloc(strlen(root_dir) + strlen(file) + 2);
			if (path == NULL) {
				// memory issue
				if (file != NULL)
					free(file);
				return 0;
			}
			os_utils_filepath_join(root_dir, file, path, strlen(root_dir) + strlen(file) + 2);
			new_root_dir = path;
		}
		// initialize parent_node as a directory
		if (ipfs_hashtable_node_create_directory(parent_node) == 0) {
			if (path != NULL)
				free(path);
			if (file != NULL)
				free(file);
			return 0;
		}
		// get list of files
		struct FileList* first = os_utils_list_directory(fileName);
		struct FileList* next = first;
		if (recursive) {
			while (next != NULL) {
				// process each file. NOTE: could be an embedded directory
				*bytes_written = 0;
				struct HashtableNode* file_node;
				// put the filename together from fileName, which is the directory, and next->file_name
				// which is a file (or a directory) within the directory we just found.
				size_t filename_len = strlen(fileName) + strlen(next->file_name) + 2;
				char full_file_name[filename_len];
				os_utils_filepath_join(fileName, next->file_name, full_file_name, filename_len);
				// adjust root directory

				if (ipfs_import_file(new_root_dir, full_file_name, &file_node, local_node, bytes_written, recursive) == 0) {
					ipfs_hashtable_node_free(*parent_node);
					os_utils_free_file_list(first);
					if (file != NULL)
						free(file);
					if (path != NULL)
						free (path);
					return 0;
				}
				// TODO: probably need to display what was imported
				int len = strlen(next->file_name) + strlen(new_root_dir) + 2;
				char full_path[len];
				os_utils_filepath_join(new_root_dir, next->file_name, full_path, len);
				ipfs_import_print_node_results(file_node, full_path);
				// TODO: Determine what needs to be done if this file_node is a file, a split file, or a directory
				// Create link from file_node
				struct NodeLink* file_node_link;
				ipfs_node_link_create(next->file_name, file_node->hash, file_node->hash_size, &file_node_link);
				file_node_link->t_size = *bytes_written;
				// add file_node as link to parent_node
				ipfs_hashtable_node_add_link(*parent_node, file_node_link);
				// clean up file_node
				ipfs_hashtable_node_free(file_node);
				// move to next file in list
				next = next->next;
			} // while going through files
		}
		// save the parent_node (the directory)
		size_t bytes_written;
		ipfs_merkledag_add(*parent_node, local_node->repo, &bytes_written);
		if (file != NULL)
			free(file);
		if (path != NULL)
			free (path);
		os_utils_free_file_list(first);
	} else {
		// process this file
		FILE* file = fopen(fileName, "rb");
		if (file == 0)
			return 0;
		retVal = ipfs_hashtable_node_new(parent_node);
		if (retVal == 0) {
			return 0;
		}

		// add all nodes (will be called multiple times for large files)
		while ( bytes_read == MAX_DATA_SIZE) {
			size_t written = 0;
			bytes_read = ipfs_import_chunk(file, *parent_node, local_node->repo, &total_size, &written);
			*bytes_written += written;
		}
		fclose(file);
	}

	// notify the network
	struct HashtableNode *htn = *parent_node;
	local_node->routing->Provide(local_node->routing, htn->hash, htn->hash_size);
	// notify the network of the subnodes too
	struct NodeLink *nl = htn->head_link;
	while (nl != NULL) {
		local_node->routing->Provide(local_node->routing, nl->hash, nl->hash_size);
		nl = nl->next;
	}

	return 1;
}

/**
 * Pulls list of files from command line parameters
 * @param argc number of command line parameters
 * @param argv command line parameters
 * @returns a FileList linked list of filenames
 */
struct FileList* ipfs_import_get_filelist(struct CliArguments* args) {
	struct FileList* first = NULL;
	struct FileList* last = NULL;

	for (int i = args->verb_index + 1; i < args->argc; i++) {
		if (strcmp(args->argv[i], "add") == 0) {
			continue;
		}
		struct FileList* current = (struct FileList*)malloc(sizeof(struct FileList));
		if (current == NULL) {
			return NULL;
		}
		current->next = NULL;
		current->file_name = args->argv[i];
		// now wire it in
		if (first == NULL) {
			first = current;
		}
		if (last != NULL) {
			last->next = current;
		}
		// now set last to current
		last = current;
	}
	return first;
}

/**
 * See if the recursive flag was passed on the command line
 * @param argc number of command line parameters
 * @param argv command line parameters
 * @returns true(1) if -r was passed, false(0) otherwise
 */
int ipfs_import_is_recursive(int argc, char** argv) {
	for(int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "-r") == 0)
			return 1;
	}
	return 0;
}

/**
 * called from the command line to import multiple files or directories
 * @param argc the number of arguments
 * @param argv the arguments
 */
int ipfs_import_files(struct CliArguments* args) {
	/*
	 * Param 0: ipfs
	 * param 1: add
	 * param 2: -r (optional)
	 * param 3: directoryname
	 */
	struct IpfsNode* local_node = NULL;
	char* repo_path = NULL;
	int retVal = 0;
	struct FileList* first = NULL;
	struct FileList* current = NULL;
	char* path = NULL;
	char* filename = NULL;
	struct HashtableNode* directory_entry = NULL;

	int recursive = ipfs_import_is_recursive(args->argc, args->argv);

	// parse the command line
	first = ipfs_import_get_filelist(args);

	// open the repo
	if (!ipfs_repo_get_directory(args->argc, args->argv, &repo_path)) {
		fprintf(stderr, "Repo does not exist: %s\n", repo_path);
		goto exit;
	}
	if (!ipfs_node_offline_new(repo_path, &local_node)) {
		fprintf(stderr, "Error: unable to open IPFS repo at %s\n", repo_path);
		goto exit;
	}

	/** disabling for the time being
	if (local_node->mode == MODE_API_AVAILABLE) {
		// do this through the API
		struct HttpRequest* request = ipfs_core_http_request_new();
		request->command = "add";
		struct HttpParam* recursive_param = ipfs_core_http_param_new();
		recursive_param->name = strdup("recursive");
		recursive_param->value = strdup((recursive ? "true" : "false"));
		libp2p_utils_vector_add(request->params, recursive_param);
		current = first;
		while (current != NULL) {
			libp2p_utils_vector_add(request->arguments, current->file_name);
			current = current->next;
		}
		uint8_t* result = NULL;
		size_t result_size = 0;
		if (!ipfs_core_http_request_post(local_node, request, &result, &result_size, data, data_size)) {

		}

	} else {
	*/
		// No daemon is running. Do this without using the API
		// import the file(s)
		current = first;
		while (current != NULL) {
			if (current->file_name[0] != '-') { // not a switch
				os_utils_split_filename(current->file_name, &path, &filename);
				size_t bytes_written = 0;
				if (!ipfs_import_file(NULL, current->file_name, &directory_entry, local_node, &bytes_written, recursive))
					goto exit;
				ipfs_import_print_node_results(directory_entry, filename);
				// cleanup
				if (path != NULL) {
					free(path);
					path = NULL;
				}
				if (filename != NULL) {
					free(filename);
					filename = NULL;
				}
				if (directory_entry != NULL) {
					ipfs_hashtable_node_free(directory_entry);
					directory_entry = NULL;
				}
			}
			current = current->next;
		}
	// } uncomment this line when the api is up and running with file transfer

	retVal = 1;
	exit:
	if (local_node != NULL)
		ipfs_node_free(local_node);
	// free file list
	current = first;
	while (current != NULL) {
		first = current->next;
		free(current);
		current = first;
	}
	if (path != NULL)
		free(path);
	if (filename != NULL)
		free(filename);
	if (directory_entry != NULL)
		ipfs_hashtable_node_free(directory_entry);
	//if (repo_path != NULL)
	//	free(repo_path);
	return retVal;
}

