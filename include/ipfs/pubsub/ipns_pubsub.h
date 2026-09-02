#ifndef __IPFS_PUBSUB_IPNS_PUBSUB_H__
#define __IPFS_PUBSUB_IPNS_PUBSUB_H__

#include <stdint.h>
#include <stddef.h>

#define IPNS_PUB_SUB_TOPIC "/ipfs/namesys/1.0.0"
#define IPNS_TOPIC_PREFIX  "/ipns/"
#define IPNS_PUBSUB_VALUE_MAX     512
#define IPNS_PUBSUB_SIG_MAX       256
#define IPNS_PUBSUB_PUBKEY_MAX    256

/**
 * Wire structure for raw GossipSub messages.
 */
struct IpnsPubsubMessage {
    const uint8_t *from_peer;
    size_t         from_len;
    const uint8_t *data;
    size_t         data_len;
    const char    *topic;
};

/**
 * Decoded IPNS Record representation (matches IpnsEntry protobuf).
 */
struct IpnsPubsubEntry {
    uint8_t  value[IPNS_PUBSUB_VALUE_MAX];
    size_t   value_len;
    uint64_t sequence;
    uint64_t validity_nsec;
    uint8_t  signature[IPNS_PUBSUB_SIG_MAX];
    size_t   sig_len;
    uint8_t  pubkey[IPNS_PUBSUB_PUBKEY_MAX];
    size_t   pubkey_len;
};

/**
 * Parse a raw protobuf-encoded IPNS entry.
 *
 * @param buffer the protobuf bytes
 * @param size the buffer length
 * @param out_entry the decoded entry
 * @returns 1 on success, 0 on failure
 */
int ipfs_pubsub_ipns_entry_parse(const uint8_t *buffer, size_t size, struct IpnsPubsubEntry *out_entry);

/**
 * Validate topic string for IPNS routing.
 *
 * @param topic the topic string
 * @returns 1 if it is an IPNS topic, 0 otherwise
 */
int ipfs_pubsub_ipns_topic_valid(const char *topic);

/**
 * Process an incoming GossipSub message for IPNS.
 *
 * @param msg the raw GossipSub message
 * @returns 1 if processed as IPNS, 0 otherwise
 */
int ipfs_pubsub_ipns_on_message(const struct IpnsPubsubMessage *msg);

#endif
