#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs/transport/registry.h"

void transport_registry_init(transport_registry_t *reg) {
    if (reg) {
        reg->head = NULL;
    }
}

int transport_registry_add(transport_registry_t *reg, libp2p_transport_t *transport) {
    if (!reg || !transport) return -1;

    transport_registry_entry_t *entry = malloc(sizeof(transport_registry_entry_t));
    if (!entry) return -1;

    entry->transport = transport;
    entry->next = reg->head;
    reg->head = entry;
    return 0;
}

int transport_registry_remove(transport_registry_t *reg, const char *name) {
    if (!reg || !name) return -1;

    transport_registry_entry_t **curr = &reg->head;
    while (*curr) {
        if ((*curr)->transport && (*curr)->transport->name &&
            strcmp((*curr)->transport->name, name) == 0) {
            transport_registry_entry_t *to_remove = *curr;
            *curr = (*curr)->next;
            if (to_remove->transport->close) {
                to_remove->transport->close(to_remove->transport);
            }
            free(to_remove->transport);
            free(to_remove);
            return 0;
        }
        curr = &(*curr)->next;
    }
    return -1;
}

int transport_registry_dial(transport_registry_t *reg, const char *multiaddr, libp2p_stream_t **out_stream) {
    if (!reg || !multiaddr || !out_stream) return -1;
    *out_stream = NULL;

    transport_registry_entry_t *curr = reg->head;
    while (curr) {
        if (curr->transport && curr->transport->dial) {
            /* Simple heuristic: check if multiaddr contains the transport name */
            if (strstr(multiaddr, curr->transport->name)) {
                int ret = curr->transport->dial(curr->transport, multiaddr, out_stream);
                if (ret == 0) {
                    return 0;
                }
            }
        }
        curr = curr->next;
    }

    return -1;
}

void transport_registry_free(transport_registry_t *reg) {
    if (!reg) return;

    transport_registry_entry_t *curr = reg->head;
    while (curr) {
        transport_registry_entry_t *next = curr->next;
        if (curr->transport) {
            if (curr->transport->close) {
                curr->transport->close(curr->transport);
            }
            free(curr->transport);
        }
        free(curr);
        curr = next;
    }
    reg->head = NULL;
}
