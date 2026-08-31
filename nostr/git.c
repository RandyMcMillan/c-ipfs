#include <stdio.h>
#include <string.h>

#include "ipfs/nostr/git.h"
#include "ipfs/nostr/kind.h"

int nostr_git_repo_announce(void *ctx, struct NostrKey *key,
                            struct NostrGitRepo *repo,
                            struct NostrEvent *ev)
{
    unsigned char buf[65536];
    nostr_event_init(ev);
    ev->kind = NOSTR_KIND_GIT_REPO;
    nostr_event_set_content(ev, "");

    if (!nostr_tags_add(&ev->tags, "d", repo->repo_id)) return 0;
    if (repo->name[0])
        if (!nostr_tags_add(&ev->tags, "name", repo->name)) return 0;
    if (repo->description[0])
        if (!nostr_tags_add(&ev->tags, "description", repo->description)) return 0;
    if (repo->web[0])
        if (!nostr_tags_add(&ev->tags, "web", repo->web)) return 0;
    if (repo->clone[0])
        if (!nostr_tags_add(&ev->tags, "clone", repo->clone)) return 0;
    if (repo->euc[0])
        if (!nostr_tags_add_n(&ev->tags, 3, "r", repo->euc, "euc")) return 0;

    memcpy(ev->pubkey, key->pubkey, 32);
    if (!nostr_event_commit(ev, buf, sizeof(buf))) return 0;
    return nostr_event_sign(ctx, key, ev);
}

int nostr_git_patch_publish(void *ctx, struct NostrKey *key,
                             struct NostrGitPatch *patch,
                             struct NostrEvent *ev)
{
    unsigned char buf[65536];
    char a_tag[512];
    nostr_event_init(ev);
    ev->kind = NOSTR_KIND_GIT_PATCH;
    nostr_event_set_content(ev, patch->body);

    snprintf(a_tag, sizeof(a_tag), "30617:%s:%s", patch->repo_pubkey, patch->repo_id);
    if (!nostr_tags_add(&ev->tags, "a", a_tag)) return 0;
    if (patch->euc[0])
        if (!nostr_tags_add(&ev->tags, "r", patch->euc)) return 0;

    memcpy(ev->pubkey, key->pubkey, 32);
    if (!nostr_event_commit(ev, buf, sizeof(buf))) return 0;
    return nostr_event_sign(ctx, key, ev);
}

int nostr_git_issue_publish(void *ctx, struct NostrKey *key,
                             struct NostrGitPatch *issue,
                             struct NostrEvent *ev)
{
    unsigned char buf[65536];
    char a_tag[512];
    nostr_event_init(ev);
    ev->kind = NOSTR_KIND_GIT_ISSUE;
    nostr_event_set_content(ev, issue->body);

    snprintf(a_tag, sizeof(a_tag), "30617:%s:%s", issue->repo_pubkey, issue->repo_id);
    if (!nostr_tags_add(&ev->tags, "a", a_tag)) return 0;
    if (issue->subject[0])
        if (!nostr_tags_add(&ev->tags, "subject", issue->subject)) return 0;

    memcpy(ev->pubkey, key->pubkey, 32);
    if (!nostr_event_commit(ev, buf, sizeof(buf))) return 0;
    return nostr_event_sign(ctx, key, ev);
}

int nostr_git_repo_announce_ipfs(void *ctx, struct NostrKey *key,
                                  const char *repo_id,
                                  const char *name,
                                  const char *cid,
                                  const char *euc,
                                  struct NostrEvent *ev)
{
    struct NostrGitRepo repo = {0};
    strncpy(repo.repo_id, repo_id, sizeof(repo.repo_id) - 1);
    strncpy(repo.name, name, sizeof(repo.name) - 1);
    snprintf(repo.clone, sizeof(repo.clone), "ipfs://%s", cid);
    if (euc) strncpy(repo.euc, euc, sizeof(repo.euc) - 1);
    return nostr_git_repo_announce(ctx, key, &repo, ev);
}
