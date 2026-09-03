#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs/core/client_api.h"

typedef struct {
    char filename[256];
    char content_type[128];
} multipart_headers_t;

int api_parse_multipart_headers(const char *raw_headers, multipart_headers_t *out_headers) {
    if (!raw_headers || !out_headers) return 0;
    memset(out_headers, 0, sizeof(multipart_headers_t));
    strncpy(out_headers->content_type, "application/octet-stream", sizeof(out_headers->content_type) - 1);

    const char *cd = strstr(raw_headers, "Content-Disposition:");
    if (cd) {
        const char *fn = strstr(cd, "filename=\"");
        if (fn) {
            fn += 10;
            const char *end = strchr(fn, '"');
            if (end && (size_t)(end - fn) < sizeof(out_headers->filename)) {
                strncpy(out_headers->filename, fn, end - fn);
                out_headers->filename[end - fn] = '\0';
            }
        }
    }

    const char *ct = strstr(raw_headers, "Content-Type:");
    if (ct) {
        ct += 13;
        while (*ct == ' ') ct++;
        const char *end = strstr(ct, "\r\n");
        if (!end) end = strchr(ct, '\n');
        if (end && (size_t)(end - ct) < sizeof(out_headers->content_type)) {
            strncpy(out_headers->content_type, ct, end - ct);
            out_headers->content_type[end - ct] = '\0';
        }
    }

    return 1;
}
