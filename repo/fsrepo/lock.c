#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#else
#include <sys/file.h>
#endif

#include "ipfs/repo/fsrepo/fs_repo.h"

typedef struct fs_repo {
    char *path;
    char *lockfile_path;
    int lock_fd;
#if defined(_WIN32)
    HANDLE lock_handle;
#endif
    bool is_open;
    int lock_depth;
    pthread_mutex_t inproc_mtx;
} fs_repo_t;

static fs_repo_t *g_repo_instance = NULL;
static pthread_mutex_t g_repo_global_mtx = PTHREAD_MUTEX_INITIALIZER;

static int platform_lock_file(fs_repo_t *repo, bool non_blocking) {
#if defined(_WIN32)
    DWORD flags = LOCKFILE_EXCLUSIVE_LOCK;
    if (non_blocking) flags |= LOCKFILE_FAIL_IMMEDIATELY;
    OVERLAPPED overlapped = {0};
    repo->lock_handle = (HANDLE)_get_osfhandle(repo->lock_fd);
    if (!LockFileEx(repo->lock_handle, flags, 0, 1, 0, &overlapped)) {
        return -1;
    }
    return 0;
#else
    int operation = LOCK_EX;
    if (non_blocking) operation |= LOCK_NB;
    return flock(repo->lock_fd, operation);
#endif
}

static int platform_unlock_file(fs_repo_t *repo) {
#if defined(_WIN32)
    OVERLAPPED overlapped = {0};
    if (!UnlockFileEx(repo->lock_handle, 0, 1, 0, &overlapped)) {
        return -1;
    }
    return 0;
#else
    return flock(repo->lock_fd, LOCK_UN);
#endif
}

static char *build_lock_path(const char *repo_path) {
    size_t len = strlen(repo_path) + 12;
    char *path = malloc(len);
    if (!path) return NULL;
    snprintf(path, len, "%s/repo.lock", repo_path);
    return path;
}

fs_repo_t *fs_repo_create(const char *path) {
    fs_repo_t *repo = calloc(1, sizeof(fs_repo_t));
    if (!repo) return NULL;

    repo->path = strdup(path);
    repo->lockfile_path = build_lock_path(path);
    repo->lock_fd = -1;
    repo->is_open = false;
    repo->lock_depth = 0;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&repo->inproc_mtx, &attr);
    pthread_mutexattr_destroy(&attr);

    return repo;
}

int fs_repo_lock(fs_repo_t *repo) {
    if (!repo) return -1;
    pthread_mutex_lock(&repo->inproc_mtx);

    if (repo->lock_depth > 0) {
        repo->lock_depth++;
        pthread_mutex_unlock(&repo->inproc_mtx);
        return 0;
    }

    repo->lock_fd = open(repo->lockfile_path, O_RDWR | O_CREAT, 0600);
    if (repo->lock_fd < 0) {
        pthread_mutex_unlock(&repo->inproc_mtx);
        return -1;
    }

    if (platform_lock_file(repo, true) != 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            fprintf(stderr, "[fs_repo] Error: Repo locked by another process.\n");
        }
        close(repo->lock_fd);
        repo->lock_fd = -1;
        pthread_mutex_unlock(&repo->inproc_mtx);
        return -1;
    }

    ftruncate(repo->lock_fd, 0);
    dprintf(repo->lock_fd, "%d\n", getpid());

    repo->lock_depth = 1;
    pthread_mutex_unlock(&repo->inproc_mtx);
    return 0;
}

int fs_repo_unlock(fs_repo_t *repo) {
    if (!repo) return -1;
    pthread_mutex_lock(&repo->inproc_mtx);

    if (repo->lock_depth == 0) {
        pthread_mutex_unlock(&repo->inproc_mtx);
        return -1;
    }

    repo->lock_depth--;
    if (repo->lock_depth > 0) {
        pthread_mutex_unlock(&repo->inproc_mtx);
        return 0;
    }

    if (repo->lock_fd >= 0) {
        platform_unlock_file(repo);
        close(repo->lock_fd);
        repo->lock_fd = -1;
        unlink(repo->lockfile_path);
    }

    pthread_mutex_unlock(&repo->inproc_mtx);
    return 0;
}

static bool fs_repo_is_initialized_local(fs_repo_t *repo) {
    if (!repo) return false;
    pthread_mutex_lock(&repo->inproc_mtx);

    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/config", repo->path);

    struct stat st;
    bool init = (stat(config_path, &st) == 0);

    pthread_mutex_unlock(&repo->inproc_mtx);
    return init;
}

int fs_repo_init(fs_repo_t *repo) {
    pthread_mutex_lock(&g_repo_global_mtx);

    if (fs_repo_lock(repo) != 0) {
        pthread_mutex_unlock(&g_repo_global_mtx);
        return -1;
    }

    if (fs_repo_is_initialized_local(repo)) {
        fs_repo_unlock(repo);
        pthread_mutex_unlock(&g_repo_global_mtx);
        return -1;
    }

    mkdir(repo->path, 0755);

    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/config", repo->path);
    FILE *f = fopen(config_path, "w");
    if (f) {
        fputs("{\n  \"Identity\": {}\n}\n", f);
        fclose(f);
    }

    fs_repo_unlock(repo);
    pthread_mutex_unlock(&g_repo_global_mtx);
    return 0;
}

int fs_repo_open(fs_repo_t *repo) {
    pthread_mutex_lock(&g_repo_global_mtx);

    if (fs_repo_lock(repo) != 0) {
        pthread_mutex_unlock(&g_repo_global_mtx);
        return -1;
    }

    if (!fs_repo_is_initialized_local(repo)) {
        fs_repo_unlock(repo);
        pthread_mutex_unlock(&g_repo_global_mtx);
        return -1;
    }

    repo->is_open = true;
    pthread_mutex_unlock(&g_repo_global_mtx);
    return 0;
}

void fs_repo_free(fs_repo_t *repo) {
    if (!repo) return;
    if (repo->is_open) {
        fs_repo_unlock(repo);
    }
    pthread_mutex_destroy(&repo->inproc_mtx);
    free(repo->path);
    free(repo->lockfile_path);
    free(repo);
}
