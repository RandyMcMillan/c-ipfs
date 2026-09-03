#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "ipfs/cid/cid.h"
#include "ipfs/path/path.h"
#include "ipfs/namesys/namesys.h"
#include "ipfs/dnslink/dnslink.h"

/*type LookupTXTFunc func(name string) (txt []string, err error)

// DNSResolver implements a Resolver on DNS domains
type DNSResolver struct {
	lookupTXT LookupTXTFunc
	// TODO: maybe some sort of caching?
	// cache would need a timeout
}

// NewDNSResolver constructs a name resolver using DNS TXT records.
func NewDNSResolver() Resolver {
	return &DNSResolver{lookupTXT: net.LookupTXT}
}

// newDNSResolver constructs a name resolver using DNS TXT records,
// returning a resolver instead of NewDNSResolver's Resolver.
func newDNSResolver() resolver {
	return &DNSResolver{lookupTXT: net.LookupTXT}
}

// Resolve implements Resolver.
func (r *DNSResolver) Resolve(ctx context.Context, name string) (path.Path, error) {
	return r.ResolveN(ctx, name, DefaultDepthLimit)
}

// ResolveN implements Resolver.
func (r *DNSResolver) ResolveN(ctx context.Context, name string, depth int) (path.Path, error) {
	return resolve(ctx, r, name, depth, "/ipns/")
}

type lookupRes struct {
	path  path.Path
	error error
}*/

// resolveOnce implements resolver.
// TXT records for a given domain name should contain a b58
// encoded multihash.
int ipfs_dns_resolver_resolve_once (char **path, char *name)
{
    char **segments, *domain, *dnslink, buf[500], dlprefix[] = "_dnslink.";
    int p1[2], p2[2], r, l, c=2;
    struct pollfd event[2], *e;
    DNSResolver dnsr;

    segments = ipfs_path_split_n(name, "/", 2);
    domain = segments[0];

    *path = NULL;

    if (!ipfs_isdomain_is_domain(domain)) {
        return ErrInvalidDomain;
    }

    char cached_path[512];
    if (dns_cache_get(domain, cached_path, sizeof(cached_path))) {
        *path = malloc(strlen(cached_path) + 1);
        if (!*path) {
            ipfs_path_free_segments(&segments);
            return ErrAllocFailed;
        }
        strcpy(*path, cached_path);
        goto append_segments;
    }

    //log.Infof("DNSResolver resolving %s", domain);

    if (pipe(p1) || pipe(p2)) {
        return ErrPipe;
    }

    dnsr.lookupTXT = ipfs_dnslink_resolv_lookupTXT;

    r = fork();
    switch(r) {
        case -1:
            return ErrPipe;
        case 0: // child
            close(p1[STDIN_FILENO]); // we don't need to read at child process.
            return ipfs_dns_work_domain (p1[STDOUT_FILENO], &dnsr, domain);
    }
    close(p1[STDOUT_FILENO]); // we don't need to write at main process.
    r = fork();
    switch(r) {
        case -1:
            return ErrPipe;
        case 0: // child
            close(p2[STDIN_FILENO]); // we don't need to read at child process.

            l = strlen(domain) + sizeof(dlprefix);
            dnslink = malloc(l);
            if (!dnslink) {
                return ErrAllocFailed;
            }
            dnslink[--l] = '\0';
            strncpy (dnslink, dlprefix, l);
            strncat (dnslink, domain, l - strlen(dnslink));

            return ipfs_dns_work_domain (p2[STDOUT_FILENO], &dnsr, dnslink);
    }
    close(p2[STDOUT_FILENO]); // we don't need to write at main process.

    memset(&event, 0, sizeof(struct pollfd));
    event[0].fd = p1[STDIN_FILENO];
    event[0].events = POLLIN | POLLERR | POLLHUP | POLLNVAL;
    event[1].fd = p2[STDIN_FILENO];
    event[1].events = POLLIN | POLLERR | POLLHUP | POLLNVAL;
    e = event;

    do {
        r = poll(e, c, -1);
        if (r == -1) {
            return ErrPoll;
        }
        for (r = 0 ; r < c ; r++) {
            if (e[r].revents & POLLIN) {
                r = read(e[r].fd, buf, sizeof(buf));
                if (r > 0) {
                    buf[r] = '\0';
                    *path = malloc(r+1);
                    if (*path) {
                        *path[r] = '\0';
                        strncpy(*path, buf, r);
                    }
                } else if (r <= 0) {
                    return ErrPoll;
                }
            }
        }
        if (event[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            event[0].events = 0;
            e++; c--;
            wait(&r);
        }
        if (event[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            event[1].events = 0;
            c--;
            wait(&r);
        }
    } while (c); // wait for child process finish.

    if (!*path) {
        ipfs_path_free_segments(&segments);
        return ErrResolveFailed;
    }

    dns_cache_put(domain, *path, 0);

append_segments:
    if (ipfs_path_segments_length (segments) > 1) {
        name = *path + strlen(*path) - 1;
        while (*name == '/') {
            *name-- = '\0';
        }
        name = *path;
        *path = ipfs_path_from_segments (name, segments+1);
        free (name);
        if (!*path) {
            return ErrResolveFailed;
        }
    }
    ipfs_path_free_segments (&segments);
    return 0;
}

int ipfs_dns_work_domain (int output, DNSResolver *r, char *name)
{
    char **txt, *path;
    int i, err = r->lookupTXT(&txt, name);

    if (err) {
        return err;
    }

    for (i = 0 ; txt[i] ; i++) {
        err = ipfs_dns_parse_entry (&path, txt[i]);
        if (!err) {
            err = (write (output, path, strlen(path)) != strlen(path));
            free (path);
            if (err) {
                return ErrPipe;
            }
            return 0;
        }
    }
    return ErrResolveFailed;
}

int ipfs_dns_parse_entry (char **path, char *txt)
{
    char buf[500];
    int err;

    err = ipfs_path_parse_from_cid(buf, txt); // bare IPFS multihashes
    if (! err) {
        *path = malloc(strlen(buf) + 1);
        if (!*path) {
            return ErrAllocFailed;
        }
        memcpy(*path, buf, strlen(buf) + 1);
        return 0;
    }
    return ipfs_dns_try_parse_dns_link(path, txt);
}

int ipfs_dns_try_parse_dns_link(char **path, char *txt)
{
    char **parts = ipfs_path_split_n(txt, "=", 2), buf[500];
    int err;

    if (ipfs_path_segments_length(parts) == 2 && strcmp(parts[0], "dnslink")==0) {
        err = ipfs_path_parse(buf, parts[1]);
        if (err == 0) {
            *parts = malloc(strlen(buf) + 1);
            if (! *parts) {
                return ErrAllocFailed;
            }
            memcpy(*parts, buf, strlen(buf) + 1);
            return 0;
        }
        return err;
    }
    return ErrInvalidDNSLink;
}
