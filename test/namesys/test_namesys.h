#include <stdlib.h>
#include <pthread.h>

#include "../test_helper.h"
#include "ipfs/cid/cid.h"
#include "ipfs/core/ipfs_node.h"
#include "ipfs/namesys/publisher.h"
#include "ipfs/namesys/resolver.h"

int test_namesys_publisher_publish() {
	int retVal = 0;
	struct IpfsNode* local_node = NULL;
	char* hash_text = "/ipns/QmZtAEqmnXMZkwVPKdyMGxUoo35cQMzNhmq6CN3DvgRwAD";
	char* repo_path = "/tmp/ipfs_1";
	char* peer_id = NULL;

	if (!drop_and_build_repository(repo_path, 4001, NULL, &peer_id)) {
		libp2p_logger_error("test_publisher", "Unable to build repository at %s.\n", repo_path);
		goto exit;
	}

	// get a local node
	if (!ipfs_node_offline_new(repo_path, &local_node)) {
		libp2p_logger_error("test_publisher", "publish: Unable to open ipfs repository.\n");
		goto exit;
	}

	// attempt to publish
	if (!ipfs_namesys_publisher_publish(local_node, hash_text)) {
		libp2p_logger_error("test_publisher", "publish: Unable to publish %s.\n", hash_text);
		goto exit;
	}

	retVal = 1;
	exit:
	ipfs_node_free(local_node);
	return retVal;
}

int test_namesys_resolver_resolve() {
	int retVal = 0;
	struct IpfsNode* local_node = NULL;
	char* hash_text = "QmZtAEqmnXMZkwVPKdyMGxUoo35cQMzNhmq6CN3DvgRwAD"; // the hash of a hello, world file
	char ipns_path[512] = ""; // the Peer ID of config.test1
	char* repo_path = "/tmp/ipfs_1";
	char* peer_id = NULL;
	char* result = NULL;

	drop_and_build_repository(repo_path, 4001, NULL, &peer_id);

	// get a local node
	if (!ipfs_node_offline_new(repo_path, &local_node)) {
		libp2p_logger_error("test_publisher", "publish: Unable to open ipfs repository.\n");
		goto exit;
	}

	// set ipns_path
	sprintf(ipns_path, "/ipfs/%s", peer_id);

	// attempt to publish
	if (!ipfs_namesys_publisher_publish(local_node, hash_text)) {
		libp2p_logger_error("test_publisher", "publish: Unable to publish %s.\n", hash_text);
		goto exit;
	}

	// attempt to retrieve
	if (!ipfs_namesys_resolver_resolve(local_node, ipns_path, 1, &result)) {
		libp2p_logger_error("test_namesys", "Could not resolve %s.\n", hash_text);
		goto exit;
	}

	if (strcmp(result, hash_text) != 0) {
		libp2p_logger_error("test_namesys", "Retrieve wrong result. %s should be %s.\n", result, hash_text);
		goto exit;
	}

	libp2p_logger_error("test_namesys", "Asked for %s and received %s. Success!\n", ipns_path, result);

	retVal = 1;
	exit:
	ipfs_node_free(local_node);
	if (result != NULL)
		free(result);
	return retVal;
}

int test_ipns_signed_record_roundtrip() {
	int retVal = 0;
	struct IpfsNode* local_node = NULL;
	char* hash_text = "/ipfs/QmZtAEqmnXMZkwVPKdyMGxUoo35cQMzNhmq6CN3DvgRwAD";
	char ipns_path[512] = "";
	char* repo_path = "/tmp/ipfs_ipns_roundtrip";
	char* peer_id = NULL;
	char* result = NULL;

	if (!drop_and_build_repository(repo_path, 4002, NULL, &peer_id)) {
		libp2p_logger_error("test_ipns_roundtrip", "Unable to build repository at %s.\n", repo_path);
		goto exit;
	}

	if (!ipfs_node_offline_new(repo_path, &local_node)) {
		libp2p_logger_error("test_ipns_roundtrip", "Unable to open ipfs repository.\n");
		goto exit;
	}

	sprintf(ipns_path, "/ipns/%s", peer_id);

	// Publish a signed IPNS record
	if (!ipfs_namesys_publisher_publish(local_node, hash_text)) {
		libp2p_logger_error("test_ipns_roundtrip", "Unable to publish %s.\n", hash_text);
		goto exit;
	}

	// Resolve the IPNS name
	if (!ipfs_namesys_resolver_resolve(local_node, ipns_path, 1, &result)) {
		libp2p_logger_error("test_ipns_roundtrip", "Could not resolve %s.\n", ipns_path);
		goto exit;
	}

	if (strcmp(result, hash_text) != 0) {
		libp2p_logger_error("test_ipns_roundtrip", "Wrong result: %s should be %s.\n", result, hash_text);
		goto exit;
	}

	libp2p_logger_error("test_ipns_roundtrip", "Roundtrip OK: %s -> %s\n", ipns_path, result);
	retVal = 1;

exit:
	ipfs_node_free(local_node);
	if (result != NULL)
		free(result);
	return retVal;
}
