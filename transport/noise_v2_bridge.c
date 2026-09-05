#include <stdlib.h>
#include <string.h>

#include "libp2p/net/stream.h"
#include "libp2p/net/tcp.h"
#include "libp2p/conn/noise.h"
#include "libp2p/utils/logger.h"
#include "libp2p/crypto/rsa.h"
#include "libp2p/crypto/key.h"
#include "ipfs/transport/stream.h"
#include "ipfs/transport/stream_bridge.h"
#include "ipfs/transport/v2_stream_wrapper.h"
#include "ipfs/transport/noise_v2_bridge.h"

/* ============================================================================
 * RSA Identity Callbacks for Noise Handshake Payload
 * ============================================================================ */

static int noise_rsa_get_identity_key(void *private_key, uint8_t **out_key, size_t *out_len) {
    struct RsaPrivateKey *rsa = (struct RsaPrivateKey *)private_key;
    if (!rsa || !rsa->public_key_der || rsa->public_key_length == 0)
        return 0;

    struct PublicKey pubkey;
    memset(&pubkey, 0, sizeof(pubkey));
    pubkey.type = KEYTYPE_RSA;
    pubkey.data = (unsigned char *)rsa->public_key_der;
    pubkey.data_size = rsa->public_key_length;

    size_t needed = libp2p_crypto_public_key_protobuf_encode_size(&pubkey);
    unsigned char *buf = malloc(needed);
    if (!buf)
        return 0;

    size_t written = 0;
    if (!libp2p_crypto_public_key_protobuf_encode(&pubkey, buf, needed, &written)) {
        free(buf);
        return 0;
    }

    *out_key = buf;
    *out_len = written;
    return 1;
}

#define NOISE_SIG_PREFIX "noise-libp2p-static-key:"
#define NOISE_SIG_PREFIX_LEN 24

static int noise_rsa_sign(void *private_key, const uint8_t *data, size_t data_len,
                          uint8_t **out_sig, size_t *out_len) {
    struct RsaPrivateKey *rsa = (struct RsaPrivateKey *)private_key;
    if (!rsa)
        return 0;

    char *to_sign = (char *)malloc(NOISE_SIG_PREFIX_LEN + data_len);
    if (!to_sign)
        return 0;

    memcpy(to_sign, NOISE_SIG_PREFIX, NOISE_SIG_PREFIX_LEN);
    memcpy(to_sign + NOISE_SIG_PREFIX_LEN, data, data_len);

    unsigned char *sig = NULL;
    size_t sig_len = 0;
    int ret = libp2p_crypto_rsa_sign(rsa, to_sign, NOISE_SIG_PREFIX_LEN + data_len, &sig, &sig_len);
    free(to_sign);
    if (!ret)
        return 0;

    *out_sig = sig;
    *out_len = sig_len;
    return 1;
}

static int noise_rsa_verify(const uint8_t *identity_key, size_t identity_key_len,
                            const uint8_t *data, size_t data_len,
                            const uint8_t *sig, size_t sig_len) {
    (void)sig_len;

    struct PublicKey *pubkey = NULL;
    if (!libp2p_crypto_public_key_protobuf_decode((unsigned char *)identity_key, identity_key_len, &pubkey))
        return 0;

    if (!pubkey || pubkey->type != KEYTYPE_RSA || !pubkey->data) {
        if (pubkey) libp2p_crypto_public_key_free(pubkey);
        return 0;
    }

    struct RsaPublicKey rsa_pub;
    memset(&rsa_pub, 0, sizeof(rsa_pub));
    rsa_pub.der = pubkey->data;
    rsa_pub.der_length = pubkey->data_size;

    unsigned char *to_verify = (unsigned char *)malloc(NOISE_SIG_PREFIX_LEN + data_len);
    if (!to_verify) {
        libp2p_crypto_public_key_free(pubkey);
        return 0;
    }

    memcpy(to_verify, NOISE_SIG_PREFIX, NOISE_SIG_PREFIX_LEN);
    memcpy(to_verify + NOISE_SIG_PREFIX_LEN, data, data_len);

    int ret = libp2p_crypto_rsa_verify(&rsa_pub, to_verify, NOISE_SIG_PREFIX_LEN + data_len, sig);
    free(to_verify);
    libp2p_crypto_public_key_free(pubkey);
    return ret;
}

static void noise_rsa_free_buffer(uint8_t *buf) {
    free(buf);
}

static const noise_identity_callbacks_t noise_rsa_callbacks = {
    .get_identity_key = noise_rsa_get_identity_key,
    .sign = noise_rsa_sign,
    .verify = noise_rsa_verify,
    .free_buffer = noise_rsa_free_buffer,
};

/* ============================================================================
 * Bridge Functions
 * ============================================================================ */

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

    /* Wire parent_stream so handle_upgrade propagates to the raw TCP stream,
     * which updates session_context->default_stream for yamux. */
    legacy_noise->parent_stream = legacy_raw_stream;

    libp2p_logger_info("noise_bridge", "Noise handshake completed and bridged to legacy stream\n");
    return legacy_noise;
}

struct Stream *ipfs_noise_handshake_legacy_2arg(struct Stream *legacy_raw_stream,
                                                 void *private_key) {
    return ipfs_noise_handshake_legacy(legacy_raw_stream, private_key, &noise_rsa_callbacks);
}
