#ifndef NAMESYS_H
    #define NAMESYS_H

    #define DefaultDepthLimit 32

    #include "ipfs/util/errs.h"

    typedef struct s_resolvers {
        char *protocol;
        int (*func)(char**, char*);
    } resolvers;

    // Resolver provides path resolution to IPFS
    // It has a pointer to a DAGService, which is uses to resolve nodes.
    typedef struct s_resolver {
        //DAGService DAG;
        //NodeLink *lnk;
        // resolveOnce looks up a name once (without recursion).
        int (*resolveOnce) (char **, char *);
    } resolver;

    // ciPrivKey is an opaque private key handle (backed by c-libp2p-crypto)
    typedef void* ciPrivKey;

    typedef struct s_publishers {
        char *protocol;
        int (*func) (ciPrivKey, char*);
        int (*func_eol) (ciPrivKey, char*, time_t);
    } publishers;

    typedef struct s_mpns {
        resolvers  *resolver;
        publishers *publisher;
    } mpns;

    typedef struct s_tlds {
        char *str;
        int  condition;
    } tlds;

    int ipfs_namesys_resolve(char **path, char *name);
    int ipfs_namesys_resolve_n(char **path, char *name, int depth);
    int ipfs_namesys_resolve_once (char **path, char *name);
    int ipfs_namesys_publish (char *proto, ciPrivKey name, char *value);
    int ipfs_namesys_publish_with_eol (char *proto, ciPrivKey name, char *value, time_t eol);

    int ipfs_proquint_is_proquint(char *str);
    char *ipfs_proquint_encode(char *buf, int size);
    char *ipfs_proquint_decode(char *str);
    int ipfs_proquint_resolve_once (char **p, char *name);

    int ipfs_isdomain_match_string (char *d);
    int ipfs_isdomain_is_icann_tld(char *s);
    int ipfs_isdomain_is_extended_tld (char *s);
    int ipfs_isdomain_is_tld (char *s);
    int ipfs_isdomain_is_domain (char *s);

    typedef struct s_DNSResolver {
        // DNS TXT record lookup function pointer
        int (*lookupTXT) (char ***, char *);
    } DNSResolver;

    int ipfs_dns_resolver_resolve_once (char **path, char *name);
    int ipfs_dns_work_domain (int output, DNSResolver *r, char *name);
    int ipfs_dns_parse_entry (char **path, char *txt);
    int ipfs_dns_try_parse_dns_link(char **path, char *txt);
#endif //NAMESYS_H
