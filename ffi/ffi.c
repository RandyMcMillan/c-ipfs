/**
 * C FFI layer for c-ipfs, matching the kubo FFI interface.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <unistd.h>

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
#include "ipfs/nostr/event.h"
#include "hex.h"
#include "ipfs/repo/fsrepo/jsmn.h"
#include "libp2p/crypto/rsa.h"
#include "libp2p/crypto/key.h"
#include "libp2p/crypto/peerutils.h"
#include "libp2p/net/p2pnet.h"
#include "multiaddr/multiaddr.h"

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

/* ---------------------------------------------------------------------------
 * Nostr FFI
 * --------------------------------------------------------------------------- */

static int unescape_json_str(const char* src, int src_len, char* dst, size_t dst_max) {
    size_t j = 0;
    for (int i = 0; i < src_len && j < dst_max - 1; i++) {
        if (src[i] == '\\' && i + 1 < src_len) {
            switch (src[i + 1]) {
                case '"': dst[j++] = '"'; i++; break;
                case '\\': dst[j++] = '\\'; i++; break;
                case '/': dst[j++] = '/'; i++; break;
                case 'b': dst[j++] = '\b'; i++; break;
                case 'f': dst[j++] = '\f'; i++; break;
                case 'n': dst[j++] = '\n'; i++; break;
                case 'r': dst[j++] = '\r'; i++; break;
                case 't': dst[j++] = '\t'; i++; break;
                default: dst[j++] = src[i]; break;
            }
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
    return 1;
}

static int nostr_event_from_json(const char* json, struct NostrEvent* ev) {
    jsmn_parser parser;
    jsmntok_t tokens[512];
    jsmn_init(&parser);
    int r = jsmn_parse(&parser, json, strlen(json), tokens, 512);
    if (r < 0 || tokens[0].type != JSMN_OBJECT)
        return 0;

    ipfs_nostr_event_init(ev);

    for (int i = 1; i < r; i++) {
        if (tokens[i].type != JSMN_STRING)
            continue;
        int klen = tokens[i].end - tokens[i].start;
        const char* key = json + tokens[i].start;
        i++;
        if (i >= r)
            break;
        jsmntok_t* val = &tokens[i];
        int vlen = val->end - val->start;
        const char* vstr = json + val->start;

        if (klen == 2 && memcmp(key, "id", 2) == 0) {
            if (!hex_decode(vstr, vlen, ev->id, 32)) return 0;
        } else if (klen == 6 && memcmp(key, "pubkey", 6) == 0) {
            if (!hex_decode(vstr, vlen, ev->pubkey, 32)) return 0;
        } else if (klen == 3 && memcmp(key, "sig", 3) == 0) {
            if (!hex_decode(vstr, vlen, ev->sig, 64)) return 0;
        } else if (klen == 10 && memcmp(key, "created_at", 10) == 0) {
            ev->created_at = (uint64_t)strtoull(vstr, NULL, 10);
        } else if (klen == 4 && memcmp(key, "kind", 4) == 0) {
            ev->kind = atoi(vstr);
        } else if (klen == 7 && memcmp(key, "content", 7) == 0) {
            if (val->type != JSMN_STRING) return 0;
            if (!unescape_json_str(vstr, vlen, ev->content, sizeof(ev->content))) return 0;
        } else if (klen == 4 && memcmp(key, "tags", 4) == 0) {
            if (val->type != JSMN_ARRAY) return 0;
            int tag_count = val->size;
            i++;
            for (int t = 0; t < tag_count && i < r; t++) {
                if (tokens[i].type != JSMN_ARRAY) {
                    i++;
                    t--;
                    continue;
                }
                int tag_end = tokens[i].end;
                i++;
                char* elems[16];
                int ecount = 0;
                while (i < r && tokens[i].start < tag_end && ecount < 16) {
                    int elen = tokens[i].end - tokens[i].start;
                    elems[ecount] = malloc(elen + 1);
                    if (!elems[ecount]) {
                        for (int k = 0; k < ecount; k++) free(elems[k]);
                        return 0;
                    }
                    memcpy(elems[ecount], json + tokens[i].start, elen);
                    elems[ecount][elen] = '\0';
                    ecount++;
                    i++;
                }
                if (ecount >= 2) {
                    switch (ecount) {
                        case 2: ipfs_nostr_tags_add(&ev->tags, elems[0], elems[1]); break;
                        case 3: ipfs_nostr_tags_add_n(&ev->tags, 3, elems[0], elems[1], elems[2]); break;
                        case 4: ipfs_nostr_tags_add_n(&ev->tags, 4, elems[0], elems[1], elems[2], elems[3]); break;
                        default: ipfs_nostr_tags_add_n(&ev->tags, ecount, elems[0], elems[1], elems[2], elems[3]); break;
                    }
                }
                for (int k = 0; k < ecount; k++) free(elems[k]);
            }
            i--;
        }
    }
    return 1;
}

char* ipfs_ffi_nostr_generate_key(void) {
    void* ctx = nostr_context_new();
    if (!ctx) {
        set_error("failed to create nostr context");
        return NULL;
    }
    struct NostrKey key;
    if (!nostr_key_generate(ctx, &key)) {
        nostr_context_free(ctx);
        set_error("failed to generate key");
        return NULL;
    }
    char* hex = malloc(65);
    if (!hex) {
        nostr_context_free(ctx);
        set_error("out of memory");
        return NULL;
    }
    if (!hex_encode(key.seckey, 32, hex, 65)) {
        free(hex);
        nostr_context_free(ctx);
        set_error("hex encode failed");
        return NULL;
    }
    nostr_context_free(ctx);
    clear_error();
    return hex;
}

char* ipfs_ffi_nostr_get_public_key(const char* sk) {
    if (!sk || strlen(sk) != 64) {
        set_error("invalid secret key");
        return NULL;
    }
    void* ctx = nostr_context_new();
    if (!ctx) {
        set_error("failed to create nostr context");
        return NULL;
    }
    struct NostrKey key;
    if (!nostr_key_from_hex(ctx, sk, &key)) {
        nostr_context_free(ctx);
        set_error("failed to derive public key");
        return NULL;
    }
    char* hex = malloc(65);
    if (!hex) {
        nostr_context_free(ctx);
        set_error("out of memory");
        return NULL;
    }
    if (!hex_encode(key.pubkey, 32, hex, 65)) {
        free(hex);
        nostr_context_free(ctx);
        set_error("hex encode failed");
        return NULL;
    }
    nostr_context_free(ctx);
    clear_error();
    return hex;
}

char* ipfs_ffi_nostr_event_sign(const char* sk, const char* content, int kind) {
    if (!sk || strlen(sk) != 64) {
        set_error("invalid secret key");
        return NULL;
    }
    void* ctx = nostr_context_new();
    if (!ctx) {
        set_error("failed to create nostr context");
        return NULL;
    }
    struct NostrKey key;
    if (!nostr_key_from_hex(ctx, sk, &key)) {
        nostr_context_free(ctx);
        set_error("failed to parse secret key");
        return NULL;
    }
    struct NostrEvent ev;
    ipfs_nostr_event_init(&ev);
    ev.kind = kind;
    memcpy(ev.pubkey, key.pubkey, 32);
    if (content) {
        strncpy(ev.content, content, sizeof(ev.content) - 1);
        ev.content[sizeof(ev.content) - 1] = '\0';
    }
    unsigned char commit_buf[32768];
    if (!ipfs_nostr_event_commit(&ev, commit_buf, sizeof(commit_buf))) {
        nostr_context_free(ctx);
        set_error("event commit failed");
        return NULL;
    }
    if (!ipfs_nostr_event_sign(ctx, &key, &ev)) {
        nostr_context_free(ctx);
        set_error("event sign failed");
        return NULL;
    }
    char* json = malloc(65536);
    if (!json) {
        nostr_context_free(ctx);
        set_error("out of memory");
        return NULL;
    }
    if (!ipfs_nostr_event_to_json(&ev, json, 65536)) {
        free(json);
        nostr_context_free(ctx);
        set_error("event json serialization failed");
        return NULL;
    }
    nostr_context_free(ctx);
    clear_error();
    return json;
}

int64_t ipfs_ffi_nostr_event_verify(const char* json_str) {
    if (!json_str || strlen(json_str) == 0) {
        set_error("json_str is null or empty");
        return -1;
    }
    void* ctx = nostr_context_new();
    if (!ctx) {
        set_error("failed to create nostr context");
        return -1;
    }
    struct NostrEvent ev;
    if (!nostr_event_from_json(json_str, &ev)) {
        nostr_context_free(ctx);
        set_error("failed to parse event json");
        return -1;
    }
    int ret = ipfs_nostr_event_verify(ctx, &ev);
    nostr_context_free(ctx);
    clear_error();
    return ret ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * Git FFI
 * --------------------------------------------------------------------------- */

typedef struct git_repo_handle {
    char* path;
} git_repo_handle_t;

static pthread_mutex_t git_repos_mutex = PTHREAD_MUTEX_INITIALIZER;
static git_repo_handle_t** git_repos = NULL;
static size_t git_repos_capacity = 0;
static uint64_t git_next_handle = 1;

static int run_git(const char* fmt, ...) {
    char cmd[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, args);
    va_end(args);
    int ret = system(cmd);
    if (ret == -1)
        return 0;
    return WIFEXITED(ret) && WEXITSTATUS(ret) == 0;
}

int64_t ipfs_ffi_git_clone(const char* url, const char* path, uint8_t bare) {
    if (!url || !path) {
        set_error("url or path is null");
        return -1;
    }
    if (!run_git("git clone %s%s \"%s\" %s", bare ? "--bare " : "", url, path, ">/dev/null 2>&1")) {
        set_error("git clone failed");
        return -1;
    }
    clear_error();
    return 0;
}

uint64_t ipfs_ffi_git_open(const char* path) {
    if (!path || strlen(path) == 0) {
        set_error("path is null or empty");
        return 0;
    }
    git_repo_handle_t* h = malloc(sizeof(git_repo_handle_t));
    if (!h) {
        set_error("out of memory");
        return 0;
    }
    h->path = strdup(path);
    if (!h->path) {
        free(h);
        set_error("out of memory");
        return 0;
    }
    pthread_mutex_lock(&git_repos_mutex);
    uint64_t handle = git_next_handle++;
    if (handle > git_repos_capacity) {
        size_t new_cap = git_repos_capacity ? git_repos_capacity * 2 : 4;
        git_repo_handle_t** new_repos = realloc(git_repos, new_cap * sizeof(git_repo_handle_t*));
        if (!new_repos) {
            pthread_mutex_unlock(&git_repos_mutex);
            free(h->path);
            free(h);
            set_error("out of memory growing handle table");
            return 0;
        }
        memset(new_repos + git_repos_capacity, 0, (new_cap - git_repos_capacity) * sizeof(git_repo_handle_t*));
        git_repos = new_repos;
        git_repos_capacity = new_cap;
    }
    git_repos[handle - 1] = h;
    pthread_mutex_unlock(&git_repos_mutex);
    clear_error();
    return handle;
}

char* ipfs_ffi_git_repo_head(uint64_t handle) {
    pthread_mutex_lock(&git_repos_mutex);
    if (handle == 0 || handle > git_repos_capacity || git_repos[handle - 1] == NULL) {
        pthread_mutex_unlock(&git_repos_mutex);
        set_error("invalid git handle %llu", (unsigned long long)handle);
        return NULL;
    }
    char* path = strdup(git_repos[handle - 1]->path);
    pthread_mutex_unlock(&git_repos_mutex);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" rev-parse HEAD 2>/dev/null", path);
    FILE* fp = popen(cmd, "r");
    free(path);
    if (!fp) {
        set_error("failed to run git rev-parse");
        return NULL;
    }
    char buf[128];
    if (!fgets(buf, sizeof(buf), fp)) {
        pclose(fp);
        set_error("git rev-parse returned no output");
        return NULL;
    }
    pclose(fp);
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';
    char* result = strdup(buf);
    if (!result) {
        set_error("out of memory");
        return NULL;
    }
    clear_error();
    return result;
}

int64_t ipfs_ffi_git_repo_free(uint64_t handle) {
    pthread_mutex_lock(&git_repos_mutex);
    if (handle == 0 || handle > git_repos_capacity || git_repos[handle - 1] == NULL) {
        pthread_mutex_unlock(&git_repos_mutex);
        set_error("invalid git handle %llu", (unsigned long long)handle);
        return -1;
    }
    git_repo_handle_t* h = git_repos[handle - 1];
    git_repos[handle - 1] = NULL;
    pthread_mutex_unlock(&git_repos_mutex);
    free(h->path);
    free(h);
    clear_error();
    return 0;
}

int64_t ipfs_ffi_git_init(const char* path, uint8_t bare) {
    if (!path) {
        set_error("path is null");
        return -1;
    }
    if (!run_git("git init %s\"%s\" >/dev/null 2>&1", bare ? "--bare " : "", path)) {
        set_error("git init failed");
        return -1;
    }
    clear_error();
    return 0;
}

/* ---------------------------------------------------------------------------
 * libp2p standalone host FFI
 * --------------------------------------------------------------------------- */

typedef struct libp2p_host_handle {
    struct Libp2pPeer* peer;
    int listen_fd;
    uint16_t listen_port;
    struct RsaPrivateKey* private_key;
} libp2p_host_handle_t;

static pthread_mutex_t libp2p_hosts_mutex = PTHREAD_MUTEX_INITIALIZER;
static libp2p_host_handle_t** libp2p_hosts = NULL;
static size_t libp2p_hosts_capacity = 0;
static uint64_t libp2p_next_handle = 1;

uint64_t ipfs_ffi_libp2p_host_new(void) {
    libp2p_host_handle_t* h = calloc(1, sizeof(libp2p_host_handle_t));
    if (!h) {
        set_error("out of memory");
        return 0;
    }

    h->private_key = libp2p_crypto_rsa_rsa_private_key_new();
    if (!h->private_key) {
        free(h);
        set_error("out of memory allocating rsa key");
        return 0;
    }

    if (!libp2p_crypto_rsa_generate_keypair(h->private_key, 2048)) {
        libp2p_crypto_rsa_rsa_private_key_free(h->private_key);
        free(h);
        set_error("failed to generate rsa keypair");
        return 0;
    }

    if (!libp2p_crypto_rsa_private_key_fill_public_key(h->private_key)) {
        libp2p_crypto_rsa_rsa_private_key_free(h->private_key);
        free(h);
        set_error("failed to fill public key");
        return 0;
    }

    struct PublicKey pub_key;
    pub_key.type = KEYTYPE_RSA;
    pub_key.data = (unsigned char*)h->private_key->public_key_der;
    pub_key.data_size = h->private_key->public_key_length;

    char* peer_id = NULL;
    if (!libp2p_crypto_public_key_to_peer_id(&pub_key, &peer_id)) {
        libp2p_crypto_rsa_rsa_private_key_free(h->private_key);
        free(h);
        set_error("failed to derive peer id");
        return 0;
    }

    h->peer = libp2p_peer_new();
    if (!h->peer) {
        free(peer_id);
        libp2p_crypto_rsa_rsa_private_key_free(h->private_key);
        free(h);
        set_error("out of memory allocating peer");
        return 0;
    }
    h->peer->id = peer_id;
    h->peer->id_size = strlen(peer_id);
    h->peer->is_local = 1;

    h->listen_fd = socket_tcp4();
    if (h->listen_fd < 0) {
        libp2p_peer_free(h->peer);
        libp2p_crypto_rsa_rsa_private_key_free(h->private_key);
        free(h);
        set_error("failed to create tcp socket");
        return 0;
    }

    uint32_t local_ip = htonl(0x7f000001);
    uint16_t local_port = 0;
    if (socket_listen(h->listen_fd, &local_ip, &local_port) < 0) {
        close(h->listen_fd);
        libp2p_peer_free(h->peer);
        libp2p_crypto_rsa_rsa_private_key_free(h->private_key);
        free(h);
        set_error("failed to listen on tcp socket");
        return 0;
    }

    h->listen_port = local_port;

    pthread_mutex_lock(&libp2p_hosts_mutex);
    uint64_t handle = libp2p_next_handle++;
    if (handle > libp2p_hosts_capacity) {
        size_t new_cap = libp2p_hosts_capacity ? libp2p_hosts_capacity * 2 : 4;
        libp2p_host_handle_t** new_hosts = realloc(libp2p_hosts, new_cap * sizeof(libp2p_host_handle_t*));
        if (!new_hosts) {
            pthread_mutex_unlock(&libp2p_hosts_mutex);
            close(h->listen_fd);
            libp2p_peer_free(h->peer);
            libp2p_crypto_rsa_rsa_private_key_free(h->private_key);
            free(h);
            set_error("out of memory growing handle table");
            return 0;
        }
        memset(new_hosts + libp2p_hosts_capacity, 0, (new_cap - libp2p_hosts_capacity) * sizeof(libp2p_host_handle_t*));
        libp2p_hosts = new_hosts;
        libp2p_hosts_capacity = new_cap;
    }
    libp2p_hosts[handle - 1] = h;
    pthread_mutex_unlock(&libp2p_hosts_mutex);

    clear_error();
    return handle;
}

int64_t ipfs_ffi_libp2p_host_close(uint64_t handle) {
    pthread_mutex_lock(&libp2p_hosts_mutex);
    if (handle == 0 || handle > libp2p_hosts_capacity || libp2p_hosts[handle - 1] == NULL) {
        pthread_mutex_unlock(&libp2p_hosts_mutex);
        set_error("invalid libp2p handle %llu", (unsigned long long)handle);
        return -1;
    }
    libp2p_host_handle_t* h = libp2p_hosts[handle - 1];
    libp2p_hosts[handle - 1] = NULL;
    pthread_mutex_unlock(&libp2p_hosts_mutex);

    if (h->listen_fd >= 0)
        close(h->listen_fd);
    if (h->peer)
        libp2p_peer_free(h->peer);
    if (h->private_key)
        libp2p_crypto_rsa_rsa_private_key_free(h->private_key);
    free(h);
    clear_error();
    return 0;
}

char* ipfs_ffi_libp2p_host_peer_id(uint64_t handle) {
    pthread_mutex_lock(&libp2p_hosts_mutex);
    if (handle == 0 || handle > libp2p_hosts_capacity || libp2p_hosts[handle - 1] == NULL) {
        pthread_mutex_unlock(&libp2p_hosts_mutex);
        set_error("invalid libp2p handle %llu", (unsigned long long)handle);
        return NULL;
    }
    libp2p_host_handle_t* h = libp2p_hosts[handle - 1];
    char* result = h->peer && h->peer->id ? strdup(h->peer->id) : NULL;
    pthread_mutex_unlock(&libp2p_hosts_mutex);
    clear_error();
    return result;
}

char* ipfs_ffi_libp2p_host_listening_addrs(uint64_t handle) {
    pthread_mutex_lock(&libp2p_hosts_mutex);
    if (handle == 0 || handle > libp2p_hosts_capacity || libp2p_hosts[handle - 1] == NULL) {
        pthread_mutex_unlock(&libp2p_hosts_mutex);
        set_error("invalid libp2p handle %llu", (unsigned long long)handle);
        return NULL;
    }
    libp2p_host_handle_t* h = libp2p_hosts[handle - 1];
    size_t len = 64;
    if (h->peer && h->peer->id)
        len += strlen(h->peer->id);
    char* result = malloc(len);
    if (!result) {
        pthread_mutex_unlock(&libp2p_hosts_mutex);
        set_error("out of memory");
        return NULL;
    }
    snprintf(result, len, "/ip4/127.0.0.1/tcp/%u/p2p/%s",
             h->listen_port, h->peer && h->peer->id ? h->peer->id : "?");
    pthread_mutex_unlock(&libp2p_hosts_mutex);
    clear_error();
    return result;
}

int64_t ipfs_ffi_libp2p_host_connect(uint64_t handle, const char* addr) {
    if (!addr || strlen(addr) == 0) {
        set_error("addr is null or empty");
        return -1;
    }

    struct MultiAddress* ma = multiaddress_new_from_string(addr);
    if (!ma) {
        set_error("failed to parse multiaddr: %s", addr);
        return -1;
    }

    char* ip_str = NULL;
    if (!multiaddress_get_ip_address(ma, &ip_str)) {
        multiaddress_free(ma);
        set_error("failed to extract ip from multiaddr");
        return -1;
    }

    int port = multiaddress_get_ip_port(ma);
    if (port <= 0) {
        free(ip_str);
        multiaddress_free(ma);
        set_error("failed to extract port from multiaddr");
        return -1;
    }

    int fd = socket_tcp4();
    if (fd < 0) {
        free(ip_str);
        multiaddress_free(ma);
        set_error("failed to create socket");
        return -1;
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = inet_addr(ip_str);
    free(ip_str);
    multiaddress_free(ma);

    if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) != 0) {
        close(fd);
        set_error("failed to connect to %s", addr);
        return -1;
    }

    close(fd);
    clear_error();
    return 0;
}

void ipfs_ffi_free_buffer(uint8_t* buf) {
    free(buf);
}
