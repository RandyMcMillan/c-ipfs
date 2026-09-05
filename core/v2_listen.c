#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libp2p/conn/multistream.h"
#include "libp2p/conn/noise.h"
#include "libp2p/conn/yamux.h"
#include "libp2p/identify/identify_v2.h"
#include "libp2p/identify/identify_v2_ipfs.h"
#include "libp2p/net/tcp.h"
#include "libp2p/peer/peerstore.h"
#include "libp2p/utils/logger.h"
#include "ipfs/core/v2_listen.h"
#include "ipfs/core/ipfs_node.h"

int ipfs_v2_listen_handler(int fd, struct IpfsNode* local_node) {
    if (fd < 0 || !local_node) {
        if (fd >= 0) close(fd);
        return 0;
    }

    struct Libp2pV2Stream* tcp_stream = libp2p_net_tcp_wrap_fd(fd);
    if (!tcp_stream) {
        close(fd);
        return 0;
    }

    libp2p_logger_info("v2_listen", "New connection on fd %d\n", fd);

    /* 1. Multistream: accept /noise */
    if (!libp2p_net_multistream_accept(tcp_stream, "/noise")) {
        libp2p_logger_error("v2_listen", "Multistream accept failed on fd %d\n", fd);
        tcp_stream->close(tcp_stream);
        return 0;
    }
    libp2p_logger_info("v2_listen", "Multistream accepted /noise on fd %d\n", fd);

    /* 2. Noise responder handshake */
    noise_identity_callbacks_t noise_cbs;
    libp2p_noise_identity_callbacks_init(&noise_cbs);

    struct Libp2pPeer remote_peer;
    memset(&remote_peer, 0, sizeof(remote_peer));

    struct Libp2pV2Stream* noise_stream = libp2p_noise_handshake_responder_raw(
        tcp_stream, &local_node->identity->private_key, &remote_peer, &noise_cbs);
    if (!noise_stream) {
        libp2p_logger_error("v2_listen", "Noise responder handshake failed on fd %d\n", fd);
        tcp_stream->close(tcp_stream);
        return 0;
    }
    libp2p_logger_info("v2_listen", "Noise handshake completed on fd %d\n", fd);

    /* 3. Yamux server session */
    struct YamuxSession* yamux_sess = libp2p_yamux_session_new(noise_stream, 1);
    if (!yamux_sess) {
        libp2p_logger_error("v2_listen", "Yamux session creation failed on fd %d\n", fd);
        noise_stream->close(noise_stream);
        return 0;
    }

    /* 4. Accept incoming Identify stream (client opens odd stream ID) */
    struct Libp2pV2Stream* id_stream = libp2p_yamux_stream_accept(yamux_sess);
    if (!id_stream) {
        libp2p_logger_error("v2_listen", "Yamux stream accept failed on fd %d\n", fd);
        libp2p_yamux_session_free(yamux_sess);
        return 0;
    }
    libp2p_logger_info("v2_listen", "Yamux identify stream accepted on fd %d\n", fd);

    /* 5. Exchange Identify */
    libp2p_identify_receive(id_stream, &remote_peer);

    /* Gather listen addresses from config */
    char** listen_addrs = NULL;
    size_t listen_count = 0;
    if (local_node->repo && local_node->repo->config &&
        local_node->repo->config->addresses &&
        local_node->repo->config->addresses->swarm_head) {
        struct Libp2pLinkedList* cur = local_node->repo->config->addresses->swarm_head;
        while (cur) { listen_count++; cur = cur->next; }
        listen_addrs = (char**)calloc(listen_count, sizeof(char*));
        size_t idx = 0;
        cur = local_node->repo->config->addresses->swarm_head;
        while (cur && idx < listen_count) {
            if (cur->item) listen_addrs[idx++] = (char*)cur->item;
            cur = cur->next;
        }
        listen_count = idx;
    }

    struct RsaPrivateKey* rsa = &local_node->identity->private_key;
    libp2p_identify_send_response_ipfs(
        id_stream,
        local_node->identity->peer ? local_node->identity->peer->id : NULL,
        (const unsigned char*)rsa->public_key_der,
        rsa->public_key_length,
        listen_addrs,
        listen_count);

    free(listen_addrs);

    /* Register peer in peerstore so swarm peers can report it */
    if (remote_peer.id != NULL && remote_peer.id_size > 0 && local_node->peerstore != NULL) {
        remote_peer.connection_type = CONNECTION_TYPE_CONNECTED;
        libp2p_peerstore_add_peer(local_node->peerstore, &remote_peer);
        libp2p_logger_info("v2_listen", "Registered peer %s in peerstore from fd %d\n", remote_peer.id, fd);
    }

    id_stream->close(id_stream);
    libp2p_yamux_session_free(yamux_sess);

    if (remote_peer.id != NULL)
        free(remote_peer.id);

    libp2p_logger_info("v2_listen", "v2 handler completed for fd %d\n", fd);
    return 1;
}
