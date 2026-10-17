#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <resolv.h>

#ifndef T_TXT
#define T_TXT 16
#endif
#ifndef NS_MAXDNAME
#define NS_MAXDNAME 1025
#endif
#ifndef DNSDNS_MAX_RESPONSE
#define DNSDNS_MAX_RESPONSE 65535
#endif

/* Test-injectable DNS/network hooks. Production uses real res_query/getaddrinfo. */
static int (*dnsaddr_res_query)(const char *dname, int class, int type, unsigned char *answer, int anslen) = res_query;
static int (*dnsaddr_getaddrinfo)(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res) = getaddrinfo;
static void (*dnsaddr_freeaddrinfo)(struct addrinfo *res) = freeaddrinfo;

void ipfs_dnsaddr_resolver_set_res_query(int (*fn)(const char *dname, int class, int type, unsigned char *answer, int anslen)) {
	dnsaddr_res_query = fn ? fn : res_query;
}

void ipfs_dnsaddr_resolver_set_getaddrinfo(int (*fn)(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)) {
	dnsaddr_getaddrinfo = fn ? fn : getaddrinfo;
}

void ipfs_dnsaddr_resolver_set_freeaddrinfo(void (*fn)(struct addrinfo *res)) {
	dnsaddr_freeaddrinfo = fn ? fn : freeaddrinfo;
}

static int translate_dnsaddr(const char *input, int depth, char ***results, size_t *count, size_t *capacity);
static int materialize_dns_multiaddr(const char *value, char ***results, size_t *count, size_t *capacity);

static int append_multiaddr(char*** all, size_t* count, size_t* capacity, const char* value) {
    if (!all || !count || !capacity || !value || value[0] == '\0') return 0;

    if (*count == *capacity) {
        size_t new_capacity = (*capacity == 0) ? 16 : (*capacity * 2);
        char **next = realloc(*all, new_capacity * sizeof(char *));
        if (!next) return -1;
        *all = next;
        *capacity = new_capacity;
    }

    char *copy = strdup(value);
    if (!copy) return -1;
    (*all)[(*count)++] = copy;
    return 0;
}

static int parse_domain_name(const unsigned char *buf, size_t buf_len, size_t off, char *name_out, size_t name_len, size_t *consumed) {
    int n = dn_expand(buf, buf + buf_len, buf + off, name_out, (int)name_len);
    if (n < 0) return -1;
    *consumed = (size_t)n;
    return 0;
}

static int resolve_one_query(const char *domain, int depth, char ***results, size_t *count, size_t *capacity) {
    /* Remove caller prefixing and recursive handles. */
    const char *target = domain;
    if (strncmp(domain, "/dnsaddr/", 9) == 0) {
        target = domain + 9;
    }

    char host[512];
    size_t host_len = 0;
    while (target[host_len] != '\0' && target[host_len] != '/' && host_len < sizeof(host) - 1) {
        host[host_len] = target[host_len];
        host_len++;
    }
    host[host_len] = '\0';
    if (host[0] == '\0') return 0;

    char query_name[1024];
    if (snprintf(query_name, sizeof(query_name), "_dnsaddr.%s", host) >= (int)sizeof(query_name)) {
        return -1;
    }

    unsigned char *answer = calloc(1, DNSDNS_MAX_RESPONSE);
    if (!answer) return -1;

    int len = dnsaddr_res_query(query_name, 1, T_TXT, answer, DNSDNS_MAX_RESPONSE);
    if (len < 12) {
        free(answer);
        return -1;
    }

    int qdcount = (answer[4] << 8) | answer[5];
    int ancount = (answer[6] << 8) | answer[7];
    size_t off = 12;

    for (int i = 0; i < qdcount; i++) {
        size_t consumed = 0;
        char ignored[NS_MAXDNAME];
        if (parse_domain_name(answer, (size_t)len, off, ignored, sizeof(ignored), &consumed) != 0) {
            free(answer);
            return -1;
        }
        off += consumed + 4;
        if (off > (size_t)len) {
            free(answer);
            return -1;
        }
    }

    int status = 0;
    for (int i = 0; i < ancount; i++) {
        char ignored[NS_MAXDNAME];
        size_t consumed;
        if (parse_domain_name(answer, (size_t)len, off, ignored, sizeof(ignored), &consumed) != 0) {
            status = -1;
            break;
        }
        off += consumed;
        if (off + 10 > (size_t)len) {
            status = -1;
            break;
        }

        uint16_t type = (answer[off] << 8) | answer[off + 1];
        uint16_t rdlen = (answer[off + 8] << 8) | answer[off + 9];
        off += 10;
        if (rdlen > (uint16_t)((size_t)len - off)) {
            status = -1;
            break;
        }

        if (type == T_TXT) {
            size_t rdend = off + rdlen;
            size_t txt_off = off;
            while (txt_off < rdend) {
                uint8_t clen = answer[txt_off++];
                if (clen == 0) break;
                if (txt_off + clen > rdend) break;

                char *str = calloc(1, clen + 1);
                if (str) {
                    memcpy(str, &answer[txt_off], clen);
                    char *start = str;
                    if (clen >= 2 && str[0] == '"' && str[clen - 1] == '"') {
                        str[clen - 1] = '\0';
                        start = str + 1;
                    }

                    if (strncmp(start, "dnsaddr=", 8) == 0) {
                        const char *value = start + 8;
                        if (strncmp(value, "/dnsaddr/", 9) == 0 && depth < 8) {
                            if (translate_dnsaddr(value, depth + 1, results, count, capacity) != 0) {
                                /* Keep returning success as long as ordinary records decode. */
                            }
                        } else {
                            if (materialize_dns_multiaddr(value, results, count, capacity) != 0) {
                                status = -1;
                            }
                        }
                    }
                    free(str);
                }
                txt_off += clen;
            }
        }

        off += rdlen;
    }

    free(answer);
    return status;
}

static int materialize_dns_multiaddr(const char *value, char ***results, size_t *count, size_t *capacity) {
    if (strncmp(value, "/dns4/", 6) == 0) {
        const char *host_start = value + 6;
        const char *host_end = strchr(host_start, '/');
        char host[256];
        size_t host_len = (host_end) ? (size_t)(host_end - host_start) : strlen(host_start);
        if (host_len < sizeof(host)) {
            memcpy(host, host_start, host_len);
            host[host_len] = '\0';

            struct addrinfo hints, *res, *p;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            if (dnsaddr_getaddrinfo(host, NULL, &hints, &res) == 0) {
                const char *suffix = host_end ? host_end : "";
                int found = 0;
                for (p = res; p != NULL; p = p->ai_next) {
                    struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(ipv4->sin_addr), ip_str, INET_ADDRSTRLEN);
                    char buf[512];
                    snprintf(buf, sizeof(buf), "/ip4/%s%s", ip_str, suffix);
                    if (append_multiaddr(results, count, capacity, buf) != 0) break;
                    found = 1;
                }
                dnsaddr_freeaddrinfo(res);
                if (found) {
                    return 0;
                }
            }
        }
    }

    return append_multiaddr(results, count, capacity, value);
}

static int translate_dnsaddr(const char *input, int depth, char ***results, size_t *count, size_t *capacity) {
    if (!input) return -1;
    if (depth > 12) return -1;
    return resolve_one_query(input, depth, results, count, capacity);
}

int ipfs_resolve_dnsaddr(const char *domain, char ***out_resolved_multiaddrs, size_t *out_count) {
    if (!domain || !out_resolved_multiaddrs || !out_count) return -1;

    *out_resolved_multiaddrs = NULL;
    *out_count = 0;

    char **results = NULL;
    size_t count = 0;
    size_t capacity = 0;
    if (translate_dnsaddr(domain, 0, &results, &count, &capacity) != 0 || count == 0) {
        free(results);
        return -1;
    }

    *out_resolved_multiaddrs = results;
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
