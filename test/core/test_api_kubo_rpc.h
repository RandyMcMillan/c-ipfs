#include "../test_helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ipfs/core/api_kubo_rpc.h"
#include "ipfs/core/ipfs_node.h"

struct rpc_test_ctx {
    struct IpfsNode* node;
    int port;
};

static void* rpc_server_thread(void* arg)
{
    struct rpc_test_ctx* ctx = (struct rpc_test_ctx*)arg;
    ipfs_start_http_rpc_server(ctx->node, ctx->port);
    return NULL;
}

static int rpc_request(int port, const char* req, char* out, size_t out_len)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return 0;
    }
    if (write(fd, req, strlen(req)) != (ssize_t)strlen(req)) {
        close(fd);
        return 0;
    }
    shutdown(fd, SHUT_WR);
    size_t total = 0;
    for (;;) {
        if (total >= out_len - 1) break;
        ssize_t n = read(fd, out + total, out_len - 1 - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    out[total] = '\0';
    close(fd);
    return 1;
}

int test_api_kubo_rpc_id(void)
{
    const char* ipfs_path = "./tmp/test_api_kubo_rpc";
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", ipfs_path);
        system(cmd);
    }

    char* peer_id = NULL;
    if (!drop_and_build_repository_without_bootstrap(ipfs_path, 0, &peer_id)) {
        return 0;
    }

    struct IpfsNode* local_node = NULL;
    if (!ipfs_node_offline_new(ipfs_path, &local_node)) {
        if (peer_id) free(peer_id);
        return 0;
    }
    if (peer_id) free(peer_id);

    int port = 18011;
    struct rpc_test_ctx ctx = { local_node, port };
    pthread_t tid;
    if (pthread_create(&tid, NULL, rpc_server_thread, &ctx) != 0) {
        ipfs_node_free(local_node);
        return 0;
    }

    /* Give the server a moment to bind. */
    usleep(500000);

    char buf[4096];
    int ok = 0;
    if (rpc_request(port, "GET /api/v0/id HTTP/1.1\r\nHost: localhost\r\n\r\n", buf, sizeof(buf))) {
        if (strstr(buf, "HTTP/1.1 200 OK") && strstr(buf, "ID") && local_node->identity->peer->id) {
            if (strstr(buf, local_node->identity->peer->id)) {
                ok = 1;
            }
        }
    }

    ipfs_stop_http_rpc_server();
    pthread_join(tid, NULL);
    ipfs_node_free(local_node);
    return ok;
}

int test_api_kubo_rpc_version(void)
{
    const char* ipfs_path = "./tmp/test_api_kubo_rpc_version";
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", ipfs_path);
        system(cmd);
    }

    char* peer_id = NULL;
    if (!drop_and_build_repository_without_bootstrap(ipfs_path, 0, &peer_id)) {
        return 0;
    }
    if (peer_id) free(peer_id);

    struct IpfsNode* local_node = NULL;
    if (!ipfs_node_offline_new(ipfs_path, &local_node)) {
        return 0;
    }

    int port = 18012;
    struct rpc_test_ctx ctx = { local_node, port };
    pthread_t tid;
    if (pthread_create(&tid, NULL, rpc_server_thread, &ctx) != 0) {
        ipfs_node_free(local_node);
        return 0;
    }

    usleep(500000);

    char buf[4096];
    int ok = 0;
    if (rpc_request(port, "GET /api/v0/version HTTP/1.1\r\nHost: localhost\r\n\r\n", buf, sizeof(buf))) {
        ok = strstr(buf, "HTTP/1.1 200 OK") && strstr(buf, "Version");
    }

    ipfs_stop_http_rpc_server();
    pthread_join(tid, NULL);
    ipfs_node_free(local_node);
    return ok;
}
