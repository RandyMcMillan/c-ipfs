#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>

#include "ipfs/exchange/bitswap/future.h"

typedef struct bitswap_want_entry {
    char cid_str[64];
    bool fulfilled;
    void *block_data;
    size_t block_size;
    struct bitswap_want_entry *next;
} bitswap_want_entry_t;

typedef struct {
    bitswap_want_entry_t *head;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} bitswap_want_manager_t;

bitswap_want_manager_t *bitswap_want_manager_new(void) {
    bitswap_want_manager_t *mgr = calloc(1, sizeof(bitswap_want_manager_t));
    if (!mgr) return NULL;
    pthread_mutex_init(&mgr->mutex, NULL);
    pthread_cond_init(&mgr->cond, NULL);
    return mgr;
}

int bitswap_want_manager_wait_for_block(bitswap_want_manager_t *mgr, const char *cid_str, uint32_t timeout_ms, void **out_data, size_t *out_size) {
    if (!mgr || !cid_str) return 0;

    pthread_mutex_lock(&mgr->mutex);

    bitswap_want_entry_t *entry = mgr->head;
    while (entry && strcmp(entry->cid_str, cid_str) != 0) {
        entry = entry->next;
    }

    if (!entry) {
        entry = calloc(1, sizeof(bitswap_want_entry_t));
        strncpy(entry->cid_str, cid_str, sizeof(entry->cid_str) - 1);
        entry->next = mgr->head;
        mgr->head = entry;
    }

    int result = 1;
    while (!entry->fulfilled) {
        if (timeout_ms == 0) {
            pthread_cond_wait(&mgr->cond, &mgr->mutex);
        } else {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            struct timespec ts;
            uint64_t nsec = (uint64_t)tv.tv_usec * 1000 + (uint64_t)(timeout_ms % 1000) * 1000000ULL;
            ts.tv_sec = tv.tv_sec + (timeout_ms / 1000) + (nsec / 1000000000ULL);
            ts.tv_nsec = nsec % 1000000000ULL;

            int rc = pthread_cond_timedwait(&mgr->cond, &mgr->mutex, &ts);
            if (rc == ETIMEDOUT) {
                result = 0;
                break;
            }
        }
    }

    if (result == 1 && entry->fulfilled) {
        if (out_data) *out_data = entry->block_data;
        if (out_size) *out_size = entry->block_size;
    }

    pthread_mutex_unlock(&mgr->mutex);
    return result;
}

void bitswap_want_manager_fulfill(bitswap_want_manager_t *mgr, const char *cid_str, void *data, size_t size) {
    if (!mgr || !cid_str) return;

    pthread_mutex_lock(&mgr->mutex);
    bitswap_want_entry_t *entry = mgr->head;
    while (entry) {
        if (strcmp(entry->cid_str, cid_str) == 0) {
            entry->block_data = data;
            entry->block_size = size;
            entry->fulfilled = true;
            pthread_cond_broadcast(&mgr->cond);
            break;
        }
        entry = entry->next;
    }
    pthread_mutex_unlock(&mgr->mutex);
}
