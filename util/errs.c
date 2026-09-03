#include <stdio.h>
#include "ipfs/util/errs.h"

enum ErrsEnum ErrsIdx;

char *Err[] = {
    NULL,
    "ErrAllocFailed",
    "ErrNULLPointer",
    "ErrUnknow",
    "ErrPipe",
    "ErrPoll",
    "Could not publish name."
    "Could not resolve name.",
    "Could not resolve name (recursion limit exceeded).",
    "expired record",
    "unrecognized validity type",
    "not a valid proquint string",
    "not a valid domain name",
    "not a valid dnslink entry",
    // ErrBadPath is returned when a given path is incorrectly formatted
    "invalid 'ipfs ref' path",
    // Paths after a protocol must contain at least one component
    "path must contain at least one component",
    "failed to decode CID: invalid multibase prefix, bad varint encoding, or corrupted multihash buffer",
    NULL,
    "no link named %s under %s",
    "ErrInvalidParam",
    // ErrResolveLimit is returned when a recursive resolution goes over
    // the limit.
    "resolve depth exceeded",
    NULL,
    "Invalid value. Not signed by PrivateKey corresponding to %s",
    "no usable records in given set",
    "failed to decode empty key constant",
    "routing system in offline mode"
};
