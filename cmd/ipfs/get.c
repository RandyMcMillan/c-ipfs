#include <stdio.h>
#include <string.h>

#include "ipfs/cmd/ipfs/get.h"
#include "ipfs/importer/exporter.h"
#include "ipfs/repo/init.h"
#include "ipfs/core/ipfs_node.h"
#include "libp2p/utils/logger.h"

int ipfs_get(int argc, char** argv) {
    char* repo_path = NULL;
    if (!ipfs_repo_get_directory(argc, argv, &repo_path)) {
        fprintf(stderr, "Unable to open repository: %s\n", repo_path);
        return 0;
    }

    struct IpfsNode* local_node = NULL;
    if (!ipfs_node_offline_new(repo_path, &local_node)) {
        fprintf(stderr, "Unable to create offline node.\n");
        return 0;
    }

    /* Find hash argument (skip switches) */
    int hash_index = -1;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            hash_index = i;
            break;
        }
    }
    if (hash_index < 0 || hash_index + 1 >= argc) {
        fprintf(stderr, "Usage: ipfs get <hash> [<output-file>]\n");
        ipfs_node_free(local_node);
        return 0;
    }

    const char* hash = argv[hash_index + 1];
    const char* outfile = (hash_index + 2 < argc) ? argv[hash_index + 2] : hash;

    int ret = ipfs_exporter_to_file((const unsigned char*)hash, outfile, local_node);
    if (!ret) {
        fprintf(stderr, "Error: failed to get %s\n", hash);
    } else {
        printf("%s\n", outfile);
    }

    ipfs_node_free(local_node);
    return ret;
}
