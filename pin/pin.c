#include <stdlib.h>
#include <string.h>

#include "ipfs/repo/fsrepo/fs_repo.h"

#define IPFS_PIN_C
#include "ipfs/pin/pin.h"

#include "ipfs/cid/cid.h"
#include "ipfs/datastore/key.h"
#include "ipfs/merkledag/merkledag.h"
#include "ipfs/blocks/blockstore.h"
#include "ipfs/util/errs.h"

// package pin implements structures and methods to keep track of
// which objects a user wants to keep stored locally.

#define PIN_DATASTOREKEY_SIZE 100
char *pinDatastoreKey = NULL;
size_t pinDatastoreKeySize = 0;

struct Cid *emptyKey = NULL;

int ipfs_pin_init ()
{
    int err;
    unsigned char *empty_hash = (unsigned char*) "QmdfTbBqBPQ7VNxZEYEj14VmRuZBkqFbiwReogJgS1zR1n";

    if (!pinDatastoreKey) { // initialize just one time.
        pinDatastoreKey = malloc(PIN_DATASTOREKEY_SIZE);
        if (!pinDatastoreKey) {
            return ErrAllocFailed;
        }
        err = ipfs_datastore_key_new("/local/pins", pinDatastoreKey, PIN_DATASTOREKEY_SIZE, &pinDatastoreKeySize);
        if (err) {
            free (pinDatastoreKey);
            pinDatastoreKey = NULL;
            return err;
        }

        if (!ipfs_cid_protobuf_decode(empty_hash, strlen ((char*)empty_hash), &emptyKey)) {
            return ErrCidDecodeFailed;
        }
    }

    return 0;
}

// Return pointer to string or NULL if invalid.
char *ipfs_pin_mode_to_string (PinMode mode)
{
    if (mode < 0 || mode >= (sizeof (ipfs_pin_linkmap) / sizeof (void*))) {
        return NULL;
    }
    return (char*)ipfs_pin_linkmap[mode];
}

// Return array index or -1 if fail.
PinMode ipfs_string_to_pin_mode (char *str)
{
    PinMode pm;

    for (pm = 0 ; pm < (sizeof (ipfs_pin_linkmap) / sizeof (void*)) ; pm++) {
        if (strcmp(ipfs_pin_linkmap[pm], str) == 0) {
            return pm;
        }
    }
    return -1; // not found.
}

int ipfs_pin_is_pinned (struct Pinned *p)
{
    return (p->Mode != NotPinned);
}

char *ipfs_pin_pinned_msg (struct Pinned *p)
{
    char *ret, *ptr, *msg;

    switch (p->Mode) {
        case NotPinned:
            msg = "not pinned";
            ret = malloc(strlen (msg) + 1);
            if (!ret) {
                return NULL;
            }
            strcpy(ret, msg);
            break;
        case Indirect:
            msg = "pinned via ";
            ret = malloc(strlen (msg) + p->Via->hash_length + 1);
            if (!ret) {
                return NULL;
            }
            ptr = ret;
            memcpy(ptr, msg, strlen(msg));
            ptr += strlen(msg);
            memcpy(ptr, p->Via->hash, p->Via->hash_length);
            ptr += p->Via->hash_length;
            *ptr = '\0';
            break;
        default:
            ptr = ipfs_pin_mode_to_string (p->Mode);
            if (!ptr) {
                return NULL;
            }
            msg = "pinned: ";
            ret = malloc(strlen (msg) + strlen (ptr) + 1);
            if (!ret) {
                return NULL;
            }
            strcpy(ret, msg);
            strcat(ret, ptr);
    }
    return ret;
}

// Find out if the child is in the hash.
int ipfs_pin_has_child (struct FSRepo *ds,
                        unsigned char *hash,  size_t hash_size,
                        unsigned char *child, size_t child_size)
{
    struct HashtableNode *node;
    struct NodeLink *node_link;

    if (ipfs_merkledag_get (hash, hash_size, &node, ds)) {
        if (node) {
            if ((node->hash_size == child_size) &&
                (memcmp (node->hash, child, child_size) == 0)) {
                return 1;
            }
            // browse the node links.
            for (node_link = node->head_link ; node_link ; node_link = node_link->next) {
                if ((node_link->hash_size == child_size) &&
                    (memcmp (node_link->hash, child, child_size) != 0) &&
                     ipfs_pin_has_child (ds, node_link->hash, node_link->hash_size,
                                             child, child_size)) {
                    return 1;
                }
                if ((node_link->hash_size == child_size) &&
                    (memcmp (node_link->hash, child, child_size) == 0)) {
                    return 1; // child is a child of the hash node.
                }
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Pin persistence and GC                                             */

static const char* PIN_DATASTORE_KEY = "/local/pins";

static int _pin_save(struct FSRepo* fs_repo, struct PinEntry* entries) {
    // Serialize pin list to binary: count (4 bytes BE) + [(hash_len 4 BE) + hash + mode 1]
    size_t buf_size = 4;
    struct PinEntry* e = entries;
    while (e != NULL) {
        buf_size += 4 + e->hash_size + 1;
        e = e->next;
    }
    unsigned char* buf = (unsigned char*)malloc(buf_size);
    if (!buf) return 0;

    size_t pos = 0;
    uint32_t count = 0;
    e = entries;
    while (e != NULL) { count++; e = e->next; }
    buf[pos++] = (count >> 24) & 0xFF;
    buf[pos++] = (count >> 16) & 0xFF;
    buf[pos++] = (count >> 8) & 0xFF;
    buf[pos++] = count & 0xFF;

    e = entries;
    while (e != NULL) {
        uint32_t hlen = (uint32_t)e->hash_size;
        buf[pos++] = (hlen >> 24) & 0xFF;
        buf[pos++] = (hlen >> 16) & 0xFF;
        buf[pos++] = (hlen >> 8) & 0xFF;
        buf[pos++] = hlen & 0xFF;
        memcpy(&buf[pos], e->hash, hlen);
        pos += hlen;
        buf[pos++] = (unsigned char)e->mode;
        e = e->next;
    }

    struct DatastoreRecord rec;
    rec.key = (uint8_t*)PIN_DATASTORE_KEY;
    rec.key_size = strlen(PIN_DATASTORE_KEY);
    rec.value = buf;
    rec.value_size = pos;
    rec.timestamp = 0;
    int ret = fs_repo->config->datastore->datastore_put(&rec, fs_repo->config->datastore);
    free(buf);
    return ret;
}

struct PinEntry* ipfs_pin_load(struct FSRepo* fs_repo) {
    struct DatastoreRecord* rec = NULL;
    if (!fs_repo->config->datastore->datastore_get(
            (const unsigned char*)PIN_DATASTORE_KEY, strlen(PIN_DATASTORE_KEY), &rec, fs_repo->config->datastore))
        return NULL;

    struct PinEntry* head = NULL;
    struct PinEntry* tail = NULL;
    if (rec->value_size < 4) {
        libp2p_datastore_record_free(rec);
        return NULL;
    }
    size_t pos = 0;
    uint32_t count = ((uint32_t)rec->value[pos]) << 24 |
                     ((uint32_t)rec->value[pos+1]) << 16 |
                     ((uint32_t)rec->value[pos+2]) << 8 |
                     ((uint32_t)rec->value[pos+3]);
    pos += 4;

    for (uint32_t i = 0; i < count; i++) {
        if (pos + 4 > rec->value_size) break;
        uint32_t hlen = ((uint32_t)rec->value[pos]) << 24 |
                        ((uint32_t)rec->value[pos+1]) << 16 |
                        ((uint32_t)rec->value[pos+2]) << 8 |
                        ((uint32_t)rec->value[pos+3]);
        pos += 4;
        if (pos + hlen + 1 > rec->value_size) break;

        struct PinEntry* e = (struct PinEntry*)calloc(1, sizeof(struct PinEntry));
        if (!e) break;
        e->hash = (unsigned char*)malloc(hlen);
        if (!e->hash) { free(e); break; }
        memcpy(e->hash, &rec->value[pos], hlen);
        e->hash_size = hlen;
        pos += hlen;
        e->mode = (PinMode)rec->value[pos++];
        e->next = NULL;

        if (head == NULL) head = e;
        else tail->next = e;
        tail = e;
    }
    libp2p_datastore_record_free(rec);
    return head;
}

void ipfs_pin_entry_free(struct PinEntry* entries) {
    while (entries != NULL) {
        struct PinEntry* next = entries->next;
        free(entries->hash);
        free(entries);
        entries = next;
    }
}

int ipfs_pin_add(struct FSRepo* fs_repo, const unsigned char* hash, size_t hash_size, PinMode mode) {
    struct PinEntry* entries = ipfs_pin_load(fs_repo);
    struct PinEntry* e = entries;
    while (e != NULL) {
        if (e->hash_size == hash_size && memcmp(e->hash, hash, hash_size) == 0) {
            e->mode = mode; // update existing
            int ret = _pin_save(fs_repo, entries);
            ipfs_pin_entry_free(entries);
            return ret;
        }
        e = e->next;
    }
    // append new
    struct PinEntry* new_entry = (struct PinEntry*)calloc(1, sizeof(struct PinEntry));
    if (!new_entry) {
        ipfs_pin_entry_free(entries);
        return 0;
    }
    new_entry->hash = (unsigned char*)malloc(hash_size);
    if (!new_entry->hash) {
        free(new_entry);
        ipfs_pin_entry_free(entries);
        return 0;
    }
    memcpy(new_entry->hash, hash, hash_size);
    new_entry->hash_size = hash_size;
    new_entry->mode = mode;
    new_entry->next = entries;
    int ret = _pin_save(fs_repo, new_entry);
    ipfs_pin_entry_free(new_entry);
    return ret;
}

int ipfs_pin_rm(struct FSRepo* fs_repo, const unsigned char* hash, size_t hash_size) {
    struct PinEntry* entries = ipfs_pin_load(fs_repo);
    struct PinEntry* prev = NULL;
    struct PinEntry* e = entries;
    int found = 0;
    while (e != NULL) {
        if (e->hash_size == hash_size && memcmp(e->hash, hash, hash_size) == 0) {
            found = 1;
            if (prev == NULL) {
                entries = e->next;
            } else {
                prev->next = e->next;
            }
            struct PinEntry* to_free = e;
            e = e->next;
            free(to_free->hash);
            free(to_free);
            continue;
        }
        prev = e;
        e = e->next;
    }
    if (!found) {
        ipfs_pin_entry_free(entries);
        return 0;
    }
    int ret = _pin_save(fs_repo, entries);
    ipfs_pin_entry_free(entries);
    return ret;
}

/* GC helpers */

static int _gc_mark_visit(struct HashtableNode* node, int depth, void* ctx) {
    (void)depth;
    struct CidSet* marked = (struct CidSet*)ctx;
    struct Cid cid;
    cid.hash = node->hash;
    cid.hash_length = node->hash_size;
    ipfs_cid_set_add(marked, &cid, 1);
    return 0;
}

int ipfs_gc_collect(struct FSRepo* fs_repo, size_t* bytes_reclaimed) {
    if (bytes_reclaimed) *bytes_reclaimed = 0;

    struct PinEntry* pins = ipfs_pin_load(fs_repo);

    struct CidSet* marked = ipfs_cid_set_new();
    if (!marked) {
        ipfs_pin_entry_free(pins);
        return 0;
    }

    // Mark phase: traverse each pinned root
    struct PinEntry* p = pins;
    while (p != NULL) {
        ipfs_merkledag_traverse(fs_repo, p->hash, p->hash_size, 1000, _gc_mark_visit, marked);
        p = p->next;
    }
    ipfs_pin_entry_free(pins);

    // Sweep phase: list all blocks and delete unmarked ones
    struct BlockstoreEntry* blocks = NULL;
    int num_blocks = ipfs_blockstore_list(fs_repo, &blocks);
    if (num_blocks < 0) {
        ipfs_cid_set_destroy(&marked);
        return 0;
    }

    struct Blockstore* bs = ipfs_blockstore_new(fs_repo);
    struct BlockstoreEntry* b = blocks;
    size_t reclaimed = 0;
    while (b != NULL) {
        struct Cid cid;
        cid.hash = b->hash;
        cid.hash_length = b->hash_size;
        if (!ipfs_cid_set_has(marked, &cid)) {
            // delete from blockstore
            bs->Delete(bs->blockstoreContext, &cid);
            reclaimed += b->block_size; /* block_size populated by ipfs_blockstore_list */
        }
        b = b->next;
    }
    ipfs_blockstore_list_free(blocks);
    ipfs_blockstore_free(bs);
    ipfs_cid_set_destroy(&marked);

    if (bytes_reclaimed) *bytes_reclaimed = reclaimed;
    return 1;
}
