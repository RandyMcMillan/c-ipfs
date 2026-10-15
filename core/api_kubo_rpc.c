#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>

#include "ipfs/core/api_kubo_rpc.h"
#include "ipfs/core/api.h"
#include "ipfs/core/http_request.h"
#include "libp2p/utils/urlencode.h"
#include "libp2p/utils/logger.h"

#define DEFAULT_HTTP_RPC_PORT 5011
#define HTTP_RESP_404 "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"

static volatile int http_rpc_shutting_down = 0;
static int http_rpc_server_fd = -1;

static int send_json_response(int client_fd, int status_code, const char* content_type,
                              const uint8_t* body, size_t body_len)
{
    char header[512];
    const char* status_text = (status_code == 200) ? "200 OK" :
                              (status_code == 400) ? "400 Bad Request" :
                              (status_code == 500) ? "500 Internal Server Error" : "404 Not Found";
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        status_text, content_type, body_len);
    if (header_len <= 0 || (size_t)header_len >= sizeof(header))
        return 0;
    if (write(client_fd, header, header_len) != header_len)
        return 0;
    if (body_len > 0 && write(client_fd, body, body_len) != (ssize_t)body_len)
        return 0;
    return 1;
}

static void http_request_from_query(struct HttpRequest* request, const char* query)
{
    if (!query || !request)
        return;
    char* q = strdup(query);
    if (!q)
        return;
    char* p = q;
    while (*p) {
        char* amp = strchr(p, '&');
        if (amp)
            *amp++ = '\0';
        char* eq = strchr(p, '=');
        char* name = p;
        char* value = NULL;
        if (eq) {
            *eq++ = '\0';
            value = libp2p_utils_url_decode(eq);
        }
        if (strcmp(name, "arg") == 0 && value) {
            libp2p_utils_vector_add(request->arguments, value);
        } else {
            struct HttpParam* hp = ipfs_core_http_param_new();
            if (hp) {
                hp->name = strdup(name);
                hp->value = value ? value : strdup("");
                libp2p_utils_vector_add(request->params, hp);
            } else if (value) {
                free(value);
            }
        }
        p = amp ? amp : p + strlen(p);
    }
    free(q);
}

static void handle_http_client(int client_fd, struct IpfsNode* local_node) {
    char buffer[8192];
    ssize_t bytes = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes <= 0) {
        close(client_fd);
        return;
    }
    buffer[bytes] = '\0';

    char* line_end = strstr(buffer, "\r\n");
    if (!line_end) {
        write(client_fd, HTTP_RESP_404, strlen(HTTP_RESP_404));
        close(client_fd);
        return;
    }
    *line_end = '\0';

    char method[16] = "";
    char path[4096] = "";
    char version[16] = "";
    if (sscanf(buffer, "%15s %4095s %15s", method, path, version) != 3) {
        write(client_fd, HTTP_RESP_404, strlen(HTTP_RESP_404));
        close(client_fd);
        return;
    }

    if (strncmp(path, "/api/v0/", 8) != 0) {
        write(client_fd, HTTP_RESP_404, strlen(HTTP_RESP_404));
        close(client_fd);
        return;
    }

    char* query = strchr(path, '?');
    if (query)
        *query++ = '\0';

    const char* cmd_start = path + 8; /* after /api/v0/ */
    char* cmd_dup = strdup(cmd_start);
    if (!cmd_dup) {
        write(client_fd, HTTP_RESP_404, strlen(HTTP_RESP_404));
        close(client_fd);
        return;
    }

    struct HttpRequest* request = ipfs_core_http_request_new();
    if (!request) {
        free(cmd_dup);
        write(client_fd, HTTP_RESP_404, strlen(HTTP_RESP_404));
        close(client_fd);
        return;
    }

    request->command = cmd_dup;
    char* slash = strchr(cmd_dup, '/');
    if (slash) {
        *slash++ = '\0';
        request->sub_command = slash;
    }

    http_request_from_query(request, query);

    /* For POST, attach the body as request data. */
    if (strcmp(method, "POST") == 0) {
        char* body = strstr(line_end + 2, "\r\n\r\n");
        if (body) {
            body += 4;
            size_t body_len = bytes - (body - buffer);
            if (body_len > 0) {
                request->data = (uint8_t*)malloc(body_len);
                if (request->data) {
                    memcpy(request->data, body, body_len);
                    request->data_size = body_len;
                }
            }
        }
    }

    struct HttpResponse* response = NULL;
    if (!ipfs_core_http_request_process(local_node, request, &response) || !response) {
        const char* err_body = "{\"Message\":\"Internal error\",\"Code\":0,\"Type\":\"error\"}";
        send_json_response(client_fd, 500, "application/json",
                           (const uint8_t*)err_body, strlen(err_body));
    } else {
        send_json_response(client_fd, 200,
                           response->content_type ? response->content_type : "application/json",
                           response->bytes, response->bytes_size);
        ipfs_core_http_response_free(response);
    }

    ipfs_core_http_request_free(request);
    close(client_fd);
}

int ipfs_start_http_rpc_server(struct IpfsNode* local_node, uint16_t port) {
    if (!local_node)
        return -1;

    http_rpc_shutting_down = 0;

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
            handle_http_client(client_fd, local_node);
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
