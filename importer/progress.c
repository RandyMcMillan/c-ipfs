#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "ipfs/importer/progress.h"

ipfs_file_type_t importer_detect_file_type(uint64_t file_size, bool is_dir) {
    if (is_dir) return FILE_TYPE_DIRECTORY;
    if (file_size > 262144) return FILE_TYPE_SPLIT_SHARD;
    return FILE_TYPE_UNIXFS_FILE;
}

void importer_report_progress(const char *path, uint64_t imported, uint64_t total, import_progress_cb cb, void *user_data) {
    if (cb) {
        cb(path, imported, total, user_data);
    } else {
        float pct = total ? ((float)imported / total) * 100.0f : 100.0f;
        printf("\r[import] %s: %.1f%% (%llu/%llu bytes)", path, pct, (unsigned long long)imported, (unsigned long long)total);
        fflush(stdout);
        if (imported == total) printf("\n");
    }
}
