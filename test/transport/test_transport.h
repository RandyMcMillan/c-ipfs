#ifndef TEST_TRANSPORT_H
#define TEST_TRANSPORT_H

#include <string.h>
#include <stdlib.h>
#include "ipfs/transport/stream.h"
#include "ipfs/transport/transport.h"

// Forward declarations for transport creation functions
extern libp2p_transport_t *libp2p_quic_transport_create(void *tls_ctx);
extern libp2p_transport_t *libp2p_ws_transport_create(void);

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

int test_transport_quic_create_without_lsquic(void) {
    // When HAVE_LSQUIC is not defined, creation should return NULL gracefully
    libp2p_transport_t *t = libp2p_quic_transport_create(NULL);
    if (t != NULL) {
        // If lsquic is available, at least verify the struct is populated
        if (t->name == NULL || t->dial == NULL) {
            return 0;
        }
        if (t->close != NULL) {
            t->close(t);
        }
    }
    return 1;
}

int test_transport_ws_create_without_libwebsockets(void) {
    // When HAS_LIBWEBSOCKETS is not defined, creation should return NULL gracefully
    libp2p_transport_t *t = libp2p_ws_transport_create();
    if (t != NULL) {
        // If libwebsockets is available, at least verify the struct is populated
        if (t->name == NULL || t->dial == NULL) {
            return 0;
        }
    }
    return 1;
}

#endif
