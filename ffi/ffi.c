/**
 * C FFI layer for c-ipfs, matching the kubo FFI interface.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>

#include "ipfs/ffi/ffi.h"
#include "libp2p/os/utils.h"
#include "ipfs/core/ipfs_node.h"
#include "ipfs/core/builder.h"
#include "ipfs/repo/init.h"
#include "ipfs/repo/fsrepo/fs_repo.h"
#include "ipfs/cid/cid.h"
#include "ipfs/blocks/block.h"
#include "ipfs/blocks/blockstore.h"
#include "ipfs/importer/importer.h"
#include "ipfs/importer/exporter.h"
#include "ipfs/merkledag/merkledag.h"
#include "ipfs/merkledag/node.h"
#include "ipfs/unixfs/unixfs.h"
#include "libp2p/peer/peerstore.h"
#include "libp2p/peer/peer.h"
#include "libp2p/crypto/encoding/base58.h"
#include "libp2p/crypto/encoding/base64.h"
#include "libp2p/utils/logger.h"

#define IPFS_FFI_VERSION "0.0.1-dev"

/* ---------------------------------------------------------------------------
 * Error handling
 * --------------------------------------------------------------------------- */

static pthread_mutex_t last_error_mutex = PTHREAD_MUTEX_INITIALIZER;
static char last_error[1024];

static void set_error(const char* fmt, ...) {
    pthread_mutex_lock(&last_error_mutex);
    va_list args;
    va_start(args, fmt);
    vsnprintf(last_error, sizeof(last_error), fmt, args);
    va_end(args);
    pthread_mutex_unlock(&last_error_mutex);
}

static void clear_error(void) {
    pthread_mutex_lock(&last_error_mutex);
    last_error[0] = '\0';
    pthread_mutex_unlock(&last_error_mutex);
}

const char* ipfs_ffi_last_error(void) {
    pthread_mutex_lock(&last_error_mutex);
    const char* ret = last_error[0] ? last_error : NULL;
    pthread_mutex_unlock(&last_error_mutex);
    return ret;
}

void ipfs_ffi_free_string(char* s) {
    free(s);
}

/* ---------------------------------------------------------------------------
 * Version
 * --------------------------------------------------------------------------- */

const char* ipfs_ffi_version(void) {
    return IPFS_FFI_VERSION;
}

/* ---------------------------------------------------------------------------
 * Repo initialization
 * --------------------------------------------------------------------------- */

int64_t ipfs_ffi_init_repo(const char* repo_path) {
    if (!repo_path || strlen(repo_path) == 0) {
        set_error("repo_path is null or empty");
        return -1;
    }

    if (fs_repo_is_initialized((char*)repo_path)) {
        set_error("repo already initialized at %s", repo_path);
        return -1;
    }

    if (!os_utils_directory_exists(repo_path) && !os_mkdir((char*)repo_path)) {
        set_error("failed to create repo directory %s", repo_path);
        return -1;
    }

    char* peer_id = NULL;
    if (!make_ipfs_repository(repo_path, 4001, NULL, &peer_id, NULL)) {
        set_error("failed to initialize repo at %s", repo_path);
        return -1;
    }

    if (peer_id)
        free(peer_id);

    clear_error();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Node registry
 * --------------------------------------------------------------------------- */

typedef struct node_handle {
    struct IpfsNode* node;
} node_handle_t;

static pthread_mutex_t nodes_mutex = PTHREAD_MUTEX_INITIALIZER;
static node_handle_t** nodes = NULL;
static size_t nodes_capacity = 0;
static uint64_t next_handle = 1;

static struct IpfsNode* node_lookup(uint64_t handle) {
    pthread_mutex_lock(&nodes_mutex);
    struct IpfsNode* node = NULL;
    if (handle > 0 && handle <= nodes_capacity && nodes[handle - 1] != NULL) {
        node = nodes[handle - 1]->node;
    }
    pthread_mutex_unlock(&nodes_mutex);
    return node;
}

uint64_t ipfs_ffi_node_start(const char* repo_path, uint8_t online) {
    struct IpfsNode* node = NULL;
    int ret = 0;

    if (online) {
        ret = ipfs_node_online_new(repo_path, &node);
    } else {
        ret = ipfs_node_offline_new(repo_path, &node);
    }

    if (!ret || node == NULL) {
        set_error("failed to start %s node for repo %s",
                  online ? "online" : "offline",
                  repo_path ? repo_path : "(null)");
        return 0;
    }

    node_handle_t* h = malloc(sizeof(node_handle_t));
    if (!h) {
        ipfs_node_free(node);
        set_error("out of memory allocating handle");
        return 0;
    }
    h->node = node;

    pthread_mutex_lock(&nodes_mutex);
    uint64_t handle = next_handle++;
    if (handle > nodes_capacity) {
        size_t new_cap = nodes_capacity ? nodes_capacity * 2 : 4;
        node_handle_t** new_nodes = realloc(nodes, new_cap * sizeof(node_handle_t*));
        if (!new_nodes) {
            pthread_mutex_unlock(&nodes_mutex);
            free(h);
            ipfs_node_free(node);
            set_error("out of memory growing handle table");
            return 0;
        }
        memset(new_nodes + nodes_capacity, 0, (new_cap - nodes_capacity) * sizeof(node_handle_t*));
        nodes = new_nodes;
        nodes_capacity = new_cap;
    }
    nodes[handle - 1] = h;
    pthread_mutex_unlock(&nodes_mutex);

    clear_error();
    return handle;
}

int64_t ipfs_ffi_node_stop(uint64_t handle) {
    pthread_mutex_lock(&nodes_mutex);
    if (handle == 0 || handle > nodes_capacity || nodes[handle - 1] == NULL) {
        pthread_mutex_unlock(&nodes_mutex);
        set_error("invalid handle %llu", (unsigned long long)handle);
        return -1;
    }
    node_handle_t* h = nodes[handle - 1];
    nodes[handle - 1] = NULL;
    pthread_mutex_unlock(&nodes_mutex);

    ipfs_node_free(h->node);
    free(h);
    clear_error();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Node info
 * --------------------------------------------------------------------------- */

char* ipfs_ffi_node_peer_id(uint64_t handle) {
    struct IpfsNode* node = node_lookup(handle);
    if (!node) {
        set_error("invalid handle %llu", (unsigned long long)handle);
        return NULL;
    }
    if (!node->identity || !node->identity->peer || !node->identity->peer->id) {
        set_error("node has no identity");
        return NULL;
    }
    return strdup(node->identity->peer->id);
}

char* ipfs_ffi_node_listening_addrs(uint64_t handle) {
    struct IpfsNode* node = node_lookup(handle);
    if (!node) {
        set_error("invalid handle %llu", (unsigned long long)handle);
        return NULL;
    }
    if (!node->repo || !node->repo->config || !node->repo->config->addresses) {
        set_error("node has no addresses configured");
        return NULL;
    }

    /* Build a newline-separated list from config swarm addresses */
    size_t total = 0;
    struct Libp2pLinkedList* curr = node->repo->config->addresses->swarm_head;
    while (curr) {
        if (curr->item) {
            total += strlen((char*)curr->item) + 1;
        }
        curr = curr->next;
    }
    if (total == 0) {
        return strdup("");
    }

    char* result = malloc(total + 1);
    if (!result) {
        set_error("out of memory");
        return NULL;
    }
    result[0] = '\0';

    curr = node->repo->config->addresses->swarm_head;
    while (curr) {
        if (curr->item) {
            strcat(result, (char*)curr->item);
            strcat(result, "\n");
        }
        curr = curr->next;
    }
    /* Trim trailing newline */
    size_t len = strlen(result);
    if (len > 0 && result[len - 1] == '\n') {
        result[len - 1] = '\0';
    }
    return result;
}

char* ipfs_ffi_node_id(uint64_t handle) {
    struct IpfsNode* node = node_lookup(handle);
    if (!node) {
        set_error("invalid handle %llu", (unsigned long long)handle);
        return NULL;
    }
    if (!node->identity || !node->identity->peer || !node->identity->peer->id) {
        set_error("node has no identity");
        return NULL;
    }

    const char* id = node->identity->peer->id;
    char* pub_key_b64 = NULL;

    if (node->identity->private_key.public_key_der && node->identity->private_key.public_key_length > 0) {
        size_t b64_len = libp2p_crypto_encoding_base64_encode_size(node->identity->private_key.public_key_length);
        pub_key_b64 = malloc(b64_len + 1);
        if (pub_key_b64) {
            size_t written = 0;
            if (libp2p_crypto_encoding_base64_encode((const unsigned char*)node->identity->private_key.public_key_der,
                                                      node->identity->private_key.public_key_length,
                                                      (unsigned char*)pub_key_b64, b64_len, &written)) {
                pub_key_b64[written] = '\0';
            } else {
                free(pub_key_b64);
                pub_key_b64 = NULL;
            }
        }
    }

    size_t len = strlen(id) + 64;
    if (pub_key_b64)
        len += strlen(pub_key_b64);
    char* result = malloc(len);
    if (!result) {
        free(pub_key_b64);
        set_error("out of memory");
        return NULL;
    }

    snprintf(result, len, "{\"id\":\"%s\",\"public_key\":\"%s\"}",
             id, pub_key_b64 ? pub_key_b64 : "");
    free(pub_key_b64);
    return result;
}

/* ---------------------------------------------------------------------------
 * Swarm
 * --------------------------------------------------------------------------- */

int64_t ipfs_ffi_node_connect(uint64_t handle, const char* addr) {
    struct IpfsNode* node = node_lookup(handle);
    if (!node) {
        set_error("invalid handle %llu", (unsigned long long)handle);
        return -1;
    }
    if (!addr || strlen(addr) == 0) {
        set_error("addr is null or empty");
        return -1;
    }

    /* Parse multiaddress to extract peer info */
    struct MultiAddress* ma = multiaddress_new_from_string(addr);
    if (!ma) {
        set_error("failed to parse multiaddr: %s", addr);
        return -1;
    }

    struct Libp2pPeer* peer = libp2p_peer_new_from_multiaddress(ma);
    multiaddress_free(ma);
    if (!peer) {
        set_error("failed to create peer from multiaddr");
        return -1;
    }

    /* Attempt connection via dialer */
    int ret = libp2p_peer_connect(node->dialer, peer, node->peerstore,
                                  node->repo->config->datastore, 10);
    if (!ret) {
        libp2p_peer_free(peer);
        set_error("failed to connect to %s", addr);
        return -1;
    }

    libp2p_peer_free(peer);
    clear_error();
    return 0;
}

char* ipfs_ffi_swarm_peers(uint64_t handle) {
    struct IpfsNode* node = node_lookup(handle);
    if (!node) {
        set_error("invalid handle %llu", (unsigned long long)handle);
        return NULL;
    }
    if (!node->peerstore) {
        return strdup("");
    }

    /* Count connected peers */
    size_t total = 0;
    struct Libp2pLinkedList* curr = node->peerstore->head_entry;
    while (curr) {
        struct PeerEntry* entry = (struct PeerEntry*)curr->item;
        if (entry && entry->peer && entry->peer->connection_type == CONNECTION_TYPE_CONNECTED) {
            total += (entry->peer->id ? strlen(entry->peer->id) : 0) + 1 + 64;
        }
        curr = curr->next;
    }
    if (total == 0) {
        return strdup("");
    }

    char* result = malloc(total + 1);
    if (!result) {
        set_error("out of memory");
        return NULL;
    }
    result[0] = '\0';

    curr = node->peerstore->head_entry;
    while (curr) {
        struct PeerEntry* entry = (struct PeerEntry*)curr->item;
        if (entry && entry->peer && entry->peer->connection_type == CONNECTION_TYPE_CONNECTED) {
            strcat(result, entry->peer->id ? entry->peer->id : "?");
            strcat(result, "\n");
        }
        curr = curr->next;
    }
    /* Trim trailing newline */
    size_t len = strlen(result);
    if (len > 0 && result[len - 1] == '\n') {
        result[len - 1] = '\0';
    }
    return result;
}

/* ---------------------------------------------------------------------------
 * UnixFS helpers
 * --------------------------------------------------------------------------- */

static int add_bytes_chunk(const uint8_t* data, size_t len, struct FSRepo* fs_repo,
                           unsigned char** hash, size_t* hash_size, size_t* bytes_written) {
    struct UnixFS* ufs = NULL;
    if (!ipfs_unixfs_new(&ufs))
        return 0;
    ufs->data_type = UNIXFS_FILE;
    ufs->file_size = len;
    if (len > 0) {
        if (!ipfs_unixfs_add_data((unsigned char*)data, len, ufs)) {
            ipfs_unixfs_free(ufs);
            return 0;
        }
    }

    size_t pb_size = ipfs_unixfs_protobuf_encode_size(ufs);
    unsigned char* pb = malloc(pb_size);
    if (!pb) {
        ipfs_unixfs_free(ufs);
        return 0;
    }
    size_t pb_written = 0;
    if (!ipfs_unixfs_protobuf_encode(ufs, pb, pb_size, &pb_written)) {
        free(pb);
        ipfs_unixfs_free(ufs);
        return 0;
    }
    ipfs_unixfs_free(ufs);

    struct HashtableNode* node = NULL;
    if (!ipfs_hashtable_node_new_from_data(pb, pb_written, &node)) {
        free(pb);
        return 0;
    }
    free(pb);

    size_t written = 0;
    if (!ipfs_merkledag_add(node, fs_repo, &written)) {
        ipfs_hashtable_node_free(node);
        return 0;
    }

    *hash = malloc(node->hash_size);
    if (!*hash) {
        ipfs_hashtable_node_free(node);
        return 0;
    }
    memcpy(*hash, node->hash, node->hash_size);
    *hash_size = node->hash_size;
    *bytes_written = written;

    ipfs_hashtable_node_free(node);
    return 1;
}

char* ipfs_ffi_unixfs_add_bytes(uint64_t handle, const uint8_t* data, size_t length) {
    struct IpfsNode* node = node_lookup(handle);
    if (!node) {
        set_error("invalid handle %llu", (unsigned long long)handle);
        return NULL;
    }
    if (!node->repo) {
        set_error("node has no repo");
        return NULL;
    }

    unsigned char* hash = NULL;
    size_t hash_size = 0;
    size_t bytes_written = 0;

    if (!add_bytes_chunk(data, length, node->repo, &hash, &hash_size, &bytes_written)) {
        set_error("failed to add bytes");
        return NULL;
    }

    /* Build multihash prefix for CIDv0: 0x12 0x20 */
    unsigned char multihash[hash_size + 2];
    multihash[0] = 0x12;
    multihash[1] = 0x20;
    memcpy(multihash + 2, hash, hash_size);

    size_t final_b58_size = libp2p_crypto_encoding_base58_encode_size(hash_size + 2);
    char* cid_str = malloc(final_b58_size + 1);
    if (!cid_str) {
        free(hash);
        set_error("out of memory");
        return NULL;
    }

    size_t written = final_b58_size;
    unsigned char* b58_out = (unsigned char*)cid_str;
    if (!libp2p_crypto_encoding_base58_encode(multihash, hash_size + 2, &b58_out, &written)) {
        free(hash);
        free(cid_str);
        set_error("failed to encode multihash to base58");
        return NULL;
    }

    free(hash);
    cid_str[written] = '\0';
    return cid_str;
}

typedef struct cat_buffer {
    uint8_t* data;
    size_t len;
    size_t cap;
} cat_buffer_t;

static int cat_buffer_append(cat_buffer_t* buf, const uint8_t* data, size_t len) {
    if (buf->len + len > buf->cap) {
        size_t new_cap = buf->cap ? buf->cap * 2 : 256;
        while (new_cap < buf->len + len)
            new_cap *= 2;
        uint8_t* new_data = realloc(buf->data, new_cap);
        if (!new_data)
            return 0;
        buf->data = new_data;
        buf->cap = new_cap;
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    return 1;
}

static int cat_node_to_buffer(struct HashtableNode* node, struct IpfsNode* local_node, cat_buffer_t* buf);

static int cat_link_to_buffer(struct NodeLink* link, struct IpfsNode* local_node, cat_buffer_t* buf) {
    struct HashtableNode* child = NULL;
    if (!ipfs_exporter_get_node(local_node, link->hash, link->hash_size, &child))
        return 0;
    int ret = cat_node_to_buffer(child, local_node, buf);
    ipfs_hashtable_node_free(child);
    return ret;
}

static int cat_node_to_buffer(struct HashtableNode* node, struct IpfsNode* local_node, cat_buffer_t* buf) {
    struct UnixFS* ufs = NULL;
    if (!ipfs_unixfs_protobuf_decode(node->data, node->data_size, &ufs))
        return 0;
    int ret = 1;
    if (ufs->bytes_size > 0) {
        ret = cat_buffer_append(buf, ufs->bytes, ufs->bytes_size);
    }
    ipfs_unixfs_free(ufs);
    if (!ret)
        return 0;

    struct NodeLink* link = node->head_link;
    while (link) {
        if (!cat_link_to_buffer(link, local_node, buf))
            return 0;
        link = link->next;
    }
    return 1;
}

int64_t ipfs_ffi_unixfs_cat(uint64_t handle, const char* cid_str, uint8_t** out, size_t* out_len) {
    struct IpfsNode* node = node_lookup(handle);
    if (!node) {
        set_error("invalid handle %llu", (unsigned long long)handle);
        return -1;
    }
    if (!cid_str || !out || !out_len) {
        set_error("invalid arguments");
        return -1;
    }

    struct Cid* cid = NULL;
    if (!ipfs_cid_decode_hash_from_base58((const unsigned char*)cid_str, strlen(cid_str), &cid)) {
        set_error("failed to decode cid: %s", cid_str);
        return -1;
    }

    struct HashtableNode* root = NULL;
    if (!ipfs_exporter_get_node(node, cid->hash, cid->hash_length, &root)) {
        ipfs_cid_free(cid);
        set_error("failed to retrieve node for cid: %s", cid_str);
        return -1;
    }

    cat_buffer_t buf = {0};
    int ret = cat_node_to_buffer(root, node, &buf);
    ipfs_hashtable_node_free(root);
    ipfs_cid_free(cid);

    if (!ret) {
        free(buf.data);
        set_error("failed to cat node");
        return -1;
    }

    *out = buf.data;
    *out_len = buf.len;
    clear_error();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Block API
 * --------------------------------------------------------------------------- */

char* ipfs_ffi_block_put(uint64_t handle, const uint8_t* data, size_t length) {
    struct IpfsNode* node = node_lookup(handle);
    if (!node) {
        set_error("invalid handle %llu", (unsigned long long)handle);
        return NULL;
    }
    if (!node->repo) {
        set_error("node has no repo");
        return NULL;
    }

    struct Block* block = ipfs_block_new_raw(data, length);
    if (!block) {
        set_error("failed to create raw block");
        return NULL;
    }

    size_t bytes_written = 0;
    if (!ipfs_repo_fsrepo_block_write(block, node->repo, &bytes_written)) {
        ipfs_block_free(block);
        set_error("failed to write block");
        return NULL;
    }

    char* cid_str = NULL;
    if (!ipfs_cid_to_string(block->cid, &cid_str)) {
        ipfs_block_free(block);
        set_error("failed to encode block cid");
        return NULL;
    }

    ipfs_block_free(block);
    clear_error();
    return cid_str;
}

int64_t ipfs_ffi_block_get(uint64_t handle, const char* cid_str, uint8_t** out, size_t* out_len) {
    struct IpfsNode* node = node_lookup(handle);
    if (!node) {
        set_error("invalid handle %llu", (unsigned long long)handle);
        return -1;
    }
    if (!cid_str || !out || !out_len) {
        set_error("invalid arguments");
        return -1;
    }

    struct Cid* cid = NULL;
    if (!ipfs_cid_decode_hash_from_base58((const unsigned char*)cid_str, strlen(cid_str), &cid)) {
        set_error("failed to decode cid: %s", cid_str);
        return -1;
    }

    struct Block* block = NULL;
    if (!ipfs_repo_fsrepo_block_read(cid->hash, cid->hash_length, &block, node->repo)) {
        ipfs_cid_free(cid);
        set_error("failed to read block: %s", cid_str);
        return -1;
    }

    uint8_t* result = malloc(block->data_length);
    if (!result) {
        ipfs_block_free(block);
        ipfs_cid_free(cid);
        set_error("out of memory");
        return -1;
    }
    memcpy(result, block->data, block->data_length);
    *out = result;
    *out_len = block->data_length;

    ipfs_block_free(block);
    ipfs_cid_free(cid);
    clear_error();
    return 0;
}

int64_t ipfs_ffi_block_stat(uint64_t handle, const char* cid_str) {
    struct IpfsNode* node = node_lookup(handle);
    if (!node) {
        set_error("invalid handle %llu", (unsigned long long)handle);
        return -1;
    }
    if (!cid_str) {
        set_error("cid_str is null");
        return -1;
    }

    struct Cid* cid = NULL;
    if (!ipfs_cid_decode_hash_from_base58((const unsigned char*)cid_str, strlen(cid_str), &cid)) {
        set_error("failed to decode cid: %s", cid_str);
        return -1;
    }

    struct Block* block = NULL;
    int ret = ipfs_repo_fsrepo_block_read(cid->hash, cid->hash_length, &block, node->repo);
    ipfs_cid_free(cid);
    if (!ret || !block) {
        set_error("failed to stat block: %s", cid_str);
        return -1;
    }

    int64_t size = (int64_t)block->data_length;
    ipfs_block_free(block);
    clear_error();
    return size;
}

void ipfs_ffi_free_buffer(uint8_t* buf) {
    free(buf);
}
