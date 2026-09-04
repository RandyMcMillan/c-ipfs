#include <stdio.h>
#include <stdlib.h>

#include "ipfs/repo/config/bootstrap_peers.h"
#include "multiaddr/multiaddr.h"

int repo_config_bootstrap_peers_retrieve(struct Libp2pVector** list) {
	*list = libp2p_utils_vector_new(1);

	/* Canonical libp2p bootstrap peers (Kubo v0.43.0 defaults).
	 * NOTE: /dnsaddr/ entries require runtime DNS resolution; only /ip4/
	 * multiaddrs can be parsed directly by c-multiaddr today.
	 */
	char* default_bootstrap_addresses[] = {
		/* Direct IPv4 bootstrap peer — protocol-labs / ipfs.io infrastructure */
		"/ip4/104.131.131.82/tcp/4001/p2p/QmaCpDMGvV2BGHeYERUEnRQAwe3N8SzbUtfsmvsqQLuvuJ",
		/* TODO: resolve /dnsaddr/bootstrap.libp2p.io at runtime:
		 * /dnsaddr/bootstrap.libp2p.io/p2p/QmNnooDu7bfjPFoTZYxMNLWUQJyrVwtbZg5gBMjTezGAJN
		 * /dnsaddr/bootstrap.libp2p.io/p2p/QmQCU2EcMqAqQPR2i9bChDtGNJchTbq5TbXJJ16u19uLTa
		 * /dnsaddr/bootstrap.libp2p.io/p2p/QmbLHAnMoJPWSCR5Zhtx6BHJX9KiKNN6tpvbUcqanj75Nb
		 * /dnsaddr/bootstrap.libp2p.io/p2p/QmcZf1Y3P92VACxpA3r12FKuLDZLGWTu8ams5BL8xaSF8Y
		 */
	};
	int num_peers = sizeof(default_bootstrap_addresses) / sizeof(default_bootstrap_addresses[0]);
	for(int i = 0; i < num_peers; i++) {
		struct MultiAddress* currAddr = multiaddress_new_from_string(default_bootstrap_addresses[i]);
		if (currAddr != NULL) {
			libp2p_utils_vector_add(*list, currAddr);
		}
	}
	return 1;
}

int repo_config_bootstrap_peers_free(struct Libp2pVector* list) {
	if (list != NULL) {
		for(int i = 0; i < list->total; i++) {
			struct MultiAddress* currAddr = (struct MultiAddress*)libp2p_utils_vector_get(list, i);
			multiaddress_free(currAddr);
		}
		libp2p_utils_vector_free(list);
	}
	return 1;
}
