#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs/multibase/multibase.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 100000) return 0;
    unsigned char results[4096];
    size_t results_len = 0;
    multibase_decode(data, size, results, sizeof(results), &results_len);
    return 0;
}