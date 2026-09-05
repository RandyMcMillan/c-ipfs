#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "libp2p/net/stream.h"
#include "libp2p/net/connectionstream.h"
#include "libp2p/utils/logger.h"
#include "multiaddr/multiaddr.h"
#include "ipfs/transport/stream.h"
#include "ipfs/transport/stream_bridge.h"

/* Context held inside Stream->stream_context */
typedef struct {
    libp2p_stream_t *lstream;
    unsigned long long last_comm_epoch;
    uint8_t *read_buffer;
    size_t read_buffer_len;
    size_t read_buffer_pos;
} bridge_context_t;

static int bridge_close(struct Stream *stream) {
    if (!stream || !stream->stream_context)
        return 0;
    bridge_context_t *ctx = (bridge_context_t *)stream->stream_context;
    if (ctx->lstream && ctx->lstream->close) {
        ctx->lstream->close(ctx->lstream);
    }
    if (ctx->read_buffer)
        free(ctx->read_buffer);
    free(ctx);
    stream->stream_context = NULL;
    libp2p_stream_free(stream);
    return 1;
}

static int bridge_peek(void *stream_context) {
    (void)stream_context;
    /* No socket descriptor available; stub */
    return 0;
}

static int bridge_read(void *stream_context, struct StreamMessage **msg, int timeout_secs) {
    (void)timeout_secs;
    bridge_context_t *ctx = (bridge_context_t *)stream_context;
    if (!ctx || !ctx->lstream || !ctx->lstream->read)
        return 0;

    uint8_t buffer[4096];
    ssize_t bytes = ctx->lstream->read(ctx->lstream, buffer, sizeof(buffer));
    if (bytes < 0)
        return 0;

    if (bytes > 0) {
        *msg = libp2p_stream_message_new();
        if (!*msg)
            return 0;
        (*msg)->data = malloc(bytes);
        if (!(*msg)->data) {
            libp2p_stream_message_free(*msg);
            *msg = NULL;
            return 0;
        }
        memcpy((*msg)->data, buffer, bytes);
        (*msg)->data_size = (size_t)bytes;
        (*msg)->error_number = 0;
    }
    return (int)bytes;
}

static int bridge_read_raw(void *stream_context, uint8_t *buffer, int buffer_size, int timeout_secs) {
    (void)timeout_secs;
    bridge_context_t *ctx = (bridge_context_t *)stream_context;
    if (!ctx || !ctx->lstream || !ctx->lstream->read)
        return -1;

    /* Serve from existing buffer first */
    if (ctx->read_buffer && ctx->read_buffer_pos < ctx->read_buffer_len) {
        size_t available = ctx->read_buffer_len - ctx->read_buffer_pos;
        size_t to_copy = (buffer_size < (int)available) ? (size_t)buffer_size : available;
        memcpy(buffer, ctx->read_buffer + ctx->read_buffer_pos, to_copy);
        ctx->read_buffer_pos += to_copy;
        return (int)to_copy;
    }

    /* No buffered data: read a chunk from the v2 stream into a temp buffer.
     * Frame-based transports (e.g. Noise) may return more plaintext than
     * requested in a single frame; we buffer the excess. */
    size_t temp_size = 65536;
    uint8_t *temp = (uint8_t *)malloc(temp_size);
    if (!temp)
        return -1;

    ssize_t bytes = ctx->lstream->read(ctx->lstream, temp, temp_size);
    if (bytes <= 0) {
        free(temp);
        return -1;
    }

    size_t to_copy = (buffer_size < bytes) ? (size_t)buffer_size : (size_t)bytes;
    memcpy(buffer, temp, to_copy);

    if ((size_t)bytes > to_copy) {
        size_t excess = bytes - to_copy;
        uint8_t *new_buf = (uint8_t *)realloc(ctx->read_buffer, excess);
        if (new_buf) {
            ctx->read_buffer = new_buf;
            memcpy(ctx->read_buffer, temp + to_copy, excess);
            ctx->read_buffer_len = excess;
            ctx->read_buffer_pos = 0;
        }
    }

    free(temp);
    return (int)to_copy;
}

static int bridge_write(void *stream_context, struct StreamMessage *msg) {
    bridge_context_t *ctx = (bridge_context_t *)stream_context;
    if (!ctx || !ctx->lstream || !ctx->lstream->write)
        return -1;

    ssize_t written = ctx->lstream->write(ctx->lstream, msg->data, msg->data_size);
    return (int)written;
}

static int bridge_handle_upgrade(struct Stream *stream, struct Stream *new_stream) {
    /* Propagate upgrade to parent stream so session_context->default_stream is updated */
    if (stream->parent_stream && stream->parent_stream->handle_upgrade) {
        return stream->parent_stream->handle_upgrade(stream->parent_stream, new_stream);
    }
    return 1;
}

struct Stream *ipfs_transport_stream_bridge_new(libp2p_stream_t *lstream, const char *multiaddr_str) {
    if (!lstream || !multiaddr_str)
        return NULL;

    struct Stream *out = libp2p_stream_new();
    if (!out)
        return NULL;

    out->stream_type = STREAM_TYPE_RAW;
    out->close = bridge_close;
    out->peek = bridge_peek;
    out->read = bridge_read;
    out->read_raw = bridge_read_raw;
    out->write = bridge_write;
    out->handle_upgrade = bridge_handle_upgrade;
    out->address = multiaddress_new_from_string(multiaddr_str);
    out->parent_stream = NULL;

    out->socket_mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    if (out->socket_mutex) {
        pthread_mutex_init(out->socket_mutex, NULL);
    }

    bridge_context_t *ctx = (bridge_context_t *)calloc(1, sizeof(bridge_context_t));
    if (!ctx) {
        libp2p_stream_free(out);
        return NULL;
    }
    ctx->lstream = lstream;
    ctx->last_comm_epoch = 0;
    out->stream_context = ctx;

    return out;
}
