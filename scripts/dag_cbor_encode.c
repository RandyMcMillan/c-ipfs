#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#define DAG_CBOR_LINK_TAG 42
#define MULTIBASE_BINARY_PREFIX 0x00

static size_t hex_to_bin(const char *hex, uint8_t *out, size_t max_len) {
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0 || hex_len / 2 > max_len) return 0;

    for (size_t i = 0; i < hex_len / 2; i++) {
        if (!isxdigit((unsigned char)hex[i * 2]) || !isxdigit((unsigned char)hex[i * 2 + 1])) {
            return 0;
        }
        sscanf(hex + (i * 2), "%02hhx", &out[i]);
    }
    return hex_len / 2;
}

static void print_hex(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
    }
    printf("\n");
}

static void print_formatted(const uint8_t *data, size_t len) {
    printf("DAG-CBOR Tag 42 Bytes (%zu bytes):\n", len);
    for (size_t i = 0; i < len; i++) {
        printf("0x%02X%s", data[i], (i == len - 1) ? "" : " ");
    }
    printf("\n");
}

bool encode_dag_cbor_link(const uint8_t *cid_bytes, size_t cid_len, uint8_t *out_buf, size_t *out_len) {
    if (!cid_bytes || !out_buf || !out_len || cid_len == 0) {
        return false;
    }

    size_t offset = 0;

    out_buf[offset++] = 0xD8;
    out_buf[offset++] = DAG_CBOR_LINK_TAG;

    size_t payload_len = cid_len + 1;

    if (payload_len < 24) {
        out_buf[offset++] = 0x40 | (uint8_t)payload_len;
    } else if (payload_len <= 0xFF) {
        out_buf[offset++] = 0x58;
        out_buf[offset++] = (uint8_t)payload_len;
    } else if (payload_len <= 0xFFFF) {
        out_buf[offset++] = 0x59;
        out_buf[offset++] = (uint8_t)((payload_len >> 8) & 0xFF);
        out_buf[offset++] = (uint8_t)(payload_len & 0xFF);
    } else if (payload_len <= 0xFFFFFFFF) {
        out_buf[offset++] = 0x5A;
        out_buf[offset++] = (uint8_t)((payload_len >> 24) & 0xFF);
        out_buf[offset++] = (uint8_t)((payload_len >> 16) & 0xFF);
        out_buf[offset++] = (uint8_t)((payload_len >> 8) & 0xFF);
        out_buf[offset++] = (uint8_t)(payload_len & 0xFF);
    } else {
        return false;
    }

    out_buf[offset++] = MULTIBASE_BINARY_PREFIX;
    memcpy(&out_buf[offset], cid_bytes, cid_len);
    offset += cid_len;

    *out_len = offset;
    return true;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <hex_multihash_or_cid_bytes> [--formatted|--binary]\n", argv[0]);
        fprintf(stderr, "Example: %s 01701220e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n", argv[0]);
        return 1;
    }

    const char *hex_input = argv[1];
    bool formatted = false;
    bool raw_binary = false;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--formatted") == 0) {
            formatted = true;
        } else if (strcmp(argv[i], "--binary") == 0) {
            raw_binary = true;
        }
    }

    uint8_t raw_bytes[65536];
    size_t raw_len = hex_to_bin(hex_input, raw_bytes, sizeof(raw_bytes));

    if (raw_len == 0) {
        fprintf(stderr, "Error: Invalid hex input provided.\n");
        return 1;
    }

    uint8_t cbor_output[65545];
    size_t cbor_len = 0;

    if (!encode_dag_cbor_link(raw_bytes, raw_len, cbor_output, &cbor_len)) {
        fprintf(stderr, "Error: Failed to encode DAG-CBOR Tag 42 byte string.\n");
        return 1;
    }

    if (raw_binary) {
        fwrite(cbor_output, 1, cbor_len, stdout);
    } else if (formatted) {
        print_formatted(cbor_output, cbor_len);
    } else {
        print_hex(cbor_output, cbor_len);
    }

    return 0;
}
