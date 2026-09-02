#include <string.h>
#include <stdio.h>

#include "ipfs/ipld/dag_cbor.h"

int test_ipld_dag_cbor_link_encode_decode(void) {
    int retVal = 0;
    uint8_t mock_cid[34] = {0x12, 0x20};
    struct DagCborLink link;
    uint8_t decoded[128];
    size_t decoded_len = sizeof(decoded);

    memset(&mock_cid[2], 0xAB, 32);

    // Test encode
    if (!ipfs_ipld_dag_cbor_link_encode(mock_cid, sizeof(mock_cid), &link)) {
        fprintf(stderr, "encode failed\n");
        goto exit;
    }

    // Expected length: 2 (tag) + 2 (bstr header for 35 >= 24: 0x58 0x23) + 1 (multibase) + 34 (cid) = 39
    if (link.len != 39) {
        fprintf(stderr, "unexpected encoded length: %zu != 39\n", link.len);
        goto exit;
    }

    // Verify tag 42 header
    if (!ipfs_ipld_dag_cbor_link_is_tag42(link.bytes, link.len)) {
        fprintf(stderr, "is_tag42 failed on valid link\n");
        goto exit;
    }

    // Test decode
    if (!ipfs_ipld_dag_cbor_link_decode(&link, decoded, &decoded_len)) {
        fprintf(stderr, "decode failed\n");
        goto exit;
    }

    if (decoded_len != sizeof(mock_cid)) {
        fprintf(stderr, "decoded length mismatch: %zu != %zu\n", decoded_len, sizeof(mock_cid));
        goto exit;
    }

    if (memcmp(decoded, mock_cid, sizeof(mock_cid)) != 0) {
        fprintf(stderr, "decoded bytes mismatch\n");
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}

int test_ipld_dag_cbor_link_small_cid(void) {
    int retVal = 0;
    uint8_t small_cid[4] = {0x12, 0x20, 0xAB, 0xCD};
    struct DagCborLink link;
    uint8_t decoded[128];
    size_t decoded_len = sizeof(decoded);

    if (!ipfs_ipld_dag_cbor_link_encode(small_cid, sizeof(small_cid), &link)) {
        goto exit;
    }

    if (link.len != 8) { // 2 + 1 + 1 + 4 = 8
        fprintf(stderr, "small cid length mismatch: %zu != 8\n", link.len);
        goto exit;
    }

    if (!ipfs_ipld_dag_cbor_link_decode(&link, decoded, &decoded_len)) {
        goto exit;
    }

    if (decoded_len != 4 || memcmp(decoded, small_cid, 4) != 0) {
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}

int test_ipld_dag_cbor_link_large_cid(void) {
    int retVal = 0;
    uint8_t large_cid[100];
    struct DagCborLink link;
    uint8_t decoded[128];
    size_t decoded_len = sizeof(decoded);

    memset(large_cid, 0x42, sizeof(large_cid));

    if (!ipfs_ipld_dag_cbor_link_encode(large_cid, sizeof(large_cid), &link)) {
        goto exit;
    }

    // 100 + 1 = 101 bytes payload -> needs 2-byte bstr header (0x58 0x65)
    // tag=2, bstr header=2, multibase=1, cid=100 -> total = 105
    if (link.len != 105) {
        fprintf(stderr, "large cid length mismatch: %zu != 105\n", link.len);
        goto exit;
    }

    if (!ipfs_ipld_dag_cbor_link_decode(&link, decoded, &decoded_len)) {
        goto exit;
    }

    if (decoded_len != 100 || memcmp(decoded, large_cid, 100) != 0) {
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}

int test_ipld_dag_cbor_link_invalid_inputs(void) {
    int retVal = 0;
    struct DagCborLink link;
    uint8_t mock_cid[4] = {0x12, 0x20, 0xAB, 0xCD};

    // NULL cid_bytes
    if (ipfs_ipld_dag_cbor_link_encode(NULL, 4, &link)) {
        fprintf(stderr, "should reject NULL cid_bytes\n");
        goto exit;
    }

    // NULL out
    if (ipfs_ipld_dag_cbor_link_encode(mock_cid, 4, NULL)) {
        fprintf(stderr, "should reject NULL out\n");
        goto exit;
    }

    // zero length
    if (ipfs_ipld_dag_cbor_link_encode(mock_cid, 0, &link)) {
        fprintf(stderr, "should reject zero length\n");
        goto exit;
    }

    // oversized
    uint8_t big[200];
    if (ipfs_ipld_dag_cbor_link_encode(big, 200, &link)) {
        fprintf(stderr, "should reject oversized cid\n");
        goto exit;
    }

    // is_tag42 on non-tag42 bytes
    uint8_t bad[] = {0xD8, 0x01};
    if (ipfs_ipld_dag_cbor_link_is_tag42(bad, sizeof(bad))) {
        fprintf(stderr, "should reject non-42 tag\n");
        goto exit;
    }

    // is_tag42 too short
    if (ipfs_ipld_dag_cbor_link_is_tag42(bad, 1)) {
        fprintf(stderr, "should reject short buffer\n");
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}

int test_ipld_dag_cbor_link_decode_corrupt(void) {
    int retVal = 0;
    struct DagCborLink link;
    uint8_t decoded[128];
    size_t decoded_len = sizeof(decoded);

    // Bad tag
    uint8_t bad_tag[] = {0xD8, 0x01, 0x45, 0x00, 0x12, 0x20, 0xAB, 0xCD};
    memcpy(link.bytes, bad_tag, sizeof(bad_tag));
    link.len = sizeof(bad_tag);
    if (ipfs_ipld_dag_cbor_link_decode(&link, decoded, &decoded_len)) {
        fprintf(stderr, "should reject wrong tag\n");
        goto exit;
    }

    // Bad multibase prefix
    uint8_t bad_base[] = {0xD8, 0x2A, 0x45, 0x01, 0x12, 0x20, 0xAB, 0xCD};
    memcpy(link.bytes, bad_base, sizeof(bad_base));
    link.len = sizeof(bad_base);
    if (ipfs_ipld_dag_cbor_link_decode(&link, decoded, &decoded_len)) {
        fprintf(stderr, "should reject wrong multibase prefix\n");
        goto exit;
    }

    // Truncated
    uint8_t truncated[] = {0xD8, 0x2A, 0x45};
    memcpy(link.bytes, truncated, sizeof(truncated));
    link.len = sizeof(truncated);
    if (ipfs_ipld_dag_cbor_link_decode(&link, decoded, &decoded_len)) {
        fprintf(stderr, "should reject truncated data\n");
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}
