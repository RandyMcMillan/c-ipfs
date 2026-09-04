#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "ipfs/transport/dnsaddr_resolver.h"

#define DEFAULT_BOOTSTRAP_DNS "bootstrap.libp2p.io"
#define DEFAULT_BOOTSTRAP_PORT 4001

int ipfs_resolve_dnsaddr(const char *domain, char ***out_resolved_multiaddrs, size_t *out_count) {
    if (!domain || !out_resolved_multiaddrs || !out_count) return -1;

    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; /* IPv4 targets */
    hints.ai_socktype = SOCK_STREAM;

    const char *target_domain = domain;
    if (strncmp(domain, "/dnsaddr/", 9) == 0) {
        target_domain = domain + 9;
    }

    int status = getaddrinfo(target_domain, NULL, &hints, &res);
    if (status != 0) {
        fprintf(stderr, "[DNS Resolver] getaddrinfo error for %s: %s\n", target_domain, gai_strerror(status));
        return -1;
    }

    /* Count records */
    size_t count = 0;
    for (p = res; p != NULL; p = p->ai_next) count++;

    if (count == 0) {
        freeaddrinfo(res);
        return -1;
    }

    char **addrs = malloc(count * sizeof(char *));
    size_t idx = 0;

    for (p = res; p != NULL; p = p->ai_next) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(ipv4->sin_addr), ip_str, INET_ADDRSTRLEN);

        char buf[128];
        snprintf(buf, sizeof(buf), "/ip4/%s/tcp/%d", ip_str, DEFAULT_BOOTSTRAP_PORT);
        addrs[idx++] = strdup(buf);
    }

    freeaddrinfo(res);
    *out_resolved_multiaddrs = addrs;
    *out_count = count;

    return 0;
}

void ipfs_free_resolved_multiaddrs(char **multiaddrs, size_t count) {
    if (!multiaddrs) return;
    for (size_t i = 0; i < count; i++) {
        free(multiaddrs[i]);
    }
    free(multiaddrs);
}
