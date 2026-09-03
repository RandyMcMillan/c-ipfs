#ifndef TEST_BITSWAP_FUTURE_H
#define TEST_BITSWAP_FUTURE_H

#include <string.h>
#include <stdlib.h>
#include "ipfs/exchange/bitswap/future.h"

int test_bitswap_future_create_resolve(void) {
    bitswap_future_t *fut = bitswap_future_create();
    if (!fut) return 0;

    block_t blk = { .data = (uint8_t*)"hello", .size = 5, .cid_str = "QmTest" };
    bitswap_future_resolve(fut, &blk);

    block_t *result = NULL;
    int ret = bitswap_future_wait(fut, 1000, &result);
    bitswap_future_free(fut);
    return ret && result && result->size == 5;
}

int test_bitswap_future_reject(void) {
    bitswap_future_t *fut = bitswap_future_create();
    if (!fut) return 0;
    bitswap_future_reject(fut, 42);
    block_t *result = NULL;
    int ret = bitswap_future_wait(fut, 1000, &result);
    bitswap_future_free(fut);
    return !ret;
}

#endif
