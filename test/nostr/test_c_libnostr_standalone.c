#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "nostr.h"

int main(void) {
    printf("=== c-libnostr standalone smoke test ===\n");

    nostr_error_t err = nostr_init();
    if (err != NOSTR_OK) {
        fprintf(stderr, "FAIL: nostr_init() returned %d\n", err);
        return 1;
    }

    nostr_keypair kp;
    err = nostr_keypair_generate(&kp);
    if (err != NOSTR_OK) {
        fprintf(stderr, "FAIL: nostr_keypair_generate() returned %d\n", err);
        nostr_cleanup();
        return 1;
    }

    nostr_event *ev = NULL;
    err = nostr_event_create(&ev);
    if (err != NOSTR_OK || !ev) {
        fprintf(stderr, "FAIL: nostr_event_create() returned %d\n", err);
        nostr_cleanup();
        return 1;
    }

    ev->kind = 1;
    ev->content = strdup("c-ipfs <-> c-libnostr hybrid protocol test");
    ev->created_at = time(NULL);

    err = nostr_event_sign(ev, &kp.privkey);
    if (err != NOSTR_OK) {
        fprintf(stderr, "FAIL: nostr_event_sign() returned %d\n", err);
        nostr_event_destroy(ev);
        nostr_cleanup();
        return 1;
    }

    err = nostr_event_verify(ev);
    if (err != NOSTR_OK) {
        fprintf(stderr, "FAIL: nostr_event_verify() returned %d\n", err);
        nostr_event_destroy(ev);
        nostr_cleanup();
        return 1;
    }

    /* Note: JSON round-trip skipped — c-libnostr built without cJSON
     * (NOSTR_FEATURE_JSON_ENHANCED=OFF) to avoid an extra dependency.
     * The sign/verify path above exercises the full crypto stack. */

    nostr_event_destroy(ev);
    nostr_cleanup();

    printf("=== c-libnostr standalone smoke test PASSED ===\n");
    return 0;
}
