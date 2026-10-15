# Protocol Gap Report - c-ipfs (P0.1 through P0.4)

Date: 2026-10-15
Scope: Kubo v0.43.0 interoperability blockers P0.1-P0.4

## P0.1 - IPFS HTTP Gateway spec

Status: Partial
Evidence:
- core/api.c serves /ipfs/<cid> and /ipns/<name>.
- Nested DAG path traversal is implemented.
- Content type is sniffed from the requested path.
- 404s are returned for unresolved paths.
- Range requests are now implemented: byte, open-ended, and suffix ranges; 206 Partial Content with Content-Range; 416 for invalid ranges.
- Access-Control-Allow-Origin: * is sent.
- Basic directory-index HTML generation was added for directory nodes.

Remaining:
- Full Kubo trustless gateway behavior.
- Extension headers.
- Directory traversal/web-interface features (P0.93).

## P0.2 - IPFS v2 wire protocol handshake

Status: Partial
Evidence:
- Outbound path runs TCP -> multistream -> /noise -> Noise handshake -> multistream over noise -> Yamux.
- core/null.c accepts connections via ipfs_v2_listen_handler.
- core/v2_listen.c runs multistream -> Noise responder -> Yamux -> inbound Identify exchange.
- Noise/Yamux/Identify pieces are linked into the daemon and binary builds.

Remaining:
- Production validation against a live Kubo peer.
- Yamux window updates, ping, and goaway.
- Daemon-wide v1-to-v2 migration and resource management.

## P0.3 - IPNS V2 records and validation

Status: Partial, substantive verification tests present
Evidence:
- IPNS V2 publisher embeds public keys and signs records.
- Resolver verifies embedded public-key records, signatures, public-key binding, and record data.
- Larger datastore-backed records are supported.
- Focused tests pass:
  - test_namesys_verify_entry_signature_valid_rsa
  - test_namesys_verify_ipns_record_rsa
  - test_ipns_signed_record_roundtrip

Remaining:
- Broader daemon and router integration testing across Kubo.

## P0.4 - IPNS V1 record resolution

Status: Partial, with DHT and offline repo paths
Evidence:
- /ipns/<name> resolution works in the gateway path.
- IPNS publisher and resolver paths are implemented and tested offline.
- DHT integration exists but remains less complete than production Kubo.

Remaining:
- Fully standard-compliant iterative DHT lookup.
- Re-provide and record expiration handling.
- Stronger end-to-end Kubo interop evidence.

## Overall

P0.1 through P0.4 are all partially implemented. The strongest current evidence is in gateway Range requests and IPNS record verification; the v2 handshake still needs live interop validation, and DHT/Bitswap work remains substantial.
