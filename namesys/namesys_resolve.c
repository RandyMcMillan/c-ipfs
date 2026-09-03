#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "ipfs/namesys/namesys.h"

/* Stub implementations for external cache/DHT APIs */
int ipns_cache_get(const char *name, char *out_val) {
    (void)name; (void)out_val;
    return -1;
}
int ipns_cache_set(const char *name, const char *val, uint64_t ttl) {
    (void)name; (void)val; (void)ttl;
    return -1;
}
int dht_get_value(void *dht, const uint8_t *key, size_t key_len, uint8_t **out_val, size_t *out_len) {
    (void)dht; (void)key; (void)key_len; (void)out_val; (void)out_len;
    return -1;
}

int namesys_resolve_ipns_network(void *dht, const char *ipns_id, char *out_resolved_path, size_t max_len) {
    (void)dht;
    if (!ipns_id || !out_resolved_path) return 0;

    if (ipns_cache_get(ipns_id, out_resolved_path) == 0) {
        return 1;
    }

    char dht_key[512];
    snprintf(dht_key, sizeof(dht_key), "/ipns/%s", ipns_id);

    uint8_t *raw_record = NULL;
    size_t record_len = 0;
    if (dht_get_value(dht, (uint8_t *)dht_key, strlen(dht_key), &raw_record, &record_len) != 0) {
        fprintf(stderr, "[namesys] Failed to resolve IPNS key over DHT: %s\n", ipns_id);
        return 0;
    }

    strncpy(out_resolved_path, (char *)raw_record, max_len - 1);
    out_resolved_path[max_len - 1] = '\0';
    free(raw_record);

    ipns_cache_set(ipns_id, out_resolved_path, 3600);
    return 1;
}

int ipfs_path_resolve(void *dht, const char *path, char *out_cid) {
    if (!path) return 0;

    if (strncmp(path, "/ipfs/", 6) == 0) {
        strncpy(out_cid, path + 6, 64);
        out_cid[63] = '\0';
        return 1;
    } else if (strncmp(path, "/ipns/", 6) == 0) {
        char resolved[512];
        if (namesys_resolve_ipns_network(dht, path + 6, resolved, sizeof(resolved)) == 1) {
            return ipfs_path_resolve(dht, resolved, out_cid);
        }
    }

    return 0;
}
