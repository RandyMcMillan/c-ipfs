#include <stdio.h>

#include "ipfs/util/errs.h"

typedef enum {
    ERR_SUCCESS = 0,
    ERR_CID_DECODE = 1001,
    ERR_UNKNOWN = 9999
} err_code_t;

const char *ipfs_err_to_string(int err) {
    switch (err) {
        case ERR_SUCCESS:
            return "Success";
        case ERR_CID_DECODE:
            return "Failed to decode CID: invalid multibase prefix, bad varint encoding, or corrupted multihash buffer";
        default:
            return "Unknown IPFS subsystem error";
    }
}
