#ifndef TEST_BLOCKSTORE_SHARD_H
#define TEST_BLOCKSTORE_SHARD_H

#include <string.h>
#include <stdlib.h>

/* Prototypes from blocks/blockstore_shard.c */
int blockstore_put(const char *root, const char *cid_str, const uint8_t *data, size_t len);
int blockstore_get(const char *root, const char *cid_str, uint8_t **out_data, size_t *out_len);

int test_blockstore_shard_put_get(void) {
    char tmp[] = "/tmp/test_bs_shard_XXXXXX";
    if (!mkdtemp(tmp)) return 0;
    const char *cid = "Qm1234567890abcdef";
    const uint8_t data[] = "hello shard";
    int ret = blockstore_put(tmp, cid, data, sizeof(data));
    if (!ret) { rmdir(tmp); return 0; }

    uint8_t *out = NULL;
    size_t out_len = 0;
    ret = blockstore_get(tmp, cid, &out, &out_len);
    if (ret && out && out_len == sizeof(data) && memcmp(out, data, sizeof(data)) == 0) {
        free(out);
        /* cleanup */
        char path[512];
        snprintf(path, sizeof(path), "%s/Qm/Qm1234567890abcdef.data", tmp);
        remove(path);
        rmdir(tmp);
        return 1;
    }
    if (out) free(out);
    rmdir(tmp);
    return 0;
}

#endif
