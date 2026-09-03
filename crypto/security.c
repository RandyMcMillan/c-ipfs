#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "ipfs/crypto/security.h"

bool ipfs_validate_cid(const char *cid) {
    if (!cid) return false;
    size_t len = strlen(cid);
    /* Typical CIDs: v0 is 46 chars (Qm...), v1 base32 is ~59 chars */
    if (len < 40 || len > 128) return false;

    for (size_t i = 0; i < len; i++) {
        char c = cid[i];
        if (!isalnum((unsigned char)c) && c != '_' && c != '-') {
            return false; /* Disallow shell metacharacters and path separators */
        }
    }
    return true;
}

bool ipfs_validate_hex_string(const char *hex_str, size_t expected_len) {
    if (!hex_str) return false;
    if (strlen(hex_str) != expected_len) return false;

    for (size_t i = 0; i < expected_len; i++) {
        if (!isxdigit((unsigned char)hex_str[i])) {
            return false;
        }
    }
    return true;
}

void ipfs_crypto_secure_wipe(void *buf, size_t len) {
    if (!buf || len == 0) return;
    volatile uint8_t *p = (volatile uint8_t *)buf;
    while (len--) {
        *p++ = 0;
    }
}
