#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "ipfs/dnslink/dnslink.h"

#define DNS_CACHE_SIZE 256
#define DEFAULT_TTL_SEC 300

typedef struct dns_cache_entry {
    char domain[256];
    char resolved_path[512];
    time_t expires_at;
    struct dns_cache_entry *next;
} dns_cache_entry_t;

typedef struct {
    dns_cache_entry_t *buckets[DNS_CACHE_SIZE];
    pthread_mutex_t lock;
} dns_cache_t;

static dns_cache_t g_dns_cache = { .buckets = {0}, .lock = PTHREAD_MUTEX_INITIALIZER };

static unsigned int hash_domain(const char *str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash % DNS_CACHE_SIZE;
}

int dns_cache_get(const char *domain, char *out_path, size_t max_len) {
    pthread_mutex_lock(&g_dns_cache.lock);
    unsigned int idx = hash_domain(domain);
    dns_cache_entry_t *curr = g_dns_cache.buckets[idx];
    time_t now = time(NULL);

    while (curr) {
        if (strcmp(curr->domain, domain) == 0) {
            if (curr->expires_at > now) {
                strncpy(out_path, curr->resolved_path, max_len - 1);
                out_path[max_len - 1] = '\0';
                pthread_mutex_unlock(&g_dns_cache.lock);
                return 1;
            }
            break;
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&g_dns_cache.lock);
    return 0;
}

void dns_cache_put(const char *domain, const char *resolved_path, uint32_t ttl_sec) {
    pthread_mutex_lock(&g_dns_cache.lock);
    unsigned int idx = hash_domain(domain);
    dns_cache_entry_t *curr = g_dns_cache.buckets[idx];

    while (curr) {
        if (strcmp(curr->domain, domain) == 0) {
            strncpy(curr->resolved_path, resolved_path, sizeof(curr->resolved_path) - 1);
            curr->expires_at = time(NULL) + (ttl_sec ? ttl_sec : DEFAULT_TTL_SEC);
            pthread_mutex_unlock(&g_dns_cache.lock);
            return;
        }
        curr = curr->next;
    }

    dns_cache_entry_t *new_entry = calloc(1, sizeof(dns_cache_entry_t));
    strncpy(new_entry->domain, domain, sizeof(new_entry->domain) - 1);
    strncpy(new_entry->resolved_path, resolved_path, sizeof(new_entry->resolved_path) - 1);
    new_entry->expires_at = time(NULL) + (ttl_sec ? ttl_sec : DEFAULT_TTL_SEC);
    new_entry->next = g_dns_cache.buckets[idx];
    g_dns_cache.buckets[idx] = new_entry;

    pthread_mutex_unlock(&g_dns_cache.lock);
}
