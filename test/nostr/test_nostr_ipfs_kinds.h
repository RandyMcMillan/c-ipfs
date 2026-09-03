#include <string.h>
#include <stdio.h>

#include "ipfs/nostr/event.h"
#include "ipfs/nostr/kind.h"

int test_nostr_ipfs_provider_kind(void) {
    void *ctx = nostr_context_new();
    if (!ctx) { fprintf(stderr, "FAIL: context creation\n"); return 0; }

    struct NostrKey key;
    if (!nostr_key_generate(ctx, &key)) {
        nostr_context_free(ctx);
        fprintf(stderr, "FAIL: keygen\n");
        return 0;
    }

    struct NostrEvent ev;
    if (!nostr_event_make_ipfs_provider(ctx, &key, "QmTestCID", "/ip4/127.0.0.1/tcp/4001", &ev)) {
        nostr_context_free(ctx);
        fprintf(stderr, "FAIL: provider event creation\n");
        return 0;
    }

    if (ev.kind != NOSTR_KIND_IPFS_PROVIDER) {
        fprintf(stderr, "FAIL: wrong kind %d != %d\n", ev.kind, NOSTR_KIND_IPFS_PROVIDER);
        nostr_context_free(ctx);
        return 0;
    }

    nostr_context_free(ctx);
    return 1;
}

int test_nostr_ipfs_pin_request_kind(void) {
    void *ctx = nostr_context_new();
    if (!ctx) { fprintf(stderr, "FAIL: context creation\n"); return 0; }

    struct NostrKey key;
    if (!nostr_key_generate(ctx, &key)) {
        nostr_context_free(ctx);
        fprintf(stderr, "FAIL: keygen\n");
        return 0;
    }

    struct NostrEvent ev;
    if (!nostr_event_make_ipfs_pin_request(ctx, &key, "QmTestCID", "wss://relay.example.com", &ev)) {
        nostr_context_free(ctx);
        fprintf(stderr, "FAIL: pin-request event creation\n");
        return 0;
    }

    if (ev.kind != NOSTR_KIND_IPFS_PIN_REQUEST) {
        fprintf(stderr, "FAIL: wrong kind %d != %d\n", ev.kind, NOSTR_KIND_IPFS_PIN_REQUEST);
        nostr_context_free(ctx);
        return 0;
    }

    nostr_context_free(ctx);
    return 1;
}

int test_nostr_ipfs_pin_confirm_kind(void) {
    void *ctx = nostr_context_new();
    if (!ctx) { fprintf(stderr, "FAIL: context creation\n"); return 0; }

    struct NostrKey key;
    if (!nostr_key_generate(ctx, &key)) {
        nostr_context_free(ctx);
        fprintf(stderr, "FAIL: keygen\n");
        return 0;
    }

    struct NostrEvent ev;
    if (!nostr_event_make_ipfs_pin_confirm(ctx, &key, "QmTestCID", 
                                             "17015d6f0de59ec364b88a52f40f9719933a6cf00f5b0184404d635d7efa615b",
                                             &ev)) {
        nostr_context_free(ctx);
        fprintf(stderr, "FAIL: pin-confirm event creation\n");
        return 0;
    }

    if (ev.kind != NOSTR_KIND_IPFS_PIN_CONFIRM) {
        fprintf(stderr, "FAIL: wrong kind %d != %d\n", ev.kind, NOSTR_KIND_IPFS_PIN_CONFIRM);
        nostr_context_free(ctx);
        return 0;
    }

    nostr_context_free(ctx);
    return 1;
}
