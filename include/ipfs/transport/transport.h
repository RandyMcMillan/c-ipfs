#ifndef __IPFS_TRANSPORT_TRANSPORT_H__
#define __IPFS_TRANSPORT_TRANSPORT_H__

#include "ipfs/transport/stream.h"

typedef struct libp2p_transport {
    const char *name;
    int (*dial)(struct libp2p_transport *self, const char *multiaddr, libp2p_stream_t **out_stream);
    int (*listen)(struct libp2p_transport *self, const char *multiaddr);
} libp2p_transport_t;

#endif
