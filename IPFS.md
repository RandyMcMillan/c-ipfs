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
  - ✅ **Done**: POSIX flock locking with shared (open) / exclusive (init) split; macOS same-process compatibility fixed.
- Support JSON config import with field-level merge overlay.
  - ✅ **Done**: `repo_config_merge_json` overlays Datastore, Addresses, Bootstrap, and Replication fields.

### Exit criteria

- `add`, `cat`, `ls`, and `get` interoperate with Kubo for common UnixFS trees.
- Repo state survives restart and GC scenarios without corruption.
- Metadata and layout choices round-trip correctly.
- `ipfs init` accepts an optional config file and correctly overlays imported values onto defaults.
- ✅ Test suite passes without segfaults (127/127 default-suite tests pass locally).

## Phase 4: libp2p compatibility

### Goals

- Align peer ID and key-format compatibility.
- Support multistream-select and identify behavior expected by Kubo.
- Modernize transport and security negotiation.
- Add QUIC and WebSocket transport support.
- Support yamux and mplex where required.
- Improve connection/resource management.
- Add NAT traversal, relay, AutoNAT, hole punching, and mDNS.

### Completed

- ✅ Transport registry (`transport/registry.c`) with add/remove/dial/free and unit tests.
- ✅ QUIC and WebSocket dial/listen/close stubs implemented in `transport/quic_transport.c` and `transport/ws_transport.c`.
- ✅ Transport registry wired into `ipfs_node_online_new` / `ipfs_node_free` and `core/swarm.c` fallback dial.
- ✅ libwebsockets submodule integrated into Make build; static library linked on macOS and Linux.

### Current blocker

lsquic requires a QUIC-capable TLS library. macOS ships LibreSSL (no QUIC). Ubuntu CI ships OpenSSL 3.0.x (no QUIC). We must add **BoringSSL as a submodule**, build it, and wire lsquic into the Make-based build before QUIC can be enabled at compile time.

### Next steps

1. Add `boringssl` submodule and build `libssl.a` + `libcrypto.a` via CMake.
2. Build `lsquic` against BoringSSL to produce `liblsquic.a`.
3. Update `transport/Makefile`, `main/Makefile`, and `test/Makefile` to link BoringSSL + lsquic when `HAS_LSQUIC=1` is defined.
4. Validate Kubo interoperability test after enabling transports.

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

## Research Notes: Hybrid Protocol Extensions

### Nostr NIP-34 (Git Integration)
NIP-34 defines event kinds for decentralized git workflows over Nostr:
- **kind 1617** — Patches
- **kind 1618** — Pull Requests
- **kind 1619** — Pull Request Updates
- **kind 1621** — Issues
- **kind 1630-1633** — Repository Status
- **kind 30617** — Repository Announcements
- **kind 30618** — Repository State Announcements

These map directly to the c-ipfs `nostr/` module's event kinds and can be wired into the `git.c` importer for verifiable source-code distribution. The secp256k1 Schnorr signatures from `nostril/` are already compatible.

### Iroh (n0-computer) Transport Patterns
Iroh is a modern Rust implementation that simplifies IPFS-like networking:
- **Dial-by-public-key** instead of multiaddr-heavy connection setup.
- **Built on QUIC** (via `noq`) with authenticated encryption and stream priorities.
- **Hole-punching and relay fallback** are first-class, not bolted-on.
- **Content-addressed blobs** use BLAKE3 rather than SHA-256.
- **Protocols**: `iroh-blobs`, `iroh-gossip` (pubsub), `iroh-docs` (KV store).

Lessons for c-ipfs:
1. The transport abstraction in `core/net.c` should move toward "dial by NodeId" rather than manual multiaddr parsing.
2. QUIC should be the default transport; TCP+yamux is legacy.
3. Bitswap wantlist management can borrow Iroh's session-per-peer pattern for cleaner concurrency.

## Working rule for completion

Do not mark a capability as complete until a Kubo interoperability test exists for it. Source support alone is not enough for conformance.
