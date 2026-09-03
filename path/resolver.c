#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "ipfs/cid/cid.h"
#include "ipfs/path/path.h"
#include "ipfs/merkledag/node.h"
#include "ipfs/merkledag/merkledag.h"
#include "ipfs/repo/fsrepo/fs_repo.h"
#include "ipfs/util/errs.h"

/* Forward declarations for types used in this module */
typedef struct FSRepo DAGService;
typedef struct FSRepo Context;

typedef struct Resolver {
    DAGService *DAG;
    int (*ResolveOnce)(struct NodeLink **lnk, Context *ctx, DAGService *ds, struct HashtableNode **nd, char *name);
} Resolver;

/* Forward declarations */
int ipfs_path_resolve_single(struct NodeLink **lnk, Context *ctx, DAGService *ds, struct HashtableNode **nd, char *name);
int ipfs_path_resolve_path_components(struct HashtableNode ***nd, Context *ctx, Resolver *s, char *fpath);
int ipfs_path_resolve_links(struct HashtableNode ***result, Context *ctx, DAGService *ds, struct HashtableNode *ndd, char **names);

/**
 * Find a link by name within a node.
 * @param lnk where to store the found link (pointer into node, do not free)
 * @param nd the node to search
 * @param name the link name
 * @returns 0 on success, ErrNoLink if not found
 */
static int ipfs_path_resolve_link(struct NodeLink **lnk, struct HashtableNode *nd, char *name)
{
    if (!nd || !name) return ErrNoLink;
    struct NodeLink *curr = nd->head_link;
    while (curr != NULL) {
        if (curr->name && strcmp(curr->name, name) == 0) {
            *lnk = curr;
            return 0;
        }
        curr = curr->next;
    }
    return ErrNoLink;
}

Resolver* ipfs_path_new_basic_resolver(DAGService *ds)
{
    Resolver *ret = malloc(sizeof(Resolver));
    if (!ret) return NULL;
    ret->DAG = ds;
    ret->ResolveOnce = ipfs_path_resolve_single;
    return ret;
}

// ipfs_path_split_abs_path clean up and split fpath. It extracts the first component (which
// must be a Multihash) and return it separately.
int ipfs_path_split_abs_path(struct Cid* cid, char ***parts, char *fpath)
{
    *parts = ipfs_path_split_segments(fpath);

    if (strcmp(**parts, "ipfs") == 0) (*parts)++;

    // if nothing, bail.
    if (!**parts) return ErrNoComponents;

    // first element in the path is a cid
    struct Cid *temp_cid = NULL;
    if (!ipfs_cid_decode_hash_from_base58((unsigned char*)**parts, strlen(**parts), &temp_cid)) {
        ipfs_path_free_segments(parts);
        return ErrCidDecode;
    }
    *cid = *temp_cid;
    free(temp_cid);
    return 0;
}

// ipfs_path_resolve_path fetches the node for given path. It returns the last item
// returned by ipfs_path_resolve_path_components.
int ipfs_path_resolve_path(struct HashtableNode **nd, Context *ctx, Resolver *s, char *fpath)
{
    int err = ipfs_path_is_valid(fpath);
    struct HashtableNode **ndd;

    if (err) {
        return err;
    }
    err = ipfs_path_resolve_path_components(&ndd, ctx, s, fpath);
    if (err) {
        return err;
    }
    if (ndd == NULL) {
        return ErrBadPath;
    }
    while (*ndd) {
        *nd = *ndd;
        ndd++;
    }
    return 0;
}

int ipfs_path_resolve_single(struct NodeLink **lnk, Context *ctx, DAGService *ds, struct HashtableNode **nd, char *name)
{
    (void)ctx;
    int err = ipfs_path_resolve_link(lnk, *nd, name);
    if (err) return err;

    /* Fetch the target node from the DAG service so the caller can continue walking */
    struct HashtableNode *next_node = NULL;
    if (!ipfs_merkledag_get((*lnk)->hash, (*lnk)->hash_size, &next_node, ds)) {
        return ErrNoLink;
    }
    *nd = next_node;
    return 0;
}

// ipfs_path_resolve_path_components fetches the nodes for each segment of the given path.
// It uses the first path component as a hash (key) of the first node, then
// resolves all other components walking the links, with ipfs_path_resolve_links.
int ipfs_path_resolve_path_components(struct HashtableNode ***nd, Context *ctx, Resolver *s, char *fpath)
{
    int err;
    struct Cid h;
    char **parts;

    err = ipfs_path_split_abs_path(&h, &parts, fpath);
    if (err) {
        return err;
    }

    /* Fetch the root node from the DAG service */
    struct HashtableNode *root = NULL;
    if (!ipfs_merkledag_get(h.hash, h.hash_length, &root, s->DAG)) {
        ipfs_path_free_segments(&parts);
        return ErrBadPath;
    }

    err = ipfs_path_resolve_links(nd, ctx, s->DAG, root, parts);
    ipfs_path_free_segments(&parts);
    return err;
}

// ipfs_path_resolve_links iteratively resolves names by walking the link hierarchy.
// Every node is fetched from the DAGService, resolving the next name.
// Returns the list of nodes forming the path, starting with ndd. This list is
// guaranteed never to be empty.
//
// ipfs_path_resolve_links(nd, []string{"foo", "bar", "baz"})
// would retrieve "baz" in ("bar" in ("foo" in nd.Links).Links).Links
int ipfs_path_resolve_links(struct HashtableNode ***result, Context *ctx, DAGService *ds, struct HashtableNode *ndd, char **names)
{
    (void)ctx;
    int err, idx = 0;
    struct NodeLink *lnk = NULL;
    struct HashtableNode *nd = ndd;

    int seg_count = ipfs_path_segments_length(names);
    *result = calloc(sizeof(struct HashtableNode*), seg_count + 1);
    if (!*result) {
        return -1;
    }
    memset(*result, 0, sizeof(struct HashtableNode*) * (seg_count + 1));

    (*result)[idx++] = ndd;

    while (*names) {
        err = ipfs_path_resolve_link(&lnk, nd, *names);
        if (err) {
            char msg[256];
            snprintf(msg, sizeof(msg), "no link named \"%s\" under %s", *names, nd->hash ? (char*)nd->hash : "unknown");
            if (Err[ErrNoLink]) {
                free(Err[ErrNoLink]);
            }
            Err[ErrNoLink] = malloc(strlen(msg) + 1);
            if (Err[ErrNoLink]) {
                memcpy(Err[ErrNoLink], msg, strlen(msg) + 1);
            }
            free(*result);
            *result = NULL;
            return ErrNoLink;
        }

        /* Fetch the next node via the DAG service */
        struct HashtableNode *next = NULL;
        if (!ipfs_merkledag_get(lnk->hash, lnk->hash_size, &next, ds)) {
            free(*result);
            *result = NULL;
            return ErrNoLink;
        }

        nd = next;
        (*result)[idx++] = nd;
        names++;
    }
    return 0;
}
