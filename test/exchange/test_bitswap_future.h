#ifndef TEST_BITSWAP_FUTURE_H
#define TEST_BITSWAP_FUTURE_H

#include <string.h>
#include <stdlib.h>
#include "ipfs/exchange/bitswap/future.h"
#include "ipfs/blocks/block.h"

int test_bitswap_future_create_resolve(void) {
    bitswap_future_t *fut = bitswap_future_create();
    if (!fut) return 0;

    struct Block blk;
    memset(&blk, 0, sizeof(blk));
    unsigned char data[] = "hello";
    blk.data = data;
    blk.data_length = 5;
    bitswap_future_resolve(fut, &blk);

    struct Block *result = NULL;
    int ret = bitswap_future_wait(fut, 1000, &result);
    bitswap_future_free(fut);
    return ret && result && result->data_length == 5;
}

int test_bitswap_future_reject(void) {
    bitswap_future_t *fut = bitswap_future_create();
    if (!fut) return 0;
    bitswap_future_reject(fut, 42);
    struct Block *result = NULL;
    int ret = bitswap_future_wait(fut, 1000, &result);
    bitswap_future_free(fut);
    return !ret;
}

#endif
