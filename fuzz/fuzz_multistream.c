#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 100000) return 0;
    /* Placeholder: multistream negotiation requires a stream context. */
    return 0;
}