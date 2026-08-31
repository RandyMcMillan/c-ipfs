#include <stdio.h>
#include <string.h>

#include "ipfs/nostr/pip.h"
#include "ipfs/nostr/kind.h"
#include "ipfs/nostr/tag.h"

int nostr_pip_manifest_create(void *ctx, struct NostrKey *key,
                               struct NostrPipManifest *m,
                               struct NostrEvent *ev)
{
    unsigned char buf[32768];
    char content[4096];

    snprintf(content, sizeof(content),
             "{"
             "\"protocol\":\"nostr-dag-transfer\","
             "\"version\":1,"
             "\"type\":\"manifest\","
             "\"root\":\"%s\","
             "\"sha256\":\"%s\","
             "\"size\":%llu,"
             "\"packets\":%d,"
             "\"depth\":%d,"
             "\"mtu\":%d,"
             "\"encoding\":\"%s\","
             "\"path\":\"%s\""
             "}",
             m->root, m->sha256, (unsigned long long)m->size,
             m->packets, m->depth, m->mtu,
             m->encoding, m->path);

    nostr_event_init(ev);
    ev->kind = NOSTR_KIND_PIP_MANIFEST;
    nostr_event_set_content(ev, content);
    memcpy(ev->pubkey, key->pubkey, 32);
    if (!nostr_event_commit(ev, buf, sizeof(buf)))
        return 0;
    return nostr_event_sign(ctx, key, ev);
}

int nostr_pip_attest_create(void *ctx, struct NostrKey *key,
                             const char *root_id,
                             const char *sha256_hex,
                             const char *manifest_id,
                             struct NostrEvent *ev)
{
    unsigned char buf[32768];
    char content[2048];

    snprintf(content, sizeof(content),
             "{"
             "\"protocol\":\"nostr-dag-transfer\","
             "\"version\":1,"
             "\"type\":\"attest\","
             "\"root_id\":\"%s\","
             "\"sha256\":\"%s\","
             "\"manifest_id\":\"%s\""
             "}",
             root_id, sha256_hex, manifest_id);

    nostr_event_init(ev);
    ev->kind = NOSTR_KIND_PIP_ATTEST;
    nostr_event_set_content(ev, content);
    if (manifest_id)
        nostr_tags_add_event_ref(&ev->tags, manifest_id);
    memcpy(ev->pubkey, key->pubkey, 32);
    if (!nostr_event_commit(ev, buf, sizeof(buf)))
        return 0;
    return nostr_event_sign(ctx, key, ev);
}

int nostr_pip_seal_create(void *ctx, struct NostrKey *key,
                           const char *root_id,
                           const char *sha256_hex,
                           const char **attest_ids, int num_attests,
                           struct NostrEvent *ev)
{
    unsigned char buf[32768];
    char content[8192];
    char *p = content;
    char *end = content + sizeof(content);

    p += snprintf(p, end - p,
                  "{"
                  "\"protocol\":\"nostr-dag-transfer\","
                  "\"version\":1,"
                  "\"type\":\"seal\","
                  "\"root_id\":\"%s\","
                  "\"sha256\":\"%s\","
                  "\"attest_ids\":[",
                  root_id, sha256_hex);

    for (int i = 0; i < num_attests && p < end; i++) {
        if (i > 0) p += snprintf(p, end - p, ",");
        p += snprintf(p, end - p, "\"%s\"", attest_ids[i]);
    }
    p += snprintf(p, end - p, "]}");

    nostr_event_init(ev);
    ev->kind = NOSTR_KIND_PIP_SEAL;
    nostr_event_set_content(ev, content);
    memcpy(ev->pubkey, key->pubkey, 32);
    if (!nostr_event_commit(ev, buf, sizeof(buf)))
        return 0;
    return nostr_event_sign(ctx, key, ev);
}

int nostr_pip_ack_create(void *ctx, struct NostrKey *key,
                          const char *root_id,
                          const char *manifest_id,
                          const int *received, int num_received,
                          const int *missing, int num_missing,
                          int is_nak,
                          struct NostrEvent *ev)
{
    unsigned char buf[32768];
    char content[4096];
    char *p = content;
    char *end = content + sizeof(content);

    p += snprintf(p, end - p,
                  "{"
                  "\"protocol\":\"nostr-dag-transfer\","
                  "\"version\":1,"
                  "\"type\":\"%s\","
                  "\"root_id\":\"%s\","
                  "\"manifest_id\":\"%s\"",
                  is_nak ? "nak" : "ack", root_id, manifest_id);

    if (num_received > 0) {
        p += snprintf(p, end - p, ",\"received\":[");
        for (int i = 0; i < num_received && p < end; i++) {
            if (i > 0) p += snprintf(p, end - p, ",");
            p += snprintf(p, end - p, "%d", received[i]);
        }
        p += snprintf(p, end - p, "]");
    }
    if (num_missing > 0) {
        p += snprintf(p, end - p, ",\"missing\":[");
        for (int i = 0; i < num_missing && p < end; i++) {
            if (i > 0) p += snprintf(p, end - p, ",");
            p += snprintf(p, end - p, "%d", missing[i]);
        }
        p += snprintf(p, end - p, "]");
    }
    p += snprintf(p, end - p, "}");

    nostr_event_init(ev);
    ev->kind = NOSTR_KIND_PIP_ACK;
    nostr_event_set_content(ev, content);
    if (manifest_id)
        nostr_tags_add_event_ref(&ev->tags, manifest_id);
    memcpy(ev->pubkey, key->pubkey, 32);
    if (!nostr_event_commit(ev, buf, sizeof(buf)))
        return 0;
    return nostr_event_sign(ctx, key, ev);
}

int nostr_pip_request_create(void *ctx, struct NostrKey *key,
                              const char *root_id,
                              const char *request_id,
                              struct NostrEvent *ev)
{
    unsigned char buf[32768];
    char content[1024];

    snprintf(content, sizeof(content),
             "{"
             "\"protocol\":\"nostr-dag-transfer\","
             "\"version\":1,"
             "\"type\":\"request\","
             "\"request_id\":\"%s\","
             "\"root_id\":\"%s\""
             "}",
             request_id ? request_id : "req-0", root_id);

    nostr_event_init(ev);
    ev->kind = NOSTR_KIND_PIP_REQUEST;
    nostr_event_set_content(ev, content);
    memcpy(ev->pubkey, key->pubkey, 32);
    if (!nostr_event_commit(ev, buf, sizeof(buf)))
        return 0;
    return nostr_event_sign(ctx, key, ev);
}
