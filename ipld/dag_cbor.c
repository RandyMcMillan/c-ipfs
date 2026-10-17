#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "ipfs/ipld/dag_cbor.h"

/* ---------- helpers ---------- */

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) abort();
    return p;
}

static void append(uint8_t **buf, size_t *len, size_t *cap, const uint8_t *data, size_t dlen) {
    if (*len + dlen > *cap) {
        size_t newcap = *cap ? *cap * 2 : 256;
        while (newcap < *len + dlen) newcap *= 2;
        *buf = realloc(*buf, newcap);
        *cap = newcap;
    }
    memcpy(*buf + *len, data, dlen);
    *len += dlen;
}

static void append_byte(uint8_t **buf, size_t *len, size_t *cap, uint8_t b) {
    append(buf, len, cap, &b, 1);
}

/* ---------- legacy link helpers ---------- */

int ipfs_ipld_dag_cbor_link_encode(const uint8_t *cid_bytes, size_t cid_len, struct DagCborLink *out) {
    if (!cid_bytes || !out || cid_len == 0 || cid_len > 100) return 0;
    size_t offset = 0;
    out->bytes[offset++] = 0xD8;
    out->bytes[offset++] = DAG_CBOR_LINK_TAG;
    size_t payload_len = cid_len + 1;
    if (payload_len < 24) {
        out->bytes[offset++] = 0x40 | (uint8_t)payload_len;
    } else if (payload_len <= 0xFF) {
        out->bytes[offset++] = 0x58;
        out->bytes[offset++] = (uint8_t)payload_len;
    } else return 0;
    out->bytes[offset++] = MULTIBASE_BINARY_PREFIX;
    memcpy(&out->bytes[offset], cid_bytes, cid_len);
    offset += cid_len;
    out->len = offset;
    return 1;
}

int ipfs_ipld_dag_cbor_link_decode(const struct DagCborLink *link, uint8_t *cid_bytes, size_t *cid_len) {
    if (!link || !cid_bytes || !cid_len || link->len < 4) return 0;
    size_t offset = 0;
    if (link->bytes[offset++] != 0xD8) return 0;
    if (link->bytes[offset++] != DAG_CBOR_LINK_TAG) return 0;
    size_t payload_len = 0;
    uint8_t bstr_header = link->bytes[offset++];
    if ((bstr_header & 0xE0) != 0x40) return 0;
    uint8_t bstr_info = bstr_header & 0x1F;
    if (bstr_info < 24) payload_len = bstr_info;
    else if (bstr_info == 24) {
        if (offset >= link->len) return 0;
        payload_len = link->bytes[offset++];
    } else return 0;
    if (offset + payload_len > link->len) return 0;
    if (link->bytes[offset++] != MULTIBASE_BINARY_PREFIX) return 0;
    size_t cid_len_out = payload_len - 1;
    if (cid_len_out == 0 || cid_len_out > 100) return 0;
    if (*cid_len < cid_len_out) return 0;
    memcpy(cid_bytes, &link->bytes[offset], cid_len_out);
    *cid_len = cid_len_out;
    return 1;
}

int ipfs_ipld_dag_cbor_link_is_tag42(const uint8_t *bytes, size_t len) {
    if (!bytes || len < 2) return 0;
    return (bytes[0] == 0xD8 && bytes[1] == DAG_CBOR_LINK_TAG);
}

/* ---------- value API ---------- */

struct DagCborValue *dag_cbor_value_new_null(void) {
    struct DagCborValue *v = xmalloc(sizeof(*v));
    memset(v, 0, sizeof(*v));
    v->type = DAG_CBOR_TYPE_NULL;
    return v;
}
struct DagCborValue *dag_cbor_value_new_bool(int v) {
    struct DagCborValue *val = xmalloc(sizeof(*val));
    memset(val, 0, sizeof(*val));
    val->type = DAG_CBOR_TYPE_BOOL;
    val->as.boolean = v ? 1 : 0;
    return val;
}
struct DagCborValue *dag_cbor_value_new_int(int64_t v) {
    struct DagCborValue *val = xmalloc(sizeof(*val));
    memset(val, 0, sizeof(*val));
    val->type = DAG_CBOR_TYPE_INT;
    val->as.integer = v;
    return val;
}
struct DagCborValue *dag_cbor_value_new_string(const char *s) {
    struct DagCborValue *val = xmalloc(sizeof(*val));
    memset(val, 0, sizeof(*val));
    val->type = DAG_CBOR_TYPE_STRING;
    size_t l = s ? strlen(s) : 0;
    val->as.string.data = xmalloc(l + 1);
    if (s) memcpy(val->as.string.data, s, l);
    val->as.string.data[l] = '\0';
    val->as.string.len = l;
    return val;
}
struct DagCborValue *dag_cbor_value_new_bytes(const uint8_t *data, size_t len) {
    struct DagCborValue *val = xmalloc(sizeof(*val));
    memset(val, 0, sizeof(*val));
    val->type = DAG_CBOR_TYPE_BYTES;
    val->as.bytes.data = xmalloc(len ? len : 1);
    if (data && len) memcpy(val->as.bytes.data, data, len);
    val->as.bytes.len = len;
    return val;
}
struct DagCborValue *dag_cbor_value_new_array(void) {
    struct DagCborValue *val = xmalloc(sizeof(*val));
    memset(val, 0, sizeof(*val));
    val->type = DAG_CBOR_TYPE_ARRAY;
    val->as.array.items = NULL;
    val->as.array.count = 0;
    val->as.array.capacity = 0;
    return val;
}
struct DagCborValue *dag_cbor_value_new_map(void) {
    struct DagCborValue *val = xmalloc(sizeof(*val));
    memset(val, 0, sizeof(*val));
    val->type = DAG_CBOR_TYPE_MAP;
    val->as.map.keys = NULL;
    val->as.map.values = NULL;
    val->as.map.count = 0;
    val->as.map.capacity = 0;
    return val;
}
struct DagCborValue *dag_cbor_value_new_link(const uint8_t *cid_bytes, size_t cid_len) {
    if (!cid_bytes || cid_len == 0 || cid_len > DAG_CBOR_LINK_MAX_BYTES - 1) return NULL;
    struct DagCborValue *val = xmalloc(sizeof(*val));
    memset(val, 0, sizeof(*val));
    val->type = DAG_CBOR_TYPE_LINK;
    val->as.link.len = cid_len;
    memcpy(val->as.link.bytes, cid_bytes, cid_len);
    return val;
}

int dag_cbor_value_array_append(struct DagCborValue *arr, struct DagCborValue *item) {
    if (!arr || arr->type != DAG_CBOR_TYPE_ARRAY || !item) return 0;
    if (arr->as.array.count == arr->as.array.capacity) {
        size_t newcap = arr->as.array.capacity ? arr->as.array.capacity * 2 : 8;
        arr->as.array.items = realloc(arr->as.array.items, newcap * sizeof(*arr->as.array.items));
        arr->as.array.capacity = newcap;
    }
    arr->as.array.items[arr->as.array.count++] = item;
    return 1;
}

int dag_cbor_value_map_set(struct DagCborValue *map, const char *key, struct DagCborValue *val) {
    if (!map || map->type != DAG_CBOR_TYPE_MAP || !key || !val) return 0;
    /* check existing */
    for (size_t i = 0; i < map->as.map.count; i++) {
        if (strcmp(map->as.map.keys[i], key) == 0) {
            dag_cbor_value_free(map->as.map.values[i]);
            map->as.map.values[i] = val;
            return 1;
        }
    }
    if (map->as.map.count == map->as.map.capacity) {
        size_t newcap = map->as.map.capacity ? map->as.map.capacity * 2 : 8;
        map->as.map.keys = realloc(map->as.map.keys, newcap * sizeof(*map->as.map.keys));
        map->as.map.values = realloc(map->as.map.values, newcap * sizeof(*map->as.map.values));
        map->as.map.capacity = newcap;
    }
    map->as.map.keys[map->as.map.count] = strdup(key);
    map->as.map.values[map->as.map.count] = val;
    map->as.map.count++;
    return 1;
}

void dag_cbor_value_free(struct DagCborValue *v) {
    if (!v) return;
    switch (v->type) {
        case DAG_CBOR_TYPE_STRING:
            free(v->as.string.data);
            break;
        case DAG_CBOR_TYPE_BYTES:
            free(v->as.bytes.data);
            break;
        case DAG_CBOR_TYPE_ARRAY:
            for (size_t i = 0; i < v->as.array.count; i++) dag_cbor_value_free(v->as.array.items[i]);
            free(v->as.array.items);
            break;
        case DAG_CBOR_TYPE_MAP:
            for (size_t i = 0; i < v->as.map.count; i++) {
                free(v->as.map.keys[i]);
                dag_cbor_value_free(v->as.map.values[i]);
            }
            free(v->as.map.keys);
            free(v->as.map.values);
            break;
        default: break;
    }
    free(v);
}

/* ---------- encode ---------- */

static int encode_value(const struct DagCborValue *v, uint8_t **buf, size_t *len, size_t *cap);

static int encode_uint_major(uint8_t **buf, size_t *len, size_t *cap, uint8_t major, uint64_t val) {
    uint8_t first = (uint8_t)((major << 5) | (val < 24 ? val : 24));
    if (val < 24) {
        append_byte(buf, len, cap, first);
        return 1;
    }
    append_byte(buf, len, cap, first);
    /* encode length */
    if (val <= 0xFF) { append_byte(buf, len, cap, (uint8_t)val); }
    else if (val <= 0xFFFF) { uint8_t b[2]={val>>8,val}; append(buf,len,cap,b,2); }
    else if (val <= 0xFFFFFFFF) { uint8_t b[4]={val>>24,val>>16,val>>8,val}; append(buf,len,cap,b,4); }
    else { uint8_t b[8]; for(int i=7;i>=0;i--) b[7-i]=val>>(i*8); append(buf,len,cap,b,8); }
    return 1;
}

static int encode_value(const struct DagCborValue *v, uint8_t **buf, size_t *len, size_t *cap) {
    if (!v) return 0;
    switch (v->type) {
        case DAG_CBOR_TYPE_NULL:
            append_byte(buf, len, cap, 0xf6);
            return 1;
        case DAG_CBOR_TYPE_BOOL:
            append_byte(buf, len, cap, v->as.boolean ? 0xf5 : 0xf4);
            return 1;
        case DAG_CBOR_TYPE_INT:
            if (v->as.integer >= 0) {
                return encode_uint_major(buf, len, cap, 0, (uint64_t)v->as.integer);
            } else {
                uint64_t n = (uint64_t)(-(v->as.integer + 1));
                return encode_uint_major(buf, len, cap, 1, n);
            }
        case DAG_CBOR_TYPE_STRING:
            encode_uint_major(buf, len, cap, 3, v->as.string.len);
            append(buf, len, cap, (const uint8_t*)v->as.string.data, v->as.string.len);
            return 1;
        case DAG_CBOR_TYPE_BYTES:
            encode_uint_major(buf, len, cap, 2, v->as.bytes.len);
            append(buf, len, cap, v->as.bytes.data, v->as.bytes.len);
            return 1;
        case DAG_CBOR_TYPE_ARRAY:
            encode_uint_major(buf, len, cap, 4, v->as.array.count);
            for (size_t i = 0; i < v->as.array.count; i++) {
                if (!encode_value(v->as.array.items[i], buf, len, cap)) return 0;
            }
            return 1;
        case DAG_CBOR_TYPE_MAP:
            encode_uint_major(buf, len, cap, 5, v->as.map.count);
            for (size_t i = 0; i < v->as.map.count; i++) {
                /* key as text string */
                struct DagCborValue keyval;
                memset(&keyval, 0, sizeof(keyval));
                keyval.type = DAG_CBOR_TYPE_STRING;
                keyval.as.string.data = strdup(v->as.map.keys[i]);
                keyval.as.string.len = strlen(v->as.map.keys[i]);
                if (!encode_value(&keyval, buf, len, cap)) { free(keyval.as.string.data); return 0; }
                free(keyval.as.string.data);
                if (!encode_value(v->as.map.values[i], buf, len, cap)) return 0;
            }
            return 1;
        case DAG_CBOR_TYPE_LINK: {
            /* tag 42 */
            append_byte(buf, len, cap, 0xD8);
            append_byte(buf, len, cap, 42);
            /* byte string with multibase prefix */
            size_t plen = v->as.link.len + 1;
            if (plen < 24) {
                append_byte(buf, len, cap, (uint8_t)(0x40 | plen));
            } else if (plen <= 0xFF) {
                append_byte(buf, len, cap, 0x58);
                append_byte(buf, len, cap, (uint8_t)plen);
            } else return 0;
            append_byte(buf, len, cap, MULTIBASE_BINARY_PREFIX);
            append(buf, len, cap, v->as.link.bytes, v->as.link.len);
            return 1;
        }
        default: return 0;
    }
}

int dag_cbor_encode(const struct DagCborValue *v, uint8_t **out, size_t *out_len) {
    if (!v || !out || !out_len) return 0;
    uint8_t *buf = NULL;
    size_t len = 0, cap = 0;
    if (!encode_value(v, &buf, &len, &cap)) { free(buf); return 0; }
    *out = buf;
    *out_len = len;
    return 1;
}

/* ---------- decode ---------- */

static int decode_uint(const uint8_t *buf, size_t len, size_t *pos, uint64_t *val) {
    if (*pos >= len) return 0;
    uint8_t b = buf[(*pos)++];
    uint8_t info = b & 0x1F;
    if (info < 24) { *val = info; return 1; }
    if (info == 24) {
        if (*pos >= len) return 0;
        *val = buf[(*pos)++];
        return 1;
    }
    if (info == 25) {
        if (*pos + 2 > len) return 0;
        *val = ((uint64_t)buf[*pos] << 8) | buf[*pos+1];
        *pos += 2;
        return 1;
    }
    if (info == 26) {
        if (*pos + 4 > len) return 0;
        *val = ((uint64_t)buf[*pos] << 24) | ((uint64_t)buf[*pos+1] << 16) | ((uint64_t)buf[*pos+2] << 8) | buf[*pos+3];
        *pos += 4;
        return 1;
    }
    if (info == 27) {
        if (*pos + 8 > len) return 0;
        *val = 0;
        for (int i = 0; i < 8; i++) *val = (*val << 8) | buf[(*pos)++];
        return 1;
    }
    return 0;
}

static int decode_value(const uint8_t *buf, size_t len, size_t *pos, struct DagCborValue **out);

static int decode_array(const uint8_t *buf, size_t len, size_t *pos, uint64_t count, struct DagCborValue **out) {
    struct DagCborValue *v = dag_cbor_value_new_array();
    for (uint64_t i = 0; i < count; i++) {
        struct DagCborValue *item = NULL;
        if (!decode_value(buf, len, pos, &item)) { dag_cbor_value_free(v); return 0; }
        dag_cbor_value_array_append(v, item);
    }
    *out = v;
    return 1;
}

static int decode_map(const uint8_t *buf, size_t len, size_t *pos, uint64_t count, struct DagCborValue **out) {
    struct DagCborValue *v = dag_cbor_value_new_map();
    for (uint64_t i = 0; i < count; i++) {
        struct DagCborValue *key = NULL;
        if (!decode_value(buf, len, pos, &key)) { dag_cbor_value_free(v); return 0; }
        if (key->type != DAG_CBOR_TYPE_STRING) { dag_cbor_value_free(key); dag_cbor_value_free(v); return 0; }
        char *keystr = strdup(key->as.string.data);
        dag_cbor_value_free(key);
        struct DagCborValue *val = NULL;
        if (!decode_value(buf, len, pos, &val)) { free(keystr); dag_cbor_value_free(v); return 0; }
        dag_cbor_value_map_set(v, keystr, val);
        free(keystr);
    }
    *out = v;
    return 1;
}

static int decode_value(const uint8_t *buf, size_t len, size_t *pos, struct DagCborValue **out) {
    if (*pos >= len) return 0;
    /* tag 42 link */
    if (buf[*pos] == 0xD8 && *pos + 1 < len && buf[*pos+1] == 42) {
        *pos += 2;
        if (*pos >= len) return 0;
        uint8_t bh = buf[(*pos)++];
        if ((bh & 0xE0) != 0x40) return 0;
        size_t plen = bh & 0x1F;
        if (plen == 24) { if (*pos >= len) return 0; plen = buf[(*pos)++]; }
        if (*pos + plen > len) return 0;
        if (buf[*pos] != MULTIBASE_BINARY_PREFIX) return 0;
        (*pos)++;
        struct DagCborValue *v = dag_cbor_value_new_link(buf + *pos, plen - 1);
        *pos += plen - 1;
        *out = v;
        return v != NULL;
    }

    uint8_t b = buf[*pos];
    uint8_t major = b >> 5;

    if (major == 0) { /* unsigned int */
        uint64_t val;
        if (!decode_uint(buf, len, pos, &val)) return 0;
        *out = dag_cbor_value_new_int((int64_t)val);
        return *out != NULL;
    }
    if (major == 1) { /* negative int */
        uint64_t val;
        /* consume first byte via decode_uint on (b & 0x1F) adjusted: use same decode but major already consumed? decode_uint expects first byte present */
        /* We already haven't consumed. Let decode_uint handle the whole first byte. */
        if (!decode_uint(buf, len, pos, &val)) return 0;
        *out = dag_cbor_value_new_int((int64_t)(-1 - (int64_t)val));
        return *out != NULL;
    }
    if (major == 2) { /* bytes */
        uint64_t n;
        if (!decode_uint(buf, len, pos, &n)) return 0;
        if (*pos + n > len) return 0;
        struct DagCborValue *v = dag_cbor_value_new_bytes(buf + *pos, (size_t)n);
        *pos += n;
        *out = v;
        return v != NULL;
    }
    if (major == 3) { /* text string */
        uint64_t n;
        if (!decode_uint(buf, len, pos, &n)) return 0;
        if (*pos + n > len) return 0;
        char *s = xmalloc(n + 1);
        memcpy(s, buf + *pos, n);
        s[n] = '\0';
        struct DagCborValue *v = dag_cbor_value_new_string(s);
        free(s);
        *pos += n;
        *out = v;
        return v != NULL;
    }
    if (major == 4) { /* array */
        uint64_t n;
        if (!decode_uint(buf, len, pos, &n)) return 0;
        return decode_array(buf, len, pos, n, out);
    }
    if (major == 5) { /* map */
        uint64_t n;
        if (!decode_uint(buf, len, pos, &n)) return 0;
        return decode_map(buf, len, pos, n, out);
    }
    if (major == 6) { /* tag - handle 42 already above; skip others */
        uint64_t n;
        if (!decode_uint(buf, len, pos, &n)) return 0;
        /* decode content */
        struct DagCborValue *v = NULL;
        if (!decode_value(buf, len, pos, &v)) return 0;
        *out = v;
        return 1;
    }
    if (major == 7) { /* simple values */
        uint8_t info = b & 0x1F;
        (*pos)++;        if (info == 20) { *out = dag_cbor_value_new_bool(0); return *out != NULL; }
        if (info == 21) { *out = dag_cbor_value_new_bool(1); return *out != NULL; }
        if (info == 22) { *out = dag_cbor_value_new_null(); return *out != NULL; } /* undefined -> null */
        if (info == 23) { *out = dag_cbor_value_new_null(); return *out != NULL; } /* null */
        if (info == 25 || info == 26 || info == 27) {
            /* half/single/double float - treat as int 0 for simplicity */
            if (info == 25) *pos += 2;
            else if (info == 26) *pos += 4;
            else *pos += 8;
            *out = dag_cbor_value_new_int(0);
            return *out != NULL;
        }
        return 0;
    }
    return 0;
}

int dag_cbor_decode(const uint8_t *buf, size_t len, struct DagCborValue **out) {
    if (!buf || !out || len == 0) return 0;
    size_t pos = 0;
    if (!decode_value(buf, len, &pos, out)) return 0;
    return 1;
}
