#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "ipfs/ipld/dag_cbor.h"

/**
 * Encodes a raw binary CID / Multihash into a DAG-CBOR Tag 42 Byte String.
 *
 * DAG-CBOR Link structure: Tag(42) -> ByteString(0x00 + binary_cid)
 */
int ipfs_ipld_dag_cbor_link_encode(const uint8_t *cid_bytes, size_t cid_len, struct DagCborLink *out) {
    if (!cid_bytes || !out || cid_len == 0 || cid_len > 100) {
        return 0;
    }

    size_t offset = 0;

    // 1. Write CBOR Major Type 6 (Tag), Value 42 -> 0xD8 0x2A
    out->bytes[offset++] = 0xD8;
    out->bytes[offset++] = DAG_CBOR_LINK_TAG;

    // 2. Write CBOR Major Type 2 (Byte String) Header
    // Payload length is (1 byte multibase prefix + cid_len)
    size_t payload_len = cid_len + 1;

    if (payload_len < 24) {
        out->bytes[offset++] = 0x40 | (uint8_t)payload_len;
    } else if (payload_len <= 0xFF) {
        out->bytes[offset++] = 0x58;
        out->bytes[offset++] = (uint8_t)payload_len;
    } else {
        // CIDs beyond 255 bytes are non-standard in DAG-CBOR for this implementation
        return 0;
    }

    // 3. Write Multibase Binary Prefix (0x00)
    out->bytes[offset++] = MULTIBASE_BINARY_PREFIX;

    // 4. Copy binary CID
    memcpy(&out->bytes[offset], cid_bytes, cid_len);
    offset += cid_len;

    out->len = offset;
    return 1;
}

/**
 * Decodes a DAG-CBOR Tag 42 Byte String back into raw CID bytes.
 */
int ipfs_ipld_dag_cbor_link_decode(const struct DagCborLink *link, uint8_t *cid_bytes, size_t *cid_len) {
    if (!link || !cid_bytes || !cid_len || link->len < 4) {
        return 0;
    }

    size_t offset = 0;

    // 1. Verify Tag 42 header: 0xD8 0x2A
    if (link->bytes[offset++] != 0xD8) {
        return 0;
    }
    if (link->bytes[offset++] != DAG_CBOR_LINK_TAG) {
        return 0;
    }

    // 2. Parse Byte String header
    size_t payload_len = 0;
    uint8_t bstr_header = link->bytes[offset++];

    if ((bstr_header & 0xE0) != 0x40) {
        // Not a byte string major type
        return 0;
    }

    uint8_t bstr_info = bstr_header & 0x1F;
    if (bstr_info < 24) {
        payload_len = bstr_info;
    } else if (bstr_info == 24) {
        if (offset >= link->len) {
            return 0;
        }
        payload_len = link->bytes[offset++];
    } else {
        // Only supports up to 1-byte length for now
        return 0;
    }

    if (offset + payload_len > link->len) {
        return 0;
    }

    // 3. Verify Multibase Binary Prefix
    if (link->bytes[offset++] != MULTIBASE_BINARY_PREFIX) {
        return 0;
    }

    // 4. Extract CID bytes
    size_t cid_len_out = payload_len - 1; // subtract multibase prefix byte
    if (cid_len_out == 0 || cid_len_out > 100) {
        return 0;
    }

    if (*cid_len < cid_len_out) {
        return 0;
    }

    memcpy(cid_bytes, &link->bytes[offset], cid_len_out);
    *cid_len = cid_len_out;
    return 1;
}

/**
 * Verifies that a byte sequence is a valid DAG-CBOR Tag 42 link header.
 */
int ipfs_ipld_dag_cbor_link_is_tag42(const uint8_t *bytes, size_t len) {
    if (!bytes || len < 2) {
        return 0;
    }
    return (bytes[0] == 0xD8 && bytes[1] == DAG_CBOR_LINK_TAG);
}
