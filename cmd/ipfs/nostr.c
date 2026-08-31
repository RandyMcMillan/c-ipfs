#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <secp256k1.h>
#include <secp256k1_schnorrsig.h>

#include "ipfs/cmd/ipfs/nostr.h"
#include "ipfs/nostr/event.h"
#include "ipfs/nostr/git.h"
#include "ipfs/nostr/kind.h"
#include "ipfs/rbsr.h"
#include "hex.h"

static void print_nostr_help(FILE *out) {
    fprintf(out, "USAGE:\n");
    fprintf(out, "  ipfs nostr <subcommand> [options]\n\n");
    fprintf(out, "SUBCOMMANDS:\n");
    fprintf(out, "  publish --cid <cid> [--content <text>]    Publish IPFS content (kind 1064)\n");
    fprintf(out, "  repo --id <id> --name <name> --cid <cid>  Announce git repo over IPFS (kind 30617)\n");
    fprintf(out, "  state --repo <pubkey:id> [--refs <file>]  Publish repo state with RBSR (kind 30618)\n");
    fprintf(out, "  grasp --relay <url> [--relay <url>...]    Publish grasp relay list (kind 10317)\n");
    fprintf(out, "  verify --event <json>                     Weak-verify event signature\n");
    fprintf(out, "  status --event <id> --status <s>          Set status: open/merged/closed/draft\n");
    fprintf(out, "  patch --repo <pubkey:id> --subject <s>    Publish a git patch (kind 1617)\n");
    fprintf(out, "          --body <text> [--euc <commit>]\n");
    fprintf(out, "  issue --repo <pubkey:id> --subject <s>    Publish an issue (kind 1621)\n");
    fprintf(out, "          --body <text>\n");
    fprintf(out, "\nOPTIONS:\n");
    fprintf(out, "  --seckey <hex>    Use existing 32-byte secret key (instead of generating)\n");
}

static const char* get_arg(int argc, char** argv, const char *flag) {
    for (int i = 2; i < argc - 1; i++) {
        if (strcmp(argv[i], flag) == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

static int has_flag(int argc, char** argv, const char *flag) {
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], flag) == 0) return 1;
    }
    return 0;
}

/* Weak verify: extract id/pubkey/sig from JSON and check schnorr signature */
static int weak_verify_json(void *ctx, const char *json)
{
    const char *id_p = strstr(json, "\"id\":\"");
    const char *pk_p = strstr(json, "\"pubkey\":\"");
    const char *sig_p = strstr(json, "\"sig\":\"");
    if (!id_p || !pk_p || !sig_p) return 0;

    unsigned char id[32], pubkey[32], sig[64];
    if (!hex_decode(id_p + 6, 64, id, 32)) return 0;
    if (!hex_decode(pk_p + 10, 64, pubkey, 32)) return 0;
    if (!hex_decode(sig_p + 7, 128, sig, 64)) return 0;

    secp256k1_xonly_pubkey xonly;
    if (!secp256k1_xonly_pubkey_parse((secp256k1_context*)ctx, &xonly, pubkey)) return 0;
    return secp256k1_schnorrsig_verify((secp256k1_context*)ctx, sig, id, 32, &xonly);
}

int ipfs_nostr(int argc, char** argv) {
    void *ctx;
    struct NostrKey key;
    struct NostrEvent ev;
    char json_buf[65536];
    char seckey_hex[65];
    int ret = 0;

    if (argc < 3) {
        print_nostr_help(stderr);
        return 0;
    }

    ctx = nostr_context_new();
    if (!ctx) {
        fprintf(stderr, "Error: failed to create secp256k1 context\n");
        return 0;
    }

    const char *seckey_arg = get_arg(argc, argv, "--seckey");
    if (seckey_arg) {
        if (!nostr_key_from_hex(ctx, seckey_arg, &key)) {
            fprintf(stderr, "Error: invalid --seckey (expected 64 hex chars)\n");
            nostr_context_free(ctx);
            return 0;
        }
    } else {
        if (!nostr_key_generate(ctx, &key)) {
            fprintf(stderr, "Error: failed to generate key\n");
            nostr_context_free(ctx);
            return 0;
        }
    }
    hex_encode(key.seckey, 32, seckey_hex, sizeof(seckey_hex));
    seckey_hex[64] = '\0';

    const char *subcmd = argv[2];

    if (strcmp(subcmd, "publish") == 0) {
        const char *cid = get_arg(argc, argv, "--cid");
        const char *content = get_arg(argc, argv, "--content");
        if (!cid) {
            fprintf(stderr, "Error: --cid required\n");
            goto cleanup;
        }
        if (!nostr_event_make_ipfs_content(ctx, &key, cid, content, &ev)) {
            fprintf(stderr, "Error: failed to create event\n");
            goto cleanup;
        }
        if (!nostr_event_to_json(&ev, json_buf, sizeof(json_buf))) {
            fprintf(stderr, "Error: failed to serialize event\n");
            goto cleanup;
        }
        printf("%s\n", json_buf);
        ret = 1;
    }
    else if (strcmp(subcmd, "repo") == 0) {
        const char *id = get_arg(argc, argv, "--id");
        const char *name = get_arg(argc, argv, "--name");
        const char *cid = get_arg(argc, argv, "--cid");
        const char *euc = get_arg(argc, argv, "--euc");
        if (!id || !name || !cid) {
            fprintf(stderr, "Error: --id, --name, and --cid required\n");
            goto cleanup;
        }
        if (!nostr_git_repo_announce_ipfs(ctx, &key, id, name, cid, euc, &ev)) {
            fprintf(stderr, "Error: failed to create repo event\n");
            goto cleanup;
        }
        if (!nostr_event_to_json(&ev, json_buf, sizeof(json_buf))) {
            fprintf(stderr, "Error: failed to serialize event\n");
            goto cleanup;
        }
        printf("%s\n", json_buf);
        ret = 1;
    }
    else if (strcmp(subcmd, "state") == 0) {
        const char *repo = get_arg(argc, argv, "--repo");
        const char *refs_file = get_arg(argc, argv, "--refs");
        if (!repo) {
            fprintf(stderr, "Error: --repo required\n");
            goto cleanup;
        }
        const char *colon = strchr(repo, ':');
        if (!colon || (colon - repo) != 64) {
            fprintf(stderr, "Error: --repo must be <64-char-pubkey>:<repo_id>\n");
            goto cleanup;
        }
        char repo_pubkey[65];
        char repo_id[128];
        memcpy(repo_pubkey, repo, 64);
        repo_pubkey[64] = '\0';
        strncpy(repo_id, colon + 1, sizeof(repo_id) - 1);

        char rbsr_json[4096] = "{}";
        if (refs_file) {
            FILE *fp = fopen(refs_file, "r");
            if (!fp) {
                fprintf(stderr, "Error: cannot open %s\n", refs_file);
                goto cleanup;
            }
            struct RbsrSet set;
            rbsr_set_init(&set);
            char line[512];
            while (fgets(line, sizeof(line), fp)) {
                char refname[256];
                char hashhex[128];
                if (sscanf(line, "%255s %127s", refname, hashhex) == 2) {
                    uint64_t hash = 0;
                    size_t hexlen = strlen(hashhex);
                    if (hexlen >= 16) {
                        /* Take first 16 hex chars as 64-bit hash */
                        char tmp[17];
                        memcpy(tmp, hashhex, 16);
                        tmp[16] = '\0';
                        unsigned char bytes[8];
                        if (hex_decode(tmp, 16, bytes, 8)) {
                            for (int i = 0; i < 8; i++) {
                                hash = (hash << 8) | bytes[i];
                            }
                        }
                    }
                    rbsr_set_add(&set, refname, hash);
                }
            }
            fclose(fp);
            rbsr_set_sort(&set);
            struct RbsrFingerprint fp_root = rbsr_fingerprint(&set, 0, UINT64_MAX);
            snprintf(rbsr_json, sizeof(rbsr_json),
                     "{\"rbsr\":{\"count\":%zu,\"xor_sum\":\"%016llx\"}}",
                     fp_root.count, (unsigned long long)fp_root.xor_sum);
            rbsr_set_free(&set);
        }

        if (!nostr_git_state_publish(ctx, &key, repo_pubkey, repo_id, rbsr_json, &ev)) {
            fprintf(stderr, "Error: failed to create state event\n");
            goto cleanup;
        }
        if (!nostr_event_to_json(&ev, json_buf, sizeof(json_buf))) {
            fprintf(stderr, "Error: failed to serialize event\n");
            goto cleanup;
        }
        printf("%s\n", json_buf);
        ret = 1;
    }
    else if (strcmp(subcmd, "grasp") == 0) {
        const char *relays[4];
        int num_relays = 0;
        for (int i = 2; i < argc - 1 && num_relays < 4; i++) {
            if (strcmp(argv[i], "--relay") == 0) {
                relays[num_relays++] = argv[i + 1];
            }
        }
        if (num_relays == 0) {
            fprintf(stderr, "Error: at least one --relay required\n");
            goto cleanup;
        }
        if (!nostr_git_grasp_publish(ctx, &key, relays, num_relays, &ev)) {
            fprintf(stderr, "Error: failed to create grasp event\n");
            goto cleanup;
        }
        if (!nostr_event_to_json(&ev, json_buf, sizeof(json_buf))) {
            fprintf(stderr, "Error: failed to serialize event\n");
            goto cleanup;
        }
        printf("%s\n", json_buf);
        ret = 1;
    }
    else if (strcmp(subcmd, "verify") == 0) {
        const char *event_json = get_arg(argc, argv, "--event");
        if (!event_json) {
            fprintf(stderr, "Error: --event required\n");
            goto cleanup;
        }
        if (weak_verify_json(ctx, event_json)) {
            printf("signature valid\n");
            ret = 1;
        } else {
            printf("signature invalid\n");
        }
    }
    else if (strcmp(subcmd, "status") == 0) {
        const char *event_id = get_arg(argc, argv, "--event");
        const char *status = get_arg(argc, argv, "--status");
        if (!event_id || !status) {
            fprintf(stderr, "Error: --event and --status required\n");
            goto cleanup;
        }
        int status_kind = 0;
        if (strcmp(status, "open") == 0) status_kind = NOSTR_KIND_GIT_STATUS_OPEN;
        else if (strcmp(status, "merged") == 0) status_kind = NOSTR_KIND_GIT_STATUS_MERGED;
        else if (strcmp(status, "closed") == 0) status_kind = NOSTR_KIND_GIT_STATUS_CLOSED;
        else if (strcmp(status, "draft") == 0) status_kind = NOSTR_KIND_GIT_STATUS_DRAFT;
        else {
            fprintf(stderr, "Error: status must be open/merged/closed/draft\n");
            goto cleanup;
        }
        if (!nostr_git_status_publish(ctx, &key, event_id, status_kind, &ev)) {
            fprintf(stderr, "Error: failed to create status event\n");
            goto cleanup;
        }
        if (!nostr_event_to_json(&ev, json_buf, sizeof(json_buf))) {
            fprintf(stderr, "Error: failed to serialize event\n");
            goto cleanup;
        }
        printf("%s\n", json_buf);
        ret = 1;
    }
    else if (strcmp(subcmd, "patch") == 0) {
        const char *repo = get_arg(argc, argv, "--repo");
        const char *subject = get_arg(argc, argv, "--subject");
        const char *body = get_arg(argc, argv, "--body");
        const char *euc = get_arg(argc, argv, "--euc");
        if (!repo || !subject || !body) {
            fprintf(stderr, "Error: --repo, --subject, and --body required\n");
            goto cleanup;
        }
        struct NostrGitPatch patch = {0};
        const char *colon = strchr(repo, ':');
        if (!colon || (colon - repo) != 64) {
            fprintf(stderr, "Error: --repo must be <64-char-pubkey>:<repo_id>\n");
            goto cleanup;
        }
        memcpy(patch.repo_pubkey, repo, 64);
        patch.repo_pubkey[64] = '\0';
        strncpy(patch.repo_id, colon + 1, sizeof(patch.repo_id) - 1);
        strncpy(patch.subject, subject, sizeof(patch.subject) - 1);
        strncpy(patch.body, body, sizeof(patch.body) - 1);
        if (euc) strncpy(patch.euc, euc, sizeof(patch.euc) - 1);
        if (!nostr_git_patch_publish(ctx, &key, &patch, &ev)) {
            fprintf(stderr, "Error: failed to create patch event\n");
            goto cleanup;
        }
        if (!nostr_event_to_json(&ev, json_buf, sizeof(json_buf))) {
            fprintf(stderr, "Error: failed to serialize event\n");
            goto cleanup;
        }
        printf("%s\n", json_buf);
        ret = 1;
    }
    else if (strcmp(subcmd, "issue") == 0) {
        const char *repo = get_arg(argc, argv, "--repo");
        const char *subject = get_arg(argc, argv, "--subject");
        const char *body = get_arg(argc, argv, "--body");
        if (!repo || !subject || !body) {
            fprintf(stderr, "Error: --repo, --subject, and --body required\n");
            goto cleanup;
        }
        struct NostrGitPatch issue = {0};
        const char *colon = strchr(repo, ':');
        if (!colon || (colon - repo) != 64) {
            fprintf(stderr, "Error: --repo must be <64-char-pubkey>:<repo_id>\n");
            goto cleanup;
        }
        memcpy(issue.repo_pubkey, repo, 64);
        issue.repo_pubkey[64] = '\0';
        strncpy(issue.repo_id, colon + 1, sizeof(issue.repo_id) - 1);
        strncpy(issue.subject, subject, sizeof(issue.subject) - 1);
        strncpy(issue.body, body, sizeof(issue.body) - 1);
        if (!nostr_git_issue_publish(ctx, &key, &issue, &ev)) {
            fprintf(stderr, "Error: failed to create issue event\n");
            goto cleanup;
        }
        if (!nostr_event_to_json(&ev, json_buf, sizeof(json_buf))) {
            fprintf(stderr, "Error: failed to serialize event\n");
            goto cleanup;
        }
        printf("%s\n", json_buf);
        ret = 1;
    }
    else {
        print_nostr_help(stderr);
        goto cleanup;
    }

    fprintf(stderr, "Secret key (save to sign future events): %s\n", seckey_hex);

cleanup:
    nostr_context_free(ctx);
    return ret;
}
