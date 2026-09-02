#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "ipfs/namesys/verify.h"
#include "ipfs/crypto/verify.h"

/**
 * Read a protobuf varint from a buffer.
 */
static int read_varint64(const uint8_t **buf, size_t *len, uint64_t *val) {
    *val = 0;
    uint32_t shift = 0;
    while (*len > 0) {
        uint8_t byte = **buf;
        (*buf)++;
        (*len)--;
        *val |= ((uint64_t)(byte & 0x7F)) << shift;
        if ((byte & 0x80) == 0) return 1;
        shift += 7;
        if (shift >= 64) return 0;
    }
    return 0;
}

/**
 * Parse a libp2p PublicKey protobuf:
 * Field 1: KeyType (varint)
 * Field 2: Data (bytes)
 */
static int parse_libp2p_pubkey(const uint8_t *buf, size_t len, struct IpnsVerifyEntry *entry) {
    const uint8_t *ptr = buf;
    size_t rem = len;

    while (rem > 0) {
        uint64_t tag = 0;
        if (!read_varint64(&ptr, &rem, &tag)) return 0;
        uint32_t field_num = (uint32_t)(tag >> 3);
        uint32_t wire_type = (uint32_t)(tag & 0x07);

        if (wire_type == 0 && field_num == 1) {
            uint64_t ktype = 0;
            if (!read_varint64(&ptr, &rem, &ktype)) return 0;
            entry->key_type = (enum IpfsKeyType)ktype;
        } else if (wire_type == 2 && field_num == 2) {
            uint64_t key_len = 0;
            if (!read_varint64(&ptr, &rem, &key_len)) return 0;
            if (key_len > rem || key_len > sizeof(entry->pubkey_bytes)) return 0;
            memcpy(entry->pubkey_bytes, ptr, key_len);
            entry->pubkey_bytes_len = key_len;
            ptr += key_len;
            rem -= key_len;
        } else {
            return 0;
        }
    }
    return 1;
}

/**
 * Parse a raw protobuf-encoded IPNS entry (with V2 data payload support).
 */
int ipfs_namesys_verify_entry_parse(const uint8_t *buffer, size_t size, struct IpnsVerifyEntry *out_entry) {
    if (!buffer || !out_entry || size == 0) return 0;
    memset(out_entry, 0, sizeof(struct IpnsVerifyEntry));

    const uint8_t *ptr = buffer;
    size_t remaining = size;

    while (remaining > 0) {
        uint64_t tag = 0;
        if (!read_varint64(&ptr, &remaining, &tag)) return 0;

        uint32_t field_number = (uint32_t)(tag >> 3);
        uint32_t wire_type = (uint32_t)(tag & 0x07);

        if (wire_type == 2) {
            uint64_t field_len = 0;
            if (!read_varint64(&ptr, &remaining, &field_len)) return 0;
            if (field_len > remaining) return 0;

            switch (field_number) {
                case 1: // value
                    if (field_len < sizeof(out_entry->value)) {
                        memcpy(out_entry->value, ptr, field_len);
                        out_entry->value_len = field_len;
                    }
                    break;
                case 2: // signature
                    if (field_len < sizeof(out_entry->signature)) {
                        memcpy(out_entry->signature, ptr, field_len);
                        out_entry->sig_len = field_len;
                    }
                    break;
                case 5: // pubKey (Libp2p PublicKey Protobuf envelope)
                    if (field_len < sizeof(out_entry->pubkey_raw)) {
                        memcpy(out_entry->pubkey_raw, ptr, field_len);
                        out_entry->pubkey_raw_len = field_len;
                        parse_libp2p_pubkey(ptr, field_len, out_entry);
                    }
                    break;
                case 7: // data (canonical V2 CBOR payload)
                    if (field_len < sizeof(out_entry->data_payload)) {
                        memcpy(out_entry->data_payload, ptr, field_len);
                        out_entry->data_payload_len = field_len;
                    }
                    break;
                default:
                    break;
            }
            ptr += field_len;
            remaining -= field_len;
        } else if (wire_type == 0) {
            uint64_t val = 0;
            if (!read_varint64(&ptr, &remaining, &val)) return 0;
            if (field_number == 4) out_entry->sequence = val;
            if (field_number == 6) out_entry->validity_nsec = val;
        } else {
            return 0;
        }
    }

    return 1;
}

/**
 * Verify the signature of a parsed IPNS entry.
 */
int ipfs_namesys_verify_entry_signature(const struct IpnsVerifyEntry *entry) {
    if (!entry || entry->sig_len == 0 || entry->data_payload_len == 0) return 0;

    size_t signed_msg_len = IPNS_SIG_PREFIX_LEN + entry->data_payload_len;
    uint8_t *signed_msg = (uint8_t *)malloc(signed_msg_len);
    if (!signed_msg) return 0;

    memcpy(signed_msg, IPNS_SIG_PREFIX, IPNS_SIG_PREFIX_LEN);
    memcpy(signed_msg + IPNS_SIG_PREFIX_LEN, entry->data_payload, entry->data_payload_len);

    int verified = 0;

    switch (entry->key_type) {
        case IPFS_KEY_TYPE_ED25519:
            verified = ipfs_crypto_verify_ed25519(entry->pubkey_bytes, entry->pubkey_bytes_len,
                                                   signed_msg, signed_msg_len,
                                                   entry->signature, entry->sig_len);
            break;
        case IPFS_KEY_TYPE_SECP256K1:
            verified = ipfs_crypto_verify_secp256k1(entry->pubkey_bytes, entry->pubkey_bytes_len,
                                                     signed_msg, signed_msg_len,
                                                     entry->signature, entry->sig_len);
            break;
        default:
            fprintf(stderr, "[IPNS Crypt] Unsupported public key type: %d\n", entry->key_type);
            break;
    }

    free(signed_msg);
    return verified;
}
