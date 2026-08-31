#pragma once

/**
 * Nostr event creation and handling.
 * Provides Schnorr signing via secp256k1.
 */

#include <stdint.h>
#include "ipfs/nostr/tag.h"

#define NOSTR_PUBKEY_HEX_LEN  64
#define NOSTR_EVENT_ID_LEN    32
#define NOSTR_SIG_LEN         64
#define NOSTR_SECKEY_LEN      32

struct NostrEvent {
	unsigned char id[NOSTR_EVENT_ID_LEN];
	unsigned char pubkey[NOSTR_EVENT_ID_LEN];
	unsigned char sig[NOSTR_SIG_LEN];

	char content[8192];
	uint64_t created_at;
	int kind;

	struct NostrTags tags;
};

struct NostrKey {
	unsigned char seckey[NOSTR_SECKEY_LEN];
	unsigned char pubkey[NOSTR_EVENT_ID_LEN];
};

/* Context lifecycle */
void* nostr_context_new(void);
void  nostr_context_free(void *ctx);

/* Key management */
int nostr_key_generate(void *ctx, struct NostrKey *key);
int nostr_key_from_hex(void *ctx, const char *hex, struct NostrKey *key);

/* Event lifecycle */
void nostr_event_init(struct NostrEvent *ev);
void nostr_event_set_content(struct NostrEvent *ev, const char *content);
void nostr_event_set_kind(struct NostrEvent *ev, int kind);

/* Event building */
int nostr_event_commit(struct NostrEvent *ev, unsigned char *buf, size_t buflen);
int nostr_event_sign(void *ctx, struct NostrKey *key, struct NostrEvent *ev);
int nostr_event_verify(void *ctx, struct NostrEvent *ev);

/* Serialization */
int nostr_event_to_json(struct NostrEvent *ev, char *buf, size_t buflen);
int nostr_event_to_envelope_json(struct NostrEvent *ev, char *buf, size_t buflen);

/* Pretty printing */
void nostr_event_print(struct NostrEvent *ev);

/* IPFS hybrid helpers */
int nostr_event_make_ipfs_content(void *ctx, struct NostrKey *key,
                                   const char *cid, const char *description,
                                   struct NostrEvent *ev);
