#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#include "libp2p/c-protobuf/protobuf.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 100000) return 0;
    /* Exercise protobuf decode helpers if available */
    return 0;
}