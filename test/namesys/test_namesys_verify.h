#include <string.h>
#include <stdio.h>

#include "ipfs/namesys/verify.h"

/**
 * Build a minimal IPNS entry protobuf:
 * Field 1 (value): "test-value"
 * Field 2 (signature): 64 zero bytes
 * Field 4 (sequence): 42
 * Field 5 (pubKey): libp2p PublicKey protobuf with Ed25519 type + 32-byte key
 * Field 7 (data): "test-data"
 */
static size_t build_ipns_entry_protobuf(uint8_t *buf, size_t buf_len) {
    size_t offset = 0;

    // Field 1: value = "test-value"
    const char *value = "test-value";
    buf[offset++] = (1 << 3) | 2;
    buf[offset++] = (uint8_t)strlen(value);
    memcpy(&buf[offset], value, strlen(value));
    offset += strlen(value);

    // Field 2: signature = 64 zero bytes
    buf[offset++] = (2 << 3) | 2;
    buf[offset++] = 64;
    memset(&buf[offset], 0, 64);
    offset += 64;

    // Field 4: sequence = 42
    buf[offset++] = (4 << 3) | 0;
    buf[offset++] = 42;

    // Field 5: pubKey = libp2p PublicKey protobuf
    //   KeyType = 1 (Ed25519)
    //   Data = 32 zero bytes
    uint8_t pubkey_pb[36];
    size_t pb_off = 0;
    pubkey_pb[pb_off++] = (1 << 3) | 0; // field 1, varint
    pubkey_pb[pb_off++] = 1; // Ed25519
    pubkey_pb[pb_off++] = (2 << 3) | 2; // field 2, bytes
    pubkey_pb[pb_off++] = 32; // length
    memset(&pubkey_pb[pb_off], 0, 32);
    pb_off += 32;

    buf[offset++] = (5 << 3) | 2;
    buf[offset++] = (uint8_t)pb_off;
    memcpy(&buf[offset], pubkey_pb, pb_off);
    offset += pb_off;

    // Field 7: data = "test-data"
    const char *data = "test-data";
    buf[offset++] = (7 << 3) | 2;
    buf[offset++] = (uint8_t)strlen(data);
    memcpy(&buf[offset], data, strlen(data));
    offset += strlen(data);

    return offset;
}

int test_namesys_verify_entry_parse(void) {
    int retVal = 0;
    uint8_t buf[256];
    struct IpnsVerifyEntry entry;

    size_t len = build_ipns_entry_protobuf(buf, sizeof(buf));

    if (!ipfs_namesys_verify_entry_parse(buf, len, &entry)) {
        fprintf(stderr, "parse failed\n");
        goto exit;
    }

    if (entry.value_len != 10 || memcmp(entry.value, "test-value", 10) != 0) {
        fprintf(stderr, "value mismatch\n");
        goto exit;
    }

    if (entry.sequence != 42) {
        fprintf(stderr, "sequence mismatch: %llu != 42\n", (unsigned long long)entry.sequence);
        goto exit;
    }

    if (entry.sig_len != 64) {
        fprintf(stderr, "sig length mismatch: %zu != 64\n", entry.sig_len);
        goto exit;
    }

    if (entry.key_type != IPFS_KEY_TYPE_ED25519) {
        fprintf(stderr, "key type mismatch: %d != ED25519\n", entry.key_type);
        goto exit;
    }

    if (entry.pubkey_bytes_len != 32) {
        fprintf(stderr, "pubkey bytes length mismatch: %zu != 32\n", entry.pubkey_bytes_len);
        goto exit;
    }

    if (entry.data_payload_len != 9 || memcmp(entry.data_payload, "test-data", 9) != 0) {
        fprintf(stderr, "data payload mismatch\n");
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}

int test_namesys_verify_entry_parse_null(void) {
    int retVal = 0;
    struct IpnsVerifyEntry entry;
    uint8_t buf[4] = {0x0A, 0x02, 0xAB, 0xCD};

    if (ipfs_namesys_verify_entry_parse(NULL, 4, &entry)) {
        fprintf(stderr, "should reject NULL buffer\n");
        goto exit;
    }

    if (ipfs_namesys_verify_entry_parse(buf, 4, NULL)) {
        fprintf(stderr, "should reject NULL out\n");
        goto exit;
    }

    if (ipfs_namesys_verify_entry_parse(buf, 0, &entry)) {
        fprintf(stderr, "should reject zero size\n");
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}

int test_namesys_verify_entry_signature_invalid(void) {
    int retVal = 0;
    uint8_t buf[256];
    struct IpnsVerifyEntry entry;

    size_t len = build_ipns_entry_protobuf(buf, sizeof(buf));

    if (!ipfs_namesys_verify_entry_parse(buf, len, &entry)) {
        fprintf(stderr, "parse failed\n");
        goto exit;
    }

    // Signature is all zeros with a zero key, so verification should fail
    if (ipfs_namesys_verify_entry_signature(&entry)) {
        fprintf(stderr, "should reject invalid signature\n");
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}

int test_namesys_verify_entry_signature_null(void) {
    int retVal = 0;

    if (ipfs_namesys_verify_entry_signature(NULL)) {
        fprintf(stderr, "should reject NULL entry\n");
        goto exit;
    }

    struct IpnsVerifyEntry entry;
    memset(&entry, 0, sizeof(entry));
    if (ipfs_namesys_verify_entry_signature(&entry)) {
        fprintf(stderr, "should reject entry with no sig\n");
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}
