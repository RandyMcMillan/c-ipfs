#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <secp256k1.h>
#include <secp256k1_schnorrsig.h>
#include <curl/curl.h>

#include "ipfs/cmd/ipfs/nostr.h"
#include "ipfs/crypto/security.h"
#include "ipfs/nostr/event.h"
#include "ipfs/nostr/git.h"
#include "ipfs/nostr/kind.h"
#include "ipfs/nostr/pip.h"
#include "ipfs/rbsr.h"
#include "hex.h"
#include "sha256.h"

struct sync_download_state {
    FILE *fp;
    struct sha256_ctx sha_ctx;
    size_t total;
};

static size_t sync_download_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    struct sync_download_state *st = (struct sync_download_state *)userdata;
    size_t written = fwrite(ptr, size, nmemb, st->fp);
    sha256_update(&st->sha_ctx, (const unsigned char *)ptr, written * size);
    st->total += written * size;
    return written;
}

static void print_nostr_help(FILE *out) {
    fprintf(out, "USAGE:\n");
    fprintf(out, "  ipfs nostr <subcommand> [options]\n\n");
    fprintf(out, "SUBCOMMANDS:\n");
    fprintf(out, "  note --content <text>                     Publish a text note (kind 1)\n");
    fprintf(out, "  publish --cid <cid> [--content <text>]    Publish IPFS content (kind 1064)\n");
    fprintf(out, "  provider --cid <cid> [--addr <multiaddr>] Announce provider record (kind 1065)\n");
    fprintf(out, "  pin-request --cid <cid> [--relay <url>]   Request remote pin (kind 1066)\n");
    fprintf(out, "  pin-confirm --cid <cid> --request <id>    Confirm remote pin (kind 1067)\n");
    fprintf(out, "  manifest --cid <cid> --sha256 <h> --size <n>  PIP manifest (kind 39078)\n");
    fprintf(out, "            [--path <name>] [--encoding <e>]\n");
    fprintf(out, "  attest --manifest <id> --sha256 <h> --cid <c> PIP attestation (kind 39080)\n");
    fprintf(out, "  sync --cid <cid> [--sha256 <h>]           Fetch CID via gateway and verify\n");
    fprintf(out, "       [--gateway <url>] [--output <path>]\n");
    fprintf(out, "  repo --id <id> --name <name> --cid <cid>  Announce git repo over IPFS (kind 30617)\n");
    fprintf(out, "  state --repo <pubkey:id> [--refs <file>]  Publish repo state with RBSR (kind 30618)\n");
    fprintf(out, "  grasp --relay <url> [--relay <url>...]    Publish grasp relay list (kind 10317)\n");
    fprintf(out, "  verify --event <json>                     Weak-verify event signature\n");
    fprintf(out, "  status --event <id> --status <s>          Set status: open/merged/closed/draft\n");
    fprintf(out, "  test                                      Self-test signing + verification\n");
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

    if (strcmp(subcmd, "note") == 0) {
        const char *content = get_arg(argc, argv, "--content");
        if (!content) {
            fprintf(stderr, "Error: --content required\n");
            goto cleanup;
        }
        nostr_event_init(&ev);
        ev.kind = NOSTR_KIND_TEXT_NOTE;
        nostr_event_set_content(&ev, content);
        memcpy(ev.pubkey, key.pubkey, 32);
        unsigned char nbuf[4096];
        if (!nostr_event_commit(&ev, nbuf, sizeof(nbuf))) {
            fprintf(stderr, "Error: failed to commit event\n");
            goto cleanup;
        }
        if (!nostr_event_sign(ctx, &key, &ev)) {
            fprintf(stderr, "Error: failed to sign event\n");
            goto cleanup;
        }
        if (!nostr_event_to_json(&ev, json_buf, sizeof(json_buf))) {
            fprintf(stderr, "Error: failed to serialize event\n");
            goto cleanup;
        }
        printf("%s\n", json_buf);
        ret = 1;
    }
    else if (strcmp(subcmd, "publish") == 0) {
        const char *cid = get_arg(argc, argv, "--cid");
        const char *content = get_arg(argc, argv, "--content");
        if (!cid) {
            fprintf(stderr, "Error: --cid required\n");
            goto cleanup;
        }
        if (!ipfs_validate_cid(cid)) {
            fprintf(stderr, "SECURITY ERROR: Invalid CID payload format.\n");
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
    else if (strcmp(subcmd, "provider") == 0) {
        const char *cid = get_arg(argc, argv, "--cid");
        const char *addr = get_arg(argc, argv, "--addr");
        if (!cid) {
            fprintf(stderr, "Error: --cid required\n");
            goto cleanup;
        }
        if (!ipfs_validate_cid(cid)) {
            fprintf(stderr, "SECURITY ERROR: Invalid CID payload format.\n");
            goto cleanup;
        }
        if (!nostr_event_make_ipfs_provider(ctx, &key, cid, addr, &ev)) {
            fprintf(stderr, "Error: failed to create provider event\n");
            goto cleanup;
        }
        if (!nostr_event_to_json(&ev, json_buf, sizeof(json_buf))) {
            fprintf(stderr, "Error: failed to serialize event\n");
            goto cleanup;
        }
        printf("%s\n", json_buf);
        ret = 1;
    }
    else if (strcmp(subcmd, "pin-request") == 0) {
        const char *cid = get_arg(argc, argv, "--cid");
        const char *relay = get_arg(argc, argv, "--relay");
        if (!cid) {
            fprintf(stderr, "Error: --cid required\n");
            goto cleanup;
        }
        if (!ipfs_validate_cid(cid)) {
            fprintf(stderr, "SECURITY ERROR: Invalid CID payload format.\n");
            goto cleanup;
        }
        if (!nostr_event_make_ipfs_pin_request(ctx, &key, cid, relay, &ev)) {
            fprintf(stderr, "Error: failed to create pin-request event\n");
            goto cleanup;
        }
        if (!nostr_event_to_json(&ev, json_buf, sizeof(json_buf))) {
            fprintf(stderr, "Error: failed to serialize event\n");
            goto cleanup;
        }
        printf("%s\n", json_buf);
        ret = 1;
    }
    else if (strcmp(subcmd, "pin-confirm") == 0) {
        const char *cid = get_arg(argc, argv, "--cid");
        const char *request_id = get_arg(argc, argv, "--request");
        if (!cid) {
            fprintf(stderr, "Error: --cid required\n");
            goto cleanup;
        }
        if (!ipfs_validate_cid(cid)) {
            fprintf(stderr, "SECURITY ERROR: Invalid CID payload format.\n");
            goto cleanup;
        }
        if (!nostr_event_make_ipfs_pin_confirm(ctx, &key, cid, request_id, &ev)) {
            fprintf(stderr, "Error: failed to create pin-confirm event\n");
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
        if (!ipfs_validate_cid(cid)) {
            fprintf(stderr, "SECURITY ERROR: Invalid CID payload format.\n");
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
    else if (strcmp(subcmd, "test") == 0) {
        int pass = 1;
        printf("=== nostr self-test ===\n");

        /* Test 1: generate key, sign, verify */
        struct NostrKey tkey;
        struct NostrEvent tev;
        if (!nostr_key_generate(ctx, &tkey)) { printf("FAIL: keygen\n"); pass = 0; }
        else {
            nostr_event_init(&tev);
            tev.kind = NOSTR_KIND_TEXT_NOTE;
            strncpy(tev.content, "test", sizeof(tev.content));
            memcpy(tev.pubkey, tkey.pubkey, 32);
            unsigned char tbuf[4096];
            if (!nostr_event_commit(&tev, tbuf, sizeof(tbuf))) { printf("FAIL: commit\n"); pass = 0; }
            else if (!nostr_event_sign(ctx, &tkey, &tev)) { printf("FAIL: sign\n"); pass = 0; }
            else if (!nostr_event_verify(ctx, &tev)) { printf("FAIL: verify\n"); pass = 0; }
            else printf("PASS: sign/verify\n");
        }

        /* Test 2: RBSR fingerprint */
        struct RbsrSet tset;
        rbsr_set_init(&tset);
        rbsr_set_add(&tset, "refs/heads/main", 0xdeadbeef);
        rbsr_set_add(&tset, "refs/heads/dev", 0xcafebabe);
        rbsr_set_sort(&tset);
        struct RbsrFingerprint tfp = rbsr_fingerprint(&tset, 0, UINT64_MAX);
        if (tfp.count == 2) printf("PASS: RBSR fingerprint\n");
        else { printf("FAIL: RBSR fingerprint\n"); pass = 0; }
        rbsr_set_free(&tset);

        printf("=== %s ===\n", pass ? "ALL PASSED" : "SOME FAILED");
        ret = pass;
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
    else if (strcmp(subcmd, "manifest") == 0) {
        const char *cid = get_arg(argc, argv, "--cid");
        const char *sha256_hex = get_arg(argc, argv, "--sha256");
        const char *size_str = get_arg(argc, argv, "--size");
        const char *path = get_arg(argc, argv, "--path");
        const char *encoding = get_arg(argc, argv, "--encoding");
        if (!cid || !sha256_hex || !size_str) {
            fprintf(stderr, "Error: --cid, --sha256, and --size required\n");
            goto cleanup;
        }
        if (!ipfs_validate_cid(cid)) {
            fprintf(stderr, "SECURITY ERROR: Invalid CID payload format.\n");
            goto cleanup;
        }
        if (!ipfs_validate_hex_string(sha256_hex, 64)) {
            fprintf(stderr, "SECURITY ERROR: Invalid SHA-256 hex string.\n");
            goto cleanup;
        }
        struct NostrPipManifest m = {0};
        strncpy(m.root, cid, sizeof(m.root) - 1);
        strncpy(m.sha256, sha256_hex, sizeof(m.sha256) - 1);
        m.size = strtoull(size_str, NULL, 10);
        m.packets = 1;
        m.depth = 0;
        m.mtu = 0;
        strncpy(m.encoding, encoding ? encoding : "tar.gz", sizeof(m.encoding) - 1);
        strncpy(m.path, path ? path : "", sizeof(m.path) - 1);
        if (!nostr_pip_manifest_create(ctx, &key, &m, &ev)) {
            fprintf(stderr, "Error: failed to create manifest event\n");
            goto cleanup;
        }
        if (!nostr_event_to_json(&ev, json_buf, sizeof(json_buf))) {
            fprintf(stderr, "Error: failed to serialize event\n");
            goto cleanup;
        }
        printf("%s\n", json_buf);
        ret = 1;
    }
    else if (strcmp(subcmd, "attest") == 0) {
        const char *manifest_id = get_arg(argc, argv, "--manifest");
        const char *sha256_hex = get_arg(argc, argv, "--sha256");
        const char *root_id = get_arg(argc, argv, "--cid");
        if (!manifest_id || !sha256_hex || !root_id) {
            fprintf(stderr, "Error: --manifest, --sha256, and --cid required\n");
            goto cleanup;
        }
        if (!ipfs_validate_cid(root_id)) {
            fprintf(stderr, "SECURITY ERROR: Invalid CID payload format.\n");
            goto cleanup;
        }
        if (!ipfs_validate_hex_string(sha256_hex, 64)) {
            fprintf(stderr, "SECURITY ERROR: Invalid SHA-256 hex string.\n");
            goto cleanup;
        }
        if (!nostr_pip_attest_create(ctx, &key, root_id, sha256_hex, manifest_id, &ev)) {
            fprintf(stderr, "Error: failed to create attest event\n");
            goto cleanup;
        }
        if (!nostr_event_to_json(&ev, json_buf, sizeof(json_buf))) {
            fprintf(stderr, "Error: failed to serialize event\n");
            goto cleanup;
        }
        printf("%s\n", json_buf);
        ret = 1;
    }
    else if (strcmp(subcmd, "sync") == 0) {
        const char *cid = get_arg(argc, argv, "--cid");
        const char *sha256_expected = get_arg(argc, argv, "--sha256");
        const char *gateway = get_arg(argc, argv, "--gateway");
        const char *output = get_arg(argc, argv, "--output");
        if (!cid) {
            fprintf(stderr, "Error: --cid required\n");
            goto cleanup;
        }
        if (!ipfs_validate_cid(cid)) {
            fprintf(stderr, "SECURITY ERROR: Invalid CID payload format.\n");
            goto cleanup;
        }
        if (sha256_expected && !ipfs_validate_hex_string(sha256_expected, 64)) {
            fprintf(stderr, "SECURITY ERROR: Invalid SHA-256 hex string.\n");
            goto cleanup;
        }

        char url[1024];
        snprintf(url, sizeof(url), "%s/ipfs/%s",
                 gateway ? gateway : "http://127.0.0.1:8080", cid);

        char outpath[512];
        if (output) {
            strncpy(outpath, output, sizeof(outpath) - 1);
            outpath[sizeof(outpath) - 1] = '\0';
        } else {
            snprintf(outpath, sizeof(outpath), "/tmp/c-ipfs-sync-%s.tar.gz", cid);
        }

        FILE *fp = fopen(outpath, "wb");
        if (!fp) {
            fprintf(stderr, "Error: cannot open %s for writing\n", outpath);
            goto cleanup;
        }

        struct sync_download_state st = {0};
        st.fp = fp;
        sha256_init(&st.sha_ctx);

        CURL *curl = curl_easy_init();
        if (!curl) {
            fprintf(stderr, "Error: failed to initialize curl\n");
            fclose(fp);
            goto cleanup;
        }
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sync_download_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &st);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        }
        curl_easy_cleanup(curl);
        fclose(fp);

        if (res != CURLE_OK) {
            fprintf(stderr, "Error: download failed: %s\n", curl_easy_strerror(res));
            goto cleanup;
        }

        if (http_code >= 400) {
            fprintf(stderr, "Error: download failed with HTTP status %ld\n", http_code);
            goto cleanup;
        }

        struct sha256 hash;
        sha256_done(&st.sha_ctx, &hash);
        char hash_hex[65];
        hex_encode(hash.u.u8, 32, hash_hex, sizeof(hash_hex));

        printf("Downloaded %zu bytes to %s\n", st.total, outpath);
        printf("SHA-256: %s\n", hash_hex);

        if (sha256_expected) {
            int match = 1;
            for (int i = 0; i < 64; i++) {
                char a = hash_hex[i];
                char b = sha256_expected[i];
                if (a >= 'A' && a <= 'F') a = a - 'A' + 'a';
                if (b >= 'A' && b <= 'F') b = b - 'A' + 'a';
                if (a != b) { match = 0; break; }
            }
            if (match) {
                printf("VERIFIED: SHA-256 matches expected hash\n");
                ret = 1;
            } else {
                printf("MISMATCH: expected %s\n", sha256_expected);
            }
        } else {
            ret = 1;
        }
    }
    else {
        print_nostr_help(stderr);
        goto cleanup;
    }

    fprintf(stderr, "Secret key (save to sign future events): %s\n", seckey_hex);
    ipfs_crypto_secure_wipe(seckey_hex, sizeof(seckey_hex));

cleanup:
    ipfs_crypto_secure_wipe(key.seckey, sizeof(key.seckey));
    nostr_context_free(ctx);
    return ret;
}
