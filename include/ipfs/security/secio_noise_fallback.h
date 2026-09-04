#ifndef IPFS_SECURITY_SECIO_NOISE_FALLBACK_H
#define IPFS_SECURITY_SECIO_NOISE_FALLBACK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ipfs_secure_channel ipfs_secure_channel_t;

int ipfs_secure_channel_connect(int socket_fd, const char *peer_id, ipfs_secure_channel_t **out_chan);
void ipfs_secure_channel_free(ipfs_secure_channel_t *chan);

#ifdef __cplusplus
}
#endif

#endif /* IPFS_SECURITY_SECIO_NOISE_FALLBACK_H */
