#ifndef __IPFS_FFI_FFI_H__
#define __IPFS_FFI_FFI_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Error handling
 * --------------------------------------------------------------------------- */

const char* ipfs_ffi_last_error(void);
void ipfs_ffi_free_string(char* s);

/* ---------------------------------------------------------------------------
 * Version
 * --------------------------------------------------------------------------- */

const char* ipfs_ffi_version(void);

/* ---------------------------------------------------------------------------
 * Repo initialization
 * --------------------------------------------------------------------------- */

int64_t ipfs_ffi_init_repo(const char* repo_path);

/* ---------------------------------------------------------------------------
 * Node lifecycle
 * --------------------------------------------------------------------------- */

uint64_t ipfs_ffi_node_start(const char* repo_path, uint8_t online);
int64_t ipfs_ffi_node_stop(uint64_t handle);

/* ---------------------------------------------------------------------------
 * Node info
 * --------------------------------------------------------------------------- */

char* ipfs_ffi_node_peer_id(uint64_t handle);
char* ipfs_ffi_node_listening_addrs(uint64_t handle);
char* ipfs_ffi_node_id(uint64_t handle);

/* ---------------------------------------------------------------------------
 * Swarm
 * --------------------------------------------------------------------------- */

int64_t ipfs_ffi_node_connect(uint64_t handle, const char* addr);
char* ipfs_ffi_swarm_peers(uint64_t handle);

/* ---------------------------------------------------------------------------
 * UnixFS
 * --------------------------------------------------------------------------- */

char* ipfs_ffi_unixfs_add_bytes(uint64_t handle, const uint8_t* data, size_t length);
int64_t ipfs_ffi_unixfs_cat(uint64_t handle, const char* cid_str, uint8_t** out, size_t* out_len);

/* ---------------------------------------------------------------------------
 * Block API
 * --------------------------------------------------------------------------- */

char* ipfs_ffi_block_put(uint64_t handle, const uint8_t* data, size_t length);
int64_t ipfs_ffi_block_get(uint64_t handle, const char* cid_str, uint8_t** out, size_t* out_len);
int64_t ipfs_ffi_block_stat(uint64_t handle, const char* cid_str);

/* ---------------------------------------------------------------------------
 * Nostr FFI
 * --------------------------------------------------------------------------- */

char* ipfs_ffi_nostr_generate_key(void);
char* ipfs_ffi_nostr_get_public_key(const char* sk);
char* ipfs_ffi_nostr_event_sign(const char* sk, const char* content, int kind);
int64_t ipfs_ffi_nostr_event_verify(const char* json_str);

/* ---------------------------------------------------------------------------
 * Git FFI
 * --------------------------------------------------------------------------- */

int64_t ipfs_ffi_git_clone(const char* url, const char* path, uint8_t bare);
uint64_t ipfs_ffi_git_open(const char* path);
char* ipfs_ffi_git_repo_head(uint64_t handle);
int64_t ipfs_ffi_git_repo_free(uint64_t handle);
int64_t ipfs_ffi_git_init(const char* path, uint8_t bare);

/* ---------------------------------------------------------------------------
 * libp2p standalone host FFI
 * --------------------------------------------------------------------------- */

uint64_t ipfs_ffi_libp2p_host_new(void);
int64_t ipfs_ffi_libp2p_host_close(uint64_t handle);
char* ipfs_ffi_libp2p_host_peer_id(uint64_t handle);
char* ipfs_ffi_libp2p_host_listening_addrs(uint64_t handle);
int64_t ipfs_ffi_libp2p_host_connect(uint64_t handle, const char* addr);

/* ---------------------------------------------------------------------------
 * Memory
 * --------------------------------------------------------------------------- */

void ipfs_ffi_free_buffer(uint8_t* buf);

#ifdef __cplusplus
}
#endif

#endif /* __IPFS_FFI_FFI_H__ */
