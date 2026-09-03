#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "ipfs/blocks/blockstore.h"

static int blockstore_get_shard_path(const char *root, const char *cid_str, char *out_path, size_t max_len) {
    if (strlen(cid_str) < 2) return 0;

    char prefix[3] = { cid_str[0], cid_str[1], '\0' };
    snprintf(out_path, max_len, "%s/%s", root, prefix);
    mkdir(out_path, 0755);

    snprintf(out_path, max_len, "%s/%s/%s.data", root, prefix, cid_str);
    return 1;
}

int blockstore_put(const char *root, const char *cid_str, const uint8_t *data, size_t len) {
    char path[512];
    if (!blockstore_get_shard_path(root, cid_str, path, sizeof(path))) return 0;

    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(data, 1, len, f);
    fclose(f);
    return 1;
}

int blockstore_get(const char *root, const char *cid_str, uint8_t **out_data, size_t *out_len) {
    char path[512];
    if (!blockstore_get_shard_path(root, cid_str, path, sizeof(path))) return 0;

    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    *out_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    *out_data = malloc(*out_len);
    if (!*out_data) { fclose(f); return 0; }
    fread(*out_data, 1, *out_len, f);
    fclose(f);
    return 1;
}
