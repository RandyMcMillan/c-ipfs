#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define GOSSIPSUB_ID_v1_1 "/meshsub/1.0.0"
#define IPNS_PUBSUB_TOPIC_PREFIX "/ipns/"

#define WIRE_TYPE_VARINT 0
#define WIRE_TYPE_LENGTH_DELIMITED 2

typedef enum {
    GOSSIP_GRAFT = 0,
    GOSSIP_PRUNE = 1,
    GOSSIP_IHAVE = 2,
    GOSSIP_IWANT = 3
} gossip_control_type_t;

typedef struct {
    char topic_id[128];
} gossip_graft_t;

typedef struct {
    char topic_id[128];
    uint64_t backoff_seconds;
} gossip_prune_t;

static size_t encode_varint(uint64_t val, uint8_t *buf) {
    size_t i = 0;
    while (val >= 0x80) {
        buf[i++] = (uint8_t)((val & 0x7F) | 0x80);
        val >>= 7;
    }
    buf[i++] = (uint8_t)(val & 0x7F);
    return i;
}

static size_t decode_varint(const uint8_t *buf, size_t max_len, uint64_t *val) {
    *val = 0;
    size_t i = 0;
    uint32_t shift = 0;
    while (i < max_len) {
        uint8_t byte = buf[i++];
        *val |= (uint64_t)(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) return i;
        shift += 7;
        if (shift >= 64) return 0;
    }
    return 0;
}

static size_t encode_tag(uint32_t field_num, uint8_t wire_type, uint8_t *buf) {
    uint32_t key = (field_num << 3) | (wire_type & 0x07);
    return encode_varint(key, buf);
}

int gossipsub_encode_graft(const gossip_graft_t *graft, uint8_t *out_buf, size_t max_len) {
    size_t offset = 0;
    size_t topic_len = strlen(graft->topic_id);

    uint8_t inner_buf[256];
    size_t inner_off = 0;
    inner_off += encode_tag(1, WIRE_TYPE_LENGTH_DELIMITED, inner_buf + inner_off);
    inner_off += encode_varint(topic_len, inner_buf + inner_off);
    memcpy(inner_buf + inner_off, graft->topic_id, topic_len);
    inner_off += topic_len;

    uint8_t ctrl_buf[300];
    size_t ctrl_off = 0;
    ctrl_off += encode_tag(4, WIRE_TYPE_LENGTH_DELIMITED, ctrl_buf + ctrl_off);
    ctrl_off += encode_varint(inner_off, ctrl_buf + ctrl_off);
    memcpy(ctrl_buf + ctrl_off, inner_buf, inner_off);
    ctrl_off += inner_off;

    if (max_len < ctrl_off + 10) return -1;
    offset += encode_tag(3, WIRE_TYPE_LENGTH_DELIMITED, out_buf + offset);
    offset += encode_varint(ctrl_off, out_buf + offset);
    memcpy(out_buf + offset, ctrl_buf, ctrl_off);
    offset += ctrl_off;

    return (int)offset;
}

int gossipsub_encode_prune(const gossip_prune_t *prune, uint8_t *out_buf, size_t max_len) {
    size_t topic_len = strlen(prune->topic_id);

    uint8_t inner_buf[256];
    size_t inner_off = 0;
    inner_off += encode_tag(1, WIRE_TYPE_LENGTH_DELIMITED, inner_buf + inner_off);
    inner_off += encode_varint(topic_len, inner_buf + inner_off);
    memcpy(inner_buf + inner_off, prune->topic_id, topic_len);
    inner_off += topic_len;

    if (prune->backoff_seconds > 0) {
        inner_off += encode_tag(3, WIRE_TYPE_VARINT, inner_buf + inner_off);
        inner_off += encode_varint(prune->backoff_seconds, inner_buf + inner_off);
    }

    uint8_t ctrl_buf[300];
    size_t ctrl_off = 0;
    ctrl_off += encode_tag(5, WIRE_TYPE_LENGTH_DELIMITED, ctrl_buf + ctrl_off);
    ctrl_off += encode_varint(inner_off, ctrl_buf + ctrl_off);
    memcpy(ctrl_buf + ctrl_off, inner_buf, inner_off);
    ctrl_off += inner_off;

    size_t offset = 0;
    if (max_len < ctrl_off + 10) return -1;
    offset += encode_tag(3, WIRE_TYPE_LENGTH_DELIMITED, out_buf + offset);
    offset += encode_varint(ctrl_off, out_buf + offset);
    memcpy(out_buf + offset, ctrl_buf, ctrl_off);
    offset += ctrl_off;

    return (int)offset;
}

int gossipsub_parse_control(const uint8_t *buf, size_t len, gossip_graft_t *out_graft, gossip_prune_t *out_prune) {
    size_t offset = 0;

    while (offset < len) {
        uint64_t key;
        size_t vlen = decode_varint(buf + offset, len - offset, &key);
        if (!vlen) break;
        offset += vlen;

        uint32_t field_num = (uint32_t)(key >> 3);
        uint8_t wire_type = (uint8_t)(key & 0x07);

        if (wire_type == WIRE_TYPE_LENGTH_DELIMITED) {
            uint64_t field_len;
            vlen = decode_varint(buf + offset, len - offset, &field_len);
            offset += vlen;

            if (field_num == 3) {
                size_t ctrl_end = offset + field_len;
                while (offset < ctrl_end) {
                    decode_varint(buf + offset, ctrl_end - offset, &key);
                    uint32_t ctrl_field = (uint32_t)(key >> 3);
                    offset += decode_varint(buf + offset, ctrl_end - offset, &key);

                    uint64_t msg_len;
                    offset += decode_varint(buf + offset, ctrl_end - offset, &msg_len);

                    if (ctrl_field == 4 && out_graft) {
                        size_t graft_end = offset + msg_len;
                        while (offset < graft_end) {
                            uint64_t gkey;
                            offset += decode_varint(buf + offset, graft_end - offset, &gkey);
                            uint64_t str_len;
                            offset += decode_varint(buf + offset, graft_end - offset, &str_len);
                            if ((gkey >> 3) == 1) {
                                size_t copy_len = str_len < sizeof(out_graft->topic_id) - 1 ? str_len : sizeof(out_graft->topic_id) - 1;
                                memcpy(out_graft->topic_id, buf + offset, copy_len);
                                out_graft->topic_id[copy_len] = '\0';
                            }
                            offset += str_len;
                        }
                        return 1;
                    } else if (ctrl_field == 5 && out_prune) {
                        size_t prune_end = offset + msg_len;
                        while (offset < prune_end) {
                            uint64_t pkey;
                            offset += decode_varint(buf + offset, prune_end - offset, &pkey);
                            uint32_t pfield = (uint32_t)(pkey >> 3);
                            uint8_t ptype = (uint8_t)(pkey & 0x07);

                            if (ptype == WIRE_TYPE_LENGTH_DELIMITED) {
                                uint64_t str_len;
                                offset += decode_varint(buf + offset, prune_end - offset, &str_len);
                                if (pfield == 1) {
                                    size_t copy_len = str_len < sizeof(out_prune->topic_id) - 1 ? str_len : sizeof(out_prune->topic_id) - 1;
                                    memcpy(out_prune->topic_id, buf + offset, copy_len);
                                    out_prune->topic_id[copy_len] = '\0';
                                }
                                offset += str_len;
                            } else if (ptype == WIRE_TYPE_VARINT) {
                                if (pfield == 3) {
                                    offset += decode_varint(buf + offset, prune_end - offset, &out_prune->backoff_seconds);
                                }
                            }
                        }
                        return 2;
                    } else {
                        offset += msg_len;
                    }
                }
            } else {
                offset += field_len;
            }
        }
    }
    return 0;
}
