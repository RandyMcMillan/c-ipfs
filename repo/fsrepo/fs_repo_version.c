#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include "ipfs/repo/fs_repo_version.h"

#define TARGET_REPO_VERSION 18
#define REPO_VERSION_FILE "version"
#define REPO_LOCK_FILE "repo.lock"

int ipfs_repo_check_and_migrate(const char *repo_path) {
    if (!repo_path) return -EINVAL;

    char version_filepath[512];
    snprintf(version_filepath, sizeof(version_filepath), "%s/%s", repo_path, REPO_VERSION_FILE);

    FILE *f = fopen(version_filepath, "r+");
    if (!f) {
        /* Create initial v18 version file if missing */
        f = fopen(version_filepath, "w");
        if (!f) return -EACCES;
        fprintf(f, "%d\n", TARGET_REPO_VERSION);
        fclose(f);
        return 0;
    }

    int current_version = 0;
    if (fscanf(f, "%d", &current_version) != 1) {
        fclose(f);
        return -EIO;
    }

    if (current_version < TARGET_REPO_VERSION) {
        printf("[Repo Migration] Upgrading repo from v%d to v%d...\n", current_version, TARGET_REPO_VERSION);
        fseek(f, 0, SEEK_SET);
        fprintf(f, "%d\n", TARGET_REPO_VERSION);
        fflush(f);
    }

    fclose(f);
    return 0;
}

int ipfs_repo_lock(const char *repo_path, int *out_lock_fd) {
    if (!repo_path || !out_lock_fd) return -EINVAL;

    char lock_filepath[512];
    snprintf(lock_filepath, sizeof(lock_filepath), "%s/%s", repo_path, REPO_LOCK_FILE);

    int fd = open(lock_filepath, O_RDWR | O_CREAT, 0600);
    if (fd < 0) return -EACCES;

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0; /* Lock entire file */

    if (fcntl(fd, F_SETLK, &fl) < 0) {
        close(fd);
        if (errno == EACCES || errno == EAGAIN) {
            return -EWOULDBLOCK; /* Repo locked by another process (e.g. Kubo) */
        }
        return -errno;
    }

    *out_lock_fd = fd;
    return 0;
}

void ipfs_repo_unlock(int lock_fd) {
    if (lock_fd < 0) return;

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    fcntl(lock_fd, F_SETLK, &fl);
    close(lock_fd);
}
