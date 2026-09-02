#ifndef __IPFS_IPLD_DAG_CBOR_H__
#define __IPFS_IPLD_DAG_CBOR_H__

#include <stdint.h>
#include <stddef.h>

#define DAG_CBOR_LINK_TAG 42
#define MULTIBASE_BINARY_PREFIX 0x00
#define DAG_CBOR_LINK_MAX_BYTES 128

/**
 * Encoded DAG-CBOR Tag 42 Link.
 */
struct DagCborLink {
    uint8_t bytes[DAG_CBOR_LINK_MAX_BYTES];
    size_t len;
};

/**
 * Encodes a raw binary CID / Multihash into a DAG-CBOR Tag 42 Byte String.
 *
 * DAG-CBOR Link structure: Tag(42) -> ByteString(0x00 + binary_cid)
 *
 * @param cid_bytes the raw CID bytes (e.g., a Multihash or full CID)
 * @param cid_len the length of cid_bytes (must be > 0 and <= 100)
 * @param out the output structure to hold the encoded link
 * @returns 1 on success, 0 on failure
 */
int ipfs_ipld_dag_cbor_link_encode(const uint8_t *cid_bytes, size_t cid_len, struct DagCborLink *out);

/**
 * Decodes a DAG-CBOR Tag 42 Byte String back into raw CID bytes.
 *
 * @param link the encoded DAG-CBOR link
 * @param cid_bytes output buffer for the raw CID (caller provides at least 100 bytes)
 * @param cid_len in/out: input = max buffer size, output = actual CID length
 * @returns 1 on success, 0 on failure
 */
int ipfs_ipld_dag_cbor_link_decode(const struct DagCborLink *link, uint8_t *cid_bytes, size_t *cid_len);

/**
 * Verifies that a byte sequence is a valid DAG-CBOR Tag 42 link header.
 *
 * @param bytes the bytes to inspect
 * @param len the number of bytes available
 * @returns 1 if it begins with a valid Tag 42 header, 0 otherwise
 */
int ipfs_ipld_dag_cbor_link_is_tag42(const uint8_t *bytes, size_t len);

#endif
