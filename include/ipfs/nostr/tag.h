#pragma once

/**
 * Nostr tag handling.
 * Tags are arrays of strings attached to events.
 */

#define NOSTR_MAX_TAGS       256
#define NOSTR_MAX_TAG_ELEMS  16
#define NOSTR_TAG_STR_LEN    512

struct NostrTag {
	char elems[NOSTR_MAX_TAG_ELEMS][NOSTR_TAG_STR_LEN];
	int num_elems;
};

struct NostrTags {
	struct NostrTag tags[NOSTR_MAX_TAGS];
	int num_tags;
};

void nostr_tags_init(struct NostrTags *tags);
int  nostr_tags_add(struct NostrTags *tags, const char *key, const char *val);
int  nostr_tags_add_n(struct NostrTags *tags, int n, ...);
int  nostr_tags_add_cid(struct NostrTags *tags, const char *cid);
int  nostr_tags_add_pubkey(struct NostrTags *tags, const char *pubkey_hex);
int  nostr_tags_add_event_ref(struct NostrTags *tags, const char *event_id_hex);

int  nostr_tags_to_json(struct NostrTags *tags, char *buf, size_t buflen);
