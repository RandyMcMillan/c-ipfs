#ifndef IPFS_ERRS_H
    #define IPFS_ERRS_H

    extern char *Err[];

    enum ErrsEnum {
        ErrAllocFailed = 1,
        ErrNULLPointer,
        ErrUnknow,
        ErrPipe,
        ErrPoll,
        ErrPublishFailed,
        ErrResolveFailed,
        ErrResolveRecursion,
        ErrExpiredRecord,
        ErrUnrecognizedValidity,
        ErrInvalidProquint,
        ErrInvalidDomain,
        ErrInvalidDNSLink,
        ErrBadPath,
        ErrNoComponents,
        ErrCidDecode,
        ErrNoLink,
        ErrNoLinkFmt,
        ErrInvalidParam,
        ErrResolveLimit,
        ErrInvalidSignature,
        ErrInvalidSignatureFmt,
        ErrNoRecord,
        ErrCidDecodeFailed,
        ErrOffline
    };
    extern enum ErrsEnum ErrsIdx;
#endif // IPFS_ERRS_H
