/***
 * This implements the BitswapNetwork. Members of this network can fill requests and
 * smartly handle queues of local and remote requests.
 *
 * For a somewhat accurate diagram of how this may work, @see https://github.com/ipfs/js-ipfs-bitswap
 */

#include <pthread.h>

#include "libp2p/utils/logger.h"
#include "libp2p/conn/dialer.h"
#include "ipfs/exchange/bitswap/network.h"
#include "ipfs/exchange/bitswap/peer_request_queue.h"
#include "ipfs/cid/cid.h"

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
	struct Stream* bitswap_stream = peer->sessionContext->bitswap_stream;
	if (bitswap_stream == NULL) {
		// Open a Bitswap 1.2.0 stream lazily. Hold the default stream's socket
		// mutex so the swarm listener does not race with the synchronous
		// multistream/yamux handshake.
		struct Stream* default_stream = peer->sessionContext->default_stream;
		if (default_stream != NULL && default_stream->socket_mutex != NULL)
			pthread_mutex_lock(default_stream->socket_mutex);
		bitswap_stream = libp2p_conn_dialer_get_stream(context->ipfsNode->dialer, peer, "/ipfs/bitswap/1.2.0\n");
		if (default_stream != NULL && default_stream->socket_mutex != NULL)
			pthread_mutex_unlock(default_stream->socket_mutex);
		if (bitswap_stream == NULL) {
			libp2p_logger_error("bitswap_network", "Unable to open Bitswap 1.2.0 stream for peer %s.\n", libp2p_peer_id_to_string(peer));
			return 0;
		}
		peer->sessionContext->bitswap_stream = bitswap_stream;
	}
	// protobuf the message (Bitswap 1.2.0 messages are length-prefixed by the multistream layer)
	size_t buf_size = ipfs_bitswap_message_protobuf_encode_size(message);
	uint8_t* buf = (uint8_t*) malloc(buf_size);
	if (buf == NULL)
		return 0;
	if (!ipfs_bitswap_message_protobuf_encode(message, buf, buf_size, &buf_size)) {
		free(buf);
		return 0;
	}
	// send it on the negotiated Bitswap stream
	struct StreamMessage outgoing;
	outgoing.data = buf;
	outgoing.data_size = buf_size;
	libp2p_logger_debug("bitswap_network", "Sending %zu byte Bitswap message to %s.\n", buf_size, libp2p_peer_id_to_string(peer));
	{
		char hex[256];
		hex[0] = '\0';
		for (size_t i = 0; i < buf_size && i < sizeof(hex)/3 - 1; i++) {
			char tmp[4];
			snprintf(tmp, sizeof(tmp), "%02x ", buf[i]);
			strncat(hex, tmp, sizeof(hex) - strlen(hex) - 1);
		}
		fprintf(stderr, "[bitswap_network] Message hex (%zu bytes): %s\n", buf_size, hex);
	}
	int bytes_written = bitswap_stream->write(bitswap_stream->stream_context, &outgoing);
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
		return 1;
	}

	return 0;
}

/***
 * React to a remote peer's Bitswap 1.2.0 block presence announcement.
 * If the peer has a block that we were requesting, mark the request as
 * complete/canceled so duplicate work is avoided.
 * @param peer_request the request queue entry for the remote peer
 * @param presence the received presence announcement
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_bitswap_network_handle_block_presence(struct PeerRequest* peer_request, const struct BitswapBlockPresence* presence) {
	if (peer_request == NULL || presence == NULL || presence->cid == NULL || presence->cid_size == 0)
		return 0;

	struct Cid* cid = (struct Cid*) malloc(sizeof(struct Cid));
	if (cid == NULL || !ipfs_cid_cast(presence->cid, presence->cid_size, cid)) {
		free(cid);
		return 0;
	}

	int retVal = 0;
	if (presence->type == BLOCK_PRESENCE_HAVE) {
		/* A HAVE announcement means the peer can satisfy matching pending wants. */
		ipfs_bitswap_network_adjust_cid_queue(peer_request->cids_we_want, cid, 1);
		retVal = 1;
	} else {
		retVal = 1;
	}

	ipfs_cid_free(cid);
	return retVal;
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
	// un-protobuf the message
	struct BitswapMessage* message = NULL;
	if (!ipfs_bitswap_message_protobuf_decode(bytes, bytes_length, &message))
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
	// block presences - what peers can serve (Bitswap 1.2.0)
	if (message->block_presences != NULL && message->block_presences->total > 0 && sessionContext->remote_peer_id != NULL) {
		struct Libp2pPeer* presence_peer = libp2p_peerstore_get_or_add_peer_by_id(node->peerstore, (unsigned char*)sessionContext->remote_peer_id, strlen(sessionContext->remote_peer_id));
		if (presence_peer != NULL) {
			struct PeerRequest* presence_request = ipfs_peer_request_queue_find_peer(bitswapContext->peerRequestQueue, presence_peer);
			for(int i = 0; i < message->block_presences->total; i++) {
				struct BitswapBlockPresence* presence_entry = (struct BitswapBlockPresence*)libp2p_utils_vector_get(message->block_presences, i);
				ipfs_bitswap_network_handle_block_presence(presence_request, presence_entry);
			}
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
			struct Cid* cid = (struct Cid*) malloc(sizeof(struct Cid));
			if (cid == NULL || !ipfs_cid_cast(entry->block, entry->block_size, cid) || cid->hash_length == 0) {
				libp2p_logger_error("bitswap_network", "Message had invalid CID\n");
				free(cid);
				ipfs_bitswap_message_free(message);
				return 0;
			}
			ipfs_bitswap_network_adjust_cid_queue(peerRequest->cids_they_want, cid, entry->cancel);
		}
	}
	ipfs_bitswap_message_free(message);
	return 1;
}
