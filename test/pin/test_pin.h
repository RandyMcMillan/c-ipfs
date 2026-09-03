#ifndef test_pin_h
#define test_pin_h

#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "ipfs/repo/fsrepo/fs_repo.h"
#include "ipfs/core/ipfs_node.h"
#include "ipfs/pin/pin.h"
#include "ipfs/merkledag/merkledag.h"
#include "ipfs/blocks/blockstore.h"

int test_pin_add_rm_load() {
    const char* repo_dir = "./tmp/ipfs_pin_test";
    struct IpfsNode* local_node = NULL;
    int retVal = 0;

    if (!drop_and_build_repository(repo_dir, 4004, NULL, NULL)) {
        fprintf(stderr, "Unable to build repo\n");
        return 0;
    }
    if (!ipfs_node_offline_new(repo_dir, &local_node)) {
        fprintf(stderr, "Unable to create node\n");
        return 0;
    }

    unsigned char hash1[32] = {0};
    unsigned char hash2[32] = {0};
    hash1[0] = 1; hash2[0] = 2;

    // add pins
    if (!ipfs_pin_add(local_node->repo, hash1, 32, Recursive)) goto exit;
    if (!ipfs_pin_add(local_node->repo, hash2, 32, Direct)) goto exit;

    // load and verify
    struct PinEntry* entries = ipfs_pin_load(local_node->repo);
    if (!entries) goto exit;

    int found1 = 0, found2 = 0;
    struct PinEntry* e = entries;
    while (e != NULL) {
        if (e->hash_size == 32 && e->hash[0] == 1 && e->mode == Recursive) found1 = 1;
        if (e->hash_size == 32 && e->hash[0] == 2 && e->mode == Direct) found2 = 1;
        e = e->next;
    }
    ipfs_pin_entry_free(entries);
    if (!found1 || !found2) goto exit;

    // remove first pin
    if (!ipfs_pin_rm(local_node->repo, hash1, 32)) goto exit;

    // verify removal
    entries = ipfs_pin_load(local_node->repo);
    if (!entries) goto exit;
    found1 = 0; found2 = 0;
    e = entries;
    while (e != NULL) {
        if (e->hash_size == 32 && e->hash[0] == 1) found1 = 1;
        if (e->hash_size == 32 && e->hash[0] == 2 && e->mode == Direct) found2 = 1;
        e = e->next;
    }
    ipfs_pin_entry_free(entries);
    if (found1 || !found2) goto exit;

    retVal = 1;
exit:
    if (local_node) ipfs_node_free(local_node);
    return retVal;
}

int test_gc_collect() {
    const char* repo_dir = "./tmp/ipfs_gc_test";
    struct IpfsNode* local_node = NULL;
    int retVal = 0;

    if (!drop_and_build_repository(repo_dir, 4005, NULL, NULL)) {
        fprintf(stderr, "Unable to build repo\n");
        return 0;
    }
    if (!ipfs_node_offline_new(repo_dir, &local_node)) {
        fprintf(stderr, "Unable to create node\n");
        return 0;
    }

    // Create a simple dag node and write it
    struct HashtableNode* node = NULL;
    if (!ipfs_hashtable_node_new(&node)) goto exit;
    unsigned char data[] = "gc test data";
    node->data = (unsigned char*)malloc(sizeof(data));
    if (!node->data) { ipfs_hashtable_node_free(node); goto exit; }
    memcpy(node->data, data, sizeof(data));
    node->data_size = sizeof(data);

    size_t bytes_written = 0;
    if (!ipfs_merkledag_add(node, local_node->repo, &bytes_written)) {
        ipfs_hashtable_node_free(node);
        goto exit;
    }

    size_t hash_size = node->hash_size;
    unsigned char* hash = (unsigned char*)malloc(hash_size);
    if (!hash) { ipfs_hashtable_node_free(node); goto exit; }
    memcpy(hash, node->hash, hash_size);

    // pin the node
    if (!ipfs_pin_add(local_node->repo, hash, hash_size, Recursive)) {
        ipfs_hashtable_node_free(node);
        free(hash);
        goto exit;
    }
    ipfs_hashtable_node_free(node);

    // run gc - pinned node should survive
    size_t reclaimed = 0;
    if (!ipfs_gc_collect(local_node->repo, &reclaimed)) goto exit;

    struct HashtableNode* retrieved = NULL;
    if (!ipfs_merkledag_get(hash, hash_size, &retrieved, local_node->repo)) goto exit;
    ipfs_hashtable_node_free(retrieved);

    // unpin and gc again - node should be gone
    if (!ipfs_pin_rm(local_node->repo, hash, hash_size)) goto exit;
    if (!ipfs_gc_collect(local_node->repo, &reclaimed)) goto exit;

    retrieved = NULL;
    if (ipfs_merkledag_get(hash, hash_size, &retrieved, local_node->repo)) {
        ipfs_hashtable_node_free(retrieved);
        goto exit; // should have been deleted
    }

    retVal = 1;
exit:
    if (hash) free(hash);
    if (local_node) ipfs_node_free(local_node);
    return retVal;
}

#endif
