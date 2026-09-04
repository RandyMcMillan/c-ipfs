
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include "ipfs/core/net.h"
#include "ipfs/core/ipfs_node.h"
#include "ipfs/transport/transport.h"
#include "ipfs/transport/stream_bridge.h"
#include "libp2p/conn/dialer.h"
#include "libp2p/conn/session.h"
#include "libp2p/swarm/swarm.h"
#include "libp2p/utils/logger.h"

/**
 * Do a socket accept
 * @param listener the listener
 * @param stream the returned stream
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_core_net_accept(struct IpfsListener* listener, struct Stream* stream) {
	(void)stream;
	if (listener == NULL || listener->listen_fd < 0)
		return 0;

	struct sockaddr_in client_addr;
	socklen_t addr_len = sizeof(client_addr);
	int client_fd = accept(listener->listen_fd, (struct sockaddr*)&client_addr, &addr_len);
	if (client_fd < 0) {
		libp2p_logger_error("net", "accept failed: %s\n", strerror(errno));
		return 0;
	}

	libp2p_logger_debug("net", "Accepted connection on fd %d\n", client_fd);
	close(client_fd);
	return 1;
}

/**
 * Listen using a particular protocol
 * @param node the node
 * @param protocol the protocol to use
 * @param listener the results
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_core_net_listen(struct IpfsNode* node, char* protocol, struct IpfsListener* listener) {
	(void)node;
	if (protocol == NULL || listener == NULL)
		return 0;

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		libp2p_logger_error("net", "socket creation failed: %s\n", strerror(errno));
		return 0;
	}

	int opt = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		libp2p_logger_error("net", "setsockopt failed: %s\n", strerror(errno));
		close(fd);
		return 0;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(listener->port > 0 ? listener->port : 4001);

	if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		libp2p_logger_error("net", "bind failed: %s\n", strerror(errno));
		close(fd);
		return 0;
	}

	if (listen(fd, 10) < 0) {
		libp2p_logger_error("net", "listen failed: %s\n", strerror(errno));
		close(fd);
		return 0;
	}

	listener->listen_fd = fd;
	listener->protocol = strdup(protocol);
	libp2p_logger_debug("net", "Listening on port %d for protocol %s\n",
		listener->port > 0 ? listener->port : 4001, protocol);
	return 1;
}

/**
 * Attempt to dial a peer via the transport registry (QUIC / WebSocket).
 * On success, sets up the peer's session context and adds to swarm.
 *
 * @param node the local node
 * @param peer the peer to dial
 * @return true(1) on success, false(0) otherwise
 */
static int ipfs_net_dial_registry(const struct IpfsNode* node, struct Libp2pPeer* peer) {
	if (!node || !peer || !peer->addr_head)
		return 0;

	struct Libp2pLinkedList* curr = peer->addr_head;
	while (curr) {
		struct MultiAddress* ma = (struct MultiAddress*)curr->item;
		if (!ma || !ma->string) {
			curr = curr->next;
			continue;
		}

		/* Only attempt registry dial for QUIC or WebSocket addresses */
		if (strstr(ma->string, "/quic") == NULL && strstr(ma->string, "/ws") == NULL) {
			curr = curr->next;
			continue;
		}

		libp2p_stream_t* lstream = NULL;
		if (transport_registry_dial((transport_registry_t*)&node->transport_registry,
					    ma->string, &lstream) != 0 || lstream == NULL) {
			libp2p_logger_debug("net",
				    "Registry dial failed for %s, trying next address.\n",
				    ma->string);
			curr = curr->next;
			continue;
		}

		struct Stream* bridge = ipfs_transport_stream_bridge_new(lstream, ma->string);
		if (!bridge) {
			lstream->close(lstream);
			curr = curr->next;
			continue;
		}

		/* Set up session context */
		if (peer->sessionContext == NULL) {
			peer->sessionContext = libp2p_session_context_new();
		}
		if (peer->sessionContext != NULL) {
			peer->sessionContext->insecure_stream = bridge;
			peer->sessionContext->default_stream = bridge;
			peer->sessionContext->datastore = node->repo->config->datastore;
			peer->sessionContext->filestore = node->repo->config->filestore;
		}

		peer->connection_type = CONNECTION_TYPE_CONNECTED;
		libp2p_swarm_add_peer(node->swarm, peer);

		libp2p_logger_debug("net",
			    "Registry dial succeeded to %s via %s\n",
			    peer->id ? peer->id : "?", ma->string);
		return 1;
	}

	return 0;
}

/***
 * Dial a peer
 * @param node this node
 * @param peer_id who to dial (null terminated string)
 * @param protocol the protocol to use
 * @param stream the resultant stream
 * @returns true(1) on success, otherwise false(0)
 */
int ipsf_core_net_dial(const struct IpfsNode* node, const char* peer_id, const char* protocol, struct Stream* stream) {
	(void)protocol;
	(void)stream;
	if (!node || !peer_id)
		return 0;

	struct Libp2pPeer* peer = libp2p_peerstore_get_peer(node->peerstore, (const unsigned char*)peer_id, strlen(peer_id));
	if (peer == NULL) {
		libp2p_logger_error("net", "Dial failed: peer %s not found in peerstore.\n", peer_id);
		return 0;
	}

	/* Try transport registry first for QUIC / WebSocket addresses */
	if (ipfs_net_dial_registry(node, peer)) {
		libp2p_logger_debug("net", "Dial to peer %s succeeded via transport registry.\n", peer_id);
		return 1;
	}

	/* Fall back to legacy TCP dialer */
	if (!libp2p_peer_connect(node->dialer, peer, node->peerstore, node->repo->config->datastore, 10)) {
		libp2p_logger_error("net", "Dial failed: could not connect to peer %s.\n", peer_id);
		return 0;
	}

	libp2p_logger_debug("net", "Dial to peer %s succeeded.\n", peer_id);
	return 1;
}
