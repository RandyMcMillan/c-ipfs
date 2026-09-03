#ifndef TEST_CID_V1_H
#define TEST_CID_V1_H

#include <string.h>
#include <stdlib.h>
#include "ipfs/cid/cid.h"

int test_cid_from_string_v0(void) {
    struct Cid* cid = NULL;
    const unsigned char *hash = (const unsigned char *)"QmdfTbBqBPQ7VNxZEYEj14VmRuZBkqFbiwReogJgS1zR1n";
    int ret = ipfs_cid_decode_hash_from_base58(hash, strlen((const char*)hash), &cid);
    if (!ret || cid == NULL) return 0;
    ipfs_cid_free(cid);
    return 1;
}

int test_cid_from_bytes_v0(void) {
    unsigned char mh[34] = { 0x12, 0x20 };
    for (int i = 2; i < 34; i++) mh[i] = (unsigned char)i;
    struct Cid* cid = ipfs_cid_new(0, mh, 34, CID_DAG_PROTOBUF);
    return cid != NULL;
}

#endif
