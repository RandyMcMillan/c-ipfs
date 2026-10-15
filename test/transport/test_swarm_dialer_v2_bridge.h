#include "../test_helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ipfs/transport/swarm_dialer_v2_bridge.h"
#include "ipfs/core/v2_listen.h"
#include "ipfs/repo/fsrepo/fs_repo.h"

struct v2_bridge_server_ctx {
    int listen_fd;
    int port;
    struct IpfsNode *local_node;
    int accepted;
};

static void *v2_bridge_server_thread(void *arg)
{
    struct v2_bridge_server_ctx *ctx = (struct v2_bridge_server_ctx *)arg;
    int client_fd = accept(ctx->listen_fd, NULL, NULL);
    if (client_fd < 0)
        return NULL;
    ctx->accepted = 1;
    ipfs_v2_listen_handler(client_fd, ctx->local_node);
    return NULL;
}

int test_swarm_connect_v2_bridge(void)
{
    signal(SIGPIPE, SIG_IGN);

    struct FSRepo *fs_repo = NULL;
    if (!drop_build_and_open_repo("./tmp/test-v2-bridge", &fs_repo))
        return 0;

    struct IpfsNode local_node;
    memset(&local_node, 0, sizeof(local_node));
    local_node.mode = MODE_ONLINE;
    local_node.identity = fs_repo->config->identity;
    local_node.repo = fs_repo;
    local_node.peerstore = NULL;

    int ok = 0;

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
        goto cleanup_repo;

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        goto cleanup_socket;

    socklen_t addrlen = sizeof(addr);
    if (getsockname(listen_fd, (struct sockaddr *)&addr, &addrlen) != 0)
        goto cleanup_socket;

    if (listen(listen_fd, 1) != 0)
        goto cleanup_socket;

    struct v2_bridge_server_ctx server_ctx = {
        .listen_fd = listen_fd,
        .port = ntohs(addr.sin_port),
        .local_node = &local_node,
        .accepted = 0,
    };

    pthread_t tid;
    if (pthread_create(&tid, NULL, v2_bridge_server_thread, &server_ctx) != 0)
        goto cleanup_socket;

    char multiaddr[64];
    snprintf(multiaddr, sizeof(multiaddr), "/ip4/127.0.0.1/tcp/%d", server_ctx.port);

    ipfs_v2_stream_bridge_t *bridge = NULL;
    int rc = ipfs_swarm_connect_v2_bridge(multiaddr,
                                          NULL,
                                          &fs_repo->config->identity->private_key,
                                          &bridge);

    if (rc == 0 && bridge != NULL) {
        char buf[4096];
        ssize_t total = 0;
        for (;;) {
            ssize_t n = ipfs_v2_stream_read(bridge, buf + total, sizeof(buf) - total - 1);
            if (n <= 0)
                break;
            total += n;
            if ((size_t)total >= sizeof(buf) - 1)
                break;
        }
        buf[total] = '\0';
        /* The Identify response contains the agent version string. */
        if (total > 0 && strstr(buf, "c-ipfs") != NULL)
            ok = 1;
    }

    if (bridge)
        ipfs_v2_stream_free(bridge);

    pthread_join(tid, NULL);
    /* Give the detached v2 session thread a moment to see the close. */
    usleep(100000);

cleanup_socket:
    close(listen_fd);
cleanup_repo:
    ipfs_repo_fsrepo_free(fs_repo);
    drop_repository("./tmp/test-v2-bridge");
    return ok;
}
