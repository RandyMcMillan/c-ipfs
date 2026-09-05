#ifndef __IPFS_IMPORTER_PROGRESS_H__
#define __IPFS_IMPORTER_PROGRESS_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*import_progress_cb)(const char *path, uint64_t bytes_imported, uint64_t total_bytes, void *user_data);

typedef enum {
    FILE_TYPE_RAW,
    FILE_TYPE_UNIXFS_FILE,
    FILE_TYPE_SPLIT_SHARD,
    FILE_TYPE_DIRECTORY
} ipfs_file_type_t;

ipfs_file_type_t importer_detect_file_type(uint64_t file_size, bool is_dir);

void importer_report_progress(const char *path, uint64_t imported, uint64_t total, import_progress_cb cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* __IPFS_IMPORTER_PROGRESS_H__ */
