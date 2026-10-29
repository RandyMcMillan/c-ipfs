#ifndef __IPFS_NOISE_V2_BRIDGE_H__
#define __IPFS_NOISE_V2_BRIDGE_H__

#include "libp2p/net/stream.h"
#include "libp2p/conn/noise.h"

/**
 * Perform a Noise_XX_25519_ChaChaPoly_SHA256 handshake over a legacy raw
 * TCP stream, returning a legacy Stream that encrypts/decrypts transparently.
 *
 * This bridges the v2 Noise implementation into the legacy connection stack.
 *
 * @param legacy_raw_stream  A legacy raw TCP stream (already past initial
 *                           multistream if applicable).
 * @param private_key        The local node's private key handle.
 * @param callbacks          Optional identity callbacks for libp2p payload
 *                           signing/verification. May be NULL for tests.
 * @return A new legacy Stream on success, NULL on failure.
 */
struct Stream *ipfs_noise_handshake_legacy(struct Stream *legacy_raw_stream,
                                            void *private_key,
                                            const noise_identity_callbacks_t *callbacks);

/**
 * Two-argument wrapper suitable for legacy Dialer.noise_handshake function pointer.
 * Always passes NULL for callbacks.
 */
struct Stream *ipfs_noise_handshake_legacy_2arg(struct Stream *legacy_raw_stream,
                                                 void *private_key);

/**
 * Return the default RSA-based Noise identity callbacks used by c-ipfs.
 * This allows v2 handshake initiators (e.g. swarm_dialer_v2_bridge) to share
 * the same payload signing/verification logic as the legacy bridge.
 */
const noise_identity_callbacks_t *ipfs_noise_rsa_callbacks(void);

/**
 * Return RSA-based identity callbacks suitable for libp2p TLS 1.3.
 * Unlike the Noise callbacks, these sign/verify the exact data buffer
 * passed by the caller (the TLS handshake prefix || SubjectPublicKeyInfo).
 */
const noise_identity_callbacks_t *ipfs_tls_rsa_callbacks(void);

#endif
