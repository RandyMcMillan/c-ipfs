#ifndef TEST_TRANSPORT_H
#define TEST_TRANSPORT_H

#include <string.h>
#include <stdlib.h>
#include "ipfs/transport/stream.h"
#include "ipfs/transport/transport.h"
#include "ipfs/transport/registry.h"

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
        if (t->close != NULL) {
            t->close(t);
        }
    }
    return 1;
}

int test_transport_registry_init_add_free(void) {
    transport_registry_t reg;
    transport_registry_init(&reg);

    libp2p_transport_t *ws = libp2p_ws_transport_create();
    if (ws != NULL) {
        if (transport_registry_add(&reg, ws) != 0) {
            return 0;
        }
    }

    transport_registry_free(&reg);
    return 1;
}

int test_transport_registry_remove(void) {
    transport_registry_t reg;
    transport_registry_init(&reg);

    libp2p_transport_t *ws = libp2p_ws_transport_create();
    if (ws != NULL) {
        transport_registry_add(&reg, ws);
        if (transport_registry_remove(&reg, "ws") != 0) {
            return 0;
        }
    }

    transport_registry_free(&reg);
    return 1;
}

int test_transport_registry_dial_no_match(void) {
    transport_registry_t reg;
    transport_registry_init(&reg);

    libp2p_stream_t *stream = NULL;
    int ret = transport_registry_dial(&reg, "/ip4/127.0.0.1/tcp/4001/ws", &stream);
    // With no transports registered, dial should fail
    if (ret == 0 || stream != NULL) {
        return 0;
    }

    transport_registry_free(&reg);
    return 1;
}

static ssize_t mock_stream_read(libp2p_stream_t *stream, uint8_t *buf, size_t len) {
    (void)stream;
    (void)buf;
    (void)len;
    return 0;
}

static ssize_t mock_stream_write(libp2p_stream_t *stream, const uint8_t *buf, size_t len) {
    (void)stream;
    (void)buf;
    return (ssize_t)len;
}

static void mock_stream_close(libp2p_stream_t *stream) {
    free(stream);
}

static int mock_transport_dial(libp2p_transport_t *self, const char *multiaddr, libp2p_stream_t **out_stream) {
    (void)self;
    (void)multiaddr;
    if (!out_stream) {
        return -1;
    }
    libp2p_stream_t *stream = (libp2p_stream_t*)calloc(1, sizeof(libp2p_stream_t));
    if (!stream) {
        return -1;
    }
    stream->read = mock_stream_read;
    stream->write = mock_stream_write;
    stream->close = mock_stream_close;
    *out_stream = stream;
    return 0;
}

static void mock_transport_close(libp2p_transport_t *self) {
    (void)self;
}

int test_transport_registry_dial_match(void) {
    transport_registry_t reg;
    transport_registry_init(&reg);

    libp2p_transport_t *transport = (libp2p_transport_t*)calloc(1, sizeof(libp2p_transport_t));
    if (!transport) {
        return 0;
    }
    transport->name = "ws";
    transport->dial = mock_transport_dial;
    transport->close = mock_transport_close;

    if (transport_registry_add(&reg, transport) != 0) {
        free(transport);
        return 0;
    }

    libp2p_stream_t *stream = NULL;
    if (transport_registry_dial(&reg, "/ip4/127.0.0.1/tcp/4001/ws", &stream) != 0) {
        transport_registry_free(&reg);
        return 0;
    }
    if (stream == NULL) {
        transport_registry_free(&reg);
        return 0;
    }

    (void)stream;
    return 1;
}

#endif
