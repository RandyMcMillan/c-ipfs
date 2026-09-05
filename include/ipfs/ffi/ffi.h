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
 * Memory
 * --------------------------------------------------------------------------- */

void ipfs_ffi_free_buffer(uint8_t* buf);

#ifdef __cplusplus
}
#endif

#endif /* __IPFS_FFI_FFI_H__ */
