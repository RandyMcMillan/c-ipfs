#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "libp2p/net/p2pnet.h"
#include "libp2p/net/multistream.h"
#include "libp2p/record/message.h"
#include "libp2p/secio/secio.h"
#include "libp2p/routing/dht_protocol.h"
#include "ipfs/repo/fsrepo/fs_repo.h"
#include "ipfs/repo/init.h"
#include "ipfs/core/ipfs_node.h"
#include "ipfs/routing/routing.h"
#include "ipfs/importer/resolver.h"
#include "multiaddr/multiaddr.h"

#define BUF_SIZE 4096

int ipfs_ping (int argc, char **argv)
{
	int retVal = 0;
	struct IpfsNode local_node;
	struct Libp2pPeer* peer_to_ping = NULL;
	char* id = NULL;
    struct FSRepo* fs_repo = NULL;
    char* repo_path = NULL;
	struct timeval time1, time2;
	int time_us;

	// sanity check
	local_node.peerstore = NULL;
	local_node.providerstore = NULL;
	if (argc < 3)
		goto exit;

	if (!ipfs_repo_get_directory(argc, argv, &repo_path)) {
		fprintf(stderr, "Unable to open repo: %s\n", repo_path);
		return 0;
	}
    // read the configuration
	if (!ipfs_repo_fsrepo_new(NULL, NULL, &fs_repo))
		goto exit;

	// open the repository and read the file
	if (!ipfs_repo_fsrepo_open(fs_repo))
		goto exit;

	local_node.identity = fs_repo->config->identity;
	local_node.repo = fs_repo;
	local_node.mode = MODE_ONLINE;
	local_node.routing = ipfs_routing_new_online(&local_node, &fs_repo->config->identity->private_key);
	local_node.peerstore = libp2p_peerstore_new(local_node.identity->peer);
	local_node.providerstore = libp2p_providerstore_new(fs_repo->config->datastore, fs_repo->config->identity->peer);

	if (local_node.routing->Bootstrap(local_node.routing) != 0)
		goto exit;

	if (strstr(argv[2], "Qm") == &argv[2][0]) {
		// resolve the peer id
		fprintf (stderr, "Looking up peer %s\n", argv[2]);
		peer_to_ping = ipfs_resolver_find_peer(argv[2], &local_node);
	} else {
		// perhaps they passed an IP and port
		if (argc >= 4) {
			char* str = malloc(strlen(argv[2]) + strlen(argv[3]) + 100);
			if (str == NULL) {
				// memory issue
				goto exit;
			}
			sprintf(str, "/ip4/%s/tcp/%s", argv[2], argv[3]);
			peer_to_ping = libp2p_peer_new();
			if (peer_to_ping == NULL) {
				free(str);
				goto exit;
			}
			peer_to_ping->addr_head = libp2p_utils_linked_list_new();
			if (peer_to_ping->addr_head == NULL) {
				libp2p_peer_free(peer_to_ping);
				peer_to_ping = NULL;
				free(str);
				goto exit;
			}
			struct MultiAddress* ma = multiaddress_new_from_string(str);
			if (ma == NULL) {
				libp2p_peer_free(peer_to_ping);
				peer_to_ping = NULL;
				free(str);
				goto exit;
			}
			peer_to_ping->addr_head->item = ma;
			peer_to_ping->id = str;
			peer_to_ping->id_size = strlen(str);
		}
	}

	if (peer_to_ping == NULL)
		goto exit;

	id = malloc(peer_to_ping->id_size + 1);
	if (id) {
		memcpy(id, peer_to_ping->id, peer_to_ping->id_size);
		id[peer_to_ping->id_size] = 0;
		fprintf (stderr, "PING %s.\n", id);
	}

	for (;;) {
		gettimeofday(&time1, NULL);
		if (!local_node.routing->Ping(local_node.routing, peer_to_ping)) {
			fprintf(stderr, "Unable to ping %s\n", id);
			goto exit;
		}
		gettimeofday(&time2, NULL);

		// calculate microseconds
		time_us  = (time2.tv_sec  - time1.tv_sec) * 1000000;
		time_us += (time2.tv_usec - time1.tv_usec);
		fprintf (stderr, "Pong received: time=%d.%03d ms\n", time_us / 1000, time_us % 1000);
		if (time_us < 1000000) { // if the ping took less than a second...
			sleep(1);
		}
	}

	retVal = 1;
	exit:
	if (id != NULL)
		free(id);
	if (fs_repo != NULL)
		ipfs_repo_fsrepo_free(fs_repo);
	if (local_node.peerstore != NULL)
		libp2p_peerstore_free(local_node.peerstore);
	if (local_node.providerstore != NULL)
		libp2p_providerstore_free(local_node.providerstore);
	return retVal;

}
