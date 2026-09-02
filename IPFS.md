# IPFS Implementation Plan

This repository is currently **partial** for the `c-ipfs-kubo-v0.43` compatibility profile. The goal of this plan is to reach full IPFS protocol support with **Kubo v0.43.0 interoperability evidence** for every completed capability.

## Conformance rule

A capability is not considered complete until it has:

1. Implementation in this repository.
2. A test that exercises the behavior.
3. Interoperability evidence against Kubo v0.43.0.

## Phase 1: Multiformats foundation

### Goals

- Canonical unsigned-varint encoding.
- CIDv0 and CIDv1 support.
- Identity multihash support.
- Multibase, multicodec, and content-codec validation.
- Cross-language test vectors.

### Why this comes first

CID, multihash, and multibase correctness are prerequisites for IPLD, UnixFS, Bitswap, and IPNS. If these layers are inconsistent, higher-level protocol work will fail in subtle ways.

### Exit criteria

- Invalid encodings are rejected consistently.
- CID and multihash round-trips match reference vectors.
- Kubo-backed compatibility tests pass for the supported multiformats surface.

## Phase 2: IPLD core

### Goals

- Add `raw` and `dag-cbor` codecs.
- Enforce CID validation on every block read and write.
- Make DAG-PB link ordering deterministic.
- Add bounded streaming traversal.
- Implement selector support.

### Exit criteria

- Blocks can be stored and retrieved safely with CID validation.
- Traversal obeys bounds and selectors.
- DAG-PB output is deterministic across runs.
- Kubo interoperability tests pass for `block`, `dag`, and traversal behavior.

## Phase 3: UnixFS and repository semantics

### Goals

- Complete UnixFS v1 support for files, directories, and symlinks.
- Implement HAMT directories.
- Preserve mode and mtime metadata.
- Support balanced and trickle layouts.
- Add configurable chunking.
- Improve repo semantics for flatfs, transactional metadata, garbage collection, locking, and crash-safe writes.

### Exit criteria

- `add`, `cat`, `ls`, and `get` interoperate with Kubo for common UnixFS trees.
- Repo state survives restart and GC scenarios without corruption.
- Metadata and layout choices round-trip correctly.

## Phase 4: libp2p compatibility

### Goals

- Align peer ID and key-format compatibility.
- Support multistream-select and identify behavior expected by Kubo.
- Modernize transport and security negotiation.
- Add QUIC and WebSocket transport support.
- Support yamux and mplex where required.
- Improve connection/resource management.
- Add NAT traversal, relay, AutoNAT, hole punching, and mDNS.

### Exit criteria

- This implementation can connect to Kubo peers on supported transports.
- Protocol negotiation matches Kubo expectations.
- Connection lifecycle and limits behave predictably under load.

## Phase 5: Routing and Bitswap

### Goals

- Implement standards-compatible Kademlia DHT behavior.
- Add iterative lookup and routing-table maintenance.
- Validate signed provider and routing records.
- Support provider announcements, discovery, and bootstrap.
- Implement modern Bitswap sessions, want-have, want-block, cancels, and block presences.
- Add provider fallback through the DHT.

### Exit criteria

- Provider discovery works across multiple nodes.
- Bitswap can fetch blocks from Kubo peers.
- Record validation rejects malformed or unsigned records.

## Phase 6: IPNS and DNSLink

### Goals

- Support signed IPNS records with sequence and validity rules.
- Add DHT and pubsub distribution.
- Implement recursive resolution and cache policy.
- Support DNSLink and `/ipfs` / `/ipns` path resolution.

### Exit criteria

- `name publish` and `name resolve` interoperate with Kubo.
- DNSLink resolution produces the same results as Kubo for the same records.
- Cache behavior is documented and testable.

## Phase 7: HTTP API, gateway, and CLI

### Goals

- Implement a stable daemon lifecycle.
- Add or complete the HTTP gateway.
- Support the selected `/api/v0` endpoints:
  - `add`
  - `block/get`
  - `block/put`
  - `cat`
  - `dag/get`
  - `dag/put`
  - `dht/findprovs`
  - `dht/provide`
  - `get`
  - `id`
  - `ls`
  - `pin/add`
  - `pin/ls`
  - `repo/gc`
  - `swarm/connect`
  - `version`
- Keep CLI output stable.
- Add authentication and origin controls.
- Keep public IPFS APIs separate from Nostr-specific extensions.

### Exit criteria

- The selected RPCs behave compatibly with Kubo.
- Gateway and API behavior is explicit and documented.
- CLI output is stable enough for automation and scripts.

## Phase 8: Security and verification

### Goals

- Add strict parser and resource bounds.
- Fuzz wire and content decoders.
- Verify concurrency safety in storage paths.
- Review dependencies and licenses.
- Add Linux and macOS interoperability CI.
- Add restart, GC, and multi-node regression tests.

### Exit criteria

- Security-sensitive parsers are bounded and fuzzed.
- CI covers both macOS and Linux interoperability.
- Conformance is enforced by automated tests, not by source inspection alone.

## Suggested delivery order

1. Multiformats.
2. IPLD.
3. UnixFS and repo semantics.
4. libp2p compatibility.
5. Routing and Bitswap.
6. IPNS and DNSLink.
7. HTTP API, gateway, and CLI.
8. Security and verification.

## Working rule for completion

Do not mark a capability as complete until a Kubo interoperability test exists for it. Source support alone is not enough for conformance.
