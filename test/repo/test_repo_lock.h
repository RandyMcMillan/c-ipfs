#ifndef TEST_REPO_LOCK_H
#define TEST_REPO_LOCK_H

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

/* Prototypes from repo/fsrepo/lock.c */
struct fs_repo;
struct fs_repo* fs_repo_create(const char *path);
int fs_repo_lock(struct fs_repo *repo);
int fs_repo_unlock(struct fs_repo *repo);
void fs_repo_free(struct fs_repo *repo);

int test_repo_lock_create_free(void) {
    char tmp[] = "./tmp/test_repo_lock_XXXXXX";
    if (!mkdtemp(tmp)) return 0;
    struct fs_repo *repo = fs_repo_create(tmp);
    if (!repo) { rmdir(tmp); return 0; }
    fs_repo_free(repo);
    rmdir(tmp);
    return 1;
}

int test_repo_lock_unlock_cycle(void) {
    char tmp[] = "./tmp/test_repo_lock_XXXXXX";
    if (!mkdtemp(tmp)) return 0;
    struct fs_repo *repo = fs_repo_create(tmp);
    if (!repo) { rmdir(tmp); return 0; }
    int ret = fs_repo_lock(repo);
    if (ret == 0) fs_repo_unlock(repo);
    fs_repo_free(repo);
    rmdir(tmp);
    return ret == 0;
}

#endif
