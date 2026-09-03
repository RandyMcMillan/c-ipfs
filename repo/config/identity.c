/**
 * an "Identity" 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs/repo/config/identity.h"
#include "libp2p/crypto/rsa.h"
#include "libp2p/crypto/peerutils.h"
#include "libp2p/crypto/encoding/base64.h"
#include "libp2p/crypto/encoding/x509.h"
#include "libp2p/crypto/key.h"

/**
 * Builds the Peer ID using the private key, and places it in the identity->peer_id
 * @param identity Where to get the DER of the private key
 * @returns true(1) on success
 */
int repo_config_identity_build_peer_id(struct Identity* identity) {

	struct PublicKey public_key;
	public_key.data = (unsigned char*)identity->private_key.public_key_der;
	public_key.data_size = identity->private_key.public_key_length;
	public_key.type = KEYTYPE_RSA;
	if (identity->peer == NULL)
		identity->peer = libp2p_peer_new();
	if (identity->peer->id != NULL) {
		free(identity->peer->id);
		identity->peer->id = NULL;
	}
	if (!libp2p_crypto_public_key_to_peer_id(&public_key, &identity->peer->id))
		return 0;
	identity->peer->id_size = strlen(identity->peer->id);
	return 1;
}

/***
 * public methods
 */

/***
 * Initializes a new Identity. NOTE: This builds a new private/public key pair
 * @param identity the identity to fill
 * @param num_bits_for_keypair the number of bits for the keypair
 * @returns true(1) on success, false(0) otherwise
 */
int repo_config_identity_init(struct Identity* identity, unsigned long num_bits_for_keypair) {
	if (num_bits_for_keypair < 1024)
		return 0;
	// generate the private key (& public)
	if (!libp2p_crypto_rsa_generate_keypair( &(identity->private_key), num_bits_for_keypair))
		return 0;

	if (!repo_config_identity_build_peer_id(identity)) {
		libp2p_crypto_rsa_rsa_private_key_free(&(identity->private_key));
		return 0;
	}

	return 1;
}

int repo_config_identity_new(struct Identity** identity) {
	*identity = (struct Identity*)malloc(sizeof(struct Identity));
	if (*identity == NULL)
		return 0;

	memset(*identity, 0, sizeof(struct Identity));

	(*identity)->peer = NULL;
	(*identity)->private_key.public_key_der = NULL;
	(*identity)->private_key.der = NULL;

	return 1;
}

void repo_config_identity_clear(struct Identity* identity) {
	if (identity == NULL)
		return;
	if (identity->private_key.public_key_der != NULL) {
		free(identity->private_key.public_key_der);
		identity->private_key.public_key_der = NULL;
		identity->private_key.public_key_length = 0;
	}
	if (identity->private_key.der != NULL) {
		free(identity->private_key.der);
		identity->private_key.der = NULL;
		identity->private_key.der_length = 0;
	}
	if (identity->peer != NULL) {
		libp2p_peer_free(identity->peer);
		identity->peer = NULL;
	}
}

int repo_config_identity_free(struct Identity* identity) {
	if (identity != NULL) {
		repo_config_identity_clear(identity);
		free(identity);
	}
	return 1;
}

/***
 * Build a RsaPrivateKey struct from a base64 string of the private key
 * @param identity where to put the new struct
 * @param base64 the null terminated base 64 encoded private key in DER format
 * @returns true(1) on success
 */
int repo_config_identity_build_private_key(struct Identity* identity, const char* base64) {
	size_t decoded_size = libp2p_crypto_encoding_base64_decode_size(strlen(base64));
	unsigned char decoded[decoded_size];

	int retVal = libp2p_crypto_encoding_base64_decode((unsigned char*)base64, strlen(base64), decoded, decoded_size, &decoded_size);
	if (retVal == 0)
		return 0;

	// now we have a protobuf'd struct PrivateKey. Unprotobuf it
	struct PrivateKey* priv_key;
	if (!libp2p_crypto_private_key_protobuf_decode(decoded, decoded_size, &priv_key))
			return 0;

	// now convert DER to RsaPrivateKey
	retVal = libp2p_crypto_encoding_x509_der_to_private_key(priv_key->data, priv_key->data_size, &identity->private_key);
	libp2p_crypto_private_key_free(priv_key);
	if (retVal == 0)
		return 0;

	// now build the private key DER
	retVal = libp2p_crypto_rsa_private_key_fill_public_key(&identity->private_key);
	if (retVal == 0)
		return 0;

	// now build PeerID
	retVal = repo_config_identity_build_peer_id(identity);

	return retVal;
}
