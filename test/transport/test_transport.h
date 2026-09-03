#ifndef TEST_TRANSPORT_H
#define TEST_TRANSPORT_H

#include <string.h>
#include <stdlib.h>
#include "ipfs/transport/stream.h"
#include "ipfs/transport/transport.h"

int test_transport_stream_struct_size(void) {
    libp2p_stream_t s;
    (void)s;
    return 1;
}

int test_transport_struct_size(void) {
    libp2p_transport_t t;
    (void)t;
    return 1;
}

#endif
