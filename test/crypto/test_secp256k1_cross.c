/*
 * Standalone ECDSA cross-verification test.
 * Compile against either OpenSSL or BoringSSL:
 *
 *   gcc -o test_secp256k1_cross test_secp256k1_cross.c \
 *       -I../../nostril/deps/secp256k1/include \
 *       -L../../nostril/deps/secp256k1/.libs -lsecp256k1 \
 *       -lssl -lcrypto -lpthread
 *
 * Returns 0 on success, 1 on failure.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <secp256k1.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

/* Deterministic test secret key (32 bytes) */
static const unsigned char TEST_SECKEY[32] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
};

static int hex_to_bin(const char *hex, unsigned char *out, size_t max_len) {
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0 || hex_len / 2 > max_len) return 0;
    for (size_t i = 0; i < hex_len / 2; i++) {
        sscanf(hex + (i * 2), "%02hhx", &out[i]);
    }
    return hex_len / 2;
}

static int ssl_supports_secp256k1(void) {
    EC_KEY *ec_key = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (ec_key) {
        EC_KEY_free(ec_key);
        return 1;
    }
    return 0;
}

static int test_secp256k1_vector(const char *label,
                                  const unsigned char *seckey,
                                  const unsigned char *msg,
                                  size_t msg_len) {
    int ok = 0;
    secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    if (!ctx) {
        fprintf(stderr, "[%s] failed to create secp256k1 context\n", label);
        return 0;
    }

    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_create(ctx, &pubkey, seckey)) {
        fprintf(stderr, "[%s] failed to derive public key\n", label);
        goto cleanup;
    }

    unsigned char compressed_pubkey[33];
    size_t pubkey_len = sizeof(compressed_pubkey);
    secp256k1_ec_pubkey_serialize(ctx, compressed_pubkey, &pubkey_len, &pubkey, SECP256K1_EC_COMPRESSED);

    unsigned char msg_hash[32];
    SHA256(msg, msg_len, msg_hash);

    secp256k1_ecdsa_signature sig;
    if (!secp256k1_ecdsa_sign(ctx, &sig, msg_hash, seckey, NULL, NULL)) {
        fprintf(stderr, "[%s] failed to create ECDSA signature\n", label);
        goto cleanup;
    }

    unsigned char der_sig[72];
    size_t der_len = sizeof(der_sig);
    if (!secp256k1_ecdsa_signature_serialize_der(ctx, der_sig, &der_len, &sig)) {
        fprintf(stderr, "[%s] failed to serialize DER signature\n", label);
        goto cleanup;
    }

    EC_KEY *ec_key = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!ec_key) {
        fprintf(stderr, "[%s] failed to create EC_KEY\n", label);
        goto cleanup;
    }

    const unsigned char *pk_ptr = compressed_pubkey;
    if (!o2i_ECPublicKey(&ec_key, &pk_ptr, (long)pubkey_len)) {
        fprintf(stderr, "[%s] failed to parse compressed pubkey with o2i_ECPublicKey\n", label);
        EC_KEY_free(ec_key);
        goto cleanup;
    }

    EVP_PKEY *pkey = EVP_PKEY_new();
    if (!pkey || EVP_PKEY_set1_EC_KEY(pkey, ec_key) != 1) {
        fprintf(stderr, "[%s] failed to create EVP_PKEY from EC_KEY\n", label);
        if (pkey) EVP_PKEY_free(pkey);
        EC_KEY_free(ec_key);
        goto cleanup;
    }
    EC_KEY_free(ec_key);

    EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        fprintf(stderr, "[%s] failed to create MD_CTX\n", label);
        EVP_PKEY_free(pkey);
        goto cleanup;
    }

    if (EVP_DigestVerifyInit(md_ctx, NULL, EVP_sha256(), NULL, pkey) != 1) {
        fprintf(stderr, "[%s] EVP_DigestVerifyInit failed\n", label);
        goto cleanup_md;
    }

    if (EVP_DigestVerifyUpdate(md_ctx, msg, msg_len) != 1) {
        fprintf(stderr, "[%s] EVP_DigestVerifyUpdate failed\n", label);
        goto cleanup_md;
    }

    if (EVP_DigestVerifyFinal(md_ctx, der_sig, der_len) == 1) {
        ok = 1;
    } else {
        fprintf(stderr, "[%s] EVP_DigestVerifyFinal failed\n", label);
    }

cleanup_md:
    EVP_MD_CTX_free(md_ctx);
    EVP_PKEY_free(pkey);
cleanup:
    secp256k1_context_destroy(ctx);
    return ok;
}

static int test_p256_vector(const char *label, const unsigned char *msg, size_t msg_len) {
    int ok = 0;
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (!pctx) {
        fprintf(stderr, "[%s] failed to create PKEY_CTX\n", label);
        return 0;
    }

    EVP_PKEY *pkey = NULL;
    if (EVP_PKEY_keygen_init(pctx) != 1) {
        fprintf(stderr, "[%s] EVP_PKEY_keygen_init failed\n", label);
        goto cleanup_ctx;
    }

    if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, NID_X9_62_prime256v1) != 1) {
        fprintf(stderr, "[%s] EVP_PKEY_CTX_set_ec_paramgen_curve_nid failed\n", label);
        goto cleanup_ctx;
    }

    if (EVP_PKEY_keygen(pctx, &pkey) != 1) {
        fprintf(stderr, "[%s] EVP_PKEY_keygen failed\n", label);
        goto cleanup_ctx;
    }

    EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        fprintf(stderr, "[%s] failed to create MD_CTX\n", label);
        goto cleanup_key;
    }

    unsigned char sig[256];
    size_t sig_len = sizeof(sig);

    if (EVP_DigestSignInit(md_ctx, NULL, EVP_sha256(), NULL, pkey) != 1) {
        fprintf(stderr, "[%s] EVP_DigestSignInit failed\n", label);
        goto cleanup_md;
    }

    if (EVP_DigestSignUpdate(md_ctx, msg, msg_len) != 1) {
        fprintf(stderr, "[%s] EVP_DigestSignUpdate failed\n", label);
        goto cleanup_md;
    }

    if (EVP_DigestSignFinal(md_ctx, sig, &sig_len) != 1) {
        fprintf(stderr, "[%s] EVP_DigestSignFinal failed\n", label);
        goto cleanup_md;
    }

    /* Verify */
    EVP_MD_CTX_reset(md_ctx);
    if (EVP_DigestVerifyInit(md_ctx, NULL, EVP_sha256(), NULL, pkey) != 1) {
        fprintf(stderr, "[%s] EVP_DigestVerifyInit failed\n", label);
        goto cleanup_md;
    }

    if (EVP_DigestVerifyUpdate(md_ctx, msg, msg_len) != 1) {
        fprintf(stderr, "[%s] EVP_DigestVerifyUpdate failed\n", label);
        goto cleanup_md;
    }

    if (EVP_DigestVerifyFinal(md_ctx, sig, sig_len) == 1) {
        ok = 1;
    } else {
        fprintf(stderr, "[%s] EVP_DigestVerifyFinal failed\n", label);
    }

cleanup_md:
    EVP_MD_CTX_free(md_ctx);
cleanup_key:
    EVP_PKEY_free(pkey);
cleanup_ctx:
    EVP_PKEY_CTX_free(pctx);
    return ok;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    int failures = 0;
    int has_secp256k1 = ssl_supports_secp256k1();

    printf("Linked SSL library %s secp256k1 via EC_KEY API\n",
           has_secp256k1 ? "SUPPORTS" : "DOES NOT SUPPORT");

    if (has_secp256k1) {
        printf("Running secp256k1 cross-verification (libsecp256k1 sign -> SSL verify)...\n");

        const char *message1 = "deterministic test message for secp256k1 cross-verify";
        if (!test_secp256k1_vector("raw_message", TEST_SECKEY,
                                   (const unsigned char *)message1, strlen(message1))) {
            failures++;
        }

        const char *message2 = "cross-verify message for openssl vs boringssl";
        if (!test_secp256k1_vector("variable_message", TEST_SECKEY,
                                   (const unsigned char *)message2, strlen(message2))) {
            failures++;
        }
    } else {
        printf("Skipping secp256k1 tests (curve not available in linked SSL library).\n");
        printf("Running P-256 self-test (SSL sign -> SSL verify)...\n");

        const char *message1 = "p-256 test message for boringssl verification";
        if (!test_p256_vector("p256_raw_message",
                              (const unsigned char *)message1, strlen(message1))) {
            failures++;
        }

        const char *message2 = "cross-verify p-256 message for boringssl";
        if (!test_p256_vector("p256_variable_message",
                              (const unsigned char *)message2, strlen(message2))) {
            failures++;
        }
    }

    if (failures == 0) {
        printf("PASS: all ECDSA verification vectors passed with linked SSL library\n");
        return 0;
    } else {
        printf("FAIL: %d verification vector(s) failed\n", failures);
        return 1;
    }
}
