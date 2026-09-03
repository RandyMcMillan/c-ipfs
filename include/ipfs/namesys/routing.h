#ifndef IPNS_NAMESYS_ROUTING_H
    #define IPNS_NAMESYS_ROUTING_H

    #include "mh/multihash.h"
    #include "ipfs/util/time.h"
    #include "ipfs/namesys/pb.h"

    #define DefaultResolverCacheTTL 60 // a minute

    struct cacheEntry {
        char *key;
        char *value;
        struct timespec eol;
    };

    struct routingResolver {
        int cachesize;
        int next;
        struct cacheEntry **data;
    };

    struct libp2p_routing_value_store { // dummy declaration, not implemented yet.
        void *missing;
    };

    char* ipfs_routing_cache_get (char *key, struct ipns_entry *ientry);
    void ipfs_routing_cache_set (char *key, char *value, struct ipns_entry *ientry);
    struct routingResolver* ipfs_namesys_new_routing_resolver (struct libp2p_routing_value_store *route, int cachesize);
    // ipfs_namesys_routing_resolve implements Resolver.
    int ipfs_namesys_routing_resolve (char **path, char *name, struct namesys_pb *pb);
    // ipfs_namesys_routing_resolve_n implements Resolver.
    int ipfs_namesys_routing_resolve_n (char **path, char *name, int depth, struct namesys_pb *pb);
    // ipfs_namesys_routing_resolve_once implements resolver. Uses the IPFS
    // routing system to resolve SFS-like names.
    int ipfs_namesys_routing_resolve_once (char **path, char *name, int depth, char *prefix, struct namesys_pb *pb);
    int ipfs_namesys_routing_check_EOL (struct timespec *ts, struct namesys_pb *pb);

    int ipfs_namesys_routing_get_value (char*, char*);
    int ipfs_namesys_routing_getpublic_key (unsigned char *out_pubkey, size_t *out_len, unsigned char* multihash, size_t multihash_size);
#endif // IPNS_NAMESYS_ROUTING_H
