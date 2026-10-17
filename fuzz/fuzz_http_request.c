#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs/core/http_request.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 100000) return 0;
    /* Feed raw bytes to http request parsing is not directly exposed;
       exercise allocation/free path instead. */
    struct HttpRequest *req = ipfs_core_http_request_new();
    if (req) {
        if (size > 0) {
            req->data = (uint8_t*)malloc(size);
            if (req->data) {
                memcpy(req->data, data, size);
                req->data_size = size;
            }
        }
        ipfs_core_http_request_free(req);
    }
    return 0;
}