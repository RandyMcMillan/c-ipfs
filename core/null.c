#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "libp2p/conn/session.h"
#include "libp2p/net/connectionstream.h"
#include "libp2p/net/multistream.h"
#include "libp2p/net/p2pnet.h"
#include "libp2p/net/protocol.h"
#include "libp2p/nodeio/nodeio.h"
#include "libp2p/os/utils.h"
#include "libp2p/record/message.h"
#include "libp2p/routing/dht_protocol.h"
#include "libp2p/secio/secio.h"
#include "libp2p/utils/logger.h"
#include "libp2p/swarm/swarm.h"
#include "ipfs/core/daemon.h"
#include "ipfs/core/ipfs_node.h"
#include "ipfs/core/v2_listen.h"
#include "ipfs/exchange/bitswap/network.h"
#include "ipfs/journal/journal.h"
#include "ipfs/merkledag/merkledag.h"
#include "ipfs/merkledag/node.h"
#include "ipfs/routing/routing.h"
#include "ipfs/util/thread_pool.h"
#include "libp2p/swarm/swarm.h"

#define BUF_SIZE 4096

// this should be set to 5 for normal operation, perhaps higher for debugging purposes
#define DEFAULT_NETWORK_TIMEOUT 5

static int null_shutting_down = 0;

/***
 * Listens on a particular stream, and marshals the request
 * @param stream the stream to listen to
 * @param protocol_handlers a vector of protocol handlers
 * @returns <0 on error, 0 if we shouldn't handle this anymore, or 1 on success
 */
int ipfs_null_listen_and_handle(struct Stream* stream, struct Libp2pVector* protocol_handlers) {
	struct StreamMessage* results = NULL;
	int retVal = 0;
	// Read from the network
	if (!stream->read(stream->stream_context, &results, DEFAULT_NETWORK_TIMEOUT)) {
		libp2p_logger_error("null", "Unable to read from network. Exiting.\n");
		return retVal;
	}
	if (results != NULL) {
		libp2p_logger_debug("null", "Attempting to marshal %d bytes from network.\n", results->data_size);
		retVal = libp2p_protocol_marshal(results, stream, protocol_handlers);
		libp2p_logger_debug("null", "The return value from the attempt to marshal %d bytes was %d.\n", results->data_size, retVal);
		libp2p_stream_message_free(results);
	} else {
		libp2p_logger_debug("null", "Attempted read, but results were null. This is normal.\n");
	}
	return retVal;
}

/**
 * We've received a new connection. Find out what they want.
 *
 * @param ptr a pointer to a null_connection_params struct
 */
void ipfs_null_connection (void *ptr) {
    struct null_connection_params *connection_param = (struct null_connection_params*) ptr;
    int retVal = 0;

    struct SessionContext* session = libp2p_session_context_new();
    if (session == NULL) {
		libp2p_logger_error("null", "Unable to allocate SessionContext. Out of memory?\n");
		return;
    }

    session->datastore = connection_param->local_node->repo->config->datastore;
    session->filestore = connection_param->local_node->repo->config->filestore;
    session->host = connection_param->ip;
    session->port = connection_param->port;
    session->insecure_stream = libp2p_net_connection_new(connection_param->file_descriptor, connection_param->ip, connection_param->port, session);
    session->default_stream = session->insecure_stream;

    libp2p_logger_info("null", "Connection %d, count %d\n", connection_param->file_descriptor, *(connection_param->count));

    // try to read from the network
	// handle the call
	for(;;) {
		// Read from the network
		retVal = ipfs_null_listen_and_handle(session->default_stream, connection_param->local_node->protocol_handlers);
		if (retVal < 0) {
			// exit the loop on error
			libp2p_logger_debug("null", "Exiting loop due to retVal being %d.\n", retVal);
			break;
		}
	} // end of loop

	(*(connection_param->count))--; // update counter.
	if (connection_param->ip != NULL)
		free(connection_param->ip);
	free (connection_param);
    return;
}

int ipfs_null_do_maintenance(struct IpfsNode* local_node, struct Libp2pPeer* peer) {
	if (peer == NULL) {
		return 0;
	}
	if (peer->is_local) {
		return 1;
	}
	// Is this peer one of our backup partners?
	struct ReplicationPeer* replication_peer = repo_config_get_replication_peer(local_node->repo->config->replication, peer);
	long long announce_secs = local_node->repo->config->replication->announce_minutes * 60;
	// If so, has there been enough time since the last attempt a backup?
	if (replication_peer != NULL) {
		announce_secs -= os_utils_gmtime() - replication_peer->lastConnect;
		libp2p_logger_debug("null", "Checking to see if we should send backup notification to peer %s. Time since last backup: %lld.\n", libp2p_peer_id_to_string(replication_peer->peer), announce_secs);
	}
	// should we attempt to connect if we're not already?
	if (replication_peer != NULL && local_node->repo->config->replication->announce && announce_secs < 0) {
		// try to connect if we aren't already
		if (peer->connection_type != CONNECTION_TYPE_CONNECTED) {
			if (!libp2p_peer_connect(local_node->dialer, peer, local_node->peerstore, local_node->repo->config->datastore, 2)) {
				return 0;
			}
		}
		// attempt a backup, don't forget to reset timer
		libp2p_logger_debug("null", "Attempting a sync of node %s.\n", libp2p_peer_id_to_string(peer));
		ipfs_journal_sync(local_node, replication_peer);
		libp2p_logger_debug("null", "Sync message sent. Maintenance complete for node %s.\n", libp2p_peer_id_to_string(peer));
	} else {
		unsigned long long last_comm = libp2p_peer_last_comm(peer);
		if (os_utils_gmtime() - last_comm > 180) {
			// try a ping, but only if we're connected
			libp2p_logger_debug("null", "Attempting ping of %s.\n", libp2p_peer_id_to_string(peer));
			if (peer->connection_type == CONNECTION_TYPE_CONNECTED && !local_node->routing->Ping(local_node->routing, peer)) {
				libp2p_logger_debug("null", "Attempted ping of %s failed.\n", peer->id);
				peer->connection_type = CONNECTION_TYPE_NOT_CONNECTED;
			}
		}
	}
	return 1;
}


/***
 * Called by the daemon to listen for connections
 * @param ptr a pointer to an IpfsNodeListenParams struct
 * @returns nothing useful.
 */
void* ipfs_null_listen (void *ptr)
{
	null_shutting_down = 0;
    int socketfd, s, count = 0;
    //threadpool thpool = thpool_init(25);
    struct IpfsNodeListenParams *listen_param;

    listen_param = (struct IpfsNodeListenParams*) ptr;

    if ((socketfd = socket_listen(socket_tcp4(), &(listen_param->ipv4), &(listen_param->port))) <= 0) {
        libp2p_logger_error("null", "Failed to init null router. Address: %d, Port: %d\n", listen_param->ipv4, listen_param->port);
        return (void*) 2;
    }

    libp2p_logger_info("null", "Ipfs listening on %d\n", listen_param->port);

    // when we have nothing to do, check on the connections to see if we're still connected
    struct Libp2pLinkedList* current_peer_entry = NULL;
    if (listen_param->local_node->peerstore->head_entry != NULL)
    		current_peer_entry = listen_param->local_node->peerstore->head_entry;

    // the main loop, listening for new connections
    for (;;) {
		//libp2p_logger_debug("null", "%s Attempting socket read with fd %d.\n", listen_param->local_node->identity->peer->id, socketfd);
		int numDescriptors = socket_read_select4(socketfd, 2);
		if (null_shutting_down) {
			libp2p_logger_debug("null", "%s null_listen shutting down.\n", listen_param->local_node->identity->peer->id);
			break;
		}
		if (numDescriptors > 0) {
			s = socket_accept4(socketfd, &(listen_param->ipv4), &(listen_param->port));
			if (count >= CONNECTIONS) { // limit reached.
				close (s);
				continue;
			}

			// Handle connection with v2 stack (Noise + Yamux + Identify)
			ipfs_v2_listen_handler(s, listen_param->local_node);

			/*
			count++;
			connection_param = malloc (sizeof (struct null_connection_params));
			if (connection_param) {
				connection_param->file_descriptor = s;
				connection_param->count = &count;
				connection_param->local_node = listen_param->local_node;
				connection_param->port = listen_param->port;
				connection_param->ip = malloc(INET_ADDRSTRLEN);
				if (connection_param->ip == NULL) {
					// we are out of memory
					free(connection_param);
					continue;
				}
				if (inet_ntop(AF_INET, &(listen_param->ipv4), connection_param->ip, INET_ADDRSTRLEN) == NULL) {
					free(connection_param->ip);
					connection_param->ip = NULL;
					connection_param->port = 0;
				}
				// Create pthread for ipfs_null_connection.
				thpool_add_work(thpool, ipfs_null_connection, connection_param);
			}
			*/
    	} else {
    		// timeout... do maintenance
    		//struct PeerEntry* entry = current_peer_entry->item;
    		// JMJ Debugging
    		//ipfs_null_do_maintenance(listen_param->local_node, entry->peer);
    		if (current_peer_entry != NULL)
    			current_peer_entry = current_peer_entry->next;
    		if (current_peer_entry == NULL)
    			current_peer_entry = listen_param->local_node->peerstore->head_entry;
    	}
    }

    //thpool_destroy(thpool);

    close(socketfd);

    return (void*) 2;
}

int ipfs_null_shutdown() {
	null_shutting_down = 1;
	return 1;
}
