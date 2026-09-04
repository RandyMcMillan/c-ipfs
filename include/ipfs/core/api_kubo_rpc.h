#ifndef IPFS_CORE_API_KUBO_RPC_H
#define IPFS_CORE_API_KUBO_RPC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int ipfs_start_http_rpc_server(uint16_t port);
void ipfs_stop_http_rpc_server(void);

#ifdef __cplusplus
}
#endif

#endif /* IPFS_CORE_API_KUBO_RPC_H */
