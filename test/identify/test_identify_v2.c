#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "libp2p/identify/identify_v2.h"
#include "libp2p/net/tcp.h"
#include "libp2p/peer/peerstore.h"

int test_identify_v2_roundtrip(void)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
        return 0;

    struct Libp2pV2Stream* sender = libp2p_net_tcp_wrap_fd(sv[0]);
    struct Libp2pV2Stream* receiver = libp2p_net_tcp_wrap_fd(sv[1]);
    if (sender == NULL || receiver == NULL) {
        close(sv[0]);
        close(sv[1]);
        return 0;
    }

    struct Libp2pV2Peerstore* ps = libp2p_v2_peerstore_new("12D3KooWTestPeerId");
    if (ps == NULL) {
        sender->close(sender);
        receiver->close(receiver);
        return 0;
    }

    unsigned char dummy_key[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 };
    ps->public_key = (unsigned char*)malloc(sizeof(dummy_key));
    if (ps->public_key != NULL) {
        memcpy(ps->public_key, dummy_key, sizeof(dummy_key));
        ps->public_key_len = sizeof(dummy_key);
    }

    ps->listen_addrs = (char**)calloc(2, sizeof(char*));
    if (ps->listen_addrs != NULL) {
        ps->listen_addrs[0] = strdup("/ip4/127.0.0.1/tcp/4001");
        ps->listen_addrs[1] = strdup("/ip4/192.168.1.1/tcp/4002");
        ps->listen_addrs_count = 2;
    }

    int ok = 0;
    if (libp2p_identify_send_response(sender, ps)) {
        /* Close sender so receiver sees EOF after the identify message. */
        sender->close(sender);
        sender = NULL;
        struct Libp2pPeer peer;
        memset(&peer, 0, sizeof(peer));
        if (libp2p_identify_receive(receiver, &peer)) {
            if (peer.peer_id != NULL && strlen(peer.peer_id) > 0 &&
                peer.addr_head != NULL) {
                ok = 1;
            }
        }
        if (peer.peer_id != NULL)
            free(peer.peer_id);
        struct Libp2pLinkedList* cur = peer.addr_head;
        while (cur != NULL) {
            struct Libp2pLinkedList* next = cur->next;
            if (cur->item != NULL)
                libp2p_multiaddr_free((struct MultiAddress*)cur->item);
            free(cur);
            cur = next;
        }
    }

    libp2p_v2_peerstore_free(ps);
    if (sender != NULL)
        sender->close(sender);
    receiver->close(receiver);
    return ok;
}
