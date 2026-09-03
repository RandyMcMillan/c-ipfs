#ifndef TEST_GOSSIPSUB_H
#define TEST_GOSSIPSUB_H

#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* Prototypes from pubsub/gossipsub.c */
struct gossip_graft;
struct gossip_prune;
int gossipsub_encode_graft(const void *graft, uint8_t *out_buf, size_t max_len);
int gossipsub_encode_prune(const void *prune, uint8_t *out_buf, size_t max_len);
int gossipsub_parse_control(const uint8_t *buf, size_t len, void *out_graft, void *out_prune);

int test_gossipsub_graft_roundtrip(void) {
    typedef struct { char topic_id[128]; } graft_t;
    graft_t send = { .topic_id = "/ipns/test_topic" };
    uint8_t buf[512];
    int len = gossipsub_encode_graft(&send, buf, sizeof(buf));
    if (len <= 0) return 0;

    graft_t recv = {0};
    int type = gossipsub_parse_control(buf, (size_t)len, &recv, NULL);
    return type == 1 && strcmp(recv.topic_id, send.topic_id) == 0;
}

int test_gossipsub_prune_roundtrip(void) {
    typedef struct { char topic_id[128]; uint64_t backoff_seconds; } prune_t;
    prune_t send = { .topic_id = "/ipns/test_topic", .backoff_seconds = 60 };
    uint8_t buf[512];
    int len = gossipsub_encode_prune(&send, buf, sizeof(buf));
    if (len <= 0) return 0;

    prune_t recv = {0};
    int type = gossipsub_parse_control(buf, (size_t)len, NULL, &recv);
    return type == 2 && strcmp(recv.topic_id, send.topic_id) == 0 && recv.backoff_seconds == 60;
}

#endif
