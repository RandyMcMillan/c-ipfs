#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/sha.h>
#ifdef HAS_LSQUIC
#include <openssl/curve25519.h>
#endif
#include <secp256k1.h>

#include "libp2p/utils/debug.h"
#include "ipfs/crypto/verify.h"

/**
 * Verify an Ed25519 signature.
 *
 * First tries the BoringSSL low-level ED25519_verify when available (avoids
 * EVP Ed25519 quirks with some BoringSSL builds), then falls back to the
 * OpenSSL/BoringSSL EVP API.
 */
int ipfs_crypto_verify_ed25519(const uint8_t *pubkey, size_t pubkey_len,
                                const uint8_t *msg, size_t msg_len,
                                const uint8_t *sig, size_t sig_len) {
    DBG_SCOPE("crypto_ed25519", "verify: pubkey_len=%zu sig_len=%zu msg_len=%zu\n",
              pubkey_len, sig_len, msg_len);
    if (!pubkey || !msg || !sig) {
        DBG_SCOPE("crypto_ed25519", "verify: null argument\n");
        return 0;
    }
    if (pubkey_len != 32) {
        DBG_SCOPE("crypto_ed25519", "verify: bad pubkey_len\n");
        return 0;
    }

#ifdef HAS_LSQUIC
    if (sig_len == 64) {
        int boring_ok = ED25519_verify(msg, msg_len, sig, pubkey);
        DBG_SCOPE("crypto_ed25519", "ED25519_verify returned %d\n", boring_ok);
        if (boring_ok == 1)
            return 1;
    }
#endif

    EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, pubkey, pubkey_len);
    if (!pkey) {
        DBG_SCOPE("crypto_ed25519", "EVP_PKEY_new_raw_public_key failed\n");
        return 0;
    }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        return 0;
    }

    int status = 0;
    if (EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, pkey) == 1) {
        if (EVP_DigestVerify(ctx, sig, sig_len, msg, msg_len) == 1) {
            status = 1;
        }
    }
    DBG_SCOPE("crypto_ed25519", "EVP verify returned %d\n", status);

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return status;
}

/**
 * Verify a secp256k1 ECDSA signature via libsecp256k1.
 *
 * Expects a SEC1 compressed public key (33 bytes) and a DER-encoded
 * signature.  The message is hashed with SHA-256 internally before
 * verification, matching the behaviour of the previous OpenSSL 3.x
 * EVP implementation.
 *
 * Using libsecp256k1 removes the dependency on OpenSSL 3.x-specific
 * APIs (OSSL_PARAM_BLD, EVP_PKEY_fromdata) and allows the binary to
 * link against BoringSSL for QUIC/lsquic without symbol conflicts.
 */
int ipfs_crypto_verify_secp256k1(const uint8_t *pubkey, size_t pubkey_len,
                                  const uint8_t *msg, size_t msg_len,
                                  const uint8_t *sig, size_t sig_len) {
    if (!pubkey || !msg || !sig) return 0;
    if (pubkey_len != 33) return 0;

    secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    if (!ctx) return 0;

    secp256k1_pubkey pk;
    if (!secp256k1_ec_pubkey_parse(ctx, &pk, pubkey, pubkey_len)) {
        secp256k1_context_destroy(ctx);
        return 0;
    }

    secp256k1_ecdsa_signature signature;
    if (!secp256k1_ecdsa_signature_parse_der(ctx, &signature, sig, sig_len)) {
        secp256k1_context_destroy(ctx);
        return 0;
    }

    uint8_t hash[SHA256_DIGEST_LENGTH];
    SHA256(msg, msg_len, hash);

    int ret = secp256k1_ecdsa_verify(ctx, &signature, hash, &pk);
    secp256k1_context_destroy(ctx);
    return ret == 1;
}
