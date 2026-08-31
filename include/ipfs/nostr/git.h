#pragma once

/**
 * NIP-34: git stuff over nostr.
 * Hybrid protocol: Nostr for discovery, IPFS for storage, git for versioning.
 */

#include "ipfs/nostr/event.h"

/* NIP-34 repo announcement */
struct NostrGitRepo {
    char repo_id[128];       /* d tag - short kebab-case identifier */
    char name[256];          /* human-readable project name */
    char description[1024];  /* brief description */
    char web[512];           /* browse URL */
    char clone[512];         /* git clone URL (can be ipfs://, nostr://, https://) */
    char euc[64];            /* earliest unique commit id */
    char relays[4][512];     /* relay URLs */
    int num_relays;
};

/* NIP-34 patch / issue */
struct NostrGitPatch {
    char repo_pubkey[65];    /* a tag pubkey */
    char repo_id[128];       /* a tag repo id */
    char euc[64];            /* r tag earliest unique commit */
    char subject[256];       /* subject line */
    char body[65536];        /* patch content or issue markdown */
};

/* Create a NIP-34 repo announcement event (kind 30617) */
int nostr_git_repo_announce(void *ctx, struct NostrKey *key,
                            struct NostrGitRepo *repo,
                            struct NostrEvent *ev);

/* Create a NIP-34 patch event (kind 1617) */
int nostr_git_patch_publish(void *ctx, struct NostrKey *key,
                             struct NostrGitPatch *patch,
                             struct NostrEvent *ev);

/* Create a NIP-34 issue event (kind 1621) */
int nostr_git_issue_publish(void *ctx, struct NostrKey *key,
                             struct NostrGitPatch *issue,
                             struct NostrEvent *ev);

/* Create a NIP-34 repo state event (kind 30618) with RBSR fingerprint JSON in content */
int nostr_git_state_publish(void *ctx, struct NostrKey *key,
                             const char *repo_pubkey,
                             const char *repo_id,
                             const char *rbsr_json,
                             struct NostrEvent *ev);

/* Create a hybrid IPFS+git repo announcement (clone via /ipfs/Qm...) */
int nostr_git_repo_announce_ipfs(void *ctx, struct NostrKey *key,
                                  const char *repo_id,
                                  const char *name,
                                  const char *cid,
                                  const char *euc,
                                  struct NostrEvent *ev);
