#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "ipfs/pubsub/ipns_pubsub.h"

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
 * Parse a raw protobuf-encoded IPNS entry.
 */
int ipfs_pubsub_ipns_entry_parse(const uint8_t *buffer, size_t size, struct IpnsPubsubEntry *out_entry) {
    if (!buffer || !out_entry || size == 0) return 0;
    memset(out_entry, 0, sizeof(struct IpnsPubsubEntry));

    const uint8_t *ptr = buffer;
    size_t remaining = size;

    while (remaining > 0) {
        uint64_t tag = 0;
        if (!read_varint64(&ptr, &remaining, &tag)) return 0;

        uint32_t field_number = (uint32_t)(tag >> 3);
        uint32_t wire_type = (uint32_t)(tag & 0x07);

        if (wire_type == 2) { // Length-delimited
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
                case 5: // pubKey
                    if (field_len < sizeof(out_entry->pubkey)) {
                        memcpy(out_entry->pubkey, ptr, field_len);
                        out_entry->pubkey_len = field_len;
                    }
                    break;
                default:
                    break;
            }
            ptr += field_len;
            remaining -= field_len;
        } else if (wire_type == 0) { // Varint
            uint64_t val = 0;
            if (!read_varint64(&ptr, &remaining, &val)) return 0;

            if (field_number == 3) {
                // validityType (e.g., 0 = EOL)
            } else if (field_number == 4) {
                out_entry->sequence = val;
            } else if (field_number == 6) {
                out_entry->validity_nsec = val;
            }
        } else {
            return 0;
        }
    }

    return 1;
}

/**
 * Validate topic string for IPNS routing.
 */
int ipfs_pubsub_ipns_topic_valid(const char *topic) {
    if (!topic) return 0;
    if (strcmp(topic, IPNS_PUB_SUB_TOPIC) == 0) return 1;
    if (strncmp(topic, IPNS_TOPIC_PREFIX, strlen(IPNS_TOPIC_PREFIX)) == 0) return 1;
    return 0;
}

/**
 * Process an incoming GossipSub message for IPNS.
 */
int ipfs_pubsub_ipns_on_message(const struct IpnsPubsubMessage *msg) {
    if (!msg || !msg->topic) return 0;

    if (!ipfs_pubsub_ipns_topic_valid(msg->topic)) {
        return 0;
    }

    struct IpnsPubsubEntry entry;
    if (!ipfs_pubsub_ipns_entry_parse(msg->data, msg->data_len, &entry)) {
        return 0;
    }

    return 1;
}
