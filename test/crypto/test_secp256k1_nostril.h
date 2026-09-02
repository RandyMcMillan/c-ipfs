#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <secp256k1.h>
#include <secp256k1_schnorrsig.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/sha.h>
#include <openssl/param_build.h>

/* Deterministic test secret key (32 bytes) */
static const unsigned char TEST_SECKEY[32] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
};

/* Deterministic 32-byte message hash */
static const unsigned char TEST_MSG[32] = {
    0x31, 0x5f, 0x5b, 0xdb, 0x23, 0xd9, 0x21, 0x1a,
    0x2b, 0x23, 0x7e, 0xcd, 0xa3, 0x3f, 0x1e, 0x86,
    0x1b, 0xe7, 0xd4, 0x93, 0x6e, 0x05, 0x1f, 0x8d,
    0x01, 0x64, 0x48, 0x3d, 0x2d, 0x07, 0x3a, 0xc4
};

/**
 * Initialize a secp256k1 context with verification and signing.
 */
static secp256k1_context *secp256k1_ctx_new(void) {
    secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    if (!ctx) return NULL;

    unsigned char seed[32];
    memcpy(seed, TEST_SECKEY, 32);
    /* Use secret key as deterministic seed for context randomization */
    if (!secp256k1_context_randomize(ctx, seed)) {
        secp256k1_context_destroy(ctx);
        return NULL;
    }
    return ctx;
}

/**
 * Test: libsecp256k1 ECDSA sign/verify roundtrip.
 */
int test_secp256k1_nostril_ecdsa_roundtrip(void) {
    int retVal = 0;
    secp256k1_context *ctx = secp256k1_ctx_new();
    if (!ctx) {
        fprintf(stderr, "failed to create secp256k1 context\n");
        goto exit;
    }

    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_create(ctx, &pubkey, TEST_SECKEY)) {
        fprintf(stderr, "failed to derive public key\n");
        goto cleanup_ctx;
    }

    secp256k1_ecdsa_signature sig;
    if (!secp256k1_ecdsa_sign(ctx, &sig, TEST_MSG, TEST_SECKEY, NULL, NULL)) {
        fprintf(stderr, "failed to create ECDSA signature\n");
        goto cleanup_ctx;
    }

    if (secp256k1_ecdsa_verify(ctx, &sig, TEST_MSG, &pubkey) != 1) {
        fprintf(stderr, "ECDSA verification failed\n");
        goto cleanup_ctx;
    }

    retVal = 1;
cleanup_ctx:
    secp256k1_context_destroy(ctx);
exit:
    return retVal;
}

/**
 * Test: libsecp256k1 BIP-340 Schnorr sign/verify roundtrip.
 */
int test_secp256k1_nostril_schnorr_roundtrip(void) {
    int retVal = 0;
    secp256k1_context *ctx = secp256k1_ctx_new();
    if (!ctx) {
        fprintf(stderr, "failed to create secp256k1 context\n");
        goto exit;
    }

    secp256k1_keypair keypair;
    if (!secp256k1_keypair_create(ctx, &keypair, TEST_SECKEY)) {
        fprintf(stderr, "failed to create keypair\n");
        goto cleanup_ctx;
    }

    secp256k1_xonly_pubkey xonly_pubkey;
    if (!secp256k1_keypair_xonly_pub(ctx, &xonly_pubkey, NULL, &keypair)) {
        fprintf(stderr, "failed to derive x-only public key\n");
        goto cleanup_ctx;
    }

    unsigned char schnorr_sig[64];
    unsigned char aux_rand[32];
    memcpy(aux_rand, TEST_MSG, 32); /* deterministic aux_rand for tests */

    if (!secp256k1_schnorrsig_sign32(ctx, schnorr_sig, TEST_MSG, &keypair, aux_rand)) {
        fprintf(stderr, "failed to create Schnorr signature\n");
        goto cleanup_ctx;
    }

    if (secp256k1_schnorrsig_verify(ctx, schnorr_sig, TEST_MSG, 32, &xonly_pubkey) != 1) {
        fprintf(stderr, "Schnorr verification failed\n");
        goto cleanup_ctx;
    }

    retVal = 1;
cleanup_ctx:
    secp256k1_context_destroy(ctx);
exit:
    return retVal;
}

/**
 * Test: Cross-verify libsecp256k1 ECDSA signature with OpenSSL.
 * Uses a real message, hashes it with SHA-256 for libsecp256k1 signing,
 * and verifies the original message with OpenSSL EVP (which hashes again).
 */
int test_secp256k1_nostril_openssl_cross_verify(void) {
    int retVal = 0;
    const char *message = "cross-verify message";
    size_t message_len = strlen(message);

    /* Hash the message for libsecp256k1 (which expects a 32-byte digest) */
    unsigned char msg_hash[32];
    SHA256((const unsigned char *)message, message_len, msg_hash);

    secp256k1_context *ctx = secp256k1_ctx_new();
    if (!ctx) {
        fprintf(stderr, "failed to create secp256k1 context\n");
        goto exit;
    }

    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_create(ctx, &pubkey, TEST_SECKEY)) {
        fprintf(stderr, "failed to derive public key\n");
        goto cleanup_ctx;
    }

    /* Serialize compressed pubkey (33 bytes) */
    unsigned char compressed_pubkey[33];
    size_t pubkey_len = sizeof(compressed_pubkey);
    secp256k1_ec_pubkey_serialize(ctx, compressed_pubkey, &pubkey_len, &pubkey, SECP256K1_EC_COMPRESSED);

    /* Sign the SHA-256 hash with libsecp256k1 */
    secp256k1_ecdsa_signature sig;
    if (!secp256k1_ecdsa_sign(ctx, &sig, msg_hash, TEST_SECKEY, NULL, NULL)) {
        fprintf(stderr, "failed to create ECDSA signature\n");
        goto cleanup_ctx;
    }

    /* Serialize to DER for OpenSSL */
    unsigned char der_sig[72];
    size_t der_len = sizeof(der_sig);
    if (!secp256k1_ecdsa_signature_serialize_der(ctx, der_sig, &der_len, &sig)) {
        fprintf(stderr, "failed to serialize DER signature\n");
        goto cleanup_ctx;
    }

    /* Verify with OpenSSL (hashes message internally with SHA-256) */
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (!pctx) {
        fprintf(stderr, "failed to create OpenSSL PKEY_CTX\n");
        goto cleanup_ctx;
    }

    EVP_PKEY *pkey = NULL;
    int openssl_ok = 0;

    OSSL_PARAM_BLD *bld = OSSL_PARAM_BLD_new();
    if (bld) {
        OSSL_PARAM_BLD_push_utf8_string(bld, "group", "secp256k1", 0);
        OSSL_PARAM_BLD_push_octet_string(bld, "pub", compressed_pubkey, pubkey_len);
        OSSL_PARAM *params = OSSL_PARAM_BLD_to_param(bld);

        if (EVP_PKEY_fromdata_init(pctx) == 1) {
            EVP_PKEY_fromdata(pctx, &pkey, EVP_PKEY_PUBLIC_KEY, params);
        }
        OSSL_PARAM_free(params);
        OSSL_PARAM_BLD_free(bld);
    }

    if (pkey) {
        EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
        if (EVP_DigestVerifyInit(md_ctx, NULL, EVP_sha256(), NULL, pkey) == 1) {
            if (EVP_DigestVerifyUpdate(md_ctx, message, message_len) == 1) {
                if (EVP_DigestVerifyFinal(md_ctx, der_sig, der_len) == 1) {
                    openssl_ok = 1;
                }
            }
        }
        EVP_MD_CTX_free(md_ctx);
        EVP_PKEY_free(pkey);
    }

    EVP_PKEY_CTX_free(pctx);
    secp256k1_context_destroy(ctx);

    if (!openssl_ok) {
        fprintf(stderr, "OpenSSL cross-verification of libsecp256k1 ECDSA signature failed\n");
        return 0;
    }

    return 1;

cleanup_ctx:
    secp256k1_context_destroy(ctx);
exit:
    return retVal;
}

/* ============================================================================
 * BIP-340 Official Test Vectors
 * Source: https://github.com/bitcoin/bips/blob/master/bip-0340/test-vectors.csv
 * ============================================================================ */

static int hex_to_bin_vec(const char *hex, unsigned char *out, size_t max_len) {
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0 || hex_len / 2 > max_len) return 0;
    for (size_t i = 0; i < hex_len / 2; i++) {
        sscanf(hex + (i * 2), "%02hhx", &out[i]);
    }
    return hex_len / 2;
}

/**
 * Test: BIP-340 official verification vectors using nostril/libsecp256k1.
 */
int test_secp256k1_nostril_bip340_vectors(void) {
    int retVal = 0;
    secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    if (!ctx) {
        fprintf(stderr, "failed to create secp256k1 context\n");
        return 0;
    }

    /* Vector 0: all-zeros message, simple key, expected TRUE */
    {
        const char *pk_hex = "F9308A019258C31049344F85F89D5229B531C845836F99B08601F113BCE036F9";
        const char *msg_hex = "0000000000000000000000000000000000000000000000000000000000000000";
        const char *sig_hex = "E907831F80848D1069A5371B402410364BDF1C5F8307B0084C55F1CE2DCA821525F66A4A85EA8B71E482A74F382D2CE5EBEEE8FDB2172F477DF4900D310536C0";
        unsigned char pk[32], msg[32], sig[64];
        if (hex_to_bin_vec(pk_hex, pk, 32) != 32 ||
            hex_to_bin_vec(msg_hex, msg, 32) != 32 ||
            hex_to_bin_vec(sig_hex, sig, 64) != 64) {
            fprintf(stderr, "BIP-340 vector 0: hex decode failed\n");
            goto cleanup;
        }
        secp256k1_xonly_pubkey xonly_pk;
        if (!secp256k1_xonly_pubkey_parse(ctx, &xonly_pk, pk)) {
            fprintf(stderr, "BIP-340 vector 0: pubkey parse failed\n");
            goto cleanup;
        }
        if (secp256k1_schnorrsig_verify(ctx, sig, msg, 32, &xonly_pk) != 1) {
            fprintf(stderr, "BIP-340 vector 0: expected TRUE but verification failed\n");
            goto cleanup;
        }
    }

    /* Vector 1: standard test vector, expected TRUE */
    {
        const char *pk_hex = "DFF1D77F2A671C5F36183726DB2341BE58FEAE1DA2DECED843240F7B502BA659";
        const char *msg_hex = "243F6A8885A308D313198A2E03707344A4093822299F31D0082EFA98EC4E6C89";
        const char *sig_hex = "6896BD60EEAE296DB48A229FF71DFE071BDE413E6D43F917DC8DCF8C78DE33418906D11AC976ABCCB20B091292BFF4EA897EFCB639EA871CFA95F6DE339E4B0A";
        unsigned char pk[32], msg[32], sig[64];
        if (hex_to_bin_vec(pk_hex, pk, 32) != 32 ||
            hex_to_bin_vec(msg_hex, msg, 32) != 32 ||
            hex_to_bin_vec(sig_hex, sig, 64) != 64) {
            fprintf(stderr, "BIP-340 vector 1: hex decode failed\n");
            goto cleanup;
        }
        secp256k1_xonly_pubkey xonly_pk;
        if (!secp256k1_xonly_pubkey_parse(ctx, &xonly_pk, pk)) {
            fprintf(stderr, "BIP-340 vector 1: pubkey parse failed\n");
            goto cleanup;
        }
        if (secp256k1_schnorrsig_verify(ctx, sig, msg, 32, &xonly_pk) != 1) {
            fprintf(stderr, "BIP-340 vector 1: expected TRUE but verification failed\n");
            goto cleanup;
        }
    }

    /* Vector 5: public key not on curve, expected FALSE */
    {
        const char *pk_hex = "EEFDEA4CDB677750A420FEE807EACF21EB9898AE79B9768766E4FAA04A2D4A34";
        const char *msg_hex = "243F6A8885A308D313198A2E03707344A4093822299F31D0082EFA98EC4E6C89";
        const char *sig_hex = "6CFF5C3BA86C69EA4B7376F31A9BCB4F74C1976089B2D9963DA2E5543E17776969E89B4C5564D00349106B8497785DD7D1D713A8AE82B32FA79D5F7FC407D39B";
        unsigned char pk[32], msg[32], sig[64];
        if (hex_to_bin_vec(pk_hex, pk, 32) != 32 ||
            hex_to_bin_vec(msg_hex, msg, 32) != 32 ||
            hex_to_bin_vec(sig_hex, sig, 64) != 64) {
            fprintf(stderr, "BIP-340 vector 5: hex decode failed\n");
            goto cleanup;
        }
        secp256k1_xonly_pubkey xonly_pk;
        if (!secp256k1_xonly_pubkey_parse(ctx, &xonly_pk, pk)) {
            /* pubkey parse may fail for invalid keys; that's acceptable for FALSE vectors */
        } else if (secp256k1_schnorrsig_verify(ctx, sig, msg, 32, &xonly_pk) == 1) {
            fprintf(stderr, "BIP-340 vector 5: expected FALSE but verification succeeded\n");
            goto cleanup;
        }
    }

    /* Vector 15: empty message, expected TRUE */
    {
        const char *pk_hex = "778CAA53B4393AC467774D09497A87224BF9FAB6F6E68B23086497324D6FD117";
        const char *sig_hex = "71535DB165ECD9FBBC046E5FFAEA61186BB6AD436732FCCC25291A55895464CF6069CE26BF03466228F19A3A62DB8A649F2D560FAC652827D1AF0574E427AB63";
        unsigned char pk[32], sig[64];
        if (hex_to_bin_vec(pk_hex, pk, 32) != 32 ||
            hex_to_bin_vec(sig_hex, sig, 64) != 64) {
            fprintf(stderr, "BIP-340 vector 15: hex decode failed\n");
            goto cleanup;
        }
        secp256k1_xonly_pubkey xonly_pk;
        if (!secp256k1_xonly_pubkey_parse(ctx, &xonly_pk, pk)) {
            fprintf(stderr, "BIP-340 vector 15: pubkey parse failed\n");
            goto cleanup;
        }
        if (secp256k1_schnorrsig_verify(ctx, sig, NULL, 0, &xonly_pk) != 1) {
            fprintf(stderr, "BIP-340 vector 15: expected TRUE but verification failed\n");
            goto cleanup;
        }
    }

    retVal = 1;
cleanup:
    secp256k1_context_destroy(ctx);
    return retVal;
}
