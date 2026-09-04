#include <stdlib.h>
#include <string.h>

#include "libp2p/net/stream.h"
#include "libp2p/net/tcp.h"
#include "libp2p/conn/noise.h"
#include "libp2p/utils/logger.h"
#include "ipfs/transport/stream.h"
#include "ipfs/transport/stream_bridge.h"
#include "ipfs/transport/v2_stream_wrapper.h"
#include "ipfs/transport/noise_v2_bridge.h"

struct Stream *ipfs_noise_handshake_legacy(struct Stream *legacy_raw_stream,
                                            void *private_key,
                                            const noise_identity_callbacks_t *callbacks) {
    if (!legacy_raw_stream) {
        libp2p_logger_error("noise_bridge", "NULL legacy stream passed to Noise handshake\n");
        return NULL;
    }

    /* 1. Wrap legacy stream as v2 stream */
    struct Libp2pV2Stream *v2_raw = ipfs_v2_stream_from_legacy(legacy_raw_stream);
    if (!v2_raw) {
        libp2p_logger_error("noise_bridge", "Failed to wrap legacy stream for v2\n");
        return NULL;
    }

    /* 2. Run v2 Noise handshake (raw — caller must have already negotiated
     *    `/noise` over multistream). */
    struct Libp2pV2Stream *v2_noise = libp2p_noise_handshake_raw(v2_raw, private_key, NULL, callbacks);
    if (!v2_noise) {
        libp2p_logger_error("noise_bridge", "Noise handshake failed\n");
        ipfs_v2_stream_wrapper_free(v2_raw);
        return NULL;
    }

    /* 3. The v2_noise stream now owns the underlying I/O; we must not let
     *    v2_raw close the legacy stream when it is freed.  Disconnect the
     *    close callback so that ipfs_v2_stream_wrapper_free(v2_raw) is a
     *    no-op on the legacy fd. */
    v2_raw->close = NULL;
    ipfs_v2_stream_wrapper_free(v2_raw);

    /* 4. Wrap v2 noise stream as libp2p_stream_t */
    libp2p_stream_t *lstream = ipfs_v2_stream_wrap(v2_noise);
    if (!lstream) {
        libp2p_logger_error("noise_bridge", "Failed to wrap v2 noise stream\n");
        if (v2_noise->close)
            v2_noise->close(v2_noise);
        return NULL;
    }

    /* 5. Bridge libp2p_stream_t back to legacy Stream */
    struct Stream *legacy_noise = ipfs_transport_stream_bridge_new(lstream, "/ip4/127.0.0.1/tcp/0/noise");
    if (!legacy_noise) {
        libp2p_logger_error("noise_bridge", "Failed to bridge noise stream to legacy\n");
        lstream->close(lstream);
        return NULL;
    }

    libp2p_logger_info("noise_bridge", "Noise handshake completed and bridged to legacy stream\n");
    return legacy_noise;
}

struct Stream *ipfs_noise_handshake_legacy_2arg(struct Stream *legacy_raw_stream,
                                                 void *private_key) {
    return ipfs_noise_handshake_legacy(legacy_raw_stream, private_key, NULL);
}
