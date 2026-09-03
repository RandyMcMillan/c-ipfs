#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ipfs/cid/cid.h"

#define CID_V0_MULTIHASH_CODE 0x12
#define CID_V0_MULTIHASH_SIZE 32
#define CID_CODEC_DAG_PB      0x70
#define CID_CODEC_RAW         0x55

typedef struct cid {
    uint64_t version;
    uint64_t codec;
    uint8_t *multihash;
    size_t multihash_len;
} cid_t;

static size_t decode_uvarint(const uint8_t *buf, size_t max, uint64_t *val) {
    *val = 0;
    size_t i = 0;
    uint32_t shift = 0;
    while (i < max) {
        uint8_t byte = buf[i++];
        *val |= (uint64_t)(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) return i;
        shift += 7;
        if (shift >= 64) return 0;
    }
    return 0;
}

static const int8_t b58_digits[] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8,-1,-1,-1,-1,-1,-1,
    -1, 9,10,11,12,13,14,15,16,-1,17,18,19,20,21,-1,
    22,23,24,25,26,27,28,29,30,31,-1,-1,-1,-1,-1,-1,
    -1,32,33,34,35,36,37,38,39,40,41,42,-1,43,44,45,
    46,47,48,49,50,51,52,53,54,-1,-1,-1,-1,-1
};

static int decode_base58(const char *str, uint8_t *out, size_t *out_len) {
    size_t len = strlen(str);
    size_t zero_count = 0;
    while (zero_count < len && str[zero_count] == '1') zero_count++;

    size_t b58_sz = (len - zero_count) * 733 / 1000 + 1;
    uint8_t *buf = calloc(1, b58_sz);
    if (!buf) return -1;

    for (size_t i = zero_count; i < len; i++) {
        if ((uint8_t)str[i] > 127 || b58_digits[(uint8_t)str[i]] < 0) {
            free(buf);
            return -1;
        }
        int carry = b58_digits[(uint8_t)str[i]];
        for (int j = (int)b58_sz - 1; j >= 0; j--) {
            carry += 58 * buf[j];
            buf[j] = carry % 256;
            carry /= 256;
        }
    }

    size_t ignore = 0;
    while (ignore < b58_sz && buf[ignore] == 0) ignore++;

    *out_len = zero_count + (b58_sz - ignore);
    memset(out, 0, zero_count);
    memcpy(out + zero_count, buf + ignore, b58_sz - ignore);
    free(buf);
    return 0;
}

int cid_from_bytes(const uint8_t *buf, size_t len, cid_t **out_cid) {
    if (!buf || len == 0 || !out_cid) return 0;

    cid_t *cid = calloc(1, sizeof(cid_t));
    if (!cid) return 0;

    if (len == 34 && buf[0] == CID_V0_MULTIHASH_CODE && buf[1] == CID_V0_MULTIHASH_SIZE) {
        cid->version = 0;
        cid->codec = CID_CODEC_DAG_PB;
        cid->multihash_len = len;
        cid->multihash = malloc(len);
        if (!cid->multihash) { free(cid); return 0; }
        memcpy(cid->multihash, buf, len);
        *out_cid = cid;
        return 1;
    }

    size_t offset = 0;
    size_t read = decode_uvarint(buf + offset, len - offset, &cid->version);
    if (!read || cid->version != 1) goto error;
    offset += read;

    read = decode_uvarint(buf + offset, len - offset, &cid->codec);
    if (!read) goto error;
    offset += read;

    cid->multihash_len = len - offset;
    cid->multihash = malloc(cid->multihash_len);
    if (!cid->multihash) goto error;
    memcpy(cid->multihash, buf + offset, cid->multihash_len);

    *out_cid = cid;
    return 1;

error:
    free(cid);
    return 0;
}

int cid_from_string(const char *str, cid_t **out_cid) {
    if (!str || !out_cid) return 0;

    if (strlen(str) == 46 && str[0] == 'Q' && str[1] == 'm') {
        uint8_t mh[64];
        size_t mh_len = 0;
        if (decode_base58(str, mh, &mh_len) != 0 || mh_len != 34) {
            return 0;
        }
        return cid_from_bytes(mh, mh_len, out_cid);
    }

    if (str[0] == 'b') {
        fprintf(stderr, "[cid] CIDv1 Multibase string conversion required: %s\n", str);
        return 0;
    }

    return 0;
}

int cid_to_v1(cid_t *cid, uint64_t target_codec) {
    if (!cid) return 0;
    if (cid->version == 1) return 1;
    cid->version = 1;
    cid->codec = target_codec ? target_codec : CID_CODEC_DAG_PB;
    return 1;
}

void cid_free(cid_t *cid) {
    if (!cid) return;
    free(cid->multihash);
    free(cid);
}
