#pragma once

/**
 * PIP (Perfect IP / NIP-PIP) event builders.
 * Kinds 39076-39082 for verified data transfer over Nostr.
 */

#include "ipfs/nostr/event.h"

/* PIP manifest descriptor */
struct NostrPipManifest {
    char root[128];      /* CID or root identifier */
    char sha256[65];     /* lowercase hex SHA-256 of payload */
    uint64_t size;
    int packets;
    int depth;
    int mtu;
    char encoding[32];   /* e.g. "tar.gz", "json" */
    char path[512];      /* repo path / name */
};

/* Create a PIP transfer manifest event (kind 39078) */
int nostr_pip_manifest_create(void *ctx, struct NostrKey *key,
                               struct NostrPipManifest *m,
                               struct NostrEvent *ev);

/* Create a PIP blob attestation event (kind 39080) */
int nostr_pip_attest_create(void *ctx, struct NostrKey *key,
                             const char *root_id,
                             const char *sha256_hex,
                             const char *manifest_id,
                             struct NostrEvent *ev);

/* Create a PIP quorum seal event (kind 39081) */
int nostr_pip_seal_create(void *ctx, struct NostrKey *key,
                           const char *root_id,
                           const char *sha256_hex,
                           const char **attest_ids, int num_attests,
                           struct NostrEvent *ev);

/* Create a PIP ACK/NAK event (kind 39076) */
int nostr_pip_ack_create(void *ctx, struct NostrKey *key,
                          const char *root_id,
                          const char *manifest_id,
                          const int *received, int num_received,
                          const int *missing, int num_missing,
                          int is_nak,
                          struct NostrEvent *ev);

/* Create a PIP transfer request event (kind 39077) */
int nostr_pip_request_create(void *ctx, struct NostrKey *key,
                              const char *root_id,
                              const char *request_id,
                              struct NostrEvent *ev);
