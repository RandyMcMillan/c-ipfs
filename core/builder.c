#include <pthread.h>
#include "ipfs/core/builder.h"
#include "ipfs/core/ipfs_node.h"
#include "libp2p/utils/logger.h"

int ipfs_core_builder_new_node(struct Context* context, struct BuildCfg* build_cfg, struct IpfsNode* buildConfig) {
	(void)context;
	if (!build_cfg || !buildConfig)
		return 0;

	struct IpfsNode* node = NULL;
	const char* repo_path = ".ipfs";
	if (build_cfg->nil_repo)
		repo_path = NULL;

	int ret = 0;
	if (build_cfg->online) {
		ret = ipfs_node_online_new(repo_path, &node);
	} else {
		ret = ipfs_node_offline_new(repo_path, &node);
	}

	if (!ret || node == NULL) {
		libp2p_logger_error("builder", "Failed to create %s node.\n",
			build_cfg->online ? "online" : "offline");
		return 0;
	}

	/* Copy constructed node into caller-provided storage */
	memcpy(buildConfig, node, sizeof(struct IpfsNode));
	free(node);
	return 1;
}
