#include <stdlib.h>
#include <string.h>

#include "ipfs/nostr/nostr_client.h"

char *ipfs_nostr_sign_event(void *event_json, const char *secp256k1_privkey) {
    (void)event_json;
    (void)secp256k1_privkey;
    return NULL;
}

int ipfs_nostr_relay_publish(const char *relay_url, const char *signed_event_json) {
    (void)relay_url;
    (void)signed_event_json;
    return -1;
}

char *ipfs_nostr_relay_query_single(const char *relay_url, const char *filter_json) {
    (void)relay_url;
    (void)filter_json;
    return NULL;
}
