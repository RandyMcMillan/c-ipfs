#ifndef test_repo_bootstrap_peers_h
#define test_repo_bootstrap_peers_h

//#include <string.h>

#include "ipfs/repo/config/bootstrap_peers.h"

int test_repo_bootstrap_peers_init() {
	/* Must stay in sync with repo/config/bootstrap_peers.c */
	char* default_bootstrap_addresses[] = {
		"/ip4/104.131.131.82/tcp/4001/p2p/QmaCpDMGvV2BGHeYERUEnRQAwe3N8SzbUtfsmvsqQLuvuJ",
	};
	int expected_count = sizeof(default_bootstrap_addresses) / sizeof(default_bootstrap_addresses[0]);

	struct Libp2pVector* list;
	int retVal = 1;
	repo_config_bootstrap_peers_retrieve(&list);

	if (list->total != expected_count) {
		printf("Bootstrap peer count mismatch: expected %d, got %d\n", expected_count, list->total);
		retVal = 0;
	}

	for(int i = 0; i < list->total && i < expected_count; i++) {
		unsigned long strLen = strlen(default_bootstrap_addresses[i]);
		struct MultiAddress* currAddr = (struct MultiAddress*)libp2p_utils_vector_get(list, i);
		if (strncmp(currAddr->string, default_bootstrap_addresses[i], strLen) != 0) {
			printf("Bootstrap peer %d mismatch: expected %s, got %s\n", i, default_bootstrap_addresses[i], currAddr->string);
			retVal = 0;
		}
	}
	repo_config_bootstrap_peers_free(list);
	return retVal;
}

#endif /* test_repo_bootstrap_peers_h */
