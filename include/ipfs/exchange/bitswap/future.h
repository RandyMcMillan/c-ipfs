#ifndef __IPFS_EXCHANGE_BITSWAP_FUTURE_H__
#define __IPFS_EXCHANGE_BITSWAP_FUTURE_H__

#include <stdint.h>
#include <stddef.h>

struct bitswap_future;
typedef struct bitswap_future bitswap_future_t;

typedef struct block {
    uint8_t *data;
    size_t size;
    char cid_str[64];
} block_t;

typedef void (*bitswap_callback_t)(bitswap_future_t *future, void *user_data);

bitswap_future_t *bitswap_future_create(void);
void bitswap_future_set_callback(bitswap_future_t *fut, bitswap_callback_t cb, void *user_data);
void bitswap_future_resolve(bitswap_future_t *fut, block_t *block);
void bitswap_future_reject(bitswap_future_t *fut, int err_code);
void bitswap_future_cancel(bitswap_future_t *fut);
int bitswap_future_wait(bitswap_future_t *fut, uint32_t timeout_ms, block_t **out_block);
void bitswap_future_free(bitswap_future_t *fut);

#endif
