#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs/repo/config/config.h"

typedef struct {
    char *peer_id;
    char *datastore_path;
    char **bootstrap_addrs;
    size_t bootstrap_count;
} repo_config_t;

void repo_config_free(repo_config_t *cfg) {
    if (!cfg) return;

    if (cfg->peer_id) {
        free(cfg->peer_id);
        cfg->peer_id = NULL;
    }

    if (cfg->datastore_path) {
        free(cfg->datastore_path);
        cfg->datastore_path = NULL;
    }

    if (cfg->bootstrap_addrs) {
        for (size_t i = 0; i < cfg->bootstrap_count; i++) {
            if (cfg->bootstrap_addrs[i]) {
                free(cfg->bootstrap_addrs[i]);
            }
        }
        free(cfg->bootstrap_addrs);
        cfg->bootstrap_addrs = NULL;
    }

    cfg->bootstrap_count = 0;
    free(cfg);
}
