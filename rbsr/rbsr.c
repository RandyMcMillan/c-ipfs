#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs/rbsr.h"

#define FNV_OFFSET_BASIS 0xcbf29ce484222325ULL
#define FNV_PRIME        0x100000001b3ULL

uint64_t rbsr_hash_path(const char *path)
{
    uint64_t hash = FNV_OFFSET_BASIS;
    while (*path) {
        hash ^= (unsigned char)*path++;
        hash *= FNV_PRIME;
    }
    return hash;
}

static int entry_cmp(const void *a, const void *b)
{
    const struct RbsrEntry *ea = a;
    const struct RbsrEntry *eb = b;
    if (ea->key < eb->key) return -1;
    if (ea->key > eb->key) return 1;
    return strcmp(ea->path, eb->path);
}

void rbsr_set_init(struct RbsrSet *set)
{
    set->entries = NULL;
    set->count = 0;
    set->capacity = 0;
}

void rbsr_set_free(struct RbsrSet *set)
{
    free(set->entries);
    set->entries = NULL;
    set->count = 0;
    set->capacity = 0;
}

int rbsr_set_add(struct RbsrSet *set, const char *path, uint64_t hash)
{
    if (set->count >= set->capacity) {
        size_t newcap = set->capacity ? set->capacity * 2 : 64;
        struct RbsrEntry *newent = realloc(set->entries,
                                            newcap * sizeof(struct RbsrEntry));
        if (!newent) return 0;
        set->entries = newent;
        set->capacity = newcap;
    }
    struct RbsrEntry *e = &set->entries[set->count++];
    e->key = rbsr_hash_path(path);
    e->hash = hash;
    strncpy(e->path, path, sizeof(e->path) - 1);
    e->path[sizeof(e->path) - 1] = '\0';
    return 1;
}

void rbsr_set_sort(struct RbsrSet *set)
{
    if (set->count > 1)
        qsort(set->entries, set->count, sizeof(struct RbsrEntry), entry_cmp);
}

struct RbsrFingerprint rbsr_fingerprint(struct RbsrSet *set,
                                         uint64_t min_key,
                                         uint64_t max_key)
{
    struct RbsrFingerprint fp = { min_key, max_key, 0, 0 };
    for (size_t i = 0; i < set->count; i++) {
        struct RbsrEntry *e = &set->entries[i];
        if (e->key >= min_key && e->key <= max_key) {
            fp.count++;
            fp.xor_sum ^= e->hash;
        }
    }
    return fp;
}

static int rbsr_reconcile_internal(struct RbsrSet *local,
                                    struct RbsrFingerprint *remote_fp,
                                    size_t threshold,
                                    struct RbsrEntry **out,
                                    size_t *out_count,
                                    size_t *out_steps)
{
    struct RbsrFingerprint local_fp =
        rbsr_fingerprint(local, remote_fp->min_key, remote_fp->max_key);

    (*out_steps)++;

    if (local_fp.count == remote_fp->count && local_fp.xor_sum == remote_fp->xor_sum) {
        return 1; /* in sync */
    }

    if (local_fp.count <= threshold && remote_fp->count <= threshold) {
        /* Base case: return all local entries in range */
        for (size_t i = 0; i < local->count; i++) {
            struct RbsrEntry *e = &local->entries[i];
            if (e->key >= remote_fp->min_key && e->key <= remote_fp->max_key) {
                struct RbsrEntry *tmp = realloc(*out, (*out_count + 1) * sizeof(struct RbsrEntry));
                if (!tmp) return 0;
                *out = tmp;
                (*out)[(*out_count)++] = *e;
            }
        }
        return 1;
    }

    /* Bisect */
    uint64_t mid = remote_fp->min_key + (remote_fp->max_key - remote_fp->min_key) / 2;

    struct RbsrFingerprint left_fp = { remote_fp->min_key, mid, 0, 0 };
    struct RbsrFingerprint right_fp = { mid + 1, remote_fp->max_key, 0, 0 };

    /* We need remote's fingerprints for left/right. In a real protocol,
     * we'd request them. Here we simulate by computing from local
     * (demonstrates the bisection structure). For actual reconciliation
     * this function should be called iteratively or via async messages. */
    if (!rbsr_reconcile_internal(local, &left_fp, threshold, out, out_count, out_steps))
        return 0;
    if (!rbsr_reconcile_internal(local, &right_fp, threshold, out, out_count, out_steps))
        return 0;

    return 1;
}

int rbsr_reconcile(struct RbsrSet *local,
                    struct RbsrFingerprint *remote_fp,
                    size_t threshold,
                    struct RbsrEntry **out_entries,
                    size_t *out_count,
                    size_t *out_steps)
{
    *out_entries = NULL;
    *out_count = 0;
    *out_steps = 0;
    return rbsr_reconcile_internal(local, remote_fp, threshold,
                                    out_entries, out_count, out_steps);
}
