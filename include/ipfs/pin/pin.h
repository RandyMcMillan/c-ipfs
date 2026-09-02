#ifndef IPFS_PIN_H
    #define IPFS_PIN_H

    #include "ipfs/util/errs.h"

    #ifdef IPFS_PIN_C
        const char *ipfs_pin_linkmap[] = {
            "recursive",
            "direct",
            "indirect",
            "internal",
            "not pinned",
            "any",
            "all"
        };
    #else // IPFS_PIN_C
        extern const char *ipfs_pin_map[];
    #endif // IPFS_PIN_C
    enum {
        Recursive = 0,
        Direct,
        Indirect,
        Internal,
        NotPinned,
        Any,
        All
    };

    typedef int PinMode;

    struct Pinned {
        struct Cid *Key;
        PinMode Mode;
        struct Cid *Via;
    };

    int ipfs_pin_init ();
    // Return pointer to string or NULL if invalid.
    char *ipfs_pin_mode_to_string (PinMode mode);
    // Return array index or -1 if fail.
    PinMode ipfs_string_to_pin_mode (char *str);
    int ipfs_pin_is_pinned (struct Pinned *p);
    // Find out if the child is in the hash.
    int ipfs_pin_has_child (struct FSRepo *ds,
                            unsigned char *hash,  size_t hash_size,
                            unsigned char *child, size_t child_size);

    struct PinEntry {
        unsigned char* hash;
        size_t hash_size;
        PinMode mode;
        struct PinEntry* next;
    };

    /**
     * Add a pin to the pin set and persist it to the datastore.
     */
    int ipfs_pin_add(struct FSRepo* fs_repo, const unsigned char* hash, size_t hash_size, PinMode mode);

    /**
     * Remove a pin from the pin set.
     */
    int ipfs_pin_rm(struct FSRepo* fs_repo, const unsigned char* hash, size_t hash_size);

    /**
     * Load the pin set from the datastore.
     */
    struct PinEntry* ipfs_pin_load(struct FSRepo* fs_repo);

    /**
     * Free a linked list of PinEntry.
     */
    void ipfs_pin_entry_free(struct PinEntry* entries);

    /**
     * Run garbage collection: delete unreferenced blocks from the blockstore.
     * @param fs_repo the repository
     * @param bytes_reclaimed number of bytes reclaimed
     * @returns 1 on success, 0 on failure
     */
    int ipfs_gc_collect(struct FSRepo* fs_repo, size_t* bytes_reclaimed);

#endif // IPFS_PIN_H
