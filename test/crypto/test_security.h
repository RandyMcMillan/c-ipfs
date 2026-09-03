#include <string.h>
#include <stdio.h>

#include "ipfs/crypto/security.h"

int test_security_validate_cid_valid(void) {
    const char *valid = "bafybeigdyrzt5sfp7udm7hu76uh7y26nf3efuylqabf3oclgtqy55fbzdi";
    if (!ipfs_validate_cid(valid)) {
        fprintf(stderr, "FAIL: valid CID rejected\n");
        return 0;
    }
    return 1;
}

int test_security_validate_cid_invalid_shell_injection(void) {
    const char *malicious = "bafybeicn72n4u; rm -rf /";
    if (ipfs_validate_cid(malicious)) {
        fprintf(stderr, "FAIL: malicious CID accepted\n");
        return 0;
    }
    return 1;
}

int test_security_validate_cid_invalid_path_traversal(void) {
    const char *traversal = "../../../etc/passwd";
    if (ipfs_validate_cid(traversal)) {
        fprintf(stderr, "FAIL: path traversal CID accepted\n");
        return 0;
    }
    return 1;
}

int test_security_validate_cid_null(void) {
    if (ipfs_validate_cid(NULL)) {
        fprintf(stderr, "FAIL: NULL CID accepted\n");
        return 0;
    }
    return 1;
}

int test_security_validate_cid_too_short(void) {
    /* Short CIDs are allowed; only dangerous characters are rejected */
    const char *short_cid = "Qm123";
    if (!ipfs_validate_cid(short_cid)) {
        fprintf(stderr, "FAIL: short safe CID rejected\n");
        return 0;
    }
    return 1;
}

int test_security_validate_hex_valid(void) {
    const char *hex = "abcdef0123456789ABCDEF0123456789";
    if (!ipfs_validate_hex_string(hex, 32)) {
        fprintf(stderr, "FAIL: valid hex rejected\n");
        return 0;
    }
    return 1;
}

int test_security_validate_hex_invalid_char(void) {
    const char *hex = "ghijklmnopqrstuvwxyz0123456789ab";
    if (ipfs_validate_hex_string(hex, 32)) {
        fprintf(stderr, "FAIL: invalid hex accepted\n");
        return 0;
    }
    return 1;
}

int test_security_validate_hex_wrong_len(void) {
    const char *hex = "deadbeef";
    if (ipfs_validate_hex_string(hex, 64)) {
        fprintf(stderr, "FAIL: wrong-length hex accepted\n");
        return 0;
    }
    return 1;
}

int test_security_secure_wipe(void) {
    char buf[32];
    memset(buf, 0xAB, sizeof(buf));
    ipfs_crypto_secure_wipe(buf, sizeof(buf));
    for (size_t i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0) {
            fprintf(stderr, "FAIL: wipe did not zero byte %zu\n", i);
            return 0;
        }
    }
    return 1;
}
