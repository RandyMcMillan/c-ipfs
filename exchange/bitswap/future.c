#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/time.h>

#include "ipfs/exchange/bitswap/future.h"

typedef enum {
    FUTURE_PENDING,
    FUTURE_SUCCESS,
    FUTURE_FAILED,
    FUTURE_CANCELLED
} future_state_t;

struct bitswap_future {
    future_state_t state;
    block_t *result_block;
    int error_code;
    bitswap_callback_t callback;
    void *user_data;
    pthread_mutex_t mtx;
    pthread_cond_t cond;
};

bitswap_future_t *bitswap_future_create(void) {
    bitswap_future_t *fut = calloc(1, sizeof(bitswap_future_t));
    if (!fut) return NULL;
    fut->state = FUTURE_PENDING;
    pthread_mutex_init(&fut->mtx, NULL);
    pthread_cond_init(&fut->cond, NULL);
    return fut;
}

void bitswap_future_set_callback(bitswap_future_t *fut, bitswap_callback_t cb, void *user_data) {
    pthread_mutex_lock(&fut->mtx);
    fut->callback = cb;
    fut->user_data = user_data;
    if (fut->state != FUTURE_PENDING && cb) {
        pthread_mutex_unlock(&fut->mtx);
        cb(fut, user_data);
        return;
    }
    pthread_mutex_unlock(&fut->mtx);
}

void bitswap_future_resolve(bitswap_future_t *fut, block_t *block) {
    pthread_mutex_lock(&fut->mtx);
    if (fut->state != FUTURE_PENDING) {
        pthread_mutex_unlock(&fut->mtx);
        return;
    }
    fut->result_block = block;
    fut->state = FUTURE_SUCCESS;
    bitswap_callback_t cb = fut->callback;
    void *ud = fut->user_data;
    pthread_cond_broadcast(&fut->cond);
    pthread_mutex_unlock(&fut->mtx);
    if (cb) cb(fut, ud);
}

void bitswap_future_reject(bitswap_future_t *fut, int err_code) {
    pthread_mutex_lock(&fut->mtx);
    if (fut->state != FUTURE_PENDING) {
        pthread_mutex_unlock(&fut->mtx);
        return;
    }
    fut->error_code = err_code;
    fut->state = FUTURE_FAILED;
    bitswap_callback_t cb = fut->callback;
    void *ud = fut->user_data;
    pthread_cond_broadcast(&fut->cond);
    pthread_mutex_unlock(&fut->mtx);
    if (cb) cb(fut, ud);
}

void bitswap_future_cancel(bitswap_future_t *fut) {
    pthread_mutex_lock(&fut->mtx);
    if (fut->state == FUTURE_PENDING) {
        fut->state = FUTURE_CANCELLED;
        pthread_cond_broadcast(&fut->cond);
    }
    pthread_mutex_unlock(&fut->mtx);
}

int bitswap_future_wait(bitswap_future_t *fut, uint32_t timeout_ms, block_t **out_block) {
    pthread_mutex_lock(&fut->mtx);

    if (fut->state == FUTURE_PENDING) {
        if (timeout_ms == 0) {
            pthread_cond_wait(&fut->cond, &fut->mtx);
        } else {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            struct timespec ts;
            uint64_t nsec = (uint64_t)tv.tv_usec * 1000 + (uint64_t)(timeout_ms % 1000) * 1000000ULL;
            ts.tv_sec = tv.tv_sec + (timeout_ms / 1000) + (nsec / 1000000000ULL);
            ts.tv_nsec = nsec % 1000000000ULL;

            int res = pthread_cond_timedwait(&fut->cond, &fut->mtx, &ts);
            if (res != 0) {
                fut->state = FUTURE_FAILED;
                fut->error_code = -1;
                pthread_mutex_unlock(&fut->mtx);
                return 0;
            }
        }
    }

    if (fut->state == FUTURE_SUCCESS) {
        if (out_block) *out_block = fut->result_block;
        pthread_mutex_unlock(&fut->mtx);
        return 1;
    }

    pthread_mutex_unlock(&fut->mtx);
    return 0;
}

void bitswap_future_free(bitswap_future_t *fut) {
    if (!fut) return;
    pthread_mutex_destroy(&fut->mtx);
    pthread_cond_destroy(&fut->cond);
    free(fut);
}
