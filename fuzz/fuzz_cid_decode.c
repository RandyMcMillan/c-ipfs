#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs/cid/cid.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 100000) return 0;
    char *buf = malloc(size + 1);
    if (!buf) return 0;
    memcpy(buf, data, size);
    buf[size] = '\0';

    struct Cid *cid = NULL;
    ipfs_cid_decode_hash_from_base58((const unsigned char*)buf, size, &cid);
    if (cid) ipfs_cid_free(cid);

    free(buf);
    return 0;
}