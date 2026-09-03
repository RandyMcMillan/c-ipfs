#pragma once
#include <stdint.h>
#include <stddef.h>

typedef int32_t IpnsEntry_ValidityType;

struct ipns_entry {
    char *value;
    char *signature;
    size_t signature_size;
    int32_t *validityType;
    char *validity;
    uint64_t *sequence;
    uint64_t *ttl;
    struct routingResolver *cache; // cache and eol should be the last items.
    struct timespec *eol;
};
struct namesys_pb {
    // IPNS protobuf entry container
    struct ipns_entry *IpnsEntry;
};

// setting an EOL says "this record is valid until..."
const static IpnsEntry_ValidityType IpnsEntry_EOL = 0;

/*
static char *IpnsEntry_ValidityType_name[] = {
    "EOL",
    NULL
};
*/

int IpnsEntry_ValidityType_value (char *s);
struct ipns_entry* ipfs_namesys_pb_new_ipns_entry ();
char* ipfs_namesys_pb_get_validity (struct ipns_entry*);
char* ipns_entry_data_for_sig(struct ipns_entry*);
char* ipfs_ipns_entry_get_signature(struct ipns_entry*);
int ipfs_namesys_pb_get_value (char**, struct ipns_entry*);
IpnsEntry_ValidityType ipfs_namesys_pb_get_validity_type (struct ipns_entry*);
void ipfs_namesys_ipnsentry_reset (struct ipns_entry *m);

/**
 * Encode an ipns_entry into protobuf format
 * @param entry the entry to encode
 * @param buffer where to store the encoded bytes
 * @param max_buffer_size the size of the allocated buffer
 * @param bytes_written the number of bytes written
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_namesys_pb_ipns_entry_encode(struct ipns_entry* entry, unsigned char* buffer, size_t max_buffer_size, size_t* bytes_written);

/**
 * Decode a protobuf byte array into an ipns_entry
 * @param buffer the protobuf bytes
 * @param buffer_size the number of bytes in the buffer
 * @param entry where to store the decoded entry (allocated)
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_namesys_pb_ipns_entry_decode(const unsigned char* buffer, size_t buffer_size, struct ipns_entry** entry);
