#ifndef __IPFS_TRANSPORT_STREAM_BRIDGE_H__
#define __IPFS_TRANSPORT_STREAM_BRIDGE_H__

#include "libp2p/net/stream.h"
#include "ipfs/transport/stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bridge a libp2p_stream_t (from transport registry) into a c-libp2p Stream.
 *
 * The returned Stream has stream_type == STREAM_TYPE_RAW and delegates
 * read/write/close to the underlying libp2p_stream_t.
 *
 * @param lstream the transport stream to wrap
 * @param multiaddr_str the multiaddress string (used for Stream->address)
 * @return a new Stream, or NULL on error
 */
struct Stream *ipfs_transport_stream_bridge_new(libp2p_stream_t *lstream, const char *multiaddr_str);

#ifdef __cplusplus
}
#endif

#endif
