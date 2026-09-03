#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "ipfs/core/client_api.h"

int api_decompress_gzip(const unsigned char *src, size_t src_len, unsigned char **dest, size_t *dest_len) {
    z_stream strm = {0};
    strm.next_in = (Bytef *)src;
    strm.avail_in = src_len;

    if (inflateInit2(&strm, 16 + MAX_WBITS) != Z_OK) return 0;

    size_t out_capacity = src_len * 4;
    unsigned char *out_buf = malloc(out_capacity);
    if (!out_buf) { inflateEnd(&strm); return 0; }

    strm.next_out = (Bytef *)out_buf;
    strm.avail_out = out_capacity;

    int ret = inflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END && ret != Z_OK) {
        inflateEnd(&strm);
        free(out_buf);
        return 0;
    }

    *dest_len = strm.total_out;
    *dest = out_buf;
    inflateEnd(&strm);
    return 1;
}

int api_handle_post_request(const char *content_encoding, const unsigned char *body, size_t body_len) {
    const unsigned char *payload = body;
    size_t payload_len = body_len;
    unsigned char *decompressed = NULL;

    if (content_encoding && strstr(content_encoding, "gzip")) {
        if (api_decompress_gzip(body, body_len, &decompressed, &payload_len) == 1) {
            payload = decompressed;
        } else {
            fprintf(stderr, "[api] Decompression failure for gzip POST body\n");
            return 0;
        }
    }

    printf("[api] Processing %zu byte POST payload\n", payload_len);

    if (decompressed) free(decompressed);
    return 1;
}
