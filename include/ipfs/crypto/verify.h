#ifndef __IPFS_CRYPTO_VERIFY_H__
#define __IPFS_CRYPTO_VERIFY_H__

#include <stdint.h>
#include <stddef.h>

/**
 * Verify an Ed25519 signature.
 *
 * @param pubkey the raw 32-byte public key
 * @param pubkey_len must be 32
 * @param msg the message that was signed
 * @param msg_len the message length
 * @param sig the signature bytes
 * @param sig_len the signature length
 * @returns 1 on valid signature, 0 otherwise
 */
int ipfs_crypto_verify_ed25519(const uint8_t *pubkey, size_t pubkey_len,
                                const uint8_t *msg, size_t msg_len,
                                const uint8_t *sig, size_t sig_len);

/**
 * Verify a secp256k1 ECDSA signature.
 *
 * Expects a SEC1 compressed public key (33 bytes) and a DER-encoded signature.
 *
 * @param pubkey the raw 33-byte compressed public key
 * @param pubkey_len must be 33
 * @param msg the message that was signed
 * @param msg_len the message length
 * @param sig the DER-encoded ECDSA signature
 * @param sig_len the signature length
 * @returns 1 on valid signature, 0 otherwise
 */
int ipfs_crypto_verify_secp256k1(const uint8_t *pubkey, size_t pubkey_len,
                                  const uint8_t *msg, size_t msg_len,
                                  const uint8_t *sig, size_t sig_len);

#endif
