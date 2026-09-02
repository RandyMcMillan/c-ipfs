#include <string.h>

#include "ipfs/blocks/block.h"
#include "ipfs/cid/cid.h"
#include "libp2p/crypto/sha256.h"
#include "mh/multihash.h"
#include "mh/hashes.h"

int test_block_new_raw() {
	const unsigned char data[] = "raw block data";
	struct Block* block = ipfs_block_new_raw(data, sizeof(data) - 1);
	if (block == NULL)
		return 0;

	if (block->cid == NULL || block->cid->version != 1 || block->cid->codec != CID_RAW) {
		ipfs_block_free(block);
		return 0;
	}

	if (block->data_length != sizeof(data) - 1 || memcmp(block->data, data, sizeof(data) - 1) != 0) {
		ipfs_block_free(block);
		return 0;
	}

	ipfs_block_free(block);
	return 1;
}

int test_block_validate() {
	const unsigned char data[] = "valid block data";
	struct Block* block = ipfs_block_new_raw(data, sizeof(data) - 1);
	if (block == NULL)
		return 0;

	if (!ipfs_block_validate(block)) {
		ipfs_block_free(block);
		return 0;
	}

	// tamper with data
	block->data[0] = 'X';
	if (ipfs_block_validate(block)) {
		ipfs_block_free(block);
		return 0;
	}

	ipfs_block_free(block);
	return 1;
}
