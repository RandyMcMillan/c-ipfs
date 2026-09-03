#pragma once

#include <stdlib.h>
#include "../test_helper.h"
#include "multiaddr/multiaddr.h"
#include "libp2p/peer/peerstore.h"
#include "libp2p/peer/peer.h"
#include "libp2p/record/message.h"
#include "ipfs/core/daemon.h"
#include "ipfs/core/ipfs_node.h"
#include "ipfs/repo/fsrepo/fs_repo.h"
#include "libp2p/conn/dialer.h"
#include "ipfs/core/daemon.h"

/**
 * Testing connectivity with other nodes
 */

int test_ping() {
	int retVal = 0;
	struct FSRepo* fs_repo = NULL;
    struct KademliaMessage* message = NULL;
    //struct IpfsNode local_node;
    struct Libp2pPeer* remote_peer = NULL;
    struct Dialer* dialer = NULL;
    struct Stream* conn = NULL;
    unsigned char* protobuf = NULL;
    size_t protobuf_size = 0;

    // act like this is a normal node
	drop_build_and_open_repo("./tmp/.ipfs", &fs_repo);

	// create a new IpfsNode
	/*
    local_node.mode = MODE_ONLINE;
    local_node.peerstore = libp2p_peerstore_new();
    local_node.repo = fs_repo;
    local_node.identity = fs_repo->config->identity;
	*/

    // build the ping message
    message = libp2p_message_new();
    message->message_type = MESSAGE_TYPE_PING;
    protobuf_size = libp2p_message_protobuf_encode_size(message);
    protobuf = (unsigned char*)malloc(protobuf_size);
    libp2p_message_protobuf_encode(message, protobuf, protobuf_size, &protobuf_size);
    libp2p_message_free(message);
    message = NULL;
    // ping a known node with a ping record
    // create the connection
    remote_peer = libp2p_peer_new();
    remote_peer->id = "QmdMZoaL4azVzEPHYVH6imn3iPYYv6L1fLcH7Vd1aLbfSD";
    remote_peer->id_size = strlen(remote_peer->id);
    remote_peer->addr_head = libp2p_utils_linked_list_new();
    remote_peer->addr_head->item = multiaddress_new_from_string("/ip4/192.168.43.234/tcp/4001/");

    // connect using a dialer
    dialer = libp2p_conn_dialer_new(fs_repo->config->identity->peer, NULL, NULL, NULL);
    conn = libp2p_conn_dialer_get_connection(dialer, remote_peer->addr_head->item);

    //TODO: Dialer should know the protocol

    // send the record
    struct StreamMessage msg;
    msg.data = (uint8_t*)protobuf;
    msg.data_size = protobuf_size;
    conn->write(conn->stream_context, &msg);
    struct StreamMessage* incoming_message = NULL;
    conn->read(conn->stream_context, &incoming_message, 10);
    libp2p_message_protobuf_decode(incoming_message->data, incoming_message->data_size, &message);
	// verify the response
    if (message->message_type != MESSAGE_TYPE_PING)
    	goto exit;

	// clean up
    retVal = 1;
    exit:
	ipfs_repo_fsrepo_free(fs_repo);
	return retVal;
}

int test_ping_remote() {
	char* argv[] = { "ipfs", "ping", "QmTjg669YQemhffXLrkA3as9jT8SzyRtWaLXHKwYN6wCBd" };
	int argc = 3;

	return ipfs_ping(argc, argv);
}
