#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ipfs/nostr/nostr_client.h"
#include "ipfs/routing/nostr_hybrid_routing.h"

#define NOSTR_KIND_IPFS_ANNOUNCE 30023

int ipfs_nostr_announce_cid(const char *relay_url, const char *cid, const char *multiaddr, const char *secp256k1_privkey) {
    if (!relay_url || !cid || !multiaddr || !secp256k1_privkey) {
        return -1;
    }

    /* Build minimal JSON event manually (no cJSON dependency in this file) */
    char event_json[1024];
    time_t now = time(NULL);
    snprintf(event_json, sizeof(event_json),
        "{\"kind\":%d,\"created_at\":%ld,\"tags\":[[\"d\",\"%s\"],[\"location\",\"%s\"],[\"proto\",\"ipfs\"]],\"content\":\"IPFS Provider Record for CID: %s at %s\"}",
        NOSTR_KIND_IPFS_ANNOUNCE, (long)now, cid, multiaddr, cid, multiaddr);

    char *signed_event_json = ipfs_nostr_sign_event(event_json, secp256k1_privkey);
    if (!signed_event_json) {
        fprintf(stderr, "[Nostr Routing] Failed to sign CID announcement event\n");
        return -1;
    }

    int rc = ipfs_nostr_relay_publish(relay_url, signed_event_json);
    free(signed_event_json);

    return rc;
}

int ipfs_nostr_resolve_cid(const char *relay_url, const char *cid, char *out_multiaddr, size_t max_len) {
    if (!relay_url || !cid || !out_multiaddr) return -1;

    /* Build REQ filter JSON manually */
    char filter_json[512];
    snprintf(filter_json, sizeof(filter_json),
        "{\"kinds\":[%d],\"#d\":[\"%s\"]}",
        NOSTR_KIND_IPFS_ANNOUNCE, cid);

    char *response_event_json = ipfs_nostr_relay_query_single(relay_url, filter_json);
    if (!response_event_json) {
        return -404; /* Not found on relay */
    }

    /* Naive parser: look for "location" in tags array */
    const char *loc_tag = strstr(response_event_json, "[\"location\",");
    if (loc_tag) {
        const char *val_start = strchr(loc_tag + 12, '"');
        if (val_start) {
            val_start++;
            const char *val_end = strchr(val_start, '"');
            if (val_end) {
                size_t len = (size_t)(val_end - val_start);
                if (len >= max_len) len = max_len - 1;
                strncpy(out_multiaddr, val_start, len);
                out_multiaddr[len] = '\0';
                free(response_event_json);
                return 0;
            }
        }
    }

    free(response_event_json);
    return -404;
}
