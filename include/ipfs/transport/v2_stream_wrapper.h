#ifndef __IPFS_V2_STREAM_WRAPPER_H__
#define __IPFS_V2_STREAM_WRAPPER_H__

#include "ipfs/transport/stream.h"

/* Forward declaration of v2 stream (avoids including v2 tcp.h here) */
struct Libp2pV2Stream;

/**
 * Wrap a v2 stream in the main-repo libp2p_stream_t interface.
 */
libp2p_stream_t *ipfs_v2_stream_wrap(struct Libp2pV2Stream *v2);

/**
 * Unwrap: retrieve the underlying v2 stream from a wrapper.
 */
struct Libp2pV2Stream *ipfs_v2_stream_unwrap(libp2p_stream_t *s);

/**
 * Wrap a legacy struct Stream* as a v2 stream for Noise handshake.
 * The returned v2 stream is heap-allocated and owns nothing; it merely
 * delegates read/write/close to the legacy stream.
 */
struct Libp2pV2Stream *ipfs_v2_stream_from_legacy(struct Stream *legacy);

/**
 * Free a v2 stream wrapper created by ipfs_v2_stream_from_legacy.
 * Does NOT close/free the underlying legacy stream.
 */
void ipfs_v2_stream_wrapper_free(struct Libp2pV2Stream *v2);

#endif
