#ifndef __REPO_CONFIG_IDENTITY_H__
#define __REPO_CONFIG_IDENTITY_H__

#include "libp2p/crypto/rsa.h"
#include "libp2p/peer/peer.h"

struct Identity {
	struct Libp2pPeer* peer; // a Peer object
	struct RsaPrivateKey private_key; // a private key
};

/***
 * initializes a new keypair, and puts it in the Identity struct
 */
int repo_config_identity_init(struct Identity* identity, unsigned long num_bits_for_keypair);

/***
 * Build a RsaPrivateKey struct from a base64 string of the private key
 * @param identity where to put the new struct
 * @param base64 the base 64 encoded private key in DER format
 * @returns true(1) on success
 */
int repo_config_identity_build_private_key(struct Identity* identity, const char* base64);

/***
 * Clears resources held by Identity without freeing the struct itself.
 * Used between retry attempts during key generation.
 * @param identity the identity to clear
 */
void repo_config_identity_clear(struct Identity* identity);

/*** 
 * Frees resources held by Identity
 * @param identity the identity that we're cleaning up
 * @returns true(0)
 */
int repo_config_identity_free(struct Identity* identity);

/***
 * Initializes a new identity struct that will need to be identity_free'd
 * @param identity the identity to allocate memory for
 * @returns true(1) on success
 */
int repo_config_identity_new(struct Identity** identity);

#endif /* identity_h */
