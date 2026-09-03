#ifndef __IPFS_TRANSPORT_STREAM_H__
#define __IPFS_TRANSPORT_STREAM_H__

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct libp2p_stream {
    ssize_t (*read)(struct libp2p_stream *stream, uint8_t *buf, size_t len);
    ssize_t (*write)(struct libp2p_stream *stream, const uint8_t *buf, size_t len);
    void (*close)(struct libp2p_stream *stream);
    void *user_data;
} libp2p_stream_t;

#endif
