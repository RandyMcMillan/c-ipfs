#ifndef IPFS_ROUTING_DHT_SERVER_API_H
#define IPFS_ROUTING_DHT_SERVER_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int ipfs_dht_engine_init(uint16_t api_port);
int ipfs_dht_publish_prov(const char *cid);
int ipfs_dht_find_providers(const char *cid, char ***out_multiaddrs, size_t *out_count);
void ipfs_dht_engine_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* IPFS_ROUTING_DHT_SERVER_API_H */
