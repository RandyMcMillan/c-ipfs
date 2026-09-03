#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs/core/client_api.h"

typedef struct {
    char peer_id[64];
    char multiaddr[256];
} peer_info_t;

int http_write_dht_findpeer_response(int fd, const peer_info_t *peers, size_t peer_count) {
    char json_buf[4096];
    size_t offset = snprintf(json_buf, sizeof(json_buf), "{\"Responses\":[");

    for (size_t i = 0; i < peer_count; i++) {
        offset += snprintf(json_buf + offset, sizeof(json_buf) - offset,
                           "%s{\"ID\":\"%s\",\"Addrs\":[\"%s\"]}",
                           (i > 0) ? "," : "",
                           peers[i].peer_id,
                           peers[i].multiaddr);
    }
    snprintf(json_buf + offset, sizeof(json_buf) - offset, "]}");

    dprintf(fd,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n\r\n%s",
            strlen(json_buf), json_buf);

    return 1;
}
