#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs/core/ipfs_node.h"

typedef struct peerstore peerstore_t;

/* Stub implementations for external peerstore APIs */
int peerstore_add_address(peerstore_t *ps, const char *peer_id, const char *multiaddr) {
    (void)ps; (void)peer_id; (void)multiaddr;
    return 0;
}
int libp2p_dial_peer(peerstore_t *ps, const char *peer_id) {
    (void)ps; (void)peer_id;
    return 0;
}

int bootstrap_connect_peer(peerstore_t *ps, const char *peer_id, const char *multiaddr) {
    if (!ps || !peer_id || !multiaddr) return 0;

    if (peerstore_add_address(ps, peer_id, multiaddr) != 0) {
        fprintf(stderr, "[bootstrap] Failed to store address for bootstrap peer: %s\n", peer_id);
        return 0;
    }

    printf("[bootstrap] Connecting to bootstrap peer: %s (%s)\n", peer_id, multiaddr);
    int res = libp2p_dial_peer(ps, peer_id);
    if (res != 0) {
        fprintf(stderr, "[bootstrap] Connection failed to peer %s\n", peer_id);
        return 0;
    }

    printf("[bootstrap] Connected to bootstrap peer %s successfully.\n", peer_id);
    return 1;
}
