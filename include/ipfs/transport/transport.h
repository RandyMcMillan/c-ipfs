#ifndef __IPFS_TRANSPORT_TRANSPORT_H__
#define __IPFS_TRANSPORT_TRANSPORT_H__

#include "ipfs/transport/stream.h"

typedef struct libp2p_transport {
    const char *name;
    int (*dial)(struct libp2p_transport *self, const char *multiaddr, libp2p_stream_t **out_stream);
    int (*listen)(struct libp2p_transport *self, const char *multiaddr);
    void (*close)(struct libp2p_transport *self);
    void *user_data;
} libp2p_transport_t;

/**
 * Create a WebSocket transport backed by libwebsockets.
 *
 * @return A libp2p_transport_t pointer, or NULL on error.
 *
 * Note: libwebsockets must be available at compile time (HAS_LIBWEBSOCKETS defined).
 * If libwebsockets is not available, this function returns NULL.
 */
libp2p_transport_t *libp2p_ws_transport_create(void);

#endif
