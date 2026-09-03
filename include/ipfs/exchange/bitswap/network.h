/***
 * This implements the BitswapNetwork. Members of this network can fill requests and
 * smartly handle queues of local and remote requests.
 */

#include "libp2p/conn/session.h"
#include "libp2p/peer/peer.h"
#include "ipfs/exchange/bitswap/bitswap.h"
#include "ipfs/exchange/bitswap/message.h"

struct BitswapRouting {
	/**
	 * Find the provider of a key asyncronously
	 * @param context the session context
	 * @param hash the key we're looking for
	 * @param forWhat I have yet to research this
	 * @param responseMethod a function pointer to call when results are found
	 * @returns true(1) on success, otherwise false(0)
	 */
	int (*FindProviderAsync)(struct SessionContext* context, unsigned char* hash, int forWhat, void (*responseMethod)(void*));

	/**
	 * Provides the key to the network. Is this an announcement or a fill?
	 * I think it is an announcement
	 * @param context the session context
	 * @param hash the hash to announce
	 * @returns true(1) on success, false(0) on error
	 */
	int (*Provide)(struct SessionContext* context, unsigned char* hash);
};

struct BitswapNetwork {
	/***
	 * Send a message to a particular peer
	 * @param context the context
	 * @param peerId the peer ID of who to send to
	 * @param message the message to send
	 * @returns true(1) on success, false(0) otherwise
	 */
	int (*SendMessage)(struct SessionContext* context, unsigned char* peerId, struct BitswapMessage* message);

	/**
	 * The BitswapReceiver is who receives messages from the network
	 * @param receiver the struct that contains function pointers for receiving messages
	 * @returns true(1) on success, otherwise false(0)
	 */
	// SetDelegate is reserved for future async receiver delegation
	//int (*SetDelegate)(struct BitswapReceiver* receiver);

	/**
	 * Attempt a connection to a particular peer
	 * @param context the session context
	 * @param peerId the id of the peer
	 * @returns true(1) on success, otherwise false(0)
	 */
	int (*ConnectTo)(struct SessionContext* context, unsigned char* peerId);

	/**
	 * A pointer to the method that creates a new BitswapMessageSender
	 * @param context the session context
	 * @param peerId the peer id of whom we should send the message to.
	 * @reutrns a pointer to the allocated struct that contains the initialized BitswapMessageSender or NULL if there was a problem
	 */
	struct BitswapMessageSender* (*NewMessageSender)(struct SessionContext* context, unsigned char* peerId);
};

/****
 * send a message to a particular peer
 * @param context the BitswapContext
 * @param peer the peer that is the recipient
 * @param message the message to send
 */
int ipfs_bitswap_network_send_message(const struct BitswapContext* context, struct Libp2pPeer* peer, const struct BitswapMessage* message);

/***
 * Handle a raw incoming bitswap message from the network
 * @param node us
 * @param sessionContext the connection context
 * @param bytes the message
 * @param bytes_size the size of the message
 * @returns true(1) on success, false(0) otherwise.
 */
int ipfs_bitswap_network_handle_message(const struct IpfsNode* node, const struct SessionContext* sessionContext, const uint8_t* bytes, size_t bytes_length);
