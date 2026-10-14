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

static int translate_dnsaddr(const char *input, int depth, char ***results, size_t *count, size_t *capacity);

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

    int len = res_query(query_name, 1, T_TXT, answer, DNSDNS_MAX_RESPONSE);
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
                            if (append_multiaddr(results, count, capacity, value) != 0) {
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
