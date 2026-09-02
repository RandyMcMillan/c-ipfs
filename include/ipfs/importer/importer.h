#ifndef __IPFS_IMPORTER_IMPORTER_H__
#define __IPFS_IMPORTER_IMPORTER_H__

#include "ipfs/cmd/cli.h"
#include "ipfs/merkledag/node.h"
#include "ipfs/core/ipfs_node.h"

enum UnixFSLayout {
	UNIXFS_LAYOUT_BALANCED,
	UNIXFS_LAYOUT_TRICKLE
};

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
int ipfs_import_file(const char* root, const char* fileName, struct HashtableNode** parent_node, struct IpfsNode *local_node, size_t* bytes_written, int recursive);

/**
 * Import a file with a specific chunking layout (balanced or trickle)
 * @param root_dir the directory for where to look for the file
 * @param file_name the file (or directory) to import
 * @param parent_node the root node
 * @param local_node the ipfs node
 * @param bytes_written number of bytes written to disk
 * @param recursive true if we should navigate directories
 * @param layout the layout strategy (balanced or trickle)
 * @returns true(1) on success
 */
int ipfs_import_file_with_layout(const char* root, const char* fileName, struct HashtableNode** parent_node, struct IpfsNode *local_node, size_t* bytes_written, int recursive, enum UnixFSLayout layout);

/**
 * called from the command line
 * @param argc the number of arguments
 * @param argv the arguments
 */
int ipfs_import_files(struct CliArguments* args);

#endif /* INCLUDE_IPFS_IMPORTER_IMPORTER_H_ */
