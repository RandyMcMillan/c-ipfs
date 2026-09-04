#ifndef IPFS_REPO_FS_REPO_VERSION_H
#define IPFS_REPO_FS_REPO_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

int ipfs_repo_check_and_migrate(const char *repo_path);
int ipfs_repo_lock(const char *repo_path, int *out_lock_fd);
void ipfs_repo_unlock(int lock_fd);

#ifdef __cplusplus
}
#endif

#endif /* IPFS_REPO_FS_REPO_VERSION_H */
