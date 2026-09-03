#ifndef __IPFS_TRANSPORT_REGISTRY_H__
#define __IPFS_TRANSPORT_REGISTRY_H__

#include "ipfs/transport/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Transport registry entry.
 */
typedef struct transport_registry_entry {
    libp2p_transport_t *transport;
    struct transport_registry_entry *next;
} transport_registry_entry_t;

/**
 * Global transport registry.
 */
typedef struct {
    transport_registry_entry_t *head;
} transport_registry_t;

/**
 * Initialize a transport registry.
 */
void transport_registry_init(transport_registry_t *reg);

/**
 * Add a transport to the registry.
 * @return 0 on success, -1 on error.
 */
int transport_registry_add(transport_registry_t *reg, libp2p_transport_t *transport);

/**
 * Remove a transport from the registry by name.
 * @return 0 on success, -1 if not found.
 */
int transport_registry_remove(transport_registry_t *reg, const char *name);

/**
 * Dial a multiaddress using the first transport that can handle it.
 * Transports are tried in registration order.
 *
 * @param reg the registry
 * @param multiaddr the target multiaddress string
 * @param out_stream output stream pointer (set on success)
 * @return 0 on success, -1 if no transport matched or dial failed.
 */
int transport_registry_dial(transport_registry_t *reg, const char *multiaddr, libp2p_stream_t **out_stream);

/**
 * Free the registry and all registered transports.
 */
void transport_registry_free(transport_registry_t *reg);

#ifdef __cplusplus
}
#endif

#endif
