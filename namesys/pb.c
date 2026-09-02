#include <string.h>
#include <stdlib.h>
#include "ipfs/namesys/routing.h"
#include "ipfs/namesys/pb.h"
#include "protobuf.h"

/*
int IpnsEntry_ValidityType_value (char *s)
{
    int r;

    if (!s) {
        return -1; // invalid.
    }

    for (r = 0 ; IpnsEntry_ValidityType_name[r] ; r++) {
        if (strcmp (IpnsEntry_ValidityType_name[r], s) == 0) {
            return r; // found
        }
    }

    return -1; // not found.
}
*/

struct ipns_entry* ipfs_namesys_pb_new_ipns_entry ()
{
    return calloc(1, sizeof (struct ipns_entry));
}

void ipfs_namesys_ipnsentry_reset (struct ipns_entry *m)
{
    if (m) {
        if (m->value) {
            free(m->value);
            m->value = NULL;
        }
        if (m->signature) {
            free(m->signature);
            m->signature = NULL;
            m->signature_size = 0;
        }
        if (m->validityType) {
            free(m->validityType);
            m->validityType = NULL;
        }
        if (m->validity) {
            free(m->validity);
            m->validity = NULL;
        }
        if (m->sequence) {
            free(m->sequence);
            m->sequence = NULL;
        }
        if (m->ttl) {
            free(m->ttl);
            m->ttl = NULL;
        }
        if (m->eol) {
            free(m->eol);
            m->eol = NULL;
        }
        // NOTE: m->cache is not freed here; it is managed separately.
    }
}

char* ipfs_namesys_pb_get_validity (struct ipns_entry* entry)
{
    if (entry) {
        return entry->validity;
    }
    return NULL;
}

char* ipfs_ipns_entry_get_signature(struct ipns_entry* entry)
{
    if (entry) {
        return entry->signature;
    }
    return NULL;
}

int ipfs_namesys_pb_get_value (char** val, struct ipns_entry* entry)
{
    if (!val || !entry) {
        return 0;
    }
    *val = entry->value;
    return 1;
}

IpnsEntry_ValidityType ipfs_namesys_pb_get_validity_type (struct ipns_entry* entry)
{
    if (entry && entry->validityType) {
        return *entry->validityType;
    }
    return IpnsEntry_EOL;
}

int ipfs_namesys_pb_ipns_entry_encode(struct ipns_entry* entry, unsigned char* buffer, size_t max_buffer_size, size_t* bytes_written)
{
    size_t bytes_used = 0;
    *bytes_written = 0;

    if (!entry || !buffer || max_buffer_size == 0) {
        return 0;
    }

    if (entry->value) {
        if (!protobuf_encode_length_delimited(1, WIRETYPE_LENGTH_DELIMITED, entry->value, strlen(entry->value),
                buffer + *bytes_written, max_buffer_size - *bytes_written, &bytes_used))
            return 0;
        *bytes_written += bytes_used;
    }
    if (entry->signature && entry->signature_size > 0) {
        if (!protobuf_encode_length_delimited(2, WIRETYPE_LENGTH_DELIMITED, entry->signature, entry->signature_size,
                buffer + *bytes_written, max_buffer_size - *bytes_written, &bytes_used))
            return 0;
        *bytes_written += bytes_used;
    }
    if (entry->validityType) {
        if (!protobuf_encode_varint(3, WIRETYPE_VARINT, (unsigned long long)*entry->validityType,
                buffer + *bytes_written, max_buffer_size - *bytes_written, &bytes_used))
            return 0;
        *bytes_written += bytes_used;
    }
    if (entry->validity) {
        if (!protobuf_encode_length_delimited(4, WIRETYPE_LENGTH_DELIMITED, entry->validity, strlen(entry->validity),
                buffer + *bytes_written, max_buffer_size - *bytes_written, &bytes_used))
            return 0;
        *bytes_written += bytes_used;
    }
    if (entry->sequence) {
        if (!protobuf_encode_varint(5, WIRETYPE_VARINT, (unsigned long long)*entry->sequence,
                buffer + *bytes_written, max_buffer_size - *bytes_written, &bytes_used))
            return 0;
        *bytes_written += bytes_used;
    }
    if (entry->ttl) {
        if (!protobuf_encode_varint(6, WIRETYPE_VARINT, (unsigned long long)*entry->ttl,
                buffer + *bytes_written, max_buffer_size - *bytes_written, &bytes_used))
            return 0;
        *bytes_written += bytes_used;
    }
    return 1;
}

int ipfs_namesys_pb_ipns_entry_decode(const unsigned char* buffer, size_t buffer_size, struct ipns_entry** entry)
{
    size_t pos = 0;
    int field_no;
    enum WireType field_type;
    size_t bytes_read = 0;

    if (!buffer || buffer_size == 0 || !entry) {
        return 0;
    }

    *entry = ipfs_namesys_pb_new_ipns_entry();
    if (!*entry) {
        return 0;
    }

    while (pos < buffer_size) {
        if (!protobuf_decode_field_and_type(&buffer[pos], buffer_size - pos, &field_no, &field_type, &bytes_read)) {
            goto error;
        }
        pos += bytes_read;

        switch (field_no) {
            case 1: // value
                if (!protobuf_decode_string(&buffer[pos], buffer_size - pos, &(*entry)->value, &bytes_read)) {
                    goto error;
                }
                pos += bytes_read;
                break;
            case 2: { // signature (binary)
                char* sig = NULL;
                size_t sig_len = 0;
                if (!protobuf_decode_length_delimited(&buffer[pos], buffer_size - pos, &sig, &sig_len, &bytes_read)) {
                    goto error;
                }
                (*entry)->signature = sig;
                (*entry)->signature_size = sig_len;
                pos += bytes_read;
                break;
            }
            case 3: { // validityType
                unsigned long long vt = 0;
                if (!protobuf_decode_varint(&buffer[pos], buffer_size - pos, &vt, &bytes_read)) {
                    goto error;
                }
                (*entry)->validityType = malloc(sizeof(int32_t));
                if (!(*entry)->validityType) {
                    goto error;
                }
                *(*entry)->validityType = (int32_t)vt;
                pos += bytes_read;
                break;
            }
            case 4: // validity
                if (!protobuf_decode_string(&buffer[pos], buffer_size - pos, &(*entry)->validity, &bytes_read)) {
                    goto error;
                }
                pos += bytes_read;
                break;
            case 5: { // sequence
                unsigned long long seq = 0;
                if (!protobuf_decode_varint(&buffer[pos], buffer_size - pos, &seq, &bytes_read)) {
                    goto error;
                }
                (*entry)->sequence = malloc(sizeof(uint64_t));
                if (!(*entry)->sequence) {
                    goto error;
                }
                *(*entry)->sequence = (uint64_t)seq;
                pos += bytes_read;
                break;
            }
            case 6: { // ttl
                unsigned long long ttl = 0;
                if (!protobuf_decode_varint(&buffer[pos], buffer_size - pos, &ttl, &bytes_read)) {
                    goto error;
                }
                (*entry)->ttl = malloc(sizeof(uint64_t));
                if (!(*entry)->ttl) {
                    goto error;
                }
                *(*entry)->ttl = (uint64_t)ttl;
                pos += bytes_read;
                break;
            }
            default:
                // skip unknown fields
                if (field_type == WIRETYPE_VARINT) {
                    unsigned long long dummy;
                    if (!protobuf_decode_varint(&buffer[pos], buffer_size - pos, &dummy, &bytes_read)) {
                        goto error;
                    }
                    pos += bytes_read;
                } else if (field_type == WIRETYPE_LENGTH_DELIMITED) {
                    char* dummy = NULL;
                    size_t dummy_len = 0;
                    if (!protobuf_decode_length_delimited(&buffer[pos], buffer_size - pos, &dummy, &dummy_len, &bytes_read)) {
                        goto error;
                    }
                    if (dummy) free(dummy);
                    pos += bytes_read;
                } else if (field_type == WIRETYPE_64BIT) {
                    pos += 8;
                } else if (field_type == WIRETYPE_32BIT) {
                    pos += 4;
                } else {
                    // start/end group - shouldn't happen for this schema
                    goto error;
                }
                break;
        }
    }
    return 1;

error:
    ipfs_namesys_ipnsentry_reset(*entry);
    free(*entry);
    *entry = NULL;
    return 0;
}
