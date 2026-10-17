#ifndef IPFS_TRANSPORT_DNSADDR_RESOLVER_H
#define IPFS_TRANSPORT_DNSADDR_RESOLVER_H

#include <stddef.h>
#include <netdb.h>

#ifdef __cplusplus
extern "C" {
#endif

int ipfs_resolve_dnsaddr(const char *domain, char ***out_resolved_multiaddrs, size_t *out_count);
void ipfs_free_resolved_multiaddrs(char **multiaddrs, size_t count);

/* Test-only hooks for DNS/network functions. Pass NULL to restore defaults. */
void ipfs_dnsaddr_resolver_set_res_query(int (*fn)(const char *dname, int class, int type, unsigned char *answer, int anslen));
void ipfs_dnsaddr_resolver_set_getaddrinfo(int (*fn)(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res));
void ipfs_dnsaddr_resolver_set_freeaddrinfo(void (*fn)(struct addrinfo *res));

#ifdef __cplusplus
}
#endif

#endif /* IPFS_TRANSPORT_DNSADDR_RESOLVER_H */
