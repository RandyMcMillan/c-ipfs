#include <stdlib.h>
#include <string.h>

#include "libp2p/net/stream.h"
#include "libp2p/net/tcp.h"
#include "libp2p/utils/logger.h"
#include "ipfs/transport/stream.h"
#include "ipfs/transport/v2_stream_wrapper.h"
#include "ipfs/transport/stream_bridge.h"

/* ============================================================================
 * v2 -> libp2p_stream_t wrapper
 * ============================================================================ */

typedef struct {
    struct Libp2pV2Stream *v2;
} v2_wrap_ctx_t;

static ssize_t wrap_v2_read(libp2p_stream_t *s, uint8_t *buf, size_t len) {
    v2_wrap_ctx_t *ctx = (v2_wrap_ctx_t *)s->user_data;
    if (!ctx || !ctx->v2 || !ctx->v2->read)
        return -1;
    return ctx->v2->read(ctx->v2, buf, len);
}

static ssize_t wrap_v2_write(libp2p_stream_t *s, const uint8_t *buf, size_t len) {
    v2_wrap_ctx_t *ctx = (v2_wrap_ctx_t *)s->user_data;
    if (!ctx || !ctx->v2 || !ctx->v2->write)
        return -1;
    return ctx->v2->write(ctx->v2, buf, len);
}

static void wrap_v2_close(libp2p_stream_t *s) {
    v2_wrap_ctx_t *ctx = (v2_wrap_ctx_t *)s->user_data;
    if (ctx && ctx->v2 && ctx->v2->close)
        ctx->v2->close(ctx->v2);
    free(ctx);
    free(s);
}

libp2p_stream_t *ipfs_v2_stream_wrap(struct Libp2pV2Stream *v2) {
    if (!v2)
        return NULL;
    libp2p_stream_t *out = (libp2p_stream_t *)calloc(1, sizeof(libp2p_stream_t));
    if (!out)
        return NULL;
    v2_wrap_ctx_t *ctx = (v2_wrap_ctx_t *)calloc(1, sizeof(v2_wrap_ctx_t));
    if (!ctx) {
        free(out);
        return NULL;
    }
    ctx->v2 = v2;
    out->user_data = ctx;
    out->read = wrap_v2_read;
    out->write = wrap_v2_write;
    out->close = wrap_v2_close;
    return out;
}

struct Libp2pV2Stream *ipfs_v2_stream_unwrap(libp2p_stream_t *s) {
    if (!s)
        return NULL;
    v2_wrap_ctx_t *ctx = (v2_wrap_ctx_t *)s->user_data;
    return ctx ? ctx->v2 : NULL;
}

/* ============================================================================
 * legacy struct Stream -> v2 wrapper
 * ============================================================================ */

typedef struct {
    struct Stream *legacy;
} legacy_wrap_ctx_t;

static ssize_t legacy_read_wrapper(struct Libp2pV2Stream *s, unsigned char *buf, size_t count) {
    legacy_wrap_ctx_t *ctx = (legacy_wrap_ctx_t *)s->stream_context;
    if (!ctx || !ctx->legacy || !ctx->legacy->read_raw)
        return -1;
    int ret = ctx->legacy->read_raw(ctx->legacy->stream_context, buf, (int)count, 10);
    return (ret < 0) ? -1 : (ssize_t)ret;
}

static ssize_t legacy_write_wrapper(struct Libp2pV2Stream *s, const unsigned char *buf, size_t count) {
    legacy_wrap_ctx_t *ctx = (legacy_wrap_ctx_t *)s->stream_context;
    if (!ctx || !ctx->legacy || !ctx->legacy->write)
        return -1;
    struct StreamMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.data = (uint8_t *)buf;
    msg.data_size = count;
    int ret = ctx->legacy->write(ctx->legacy->stream_context, &msg);
    return (ret > 0) ? (ssize_t)count : -1;
}

static void legacy_close_wrapper(struct Libp2pV2Stream *s) {
    legacy_wrap_ctx_t *ctx = (legacy_wrap_ctx_t *)s->stream_context;
    if (ctx && ctx->legacy && ctx->legacy->close)
        ctx->legacy->close(ctx->legacy);
}

struct Libp2pV2Stream *ipfs_v2_stream_from_legacy(struct Stream *legacy) {
    if (!legacy)
        return NULL;
    struct Libp2pV2Stream *v2 = (struct Libp2pV2Stream *)calloc(1, sizeof(struct Libp2pV2Stream));
    if (!v2)
        return NULL;
    legacy_wrap_ctx_t *ctx = (legacy_wrap_ctx_t *)calloc(1, sizeof(legacy_wrap_ctx_t));
    if (!ctx) {
        free(v2);
        return NULL;
    }
    ctx->legacy = legacy;
    v2->stream_context = ctx;
    v2->read = legacy_read_wrapper;
    v2->write = legacy_write_wrapper;
    v2->close = legacy_close_wrapper;
    return v2;
}

void ipfs_v2_stream_wrapper_free(struct Libp2pV2Stream *v2) {
    if (!v2)
        return;
    if (v2->stream_context)
        free(v2->stream_context);
    free(v2);
}
