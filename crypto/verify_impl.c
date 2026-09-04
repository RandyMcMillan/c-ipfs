#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <openssl/evp.h>
#include <openssl/sha.h>
#include <secp256k1.h>

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
        return ipfs_crypto_verify_ed25519(pubkey, pubkey_len, data, data_len, sig, sig_len);
    }
    else if (key_type == KEY_TYPE_SECP256K1) {
        return ipfs_crypto_verify_secp256k1(pubkey, pubkey_len, data, data_len, sig, sig_len);
    }

    return 0;
}
