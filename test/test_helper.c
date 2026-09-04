#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include "ipfs/repo/init.h"
#include "ipfs/repo/fsrepo/fs_repo.h"
#include "ipfs/core/daemon.h"
#include "libp2p/os/utils.h"
#include "libp2p/utils/vector.h"

int cp(const char *to, const char *from)
{
    int fd_to, fd_from;
    char buf[4096];
    ssize_t nread;
    int saved_errno;

    fd_from = open(from, O_RDONLY);
    if (fd_from < 0)
        return -1;

    fd_to = open(to, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd_to < 0)
        goto out_error;

    while (nread = read(fd_from, buf, sizeof buf), nread > 0)
    {
        char *out_ptr = buf;
        ssize_t nwritten;

        do {
            nwritten = write(fd_to, out_ptr, nread);

            if (nwritten >= 0)
            {
                nread -= nwritten;
                out_ptr += nwritten;
            }
            else if (errno != EINTR)
            {
                goto out_error;
            }
        } while (nread > 0);
    }

    if (nread == 0)
    {
        if (close(fd_to) < 0)
        {
            fd_to = -1;
            goto out_error;
        }
        close(fd_from);

        /* Success! */
        return 0;
    }

  out_error:
    saved_errno = errno;

    close(fd_from);
    if (fd_to >= 0)
        close(fd_to);

    errno = saved_errno;
    return -1;
}

/***
 * Helper to create a test file in the OS
 */
int create_file(const char* fileName, unsigned char* bytes, size_t num_bytes) {
	FILE* file = fopen(fileName, "wb");
	fwrite(bytes, num_bytes, 1, file);
	fclose(file);
	return 1;
}

/***
 * Create a buffer with some data
 */
int create_bytes(unsigned char* buffer, size_t num_bytes) {
	int counter = 0;

	for(int i = 0; i < num_bytes; i++) {
		buffer[i] = counter++;
		if (counter > 15)
			counter = 0;
	}
	return 1;
}

/***
 * Remove a directory and everything in it
 * @param path the directory to remove
 * @returns true(1) on success, otherwise false(0)
 */
int remove_directory(const char *path)
{
   DIR *d = opendir(path);
   size_t path_len = strlen(path);
   int r = -1;

   if (d)
   {
      struct dirent *p;

      r = 1;

      while (r && (p=readdir(d)))
      {
          int r2 = -1;
          char *buf;
          size_t len;

          /* Skip the names "." and ".." as we don't want to recurse on them. */
          if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
          {
             continue;
          }

          len = path_len + strlen(p->d_name) + 2;
          buf = malloc(len);

          if (buf)
          {
             struct stat statbuf;

             snprintf(buf, len, "%s/%s", path, p->d_name);

             if (!stat(buf, &statbuf))
             {
                if (S_ISDIR(statbuf.st_mode))
                {
                   r2 = remove_directory(buf);
                }
                else
                {
                   r2 = !unlink(buf);
                }
             }

             free(buf);
          }

          r = r2;
      }

      closedir(d);
   }

   if (r)
   {
      r = !rmdir(path);
   }

   return r;
}

/***
 * Drop a repository by removing the directory
 */
int drop_repository(const char* path) {

	if (os_utils_file_exists(path)) {
		return remove_directory(path);
	}

	return 1;
}

static void _mkdir(const char *dir, const mode_t mode) {
        char tmp[256];
        char *p = NULL;
        size_t len;

        snprintf(tmp, sizeof(tmp),"%s",dir);
        len = strlen(tmp);
        if(tmp[len - 1] == '/')
                tmp[len - 1] = 0;
        for(p = tmp + 1; *p; p++)
                if(*p == '/') {
                        *p = 0;
                        mkdir(tmp, mode);
                        *p = '/';
                }
        mkdir(tmp, S_IRWXU);
}
/**
 * drops and builds a repository at the specified path
 * @param path the path
 * @param swarm_port the port that the swarm should run on
 * @param bootstrap_peers a vector of fellow peers as MultiAddresses, can be NULL
 * @param peer_id a place to store the generated peer id
 * @returns true(1) on success, otherwise false(0)
 */
static int force_remove_directory(const char *path) {
	// First attempt: normal recursive removal
	if (remove_directory(path))
		return 1;

	// Second attempt: force-remove any remaining files (including stale locks)
	DIR *d = opendir(path);
	if (d) {
		struct dirent *p;
		while ((p = readdir(d))) {
			if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
				continue;
			char buf[512];
			snprintf(buf, sizeof(buf), "%s/%s", path, p->d_name);
			unlink(buf);
		}
		closedir(d);
	}
	rmdir(path);
	return !os_utils_file_exists(path);
}

int drop_and_build_repository(const char* path, int swarm_port, struct Libp2pVector* bootstrap_peers, char **peer_id) {

	if (os_utils_file_exists(path)) {
		if (!force_remove_directory(path)) {
			return 0;
		}
	}
	_mkdir(path, S_IRWXU);

	return make_ipfs_repository(path, swarm_port, bootstrap_peers, peer_id, NULL);
}

int drop_and_build_repository_without_bootstrap(const char* path, int swarm_port, char **peer_id) {
	struct Libp2pVector* bootstrap_peers = libp2p_utils_vector_new(1);
	if (bootstrap_peers == NULL)
		return 0;

	int retVal = drop_and_build_repository(path, swarm_port, bootstrap_peers, peer_id);
	libp2p_utils_vector_free(bootstrap_peers);
	return retVal;
}

/***
 * Drop a repository and build a new one with the specified config file
 * @param path where to create it
 * @param fs_repo the results
 * @param config_file_to_copy where to find the config file to copy
 * @returns true(1) on success, otherwise false(0)
 */
int drop_build_open_repo(const char* path, struct FSRepo** fs_repo, const char* config_filename_to_copy) {
	if (!drop_and_build_repository(path, 4001, NULL, NULL))
		return 0;

	if (config_filename_to_copy != NULL) {
		// attach config filename to path
		char *config = (char*) malloc(strlen(path) + 8);
		strcpy(config, path);
		// erase slash if there is one
		if (config[strlen(path)-1] == '/')
			config[strlen(path)-1] = 0;
		strcat(config, "/config");
		// delete the old
		if (unlink(config) != 0) {
			free(config);
			return 0;
		}
		// copy pre-built config file into directory
		if (cp(config, config_filename_to_copy) < 0) {
			fprintf(stderr, "Unable to copy %s to %s. Error number %d.\n", config_filename_to_copy, config, errno);
			free(config);
			return 0;
		}
		free(config);
	}

	if (!ipfs_repo_fsrepo_new(path, NULL, fs_repo))
		return 0;

	if (!ipfs_repo_fsrepo_open(*fs_repo)) {
		free(*fs_repo);
		*fs_repo = NULL;
		return 0;
	}
	return 1;
}

/***
 * Drop a repository and build a new one
 * @param path where to create it
 * @param fs_repo the results
 * @returns true(1) on success, otherwise false(0)
 */
int drop_build_and_open_repo(const char* path, struct FSRepo** fs_repo) {
	return drop_build_open_repo(path, fs_repo, NULL);
}

/***
 * Start a daemon (usefull in a separate thread)
 * @param arg a char string of the repo path
 * @returns NULL
 */
void* test_daemon_start(void* arg) {
	ipfs_daemon_start((char*)arg);
	return NULL;
}

int have_file_in_blockstore(const char* dir, const char* filename) {
	return 0;
}
