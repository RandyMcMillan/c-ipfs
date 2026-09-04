#ifndef IPFS_TRANSPORT_DNSADDR_RESOLVER_H
#define IPFS_TRANSPORT_DNSADDR_RESOLVER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int ipfs_resolve_dnsaddr(const char *domain, char ***out_resolved_multiaddrs, size_t *out_count);
void ipfs_free_resolved_multiaddrs(char **multiaddrs, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* IPFS_TRANSPORT_DNSADDR_RESOLVER_H */
