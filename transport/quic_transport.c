#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "ipfs/transport/stream.h"
#include "ipfs/transport/transport.h"

/* Conditional compilation: only build QUIC if lsquic is available */
#ifdef HAS_LSQUIC
#include <lsquic.h>
#include <openssl/ssl.h>

typedef struct {
    libp2p_transport_t base;
    lsquic_engine_t *engine;
    SSL_CTX *ssl_ctx;
} libp2p_quic_transport_t;

typedef struct {
    libp2p_stream_t base;
    lsquic_stream_t *ls_stream;
    uint8_t rx_buf[4096];
    size_t rx_len;
} quic_stream_impl_t;

static ssize_t quic_stream_read(libp2p_stream_t *stream, uint8_t *buf, size_t len) {
    quic_stream_impl_t *impl = (quic_stream_impl_t*)stream;
    return lsquic_stream_read(impl->ls_stream, buf, len);
}

static ssize_t quic_stream_write(libp2p_stream_t *stream, const uint8_t *buf, size_t len) {
    quic_stream_impl_t *impl = (quic_stream_impl_t*)stream;
    return lsquic_stream_write(impl->ls_stream, buf, len);
}

static void quic_stream_close(libp2p_stream_t *stream) {
    quic_stream_impl_t *impl = (quic_stream_impl_t*)stream;
    if (impl->ls_stream) {
        lsquic_stream_close(impl->ls_stream);
    }
    free(impl);
}

static lsquic_conn_ctx_t *on_new_conn(void *stream_if_ctx, lsquic_conn_t *c) {
    (void)stream_if_ctx;
    printf("[QUIC] Connected to peer successfully.\n");
    return (lsquic_conn_ctx_t *)c;
}

static lsquic_stream_ctx_t *on_new_stream(void *stream_if_ctx, lsquic_stream_t *s) {
    (void)stream_if_ctx;
    lsquic_stream_wantread(s, 1);
    quic_stream_impl_t *stream = calloc(1, sizeof(quic_stream_impl_t));
    stream->base.read = quic_stream_read;
    stream->base.write = quic_stream_write;
    stream->base.close = quic_stream_close;
    stream->ls_stream = s;
    return (lsquic_stream_ctx_t *)stream;
}

static const struct lsquic_stream_if quic_stream_if = {
    .on_new_conn = on_new_conn,
    .on_new_stream = on_new_stream,
};

static int quic_dial(libp2p_transport_t *self, const char *multiaddr, libp2p_stream_t **out_stream) {
    (void)out_stream;
    libp2p_quic_transport_t *quic = (libp2p_quic_transport_t *)self;

    char ip[64];
    int port;
    if (sscanf(multiaddr, "/ip4/%63[^/]/udp/%d/quic-v1", ip, &port) != 2) {
        fprintf(stderr, "Invalid QUIC multiaddr\n");
        return -1;
    }

    struct sockaddr_in peer_addr;
    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &peer_addr.sin_addr);

    lsquic_conn_t *conn = lsquic_engine_connect(
        quic->engine,
        N_LSQUIC_VER,
        (struct sockaddr *)&peer_addr,
        NULL, NULL, NULL, NULL, 0, NULL, 0, NULL, 0
    );

    if (!conn) return -1;
    lsquic_engine_process_conns(quic->engine);
    return 0;
}

libp2p_transport_t *libp2p_quic_transport_create(void *tls_ctx) {
    libp2p_quic_transport_t *t = calloc(1, sizeof(libp2p_quic_transport_t));
    t->base.name = "quic-v1";
    t->base.dial = quic_dial;
    t->base.listen = NULL; /* TODO: implement QUIC listen */
    t->base.close = NULL;  /* TODO: implement QUIC transport close */
    t->ssl_ctx = (SSL_CTX *)tls_ctx;

    struct lsquic_engine_api api;
    memset(&api, 0, sizeof(api));
    api.ea_stream_if = &quic_stream_if;
    api.ea_stream_if_ctx = t;

    t->engine = lsquic_engine_new(0, &api);
    return (libp2p_transport_t *)t;
}

#else /* !HAS_LSQUIC */

libp2p_transport_t *libp2p_quic_transport_create(void *tls_ctx) {
    (void)tls_ctx;
    fprintf(stderr, "[QUIC] lsquic not available at compile time\n");
    return NULL;
}

#endif
