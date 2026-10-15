#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

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

struct v2_listen_thread_ctx {
    struct YamuxSession *yamux_session;
    struct Libp2pPeer remote_peer;
    struct IpfsNode *local_node;
};

static int v2_listen_read_protocol(struct Libp2pV2Stream *stream, char *protocol, size_t maxlen)
{
    if (!stream || !protocol || maxlen == 0)
        return 0;

    char msg[256];

    /* Read and echo /multistream/1.0.0 header (preserve trailing newline). */
    if (!libp2p_net_multistream_read_protocol(stream, msg, sizeof(msg)))
        return 0;
    if (strcmp(msg, "/multistream/1.0.0\n") != 0)
        return 0;
    if (!libp2p_net_multistream_write_protocol(stream, "/multistream/1.0.0\n"))
        return 0;

    /* Read protocol proposal. */
    if (!libp2p_net_multistream_read_protocol(stream, msg, sizeof(msg)))
        return 0;

    size_t len = strlen(msg);
    if (len > 0 && msg[len - 1] == '\n')
        msg[len - 1] = '\0';

    /* Confirm the protocol so the initiator can proceed. */
    char confirm[256];
    snprintf(confirm, sizeof(confirm), "%s\n", msg);
    if (!libp2p_net_multistream_write_protocol(stream, confirm))
        return 0;

    strncpy(protocol, msg, maxlen - 1);
    protocol[maxlen - 1] = '\0';
    return 1;
}

static void v2_listen_send_identify(struct Libp2pV2Stream *stream, struct IpfsNode *local_node)
{
    char **listen_addrs = NULL;
    size_t listen_count = 0;
    if (local_node->repo && local_node->repo->config &&
        local_node->repo->config->addresses &&
        local_node->repo->config->addresses->swarm_head) {
        struct Libp2pLinkedList *cur = local_node->repo->config->addresses->swarm_head;
        while (cur) { listen_count++; cur = cur->next; }
        listen_addrs = (char **)calloc(listen_count, sizeof(char *));
        if (listen_addrs) {
            size_t idx = 0;
            cur = local_node->repo->config->addresses->swarm_head;
            while (cur && idx < listen_count) {
                if (cur->item)
                    listen_addrs[idx++] = (char *)cur->item;
                cur = cur->next;
            }
            listen_count = idx;
        }
    }

    struct RsaPrivateKey *rsa = &local_node->identity->private_key;
    libp2p_identify_send_response_ipfs(
        stream,
        local_node->identity->peer ? local_node->identity->peer->id : NULL,
        (const unsigned char *)rsa->public_key_der,
        rsa->public_key_length,
        listen_addrs,
        listen_count);

    free(listen_addrs);
}

static void *ipfs_v2_listen_session_thread(void *arg)
{
    struct v2_listen_thread_ctx *ctx = (struct v2_listen_thread_ctx *)arg;
    struct YamuxSession *session = ctx->yamux_session;
    struct IpfsNode *local_node = ctx->local_node;
    struct Libp2pPeer remote_peer = ctx->remote_peer;
    free(ctx);

    for (;;) {
        struct Libp2pV2Stream *stream = libp2p_yamux_stream_accept(session);
        if (!stream)
            break;

        char protocol[128];
        if (!v2_listen_read_protocol(stream, protocol, sizeof(protocol))) {
            libp2p_logger_error("v2_listen", "Failed to read protocol on new stream\n");
            stream->close(stream);
            continue;
        }

        if (strcmp(protocol, "/ipfs/id/1.0.0") == 0) {
            /* go-libp2p identify is a push protocol: the initiator opens a
             * stream and the responder sends its Identify info immediately. */
            v2_listen_send_identify(stream, local_node);
        } else {
            libp2p_logger_info("v2_listen", "No handler for protocol %s; closing stream\n", protocol);
        }

        stream->close(stream);
    }

    libp2p_yamux_session_free(session);
    if (remote_peer.id != NULL)
        free(remote_peer.id);

    libp2p_logger_info("v2_listen", "v2 session handler exiting\n");
    return NULL;
}

int ipfs_v2_listen_handler(int fd, struct IpfsNode *local_node)
{
    if (fd < 0 || !local_node) {
        if (fd >= 0)
            close(fd);
        return 0;
    }

    struct Libp2pV2Stream *tcp_stream = libp2p_net_tcp_wrap_fd(fd);
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

    struct Libp2pV2Stream *noise_stream = libp2p_noise_handshake_responder_raw(
        tcp_stream, &local_node->identity->private_key, &remote_peer, &noise_cbs);
    if (!noise_stream) {
        libp2p_logger_error("v2_listen", "Noise responder handshake failed on fd %d\n", fd);
        tcp_stream->close(tcp_stream);
        return 0;
    }
    libp2p_logger_info("v2_listen", "Noise handshake completed on fd %d\n", fd);

    /* 3. Yamux server session */
    struct YamuxSession *yamux_sess = libp2p_yamux_session_new(noise_stream, 1);
    if (!yamux_sess) {
        libp2p_logger_error("v2_listen", "Yamux session creation failed on fd %d\n", fd);
        noise_stream->close(noise_stream);
        return 0;
    }

    /* 4. Hand the session off to a background thread so the main listen loop
     *    can accept more connections. The thread keeps the Yamux session alive
     *    and dispatches incoming streams. */
    struct v2_listen_thread_ctx *ctx = (struct v2_listen_thread_ctx *)malloc(sizeof(*ctx));
    if (!ctx) {
        libp2p_yamux_session_free(yamux_sess);
        return 0;
    }

    ctx->yamux_session = yamux_sess;
    ctx->remote_peer = remote_peer;
    ctx->local_node = local_node;

    pthread_t tid;
    if (pthread_create(&tid, NULL, ipfs_v2_listen_session_thread, ctx) != 0) {
        free(ctx);
        libp2p_yamux_session_free(yamux_sess);
        return 0;
    }
    pthread_detach(tid);

    return 1;
}
