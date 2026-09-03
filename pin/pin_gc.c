#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdint.h>

#include "ipfs/pin/pin.h"

uint64_t pin_gc_reclaim_block(const char *block_path) {
    if (!block_path) return 0;

    struct stat st;
    uint64_t reclaimed = 0;

    if (stat(block_path, &st) == 0) {
        reclaimed = (uint64_t)st.st_size;
    } else {
        fprintf(stderr, "[pin_gc] Failed to stat block file prior to removal: %s\n", block_path);
    }

    if (remove(block_path) != 0) {
        fprintf(stderr, "[pin_gc] Failed to delete unpinned block file: %s\n", block_path);
        return 0;
    }

    return reclaimed;
}
