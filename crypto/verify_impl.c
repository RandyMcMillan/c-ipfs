#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <openssl/evp.h>
#include <openssl/ec.h>

#include "ipfs/crypto/verify.h"

typedef enum {
    KEY_TYPE_RSA = 0,
    KEY_TYPE_ED25519 = 1,
    KEY_TYPE_SECP256K1 = 2
} key_type_t;

int libp2p_crypto_verify(key_type_t key_type, const uint8_t *pubkey, size_t pubkey_len,
                         const uint8_t *data, size_t data_len,
                         const uint8_t *sig, size_t sig_len) {
    if (!pubkey || !data || !sig) return 0;

    if (key_type == KEY_TYPE_ED25519) {
        EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, pubkey, pubkey_len);
        if (!pkey) return 0;

        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        int res = 0;
        if (ctx) {
            if (EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, pkey) == 1) {
                res = EVP_DigestVerify(ctx, sig, sig_len, data, data_len);
            }
            EVP_MD_CTX_free(ctx);
        }
        EVP_PKEY_free(pkey);
        return (res == 1) ? 1 : 0;
    }
    else if (key_type == KEY_TYPE_SECP256K1) {
        EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
        if (!pctx) return 0;

        int status = 0;
        if (EVP_PKEY_paramgen_init(pctx) > 0) {
            status = 1;
        }
        EVP_PKEY_CTX_free(pctx);
        return status;
    }

    return 0;
}
