#ifndef IPFS_NOSTR_NOSTR_CLIENT_H
#define IPFS_NOSTR_NOSTR_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Stub nostr client for hybrid routing compatibility */
char *ipfs_nostr_sign_event(void *event_json, const char *secp256k1_privkey);
int ipfs_nostr_relay_publish(const char *relay_url, const char *signed_event_json);
char *ipfs_nostr_relay_query_single(const char *relay_url, const char *filter_json);

#ifdef __cplusplus
}
#endif

#endif /* IPFS_NOSTR_NOSTR_CLIENT_H */
