#include <string.h>
#include <stdio.h>

#include "ipfs/pubsub/ipns_pubsub.h"

/**
 * Helper to build a minimal protobuf-encoded IPNS entry.
 */
static size_t build_ipns_protobuf(uint8_t *buf, size_t buf_len,
                                   const char *value, uint64_t sequence) {
    size_t offset = 0;
    size_t value_len = strlen(value);

    // Field 1 (value), wire type 2
    buf[offset++] = (1 << 3) | 2;
    buf[offset++] = (uint8_t)value_len;
    memcpy(&buf[offset], value, value_len);
    offset += value_len;

    // Field 4 (sequence), wire type 0
    buf[offset++] = (4 << 3) | 0;
    uint64_t seq = sequence;
    do {
        uint8_t byte = seq & 0x7F;
        seq >>= 7;
        if (seq) byte |= 0x80;
        buf[offset++] = byte;
    } while (seq);

    return offset;
}

int test_ipns_pubsub_entry_parse_basic(void) {
    int retVal = 0;
    uint8_t buf[256];
    struct IpnsPubsubEntry entry;
    const char *value = "/ipfs/QmTest";

    size_t len = build_ipns_protobuf(buf, sizeof(buf), value, 42);

    if (!ipfs_pubsub_ipns_entry_parse(buf, len, &entry)) {
        fprintf(stderr, "parse failed\n");
        goto exit;
    }

    if (entry.value_len != strlen(value)) {
        fprintf(stderr, "value length mismatch: %zu != %zu\n", entry.value_len, strlen(value));
        goto exit;
    }

    if (memcmp(entry.value, value, entry.value_len) != 0) {
        fprintf(stderr, "value mismatch\n");
        goto exit;
    }

    if (entry.sequence != 42) {
        fprintf(stderr, "sequence mismatch: %llu != 42\n", (unsigned long long)entry.sequence);
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}

int test_ipns_pubsub_topic_valid(void) {
    int retVal = 0;

    if (!ipfs_pubsub_ipns_topic_valid(IPNS_PUB_SUB_TOPIC)) {
        fprintf(stderr, "should accept standard topic\n");
        goto exit;
    }

    if (!ipfs_pubsub_ipns_topic_valid("/ipns/some-key")) {
        fprintf(stderr, "should accept /ipns/ prefix topic\n");
        goto exit;
    }

    if (ipfs_pubsub_ipns_topic_valid("/some/other/topic")) {
        fprintf(stderr, "should reject non-ipns topic\n");
        goto exit;
    }

    if (ipfs_pubsub_ipns_topic_valid(NULL)) {
        fprintf(stderr, "should reject NULL topic\n");
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}

int test_ipns_pubsub_on_message_valid(void) {
    int retVal = 0;
    uint8_t buf[256];
    const char *value = "/ipfs/QmValid";

    size_t len = build_ipns_protobuf(buf, sizeof(buf), value, 99);

    struct IpnsPubsubMessage msg = {
        .from_peer = (const uint8_t*)"peer1",
        .from_len = 5,
        .data = buf,
        .data_len = len,
        .topic = IPNS_PUB_SUB_TOPIC
    };

    if (!ipfs_pubsub_ipns_on_message(&msg)) {
        fprintf(stderr, "on_message failed for valid msg\n");
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}

int test_ipns_pubsub_on_message_wrong_topic(void) {
    int retVal = 0;
    uint8_t buf[256];
    const char *value = "/ipfs/QmValid";

    size_t len = build_ipns_protobuf(buf, sizeof(buf), value, 1);

    struct IpnsPubsubMessage msg = {
        .from_peer = (const uint8_t*)"peer1",
        .from_len = 5,
        .data = buf,
        .data_len = len,
        .topic = "/some/random/topic"
    };

    if (ipfs_pubsub_ipns_on_message(&msg)) {
        fprintf(stderr, "should reject wrong topic\n");
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}

int test_ipns_pubsub_on_message_corrupt_data(void) {
    int retVal = 0;
    uint8_t bad_data[] = {0xFF, 0xFF, 0xFF};

    struct IpnsPubsubMessage msg = {
        .from_peer = (const uint8_t*)"peer1",
        .from_len = 5,
        .data = bad_data,
        .data_len = sizeof(bad_data),
        .topic = IPNS_PUB_SUB_TOPIC
    };

    if (ipfs_pubsub_ipns_on_message(&msg)) {
        fprintf(stderr, "should reject corrupt data\n");
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}

int test_ipns_pubsub_entry_parse_null(void) {
    int retVal = 0;
    struct IpnsPubsubEntry entry;

    if (ipfs_pubsub_ipns_entry_parse(NULL, 10, &entry)) {
        fprintf(stderr, "should reject NULL buffer\n");
        goto exit;
    }

    if (ipfs_pubsub_ipns_entry_parse((uint8_t*)"x", 1, NULL)) {
        fprintf(stderr, "should reject NULL out\n");
        goto exit;
    }

    if (ipfs_pubsub_ipns_entry_parse((uint8_t*)"x", 0, &entry)) {
        fprintf(stderr, "should reject zero size\n");
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}
