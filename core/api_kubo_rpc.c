#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>

#include "ipfs/core/api_kubo_rpc.h"

#define DEFAULT_HTTP_RPC_PORT 5011
#define HTTP_RESP_404 "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"

static volatile int http_rpc_shutting_down = 0;
static int http_rpc_server_fd = -1;

static void handle_http_client(int client_fd) {
    char buffer[2048];
    ssize_t bytes = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes <= 0) {
        close(client_fd);
        return;
    }
    buffer[bytes] = '\0';

    const char *json_body = NULL;
    const char *content_type = "application/json";
    char local_json_buf[512];

    if (strstr(buffer, "GET /api/v0/version") || strstr(buffer, "POST /api/v0/version")) {
        snprintf(local_json_buf, sizeof(local_json_buf),
            "{\"Version\":\"0.24.0-cipfs\",\"Commit\":\"c-ipfs-native\",\"System\":\"c-ipfs/v2\"}");
        json_body = local_json_buf;
    } else if (strstr(buffer, "GET /api/v0/id") || strstr(buffer, "POST /api/v0/id")) {
        snprintf(local_json_buf, sizeof(local_json_buf),
            "{\"ID\":\"12D3KooWC-c-ipfs-node-identity\",\"AgentVersion\":\"c-ipfs/0.1.0\",\"Addresses\":[\"/ip4/127.0.0.1/tcp/4001\"]}");
        json_body = local_json_buf;
    }

    if (json_body) {
        char response_header[512];
        int header_len = snprintf(response_header, sizeof(response_header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n\r\n",
            content_type, strlen(json_body));

        write(client_fd, response_header, header_len);
        write(client_fd, json_body, strlen(json_body));
    } else {
        write(client_fd, HTTP_RESP_404, strlen(HTTP_RESP_404));
    }

    close(client_fd);
}

int ipfs_start_http_rpc_server(uint16_t port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return -1;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port ? port : DEFAULT_HTTP_RPC_PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 16) < 0) {
        close(server_fd);
        return -1;
    }

    printf("[HTTP RPC] Listening for Kubo API requests on port %d\n", port ? port : DEFAULT_HTTP_RPC_PORT);

    http_rpc_server_fd = server_fd;

    while (!http_rpc_shutting_down) {
        struct pollfd pfd = { .fd = server_fd, .events = POLLIN };
        int ready = poll(&pfd, 1, 1000); // 1s timeout to check shutdown flag
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0) continue;

        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd >= 0) {
            handle_http_client(client_fd);
        }
    }

    close(server_fd);
    http_rpc_server_fd = -1;
    return 0;
}

void ipfs_stop_http_rpc_server(void) {
    http_rpc_shutting_down = 1;
    if (http_rpc_server_fd >= 0) {
        close(http_rpc_server_fd);
        http_rpc_server_fd = -1;
    }
}
