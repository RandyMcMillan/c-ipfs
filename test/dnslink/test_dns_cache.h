#ifndef TEST_DNS_CACHE_H
#define TEST_DNS_CACHE_H

#include <string.h>
#include <stdlib.h>

/* Prototypes from dnslink/dns_cache.c */
int dns_cache_get(const char *domain, char *out_path, size_t max_len);
void dns_cache_put(const char *domain, const char *resolved_path, uint32_t ttl_sec);

int test_dns_cache_put_get(void) {
    dns_cache_put("example.com", "/ipfs/QmTest", 300);
    char out[256];
    int ret = dns_cache_get("example.com", out, sizeof(out));
    return ret && strcmp(out, "/ipfs/QmTest") == 0;
}

int test_dns_cache_miss(void) {
    char out[256];
    return !dns_cache_get("nonexistent.example.com", out, sizeof(out));
}

#endif
