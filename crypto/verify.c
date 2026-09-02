#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/param_build.h>

#include "ipfs/crypto/verify.h"

/**
 * Verify an Ed25519 signature via OpenSSL EVP.
 */
int ipfs_crypto_verify_ed25519(const uint8_t *pubkey, size_t pubkey_len,
                                const uint8_t *msg, size_t msg_len,
                                const uint8_t *sig, size_t sig_len) {
    if (!pubkey || !msg || !sig) return 0;
    if (pubkey_len != 32) return 0;

    EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, pubkey, pubkey_len);
    if (!pkey) return 0;

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

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return status;
}

/**
 * Verify a secp256k1 ECDSA signature via OpenSSL EVP.
 */
int ipfs_crypto_verify_secp256k1(const uint8_t *pubkey, size_t pubkey_len,
                                  const uint8_t *msg, size_t msg_len,
                                  const uint8_t *sig, size_t sig_len) {
    if (!pubkey || !msg || !sig) return 0;
    if (pubkey_len != 33) return 0;

    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (!pctx) return 0;

    EVP_PKEY *pkey = NULL;
    int status = 0;

    OSSL_PARAM_BLD *bld = OSSL_PARAM_BLD_new();
    if (bld) {
        OSSL_PARAM_BLD_push_utf8_string(bld, "group", "secp256k1", 0);
        OSSL_PARAM_BLD_push_octet_string(bld, "pub", pubkey, pubkey_len);
        OSSL_PARAM *params = OSSL_PARAM_BLD_to_param(bld);

        if (EVP_PKEY_fromdata_init(pctx) == 1) {
            EVP_PKEY_fromdata(pctx, &pkey, EVP_PKEY_PUBLIC_KEY, params);
        }
        OSSL_PARAM_free(params);
        OSSL_PARAM_BLD_free(bld);
    }

    if (pkey) {
        EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
        if (md_ctx) {
            if (EVP_DigestVerifyInit(md_ctx, NULL, EVP_sha256(), NULL, pkey) == 1) {
                if (EVP_DigestVerifyUpdate(md_ctx, msg, msg_len) == 1) {
                    if (EVP_DigestVerifyFinal(md_ctx, sig, sig_len) == 1) {
                        status = 1;
                    }
                }
            }
            EVP_MD_CTX_free(md_ctx);
        }
        EVP_PKEY_free(pkey);
    }

    EVP_PKEY_CTX_free(pctx);
    return status;
}
