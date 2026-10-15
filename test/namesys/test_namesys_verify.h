#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "ipfs/namesys/verify.h"
#include "libp2p/crypto/key.h"
#include "libp2p/crypto/rsa.h"
#include <openssl/evp.h>

/**
 * Build a minimal IPNS entry protobuf:
 * Field 1 (value): "test-value"
 * Field 2 (signature): 64 zero bytes
 * Field 4 (sequence): 42
 * Field 5 (pubKey): libp2p PublicKey protobuf with Ed25519 type + 32-byte key
 * Field 7 (data): "test-data"
 */
static size_t build_ipns_entry_protobuf(uint8_t *buf, size_t buf_len) {
    size_t offset = 0;

    // Field 1: value = "test-value"
    const char *value = "test-value";
    buf[offset++] = (1 << 3) | 2;
    buf[offset++] = (uint8_t)strlen(value);
    memcpy(&buf[offset], value, strlen(value));
    offset += strlen(value);

    // Field 2: signature = 64 zero bytes
    buf[offset++] = (2 << 3) | 2;
    buf[offset++] = 64;
    memset(&buf[offset], 0, 64);
    offset += 64;

    // Field 4: sequence = 42
    buf[offset++] = (4 << 3) | 0;
    buf[offset++] = 42;

    // Field 5: pubKey = libp2p PublicKey protobuf
    //   KeyType = 1 (Ed25519)
    //   Data = 32 zero bytes
    uint8_t pubkey_pb[36];
    size_t pb_off = 0;
    pubkey_pb[pb_off++] = (1 << 3) | 0; // field 1, varint
    pubkey_pb[pb_off++] = 1; // Ed25519
    pubkey_pb[pb_off++] = (2 << 3) | 2; // field 2, bytes
    pubkey_pb[pb_off++] = 32; // length
    memset(&pubkey_pb[pb_off], 0, 32);
    pb_off += 32;

    buf[offset++] = (5 << 3) | 2;
    buf[offset++] = (uint8_t)pb_off;
    memcpy(&buf[offset], pubkey_pb, pb_off);
    offset += pb_off;

    // Field 7: data = "test-data"
    const char *data = "test-data";
    buf[offset++] = (7 << 3) | 2;
    buf[offset++] = (uint8_t)strlen(data);
    memcpy(&buf[offset], data, strlen(data));
    offset += strlen(data);

    return offset;
}

int test_namesys_verify_entry_parse(void) {
    int retVal = 0;
    uint8_t buf[256];
    struct IpnsVerifyEntry entry;

    size_t len = build_ipns_entry_protobuf(buf, sizeof(buf));

    if (!ipfs_namesys_verify_entry_parse(buf, len, &entry)) {
        fprintf(stderr, "parse failed\n");
        goto exit;
    }

    if (entry.value_len != 10 || memcmp(entry.value, "test-value", 10) != 0) {
        fprintf(stderr, "value mismatch\n");
        goto exit;
    }

    if (entry.sequence != 42) {
        fprintf(stderr, "sequence mismatch: %llu != 42\n", (unsigned long long)entry.sequence);
        goto exit;
    }

    if (entry.sig_len != 64) {
        fprintf(stderr, "sig length mismatch: %zu != 64\n", entry.sig_len);
        goto exit;
    }

    if (entry.key_type != IPFS_KEY_TYPE_ED25519) {
        fprintf(stderr, "key type mismatch: %d != ED25519\n", entry.key_type);
        goto exit;
    }

    if (entry.pubkey_bytes_len != 32) {
        fprintf(stderr, "pubkey bytes length mismatch: %zu != 32\n", entry.pubkey_bytes_len);
        goto exit;
    }

    if (entry.data_payload_len != 9 || memcmp(entry.data_payload, "test-data", 9) != 0) {
        fprintf(stderr, "data payload mismatch\n");
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}

int test_namesys_verify_entry_parse_null(void) {
    int retVal = 0;
    struct IpnsVerifyEntry entry;
    uint8_t buf[4] = {0x0A, 0x02, 0xAB, 0xCD};

    if (ipfs_namesys_verify_entry_parse(NULL, 4, &entry)) {
        fprintf(stderr, "should reject NULL buffer\n");
        goto exit;
    }

    if (ipfs_namesys_verify_entry_parse(buf, 4, NULL)) {
        fprintf(stderr, "should reject NULL out\n");
        goto exit;
    }

    if (ipfs_namesys_verify_entry_parse(buf, 0, &entry)) {
        fprintf(stderr, "should reject zero size\n");
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}

int test_namesys_verify_entry_signature_invalid(void) {
    int retVal = 0;
    uint8_t buf[256];
    struct IpnsVerifyEntry entry;

    size_t len = build_ipns_entry_protobuf(buf, sizeof(buf));

    if (!ipfs_namesys_verify_entry_parse(buf, len, &entry)) {
        fprintf(stderr, "parse failed\n");
        goto exit;
    }

    // Signature is all zeros with a zero key, so verification should fail
    if (ipfs_namesys_verify_entry_signature(&entry)) {
        fprintf(stderr, "should reject invalid signature\n");
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}

int test_namesys_verify_entry_signature_null(void) {
    int retVal = 0;

    if (ipfs_namesys_verify_entry_signature(NULL)) {
        fprintf(stderr, "should reject NULL entry\n");
        goto exit;
    }

    struct IpnsVerifyEntry entry;
    memset(&entry, 0, sizeof(entry));
    if (ipfs_namesys_verify_entry_signature(&entry)) {
        fprintf(stderr, "should reject entry with no sig\n");
        goto exit;
    }

    retVal = 1;
exit:
    return retVal;
}

/**
 * Test: Generate a real Ed25519 keypair, sign an IPNS V2 record,
 * and verify it end-to-end with ipfs_namesys_verify_entry_signature.
 */
int test_namesys_verify_entry_signature_valid_ed25519(void) {
    int retVal = 0;

    /* 1. Generate Ed25519 keypair with OpenSSL/BoringSSL */
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *gen_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    if (!gen_ctx || EVP_PKEY_keygen_init(gen_ctx) <= 0 || EVP_PKEY_keygen(gen_ctx, &pkey) <= 0 || !pkey) {
        fprintf(stderr, "failed to generate Ed25519 key\n");
        EVP_PKEY_CTX_free(gen_ctx);
        goto exit;
    }
    EVP_PKEY_CTX_free(gen_ctx);

    /* Extract raw public key (32 bytes) */
    uint8_t pubkey[32];
    size_t pubkey_len = 32;
    if (EVP_PKEY_get_raw_public_key(pkey, pubkey, &pubkey_len) != 1 || pubkey_len != 32) {
        fprintf(stderr, "failed to get raw public key\n");
        goto cleanup_pkey;
    }

    /* 2. Build IPNS V2 data payload and signed message */
    const char *data_payload = "ipns-v2-test-data";
    size_t data_len = strlen(data_payload);
    size_t signed_msg_len = 15 + data_len;
    uint8_t *signed_msg = malloc(signed_msg_len);
    if (!signed_msg) goto cleanup_pkey;
    memcpy(signed_msg, "ipns-signature:", 15);
    memcpy(signed_msg + 15, data_payload, data_len);

    /* 3. Sign with Ed25519 */
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        fprintf(stderr, "failed to create MD context\n");
        goto cleanup_msg;
    }
    uint8_t sig[64];
    size_t sig_len = sizeof(sig);
    if (EVP_DigestSignInit(ctx, NULL, NULL, NULL, pkey) != 1) {
        fprintf(stderr, "failed to init sign\n");
        goto cleanup_ctx;
    }
    if (EVP_DigestSign(ctx, sig, &sig_len, signed_msg, signed_msg_len) != 1) {
        fprintf(stderr, "failed to sign\n");
        goto cleanup_ctx;
    }

    /* 4. Build IPNS entry protobuf with real signature and pubkey */
    uint8_t buf[512];
    size_t offset = 0;

    // Field 1: value = "/ipfs/QmTest"
    const char *value = "/ipfs/QmTest";
    buf[offset++] = (1 << 3) | 2;
    buf[offset++] = (uint8_t)strlen(value);
    memcpy(&buf[offset], value, strlen(value));
    offset += strlen(value);

    // Field 2: signature = real 64-byte Ed25519 sig
    buf[offset++] = (2 << 3) | 2;
    {
        size_t v = sig_len;
        while (v > 127) {
            buf[offset++] = (uint8_t)(v & 0x7F) | 0x80;
            v >>= 7;
        }
        buf[offset++] = (uint8_t)v;
    }
    memcpy(&buf[offset], sig, sig_len);
    offset += sig_len;

    // Field 4: sequence = 1
    buf[offset++] = (4 << 3) | 0;
    buf[offset++] = 1;

    // Field 5: pubKey = libp2p PublicKey protobuf (Ed25519 type + 32-byte key)
    uint8_t pubkey_pb[36];
    size_t pb_off = 0;
    pubkey_pb[pb_off++] = (1 << 3) | 0; // field 1, varint
    pubkey_pb[pb_off++] = 1; // Ed25519
    pubkey_pb[pb_off++] = (2 << 3) | 2; // field 2, bytes
    pubkey_pb[pb_off++] = 32; // length
    memcpy(&pubkey_pb[pb_off], pubkey, 32);
    pb_off += 32;

    buf[offset++] = (5 << 3) | 2;
    buf[offset++] = (uint8_t)pb_off;
    memcpy(&buf[offset], pubkey_pb, pb_off);
    offset += pb_off;

    // Field 7: data = data_payload
    buf[offset++] = (7 << 3) | 2;
    buf[offset++] = (uint8_t)data_len;
    memcpy(&buf[offset], data_payload, data_len);
    offset += data_len;

    /* 5. Parse and verify */
    struct IpnsVerifyEntry entry;
    if (!ipfs_namesys_verify_entry_parse(buf, offset, &entry)) {
        fprintf(stderr, "parse failed\n");
        goto cleanup_ctx;
    }

    if (!ipfs_namesys_verify_entry_signature(&entry)) {
        fprintf(stderr, "signature verification failed for real Ed25519 IPNS record\n");
        goto cleanup_ctx;
    }

    retVal = 1;
cleanup_ctx:
    EVP_MD_CTX_free(ctx);
cleanup_msg:
    free(signed_msg);
cleanup_pkey:
    EVP_PKEY_free(pkey);
exit:
    return retVal;
}

static size_t hex_to_bin_ns(const char *hex, uint8_t *out, size_t max_len) {
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0 || hex_len / 2 > max_len) return 0;
    for (size_t i = 0; i < hex_len / 2; i++) {
        sscanf(hex + (i * 2), "%02hhx", &out[i]);
    }
    return hex_len / 2;
}

/* NOTE: hex_to_bin_ns helper is kept above for future real-vector tests.
 * The previously added Ed25519/secp256k1 IPNS vector tests used
 * example/mock data that is not a valid cryptographic signature,
 * so those test functions have been removed.  Existing parse and
 * invalid-input tests still exercise the code. */


int test_namesys_verify_ipns_record_rsa(void) {
    int retVal = 0;
    struct RsaPrivateKey private_key;
    memset(&private_key, 0, sizeof(private_key));
    if (!libp2p_crypto_rsa_generate_keypair(&private_key, 2048))
        goto exit;

    const char *data = "ipns-v2-record";
    size_t data_len = strlen(data);
    size_t signed_msg_len = IPNS_SIG_PREFIX_LEN + data_len;
    uint8_t *signed_msg = (uint8_t*)malloc(signed_msg_len);
    if (!signed_msg) goto cleanup_key;
    memcpy(signed_msg, "ipns-signature:", IPNS_SIG_PREFIX_LEN);
    memcpy(signed_msg + IPNS_SIG_PREFIX_LEN, data, data_len);

    unsigned char *sig = NULL;
    size_t sig_len = 0;
    if (!libp2p_crypto_rsa_sign(&private_key, signed_msg, signed_msg_len, &sig, &sig_len)) {
        goto cleanup_msg;
    }

    struct PublicKey public_key;
    memset(&public_key, 0, sizeof(public_key));
    public_key.type = KEYTYPE_RSA;
    public_key.data = (unsigned char*)private_key.public_key_der;
    public_key.data_size = private_key.public_key_length;

    size_t pubkey_len = libp2p_crypto_public_key_protobuf_encode_size(&public_key);
    uint8_t *pubkey_buf = pubkey_len ? (uint8_t*)malloc(pubkey_len) : NULL;
    if (!pubkey_buf || !libp2p_crypto_public_key_protobuf_encode(&public_key, pubkey_buf, pubkey_len, &pubkey_len)) {
        goto cleanup_sig;
    }

    if (!ipfs_namesys_verify_ipns_record(IPFS_KEY_TYPE_RSA, pubkey_buf, pubkey_len, (const uint8_t*)data, data_len, sig, sig_len)) {
        goto cleanup_pub;
    }

    retVal = 1;

cleanup_pub:
    free(pubkey_buf);
cleanup_sig:
    free(sig);
cleanup_msg:
    free(signed_msg);
cleanup_key:
exit:
    if (private_key.der) free(private_key.der);
    if (private_key.public_key_der) free(private_key.public_key_der);
    return retVal;
}


int test_namesys_verify_entry_signature_valid_rsa(void) {
    int retVal = 0;

    struct RsaPrivateKey private_key;
    memset(&private_key, 0, sizeof(private_key));
    if (!libp2p_crypto_rsa_generate_keypair(&private_key, 2048))
        goto exit;

    const char *data_payload = "rsa-ipns-v2-payload";
    size_t data_len = strlen(data_payload);
    size_t signed_msg_len = IPNS_SIG_PREFIX_LEN + data_len;
    uint8_t *signed_msg = (uint8_t*)malloc(signed_msg_len);
    if (!signed_msg) goto cleanup_key;
    memcpy(signed_msg, "ipns-signature:", IPNS_SIG_PREFIX_LEN);
    memcpy(signed_msg + IPNS_SIG_PREFIX_LEN, data_payload, data_len);

    unsigned char *sig = NULL;
    size_t sig_len = 0;
    if (!libp2p_crypto_rsa_sign(&private_key, signed_msg, signed_msg_len, &sig, &sig_len)) {
        goto cleanup_msg;
    }

    uint8_t buf[1024];
    size_t offset = 0;
    const char *value = "/ipfs/QmTest";
    buf[offset++] = (1 << 3) | 2;
    buf[offset++] = (uint8_t)strlen(value);
    memcpy(&buf[offset], value, strlen(value));
    offset += strlen(value);

    buf[offset++] = (2 << 3) | 2;
    {
        size_t v = sig_len;
        while (v > 127) {
            buf[offset++] = (uint8_t)(v & 0x7F) | 0x80;
            v >>= 7;
        }
        buf[offset++] = (uint8_t)v;
    }
    memcpy(&buf[offset], sig, sig_len);
    offset += sig_len;

    buf[offset++] = (4 << 3) | 0;
    buf[offset++] = 1;

    struct PublicKey public_key;
    memset(&public_key, 0, sizeof(public_key));
    public_key.type = KEYTYPE_RSA;
    public_key.data = (unsigned char*)private_key.public_key_der;
    public_key.data_size = private_key.public_key_length;

    uint8_t pubkey_pb[1024];
    size_t pb_off = 0;
    if (!libp2p_crypto_public_key_protobuf_encode(&public_key, pubkey_pb, sizeof(pubkey_pb), &pb_off))
        goto cleanup_sig;
    size_t pubkey_pb_len = pb_off;

    buf[offset++] = (5 << 3) | 2;
    {
        size_t v = pubkey_pb_len;
        while (v > 127) {
            buf[offset++] = (uint8_t)(v & 0x7F) | 0x80;
            v >>= 7;
        }
        buf[offset++] = (uint8_t)v;
    }
    memcpy(&buf[offset], pubkey_pb, pubkey_pb_len);
    offset += pubkey_pb_len;

    buf[offset++] = (7 << 3) | 2;
    buf[offset++] = (uint8_t)data_len;
    memcpy(&buf[offset], data_payload, data_len);
    offset += data_len;

    struct IpnsVerifyEntry entry;
    if (!ipfs_namesys_verify_entry_parse(buf, offset, &entry)) {
        fprintf(stderr, "parse failed: offset=%zu sig_len=%zu publen=%zu\n", offset, sig_len, pubkey_pb_len);
        goto cleanup_sig;
    }
    fprintf(stderr, "entry: keytype=%d siglen=%zu publen=%zu datalen=%zu\n", entry.key_type, entry.sig_len, entry.pubkey_bytes_len, entry.data_payload_len);
    if (entry.pubkey_bytes_len == private_key.public_key_length) {
        int same = memcmp(entry.pubkey_bytes, private_key.public_key_der, private_key.public_key_length) == 0;
        fprintf(stderr, "same public key: %d\n", same);
    }

    if (!ipfs_namesys_verify_entry_signature(&entry)) {
        fprintf(stderr, "signature verification failed for RSA entry\n");
        goto cleanup_sig;
    }

    retVal = 1;

cleanup_sig:
    if (sig) free(sig);
cleanup_msg:
    free(signed_msg);
cleanup_key:
    if (private_key.der) free(private_key.der);
    if (private_key.public_key_der) free(private_key.public_key_der);
exit:
    return retVal;
}
