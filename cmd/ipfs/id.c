#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs/cmd/ipfs/id.h"
#include "ipfs/repo/init.h"
#include "ipfs/repo/fsrepo/fs_repo.h"
#include "libp2p/utils/logger.h"
#include "libp2p/utils/linked_list.h"

int ipfs_id(int argc, char** argv) {
	char* repo_dir = ipfs_repo_get_home_directory(argc, argv);
	if (repo_dir == NULL) {
		libp2p_logger_error("id", "Could not determine repo directory.\n");
		return 0;
	}

	if (!fs_repo_is_initialized(repo_dir)) {
		fprintf(stderr, "Error: no IPFS repo found in %s.\n", repo_dir);
		fprintf(stderr, "Please run: ipfs init\n");
		return 0;
	}

	struct FSRepo* repo = NULL;
	if (!ipfs_repo_fsrepo_new(repo_dir, NULL, &repo)) {
		libp2p_logger_error("id", "Failed to allocate repo structure.\n");
		return 0;
	}

	if (!fs_repo_open_config(repo)) {
		libp2p_logger_error("id", "Failed to open repo config.\n");
		ipfs_repo_fsrepo_free(repo);
		return 0;
	}

	if (repo->config->identity == NULL || repo->config->identity->peer == NULL) {
		libp2p_logger_error("id", "Identity not found in config.\n");
		ipfs_repo_fsrepo_free(repo);
		return 0;
	}

	printf("ID\t\t%s\n", repo->config->identity->peer->id);

	if (repo->config->identity->peer->addr_head != NULL) {
		printf("Addresses:\n");
		struct Libp2pLinkedList* current = repo->config->identity->peer->addr_head;
		while (current != NULL) {
			struct MultiAddress* ma = (struct MultiAddress*)current->item;
			if (ma != NULL && ma->string != NULL) {
				printf("  %s\n", ma->string);
			}
			current = current->next;
		}
	}

	ipfs_repo_fsrepo_free(repo);
	return 1;
}
