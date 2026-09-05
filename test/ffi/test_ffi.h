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

int test_ffi_nostr_generate_key(void) {
    char* sk = ipfs_ffi_nostr_generate_key();
    if (!sk || strlen(sk) != 64) {
        if (sk) ipfs_ffi_free_string(sk);
        return 0;
    }
    ipfs_ffi_free_string(sk);
    return 1;
}

int test_ffi_nostr_get_public_key(void) {
    char* sk = ipfs_ffi_nostr_generate_key();
    if (!sk) return 0;
    char* pk = ipfs_ffi_nostr_get_public_key(sk);
    if (!pk || strlen(pk) != 64) {
        ipfs_ffi_free_string(sk);
        if (pk) ipfs_ffi_free_string(pk);
        return 0;
    }
    ipfs_ffi_free_string(sk);
    ipfs_ffi_free_string(pk);
    return 1;
}

int test_ffi_nostr_event_sign_and_verify(void) {
    char* sk = ipfs_ffi_nostr_generate_key();
    if (!sk) return 0;
    char* json = ipfs_ffi_nostr_event_sign(sk, "hello nostr ffi", 1);
    if (!json || strlen(json) == 0) {
        ipfs_ffi_free_string(sk);
        if (json) ipfs_ffi_free_string(json);
        return 0;
    }
    if (strstr(json, "\"id\"") == NULL || strstr(json, "\"sig\"") == NULL) {
        ipfs_ffi_free_string(sk);
        ipfs_ffi_free_string(json);
        return 0;
    }
    int64_t ok = ipfs_ffi_nostr_event_verify(json);
    ipfs_ffi_free_string(sk);
    ipfs_ffi_free_string(json);
    return ok == 1;
}

int test_ffi_git_init_and_head(void) {
    const char* tmp = "./tmp/test-ffi-git";
    rmrf(tmp);

    if (ipfs_ffi_git_init(tmp, 0) != 0)
        return 0;

    /* create a commit so HEAD exists */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cd \"%s\" && git config user.email \"test@test.com\" && git config user.name \"Test\" && echo hello > file.txt && git add file.txt && git commit -m \"init\" >/dev/null 2>&1", tmp);
    if (system(cmd) != 0) {
        rmrf(tmp);
        return 0;
    }

    uint64_t handle = ipfs_ffi_git_open(tmp);
    if (handle == 0) {
        rmrf(tmp);
        return 0;
    }

    char* head = ipfs_ffi_git_repo_head(handle);
    if (!head || strlen(head) != 40) {
        if (head) ipfs_ffi_free_string(head);
        ipfs_ffi_git_repo_free(handle);
        rmrf(tmp);
        return 0;
    }

    ipfs_ffi_free_string(head);
    ipfs_ffi_git_repo_free(handle);
    rmrf(tmp);
    return 1;
}

int test_ffi_git_clone(void) {
    const char* src = "./tmp/test-ffi-git-src";
    const char* dst = "./tmp/test-ffi-git-dst";
    rmrf(src);
    rmrf(dst);

    if (ipfs_ffi_git_init(src, 0) != 0)
        return 0;

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "cd \"%s\" && git config user.email \"test@test.com\" && git config user.name \"Test\" && echo hello > file.txt && git add file.txt && git commit -m \"init\" >/dev/null 2>&1", src);
    if (system(cmd) != 0) {
        rmrf(src);
        return 0;
    }

    if (ipfs_ffi_git_clone(src, dst, 0) != 0) {
        rmrf(src);
        rmrf(dst);
        return 0;
    }

    uint64_t handle = ipfs_ffi_git_open(dst);
    if (handle == 0) {
        rmrf(src);
        rmrf(dst);
        return 0;
    }

    char* head = ipfs_ffi_git_repo_head(handle);
    if (!head || strlen(head) != 40) {
        if (head) ipfs_ffi_free_string(head);
        ipfs_ffi_git_repo_free(handle);
        rmrf(src);
        rmrf(dst);
        return 0;
    }

    ipfs_ffi_free_string(head);
    ipfs_ffi_git_repo_free(handle);
    rmrf(src);
    rmrf(dst);
    return 1;
}

int test_ffi_libp2p_host_new_close(void) {
    uint64_t handle = ipfs_ffi_libp2p_host_new();
    if (handle == 0)
        return 0;
    if (ipfs_ffi_libp2p_host_close(handle) != 0)
        return 0;
    return 1;
}

int test_ffi_libp2p_host_peer_id(void) {
    uint64_t handle = ipfs_ffi_libp2p_host_new();
    if (handle == 0)
        return 0;
    char* id = ipfs_ffi_libp2p_host_peer_id(handle);
    if (!id || strlen(id) == 0 || strncmp(id, "Qm", 2) != 0) {
        if (id) ipfs_ffi_free_string(id);
        ipfs_ffi_libp2p_host_close(handle);
        return 0;
    }
    ipfs_ffi_free_string(id);
    ipfs_ffi_libp2p_host_close(handle);
    return 1;
}

int test_ffi_libp2p_host_listening_addrs(void) {
    uint64_t handle = ipfs_ffi_libp2p_host_new();
    if (handle == 0)
        return 0;
    char* addrs = ipfs_ffi_libp2p_host_listening_addrs(handle);
    if (!addrs || strlen(addrs) == 0 || strstr(addrs, "/ip4/127.0.0.1/tcp/") == NULL) {
        if (addrs) ipfs_ffi_free_string(addrs);
        ipfs_ffi_libp2p_host_close(handle);
        return 0;
    }
    ipfs_ffi_free_string(addrs);
    ipfs_ffi_libp2p_host_close(handle);
    return 1;
}

int test_ffi_libp2p_host_connect_self(void) {
    uint64_t handle = ipfs_ffi_libp2p_host_new();
    if (handle == 0)
        return 0;
    char* addrs = ipfs_ffi_libp2p_host_listening_addrs(handle);
    if (!addrs) {
        ipfs_ffi_libp2p_host_close(handle);
        return 0;
    }
    /* Try to connect to our own listen address (should succeed) */
    int64_t ret = ipfs_ffi_libp2p_host_connect(handle, addrs);
    ipfs_ffi_free_string(addrs);
    ipfs_ffi_libp2p_host_close(handle);
    return ret == 0;
}

#endif /* TEST_FFI_H */
