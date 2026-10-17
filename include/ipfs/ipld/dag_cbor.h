#ifndef __IPFS_IPLD_DAG_CBOR_H__
#define __IPFS_IPLD_DAG_CBOR_H__

#include <stdint.h>
#include <stddef.h>

#define DAG_CBOR_LINK_TAG 42
#define MULTIBASE_BINARY_PREFIX 0x00
#define DAG_CBOR_LINK_MAX_BYTES 128

/**
 * DAG-CBOR value types.
 */
typedef enum {
    DAG_CBOR_TYPE_NULL = 0,
    DAG_CBOR_TYPE_BOOL,
    DAG_CBOR_TYPE_INT,
    DAG_CBOR_TYPE_FLOAT,
    DAG_CBOR_TYPE_STRING,
    DAG_CBOR_TYPE_BYTES,
    DAG_CBOR_TYPE_ARRAY,
    DAG_CBOR_TYPE_MAP,
    DAG_CBOR_TYPE_LINK
} DagCborType;

struct DagCborValue;

typedef struct {
    struct DagCborValue **items;
    size_t count;
    size_t capacity;
} DagCborArray;

typedef struct {
    char **keys;
    struct DagCborValue **values;
    size_t count;
    size_t capacity;
} DagCborMap;

struct DagCborValue {
    DagCborType type;
    union {
        int boolean;
        int64_t integer;
        double floating;
        struct { char *data; size_t len; } string;
        struct { uint8_t *data; size_t len; } bytes;
        DagCborArray array;
        DagCborMap map;
        struct { uint8_t bytes[DAG_CBOR_LINK_MAX_BYTES]; size_t len; } link;
    } as;
};

/**
 * Encoded DAG-CBOR Tag 42 Link (legacy helper).
 */
struct DagCborLink {
    uint8_t bytes[DAG_CBOR_LINK_MAX_BYTES];
    size_t len;
};

int ipfs_ipld_dag_cbor_link_encode(const uint8_t *cid_bytes, size_t cid_len, struct DagCborLink *out);
int ipfs_ipld_dag_cbor_link_decode(const struct DagCborLink *link, uint8_t *cid_bytes, size_t *cid_len);
int ipfs_ipld_dag_cbor_link_is_tag42(const uint8_t *bytes, size_t len);

/* Value API */
struct DagCborValue *dag_cbor_value_new_null(void);
struct DagCborValue *dag_cbor_value_new_bool(int v);
struct DagCborValue *dag_cbor_value_new_int(int64_t v);
struct DagCborValue *dag_cbor_value_new_string(const char *s);
struct DagCborValue *dag_cbor_value_new_bytes(const uint8_t *data, size_t len);
struct DagCborValue *dag_cbor_value_new_array(void);
struct DagCborValue *dag_cbor_value_new_map(void);
struct DagCborValue *dag_cbor_value_new_link(const uint8_t *cid_bytes, size_t cid_len);
void dag_cbor_value_free(struct DagCborValue *v);

int dag_cbor_value_array_append(struct DagCborValue *arr, struct DagCborValue *item);
int dag_cbor_value_map_set(struct DagCborValue *map, const char *key, struct DagCborValue *val);

/* Encode/decode */
int dag_cbor_encode(const struct DagCborValue *v, uint8_t **out, size_t *out_len);
int dag_cbor_decode(const uint8_t *buf, size_t len, struct DagCborValue **out);

#endif