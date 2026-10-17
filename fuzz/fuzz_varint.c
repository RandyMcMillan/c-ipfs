#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#include "libp2p/c-protobuf/varint.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 1000000) return 0;
    size_t bytes = 0;
    varint_decode(data, (int)size, &bytes);
    return 0;
}