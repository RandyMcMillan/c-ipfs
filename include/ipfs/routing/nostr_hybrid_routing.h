#ifndef IPFS_ROUTING_NOSTR_HYBRID_ROUTING_H
#define IPFS_ROUTING_NOSTR_HYBRID_ROUTING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int ipfs_nostr_announce_cid(const char *relay_url, const char *cid, const char *multiaddr, const char *secp256k1_privkey);
int ipfs_nostr_resolve_cid(const char *relay_url, const char *cid, char *out_multiaddr, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* IPFS_ROUTING_NOSTR_HYBRID_ROUTING_H */
