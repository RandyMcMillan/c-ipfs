#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs/merkledag/merkledag.h"

typedef struct MerkleLink {
    char *name;
    unsigned char *hash;
    size_t hash_len;
    uint64_t size;
} MerkleLink;

typedef struct MerkleNode {
    MerkleLink **links;
    size_t amount;
} MerkleNode;

MerkleNode *merkledag_node_copy_links(MerkleNode *LProc, MerkleLink **source_links, size_t count) {
    if (!LProc || !source_links) return NULL;

    LProc->links = calloc(count, sizeof(MerkleLink *));
    if (!LProc->links) {
        LProc->amount = 0;
        return NULL;
    }

    for (size_t i = 0; i < count; i++) {
        if (source_links[i] != NULL) {
            LProc->links[i] = malloc(sizeof(MerkleLink));
            if (LProc->links[i] == NULL) {
                for (size_t j = 0; j < i; j++) {
                    if (LProc->links[j]) {
                        free(LProc->links[j]->name);
                        free(LProc->links[j]->hash);
                        free(LProc->links[j]);
                    }
                }
                free(LProc->links);
                LProc->links = NULL;
                LProc->amount = 0;
                return NULL;
            }

            LProc->links[i]->name = source_links[i]->name ? strdup(source_links[i]->name) : NULL;
            LProc->links[i]->size = source_links[i]->size;
            LProc->links[i]->hash_len = source_links[i]->hash_len;
            if (source_links[i]->hash && source_links[i]->hash_len > 0) {
                LProc->links[i]->hash = malloc(source_links[i]->hash_len);
                if (!LProc->links[i]->hash) {
                    free(LProc->links[i]->name);
                    free(LProc->links[i]);
                    for (size_t j = 0; j < i; j++) {
                        free(LProc->links[j]->name);
                        free(LProc->links[j]->hash);
                        free(LProc->links[j]);
                    }
                    free(LProc->links);
                    LProc->links = NULL;
                    LProc->amount = 0;
                    return NULL;
                }
                memcpy(LProc->links[i]->hash, source_links[i]->hash, source_links[i]->hash_len);
            } else {
                LProc->links[i]->hash = NULL;
            }
        } else {
            LProc->links[i] = NULL;
        }
    }

    LProc->amount = count;
    return LProc;
}
