#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "ipfs/nostr/tag.h"

void ipfs_nostr_tags_init(struct NostrTags *tags) {
    tags->num_tags = 0;
}

int ipfs_nostr_tags_add(struct NostrTags *tags, const char *key, const char *val) {
    if (tags->num_tags >= NOSTR_MAX_TAGS) return 0;
    struct NostrTag *t = &tags->tags[tags->num_tags++];
    t->num_elems = 0;
    strncpy(t->elems[t->num_elems++], key, NOSTR_TAG_STR_LEN - 1);
    t->elems[0][NOSTR_TAG_STR_LEN - 1] = '\0';
    if (val) {
        strncpy(t->elems[t->num_elems++], val, NOSTR_TAG_STR_LEN - 1);
        t->elems[1][NOSTR_TAG_STR_LEN - 1] = '\0';
    }
    return 1;
}

int ipfs_nostr_tags_add_n(struct NostrTags *tags, int n, ...) {
    if (tags->num_tags >= NOSTR_MAX_TAGS) return 0;
    if (n < 1 || n > NOSTR_MAX_TAG_ELEMS) return 0;
    struct NostrTag *t = &tags->tags[tags->num_tags++];
    t->num_elems = 0;
    va_list args;
    va_start(args, n);
    for (int i = 0; i < n; i++) {
        const char *s = va_arg(args, const char *);
        if (s) {
            strncpy(t->elems[t->num_elems++], s, NOSTR_TAG_STR_LEN - 1);
            t->elems[t->num_elems - 1][NOSTR_TAG_STR_LEN - 1] = '\0';
        } else {
            t->num_elems++;
        }
    }
    va_end(args);
    return 1;
}

int ipfs_nostr_tags_add_cid(struct NostrTags *tags, const char *cid) {
    return ipfs_nostr_tags_add(tags, "ipfs", cid);
}

int ipfs_nostr_tags_add_pubkey(struct NostrTags *tags, const char *pubkey_hex) {
    return ipfs_nostr_tags_add(tags, "p", pubkey_hex);
}

int ipfs_nostr_tags_add_event_ref(struct NostrTags *tags, const char *event_id_hex) {
    return ipfs_nostr_tags_add(tags, "e", event_id_hex);
}

int ipfs_nostr_tags_to_json(struct NostrTags *tags, char *buf, size_t buflen) {
    size_t pos = 0;
    int need_comma = 0;
    pos += snprintf(buf + pos, buflen - pos, "[");
    for (int i = 0; i < tags->num_tags; i++) {
        if (need_comma) pos += snprintf(buf + pos, buflen - pos, ",");
        need_comma = 1;
        pos += snprintf(buf + pos, buflen - pos, "[");
        for (int j = 0; j < tags->tags[i].num_elems; j++) {
            if (j > 0) pos += snprintf(buf + pos, buflen - pos, ",");
            pos += snprintf(buf + pos, buflen - pos, "\"%s\"", tags->tags[i].elems[j]);
        }
        pos += snprintf(buf + pos, buflen - pos, "]");
    }
    pos += snprintf(buf + pos, buflen - pos, "]");
    return (int)pos;
}
