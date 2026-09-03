#ifndef __IPFS_CRYPTO_SECURITY_H__
#define __IPFS_CRYPTO_SECURITY_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Validate a CID string for safe use in URLs and file paths.
 * Rejects shell metacharacters, control characters, and path separators.
 *
 * @param cid the CID string to validate
 * @returns true if the CID is safe to use, false otherwise
 */
bool ipfs_validate_cid(const char *cid);

/**
 * Validate a hex string has exactly expected_len characters and all are hex digits.
 *
 * @param hex_str the hex string to validate
 * @param expected_len the expected length in characters
 * @returns true if valid, false otherwise
 */
bool ipfs_validate_hex_string(const char *hex_str, size_t expected_len);

/**
 * Securely wipe sensitive memory to prevent leakage.
 * Uses a volatile pointer to prevent compiler optimization.
 *
 * @param buf the buffer to wipe
 * @param len the number of bytes to wipe
 */
void ipfs_crypto_secure_wipe(void *buf, size_t len);

#endif
