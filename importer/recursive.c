#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>

#include "ipfs/importer/importer.h"

typedef struct {
    char path[1024];
    char cid_str[64];
    bool is_dir;
} import_node_t;

int importer_import_recursive(const char *dir_path, int depth) {
    if (depth > 32) {
        fprintf(stderr, "[importer] Error: Exceeded max recursion depth (symlink cycle?)\n");
        return 0;
    }

    DIR *d = opendir(dir_path);
    if (!d) return 0;

    struct dirent *entry;
    char full_path[1024];

    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            printf("[importer] Traversing dir: %s (Depth %d)\n", full_path, depth);
            importer_import_recursive(full_path, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            printf("[importer] Importing file: %s (%lld bytes)\n", full_path, (long long)st.st_size);
        }
    }

    closedir(d);
    return 1;
}
