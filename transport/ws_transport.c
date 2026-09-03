#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs/transport/stream.h"
#include "ipfs/transport/transport.h"

#ifdef HAS_LIBWEBSOCKETS
#include <libwebsockets.h>

typedef struct {
    libp2p_transport_t base;
    struct lws_context *lws_ctx;
} libp2p_ws_transport_t;

typedef struct {
    libp2p_stream_t base;
    struct lws *wsi;
    uint8_t buffer[4096];
    size_t head;
    size_t tail;
} ws_stream_impl_t;

static ssize_t ws_stream_read(libp2p_stream_t *stream, uint8_t *buf, size_t len) {
    ws_stream_impl_t *impl = (ws_stream_impl_t*)stream;
    if (impl->head == impl->tail) return 0;

    size_t avail = impl->head - impl->tail;
    size_t to_read = len < avail ? len : avail;
    memcpy(buf, impl->buffer + impl->tail, to_read);
    impl->tail += to_read;
    if (impl->tail == impl->head) {
        impl->head = impl->tail = 0;
    }
    return to_read;
}

static ssize_t ws_stream_write(libp2p_stream_t *stream, const uint8_t *buf, size_t len) {
    ws_stream_impl_t *impl = (ws_stream_impl_t*)stream;
    unsigned char *write_buf = malloc(LWS_PRE + len);
    if (!write_buf) return -1;
    memcpy(&write_buf[LWS_PRE], buf, len);
    int bytes_sent = lws_write(impl->wsi, &write_buf[LWS_PRE], len, LWS_WRITE_BINARY);
    free(write_buf);
    return bytes_sent;
}

static void ws_stream_close(libp2p_stream_t *stream) {
    ws_stream_impl_t *impl = (ws_stream_impl_t*)stream;
    if (impl->wsi) {
        lws_callback_on_writable(impl->wsi);
    }
    free(impl);
}

static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    ws_stream_impl_t *stream = (ws_stream_impl_t *)user;

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("[WS] Connection established with peer.\n");
            break;
        case LWS_CALLBACK_CLIENT_RECEIVE:
            if (stream && (stream->head + len < sizeof(stream->buffer))) {
                memcpy(stream->buffer + stream->head, in, len);
                stream->head += len;
            }
            break;
        case LWS_CALLBACK_CLIENT_CLOSED:
            printf("[WS] Socket connection closed.\n");
            break;
        default:
            break;
    }
    return 0;
}

static struct lws_protocols ws_protocols[] = {
    { "libp2p-ws-protocol", ws_callback, sizeof(ws_stream_impl_t), 4096 },
    { NULL, NULL, 0, 0 }
};

static int ws_dial(libp2p_transport_t *self, const char *multiaddr, libp2p_stream_t **out_stream) {
    (void)out_stream;
    libp2p_ws_transport_t *ws_trans = (libp2p_ws_transport_t *)self;

    char ip[64];
    int port;
    if (sscanf(multiaddr, "/ip4/%63[^/]/tcp/%d/ws", ip, &port) != 2) {
        fprintf(stderr, "Invalid WebSocket multiaddr format\n");
        return -1;
    }

    struct lws_client_connect_info ccinfo;
    memset(&ccinfo, 0, sizeof(ccinfo));
    ccinfo.context = ws_trans->lws_ctx;
    ccinfo.address = ip;
    ccinfo.port = port;
    ccinfo.path = "/";
    ccinfo.host = ccinfo.address;
    ccinfo.origin = ccinfo.address;
    ccinfo.protocol = ws_protocols[0].name;
    ccinfo.ssl_connection = 0;

    struct lws *wsi = lws_client_connect_via_info(&ccinfo);
    if (!wsi) return -1;

    return 0;
}

libp2p_transport_t *libp2p_ws_transport_create(void) {
    libp2p_ws_transport_t *t = calloc(1, sizeof(libp2p_ws_transport_t));
    t->base.name = "ws";
    t->base.dial = ws_dial;

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = ws_protocols;

    t->lws_ctx = lws_create_context(&info);
    if (!t->lws_ctx) {
        free(t);
        return NULL;
    }
    return (libp2p_transport_t *)t;
}

#else /* !HAS_LIBWEBSOCKETS */

libp2p_transport_t *libp2p_ws_transport_create(void) {
    fprintf(stderr, "[WS] libwebsockets not available at compile time\n");
    return NULL;
}

#endif
