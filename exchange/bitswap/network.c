/***
 * This implements the BitswapNetwork. Members of this network can fill requests and
 * smartly handle queues of local and remote requests.
 *
 * For a somewhat accurate diagram of how this may work, @see https://github.com/ipfs/js-ipfs-bitswap
 */

#include <pthread.h>

#include "libp2p/utils/logger.h"
#include "ipfs/exchange/bitswap/network.h"
#include "ipfs/exchange/bitswap/peer_request_queue.h"

/****
 * send a message to a particular peer
 * @param context the BitswapContext
 * @param peer the peer that is the recipient
 * @param message the message to send
 */
int ipfs_bitswap_network_send_message(const struct BitswapContext* context, struct Libp2pPeer* peer, const struct BitswapMessage* message) {
	libp2p_logger_debug("bitswap_network", "Sending bitswap message to %s.\n", libp2p_peer_id_to_string(peer));
	// get a connection to the peer
	if (peer->connection_type != CONNECTION_TYPE_CONNECTED || peer->sessionContext == NULL) {
		libp2p_peer_connect(context->ipfsNode->dialer, peer, context->ipfsNode->peerstore, context->ipfsNode->repo->config->datastore, 10);
		if(peer->connection_type != CONNECTION_TYPE_CONNECTED)
			return 0;
	}
	// protobuf the message
	size_t buf_size = ipfs_bitswap_message_protobuf_encode_size(message);
	uint8_t* buf = (uint8_t*) malloc(buf_size + 20);
	if (buf == NULL)
		return 0;
	if (!ipfs_bitswap_message_protobuf_encode(message, &buf[20], buf_size, &buf_size)) {
		free(buf);
		return 0;
	}
	// tack on the protocol header
	memcpy(buf, "/ipfs/bitswap/1.1.0\n", 20);
	buf_size += 20;
	// send it
	struct StreamMessage outgoing;
	outgoing.data = buf;
	outgoing.data_size = buf_size;
	int bytes_written = peer->sessionContext->default_stream->write(peer->sessionContext, &outgoing);
	if (bytes_written <= 0) {
		free(buf);
		return 0;
	}
	free(buf);
	return 1;
}

/***
 * Remove a cid from the queue
 * @param cids the vector of cids
 * @param cid the cid to remove
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_bitswap_network_adjust_cid_queue(struct Libp2pVector* collection, struct Cid* cid, int cancel) {
	if (collection == NULL || cid == NULL)
		return 0;

	for(int i = 0; i < collection->total; i++) {
		const struct CidEntry* current = (const struct CidEntry*)libp2p_utils_vector_get(collection, i);
		if (ipfs_cid_compare(current->cid, cid) == 0) {
			if (cancel)
				libp2p_utils_vector_delete(collection, i);
			return 1;
		}
	}

	// not found. Add it if we're not cancelling
	if (!cancel) {
		struct CidEntry* cidEntry = ipfs_bitswap_peer_request_cid_entry_new();
		cidEntry->cid = cid;
		cidEntry->cancel = 0;
		libp2p_utils_vector_add(collection, cidEntry);
	}

	return 0;
}

/***
 * Handle a raw incoming bitswap message from the network
 * @param node us
 * @param sessionContext the connection context
 * @param bytes the message
 * @param bytes_size the size of the message
 * @returns true(1) on success, false(0) otherwise.
 */
int ipfs_bitswap_network_handle_message(const struct IpfsNode* node, const struct SessionContext* sessionContext, const uint8_t* bytes, size_t bytes_length) {
	struct BitswapContext* bitswapContext = (struct BitswapContext*)node->exchange->exchangeContext;
	// strip off the protocol header
	int start = -1;
	for(int i = 0; i < bytes_length; i++) {
		if (bytes[i] == '\n') {
			start = i+1;
			break;
		}
	}
	if (start == -1)
		return 0;
	// un-protobuf the message
	struct BitswapMessage* message = NULL;
	if (!ipfs_bitswap_message_protobuf_decode(&bytes[start], bytes_length - start, &message))
		return 0;
	// process the message
	// payload - what we want
	if (message->payload != NULL) {
		for(int i = 0; i < message->payload->total; i++) {
			struct Block* blk = (struct Block*)libp2p_utils_vector_get(message->payload, i);
			// we need a copy of the block so it survives the destruction of the message
			node->exchange->HasBlock(node->exchange, ipfs_block_copy(blk));
		}
	}
	// wantlist - what they want
	if (message->wantlist != NULL && message->wantlist->entries != NULL && message->wantlist->entries->total > 0) {
		// get the peer
		if (sessionContext->remote_peer_id == NULL) {
			ipfs_bitswap_message_free(message);
			return 0;
		}
		struct Libp2pPeer* peer = libp2p_peerstore_get_or_add_peer_by_id(node->peerstore, (unsigned char*)sessionContext->remote_peer_id, strlen(sessionContext->remote_peer_id));
		if (peer == NULL) {
			libp2p_logger_error("bitswap_network", "Unable to find or add peer %s of length %d to peerstore.\n", sessionContext->remote_peer_id, strlen(sessionContext->remote_peer_id));
			ipfs_bitswap_message_free(message);
			return 0;
		}
		// find the queue (adds it if it is not there)
		struct PeerRequest* peerRequest = ipfs_peer_request_queue_find_peer(bitswapContext->peerRequestQueue, peer);
		for(int i = 0; i < message->wantlist->entries->total; i++) {
			struct WantlistEntry* entry = (struct WantlistEntry*) libp2p_utils_vector_get(message->wantlist->entries, i);
			// turn the "block" back into a cid
			struct Cid* cid = NULL;
			if (!ipfs_cid_protobuf_decode(entry->block, entry->block_size, &cid) || cid->hash_length == 0) {
				libp2p_logger_error("bitswap_network", "Message had invalid CID\n");
				ipfs_cid_free(cid);
				ipfs_bitswap_message_free(message);
				return 0;
			}
			ipfs_bitswap_network_adjust_cid_queue(peerRequest->cids_they_want, cid, entry->cancel);
		}
	}
	ipfs_bitswap_message_free(message);
	return 1;
}
