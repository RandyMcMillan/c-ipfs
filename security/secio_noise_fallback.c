#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "ipfs/security/secio_noise_fallback.h"

/* Stub implementations for Noise/SECIO fallback */

int ipfs_secure_channel_connect(int socket_fd, const char *peer_id, ipfs_secure_channel_t **out_chan) {
    (void)socket_fd;
    (void)peer_id;
    (void)out_chan;
    fprintf(stderr, "[Security] ipfs_secure_channel_connect is a stub\n");
    return -ECONNREFUSED;
}

void ipfs_secure_channel_free(ipfs_secure_channel_t *chan) {
    (void)chan;
}
