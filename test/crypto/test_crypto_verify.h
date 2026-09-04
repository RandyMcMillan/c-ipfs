#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <openssl/evp.h>

#include "ipfs/crypto/verify.h"

int test_crypto_verify_ed25519_invalid_inputs(void) {
    int retVal = 0;
    uint8_t good_key[32] = {0};
    uint8_t good_sig[64] = {0};
    uint8_t msg[4] = {0xDE, 0xAD, 0xBE, 0xEF};

    // NULL pubkey
    if (ipfs_crypto_verify_ed25519(NULL, 32, msg, 4, good_sig, 64)) {
        fprintf(stderr, "should reject NULL pubkey\n");
        goto exit;
    }

    // Wrong pubkey length
    if (ipfs_crypto_verify_ed25519(good_key, 31, msg, 4, good_sig, 64)) {
        fprintf(stderr, "should reject wrong pubkey length\n");
        goto exit;
    }

    // NULL msg
    if (ipfs_crypto_verify_ed25519(good_key, 32, NULL, 4, good_sig, 64)) {
        fprintf(stderr, "should reject NULL msg\n");
        goto exit;
    }

    // NULL sig
    if (ipfs_crypto_verify_ed25519(good_key, 32, msg, 4, NULL, 64)) {
        fprintf(stderr, "should reject NULL sig\n");
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}

int test_crypto_verify_secp256k1_invalid_inputs(void) {
    int retVal = 0;
    uint8_t good_key[33] = {0x02};
    uint8_t good_sig[72] = {0};
    uint8_t msg[4] = {0xDE, 0xAD, 0xBE, 0xEF};

    // Wrong pubkey length
    if (ipfs_crypto_verify_secp256k1(good_key, 32, msg, 4, good_sig, 72)) {
        fprintf(stderr, "should reject wrong pubkey length\n");
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}

int test_crypto_verify_ed25519_roundtrip(void) {
    int retVal = 0;
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *gen_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    if (!gen_ctx || EVP_PKEY_keygen_init(gen_ctx) <= 0 || EVP_PKEY_keygen(gen_ctx, &pkey) <= 0 || !pkey) {
        fprintf(stderr, "failed to generate Ed25519 key\n");
        EVP_PKEY_CTX_free(gen_ctx);
        goto exit;
    }
    EVP_PKEY_CTX_free(gen_ctx);

    // Extract raw public key
    uint8_t pubkey[32];
    size_t pubkey_len = 32;
    if (EVP_PKEY_get_raw_public_key(pkey, pubkey, &pubkey_len) != 1 || pubkey_len != 32) {
        fprintf(stderr, "failed to get raw public key\n");
        goto cleanup_key;
    }

    // Sign message
    const uint8_t msg[] = "ipns-signature:test message";
    size_t msg_len = sizeof(msg) - 1;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        fprintf(stderr, "failed to create MD context\n");
        goto cleanup_key;
    }

    uint8_t sig[64];
    size_t sig_len = sizeof(sig);
    if (EVP_DigestSignInit(ctx, NULL, NULL, NULL, pkey) != 1) {
        fprintf(stderr, "failed to init sign\n");
        goto cleanup_ctx;
    }
    if (EVP_DigestSign(ctx, sig, &sig_len, msg, msg_len) != 1) {
        fprintf(stderr, "failed to sign\n");
        goto cleanup_ctx;
    }

    // Verify with our function
    if (!ipfs_crypto_verify_ed25519(pubkey, pubkey_len, msg, msg_len, sig, sig_len)) {
        fprintf(stderr, "Ed25519 roundtrip verification failed\n");
        goto cleanup_ctx;
    }

    // Verify that tampered message fails
    uint8_t bad_msg[] = "ipns-signature:tampered!";
    if (ipfs_crypto_verify_ed25519(pubkey, pubkey_len, bad_msg, sizeof(bad_msg)-1, sig, sig_len)) {
        fprintf(stderr, "should reject tampered message\n");
        goto cleanup_ctx;
    }

    retVal = 1;
cleanup_ctx:
    EVP_MD_CTX_free(ctx);
cleanup_key:
    EVP_PKEY_free(pkey);
exit:
    return retVal;
}

static size_t hex_to_bin(const char *hex, uint8_t *out, size_t max_len) {
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0 || hex_len / 2 > max_len) return 0;
    for (size_t i = 0; i < hex_len / 2; i++) {
        sscanf(hex + (i * 2), "%02hhx", &out[i]);
    }
    return hex_len / 2;
}

/* NOTE: hex_to_bin helper is kept above for future real-vector tests.
 * The previously added Ed25519/secp256k1 IPNS vector tests used
 * example/mock data that is not a valid cryptographic signature,
 * so those test functions have been removed.  Roundtrip tests
 * (test_crypto_verify_ed25519_roundtrip) still exercise the code. */
