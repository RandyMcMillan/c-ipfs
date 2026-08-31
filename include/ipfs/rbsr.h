#pragma once

/**
 * Range-Based Set Reconciliation (RBSR)
 * Efficiently syncs sets of entries between distributed nodes.
 * Inspired by RBSR for decentralized file systems.
 */

#include <stdint.h>
#include <stddef.h>

struct RbsrEntry {
    uint64_t key;       /* deterministic hash of path */
    uint64_t hash;      /* content hash / commit id prefix */
    char path[256];
};

struct RbsrSet {
    struct RbsrEntry *entries;
    size_t count;
    size_t capacity;
};

struct RbsrFingerprint {
    uint64_t min_key;
    uint64_t max_key;
    size_t count;
    uint64_t xor_sum;
};

/* Lifecycle */
void rbsr_set_init(struct RbsrSet *set);
void rbsr_set_free(struct RbsrSet *set);

/* Building */
int rbsr_set_add(struct RbsrSet *set, const char *path, uint64_t hash);
void rbsr_set_sort(struct RbsrSet *set);

/* Fingerprint a key range [min_key, max_key] (inclusive) */
struct RbsrFingerprint rbsr_fingerprint(struct RbsrSet *set,
                                         uint64_t min_key,
                                         uint64_t max_key);

/* Reconcile local set against remote fingerprint.
 * Returns 1 on success, 0 on alloc failure.
 * out_entries must be freed by caller.
 * threshold: when count <= threshold, return raw entries instead of bisecting. */
int rbsr_reconcile(struct RbsrSet *local,
                    struct RbsrFingerprint *remote_fp,
                    size_t threshold,
                    struct RbsrEntry **out_entries,
                    size_t *out_count,
                    size_t *out_steps);

/* Hash a string to 64-bit key using FNV-1a */
uint64_t rbsr_hash_path(const char *path);
