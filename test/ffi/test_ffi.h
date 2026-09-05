#ifndef TEST_FFI_H
#define TEST_FFI_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs/ffi/ffi.h"
#include "libp2p/os/utils.h"

static void rmrf(const char* path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", path);
    system(cmd);
}

int test_ffi_version(void) {
    const char* v = ipfs_ffi_version();
    if (!v || strlen(v) == 0)
        return 0;
    return 1;
}

int test_ffi_init_repo_and_node_lifecycle(void) {
    const char* tmp = "./tmp/test-ffi-lifecycle";
    rmrf(tmp);

    if (ipfs_ffi_init_repo(tmp) != 0) {
        fprintf(stderr, "init repo failed: %s\n", ipfs_ffi_last_error());
        return 0;
    }

    uint64_t handle = ipfs_ffi_node_start(tmp, 0);
    if (handle == 0) {
        fprintf(stderr, "node start failed: %s\n", ipfs_ffi_last_error());
        return 0;
    }

    char* peer_id = ipfs_ffi_node_peer_id(handle);
    if (!peer_id || strlen(peer_id) == 0) {
        if (peer_id) ipfs_ffi_free_string(peer_id);
        ipfs_ffi_node_stop(handle);
        return 0;
    }
    ipfs_ffi_free_string(peer_id);

    if (ipfs_ffi_node_stop(handle) != 0) {
        return 0;
    }
    return 1;
}

int test_ffi_unixfs_add_and_cat(void) {
    const char* tmp = "./tmp/test-ffi-unixfs";
    rmrf(tmp);

    if (ipfs_ffi_init_repo(tmp) != 0)
        return 0;

    uint64_t handle = ipfs_ffi_node_start(tmp, 0);
    if (handle == 0)
        return 0;

    const char* data = "hello from ffi test";
    size_t len = strlen(data);
    char* cid = ipfs_ffi_unixfs_add_bytes(handle, (const uint8_t*)data, len);
    if (!cid || strlen(cid) == 0) {
        if (cid) ipfs_ffi_free_string(cid);
        ipfs_ffi_node_stop(handle);
        return 0;
    }

    uint8_t* out = NULL;
    size_t out_len = 0;
    if (ipfs_ffi_unixfs_cat(handle, cid, &out, &out_len) != 0) {
        ipfs_ffi_free_string(cid);
        ipfs_ffi_node_stop(handle);
        return 0;
    }

    int ret = (out_len == len && memcmp(out, data, len) == 0);
    if (out) ipfs_ffi_free_buffer(out);
    ipfs_ffi_free_string(cid);
    ipfs_ffi_node_stop(handle);
    return ret;
}

int test_ffi_block_put_get_stat(void) {
    const char* tmp = "./tmp/test-ffi-block";
    rmrf(tmp);

    if (ipfs_ffi_init_repo(tmp) != 0)
        return 0;

    uint64_t handle = ipfs_ffi_node_start(tmp, 0);
    if (handle == 0)
        return 0;

    const char* data = "raw block data";
    size_t len = strlen(data);
    char* cid = ipfs_ffi_block_put(handle, (const uint8_t*)data, len);
    if (!cid || strlen(cid) == 0) {
        if (cid) ipfs_ffi_free_string(cid);
        ipfs_ffi_node_stop(handle);
        return 0;
    }

    int64_t size = ipfs_ffi_block_stat(handle, cid);
    if (size != (int64_t)len) {
        ipfs_ffi_free_string(cid);
        ipfs_ffi_node_stop(handle);
        return 0;
    }

    uint8_t* out = NULL;
    size_t out_len = 0;
    if (ipfs_ffi_block_get(handle, cid, &out, &out_len) != 0) {
        ipfs_ffi_free_string(cid);
        ipfs_ffi_node_stop(handle);
        return 0;
    }

    int ret = (out_len == len && memcmp(out, data, len) == 0);
    if (out) ipfs_ffi_free_buffer(out);
    ipfs_ffi_free_string(cid);
    ipfs_ffi_node_stop(handle);
    return ret;
}

int test_ffi_node_id(void) {
    const char* tmp = "./tmp/test-ffi-node-id";
    rmrf(tmp);

    if (ipfs_ffi_init_repo(tmp) != 0)
        return 0;

    uint64_t handle = ipfs_ffi_node_start(tmp, 0);
    if (handle == 0)
        return 0;

    char* info = ipfs_ffi_node_id(handle);
    if (!info || strlen(info) == 0) {
        if (info) ipfs_ffi_free_string(info);
        ipfs_ffi_node_stop(handle);
        return 0;
    }

    int ret = (strstr(info, "\"id\"") != NULL);
    ipfs_ffi_free_string(info);
    ipfs_ffi_node_stop(handle);
    return ret;
}

int test_ffi_empty_add_cat(void) {
    const char* tmp = "./tmp/test-ffi-empty";
    rmrf(tmp);

    if (ipfs_ffi_init_repo(tmp) != 0)
        return 0;

    uint64_t handle = ipfs_ffi_node_start(tmp, 0);
    if (handle == 0)
        return 0;

    char* cid = ipfs_ffi_unixfs_add_bytes(handle, (const uint8_t*)"", 0);
    if (!cid || strlen(cid) == 0) {
        if (cid) ipfs_ffi_free_string(cid);
        ipfs_ffi_node_stop(handle);
        return 0;
    }

    uint8_t* out = NULL;
    size_t out_len = 0;
    if (ipfs_ffi_unixfs_cat(handle, cid, &out, &out_len) != 0) {
        ipfs_ffi_free_string(cid);
        ipfs_ffi_node_stop(handle);
        return 0;
    }

    int ret = (out_len == 0);
    if (out) ipfs_ffi_free_buffer(out);
    ipfs_ffi_free_string(cid);
    ipfs_ffi_node_stop(handle);
    return ret;
}

#endif /* TEST_FFI_H */
