#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "ipfs/transport/swarm_dialer_v2_bridge.h"
#include "ipfs/repo/fsrepo/fs_repo.h"

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);

    const char *repo_path = "/tmp/c_ipfs_test_repo";
    if (argc > 1)
        repo_path = argv[1];

    struct FSRepo *fs_repo = NULL;
    if (!ipfs_repo_fsrepo_new(repo_path, NULL, &fs_repo)) {
        fprintf(stderr, "Failed to create FSRepo object\n");
        return 1;
    }

    if (!ipfs_repo_fsrepo_open(fs_repo)) {
        fprintf(stderr, "Failed to open repo at %s\n", repo_path);
        ipfs_repo_fsrepo_free(fs_repo);
        return 1;
    }

    const char *target = "/ip4/127.0.0.1/tcp/4001";
    if (argc > 2)
        target = argv[2];

    fprintf(stderr, "Dialing %s using v2 stack...\n", target);

    ipfs_v2_stream_bridge_t *bridge = NULL;
    int rc = ipfs_swarm_connect_v2_bridge(target,
                                          NULL,
                                          &fs_repo->config->identity->private_key,
                                          &bridge);

    if (rc != 0 || bridge == NULL) {
        fprintf(stderr, "v2 dial failed: rc=%d\n", rc);
        ipfs_repo_fsrepo_free(fs_repo);
        return 1;
    }

    fprintf(stderr, "v2 dial succeeded! Reading Identify response...\n");

    char buf[4096];
    ssize_t total = 0;
    for (;;) {
        ssize_t n = ipfs_v2_stream_read(bridge, buf + total, sizeof(buf) - total - 1);
        if (n <= 0)
            break;
        total += n;
        if ((size_t)total >= sizeof(buf) - 1)
            break;
    }
    buf[total] = '\0';
    fprintf(stderr, "Read %zd bytes from Identify response\n", total);
    if (total > 0)
        fprintf(stderr, "%.*s\n", (int)(total > 200 ? 200 : total), buf);

    ipfs_v2_stream_free(bridge);
    ipfs_repo_fsrepo_free(fs_repo);
    return 0;
}
