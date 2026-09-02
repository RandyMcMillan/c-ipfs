#ifndef __IPFS_NAMESYS_VERIFY_H__
#define __IPFS_NAMESYS_VERIFY_H__

#include <stdint.h>
#include <stddef.h>

#define IPNS_SIG_PREFIX "ipns-signature:"
#define IPNS_SIG_PREFIX_LEN 15

/**
 * libp2p KeyType values.
 */
enum IpfsKeyType {
    IPFS_KEY_TYPE_RSA      = 0,
    IPFS_KEY_TYPE_ED25519  = 1,
    IPFS_KEY_TYPE_SECP256K1 = 2,
    IPFS_KEY_TYPE_ECDSA    = 3,
    IPFS_KEY_TYPE_UNKNOWN  = 255
};

/**
 * Parsed IPNS entry with embedded public key and signature.
 */
struct IpnsVerifyEntry {
    uint8_t  value[512];
    size_t   value_len;
    uint64_t sequence;
    uint64_t validity_nsec;

    // V2 data payload (field 7)
    uint8_t  data_payload[1024];
    size_t   data_payload_len;

    // Signature (field 2)
    uint8_t  signature[256];
    size_t   sig_len;

    // Embedded PublicKey protobuf (field 5)
    uint8_t  pubkey_raw[256];
    size_t   pubkey_raw_len;

    enum IpfsKeyType key_type;
    uint8_t          pubkey_bytes[128];
    size_t           pubkey_bytes_len;
};

/**
 * Parse a raw protobuf-encoded IPNS entry (with V2 data payload support).
 *
 * @param buffer the protobuf bytes
 * @param size the buffer length
 * @param out_entry the decoded entry
 * @returns 1 on success, 0 on failure
 */
int ipfs_namesys_verify_entry_parse(const uint8_t *buffer, size_t size, struct IpnsVerifyEntry *out_entry);

/**
 * Verify the signature of a parsed IPNS entry.
 *
 * The signed payload is: "ipns-signature:" + data_payload
 *
 * @param entry the parsed IPNS entry
 * @returns 1 if the signature is valid, 0 otherwise
 */
int ipfs_namesys_verify_entry_signature(const struct IpnsVerifyEntry *entry);

/**
 * Verify an IPNS record signature from raw components.
 *
 * @param key_type the key type (ED25519 or SECP256K1)
 * @param pubkey the raw public key bytes
 * @param pubkey_len the public key length
 * @param cbor_data the canonical CBOR data payload
 * @param cbor_data_len the data payload length
 * @param sig the signature bytes
 * @param sig_len the signature length
 * @returns 1 if the signature is valid, 0 otherwise
 */
int ipfs_namesys_verify_ipns_record(enum IpfsKeyType key_type,
                                     const uint8_t *pubkey, size_t pubkey_len,
                                     const uint8_t *cbor_data, size_t cbor_data_len,
                                     const uint8_t *sig, size_t sig_len);

#endif
