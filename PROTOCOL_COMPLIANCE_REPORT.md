# Protocol Compliance Analysis — c-ipfs vs Kubo v0.43.1

**Date:** 2026-10-17  
**c-ipfs baseline:** `master @ 6c81341` (working tree has uncommitted `core/api.c` gateway fix + `test/Makefile`)  
**Kubo baseline:** `/Users/Shared/github/kubo @ v0.43.1` (`acc53c093`)  
**Target profile:** `ipfs-compatibility-profile.json` (declares Kubo v0.43.0; Kubo has since shipped v0.43.1 with minor fixes)  
**Scope:** libp2p, IPLD/multiformats, UnixFS, repository, routing/bitswap, naming, daemon/HTTP API/CLI/gateway.

This report is a static + evidence-based audit. It does not re-run the full Kubo interop harness; the harness (`test/scripts/test_kubo_interop.sh`) currently stops at the Noise/multistream swarm-connect step, which is the primary P0 blocker.

## 1. Executive summary

| Area | Status | Notes |
|---|---|---|
| Multiformats (CID/multihash/multibase) | Partial | CIDv0/v1, multihash, multibase compile and have unit tests; identity multihash and canonical varint need Kubo interop confirmation. |
| IPLD codecs | Partial | `raw` and `dag-pb` implemented; `dag-cbor` is link-tag-only (no general encoder/decoder). Selectors not implemented. |
| UnixFS v1 | Partial | Files/dirs/symlinks, HAMT, chunking implemented; balanced/trickle layouts and mtime/mode metadata partly handled; no Kubo interop tests. |
| Repository (blockstore/datastore/pins/GC) | Partial | FlatFS + LMDB, sharded blockstore, pins, repo lock, GC; filestore/provider reannouncement unverified. |
| libp2p (TCP/multistream/Noise/Yamux/Identify) | Partial — P0 | Outbound Noise XX works against Kubo; inbound listener still SECIO; v2 Identify scaffolded but not wired into daemon. |
| QUIC / WebSocket transports | Scaffold | Code compiles behind `HAS_LSQUIC`/`HAS_LIBWEBSOCKETS`; not linked into default build; no dial/listen integration. |
| Kademlia DHT | Partial | Message encoding + iterative lookup with alpha=3 and XOR ordering; bucket splitting/splitting and provider re-provide incomplete. |
| Bitswap | Partial | Message encoding (want-have/block/cancel/presence) present; no session/ledger/scheduling layer; multi-node tests disabled. |
| IPNS | Partial | V1 + partial V2 signing/verification; pubsub distribution and recursive resolution incomplete. |
| DNSLink | Partial | Resolver + `_dnslink` fallback + `/ipns/<domain>` gateway path present; cache and robustness need work. |
| HTTP Gateway | Partial (good progress) | Range requests, CORS, directory listings, index.html, trustless `?format=raw`, DNSLink, HEAD/OPTIONS, Kubo extension headers. |
| HTTP API (/api/v0) | Partial | 15 Kubo RPC endpoints implemented; ~25+ Kubo endpoints missing. |
| CLI | Partial | 12 top-level verbs; Kubo exposes 30+. Many operations only via HTTP API or not at all. |
| GossipSub/PubSub | Scaffold | Not functional. |
| Resource management | Not started | No connection/bandwidth limits. |
| Fuzzing/security hardening | Not started | No fuzz targets. |

**Bottom line:** c-ipfs is a credible partial implementation with strong UnixFS/gateway/DHT-basics coverage and a working outbound Noise handshake, but it is **not yet Kubo-interoperable end-to-end**: `swarm connect` to a Kubo peer is blocked by the inbound/security path and the v2 daemon wiring. Protocol-level gaps are concentrated in libp2p v2 stack wiring, Bitswap sessions, Kademlia standard compliance, IPNS v2, and API/CLI surface area.

## 2. Methodology

1. Compared `ipfs-compatibility-profile.json` / `ipfs-compatibility-matrix.json` feature list against source evidence.
2. Walked `c-libp2p/v2/` for protocol implementation status.
3. Diffed Kubo's `rootSubcommands` (`core/commands/root.go`) against c-ipfs CLI verbs (`main/main.c::parse_arguments`) and HTTP dispatch (`core/http_request.c::ipfs_core_http_request_process`).
4. Inspected `transport/{quic,ws}_transport.c`, `ipld/dag_cbor.c`, `namesys/*`, `exchange/bitswap/*`, `routing/*`, `core/api.c`.
5. Reviewed existing `PROTOCOL_GAPS.md`, `PROGRESS_UPDATE.md`, `RESUME_PLAN.md`.

## 3. Feature-by-feature compliance

### 3.1 Multiformats / CID

- **Evidence:** `cid/cid.c`, `multibase/multibase.c`, `c-libp2p/c-multihash`.
- **Status:** Partial.
- **Gaps:** Kubo interop vectors not executed; identity multihash round-trips; canonical unsigned-varint encoding needs verification against Kubo output. No fuzzing.
- **Kubo reference:** `core/commands/cid.go`, `core/commands/multibase.go`.

### 3.2 IPLD

- **Evidence:** `blocks/*`, `merkledag/*`.
- **Status:** Partial.
- **Implemented:** `raw`, `dag-pb` (link ordering, traversal, bounded export).
- **Missing:** full `dag-cbor` codec (only link tag-42 helper in `ipld/dag_cbor.c`); selectors; `dag put`/`dag get` parity for arbitrary CBOR.
- **Kubo reference:** `core/commands/dag.go`, `ipld/*`.

### 3.3 UnixFS v1

- **Evidence:** `unixfs/unixfs.c`, `unixfs/hamt.c`, `importer/*`, `exporter` (via `core/api.c`).
- **Status:** Partial.
- **Implemented:** files, directories, HAMT sharding, chunking, import/export, symlinks (partial).
- **Gaps:** balanced vs trickle layout selection; mode/mtime metadata; large-file performance; Kubo interop on chunk boundaries.

### 3.4 Repository

- **Evidence:** `repo/fsrepo/*`, `flatfs/*`, `blocks/blockstore*.c`, `pin/*`.
- **Status:** Partial.
- **Implemented:** FlatFS, LMDB, repo version 18 (matches Kubo), locking, pins, GC, sharded blockstore.
- **Gaps:** filestore, provider reannouncement, crash-safety stress tests, migration tooling.

### 3.5 libp2p

#### 3.5.1 Transports

| Transport | c-ipfs | Kubo v0.43 | Status |
|---|---|---|---|
| TCP | Yes (legacy + v2) | Yes | Partial |
| QUIC | Scaffold only | Yes (quic-v1) | Not conformant |
| WebSocket | Scaffold only | Yes | Not conformant |
| Relay/AutoNAT/Hole punching/mDNS | No | Yes | Not started |

#### 3.5.2 Security

| Protocol | c-ipfs | Kubo v0.43 | Status |
|---|---|---|---|
| SECIO | Legacy only | Removed | Deprecated (should not be default) |
| Noise XX | Outbound + inbound listener scaffold | Required | Partial — P0 |
| TLS 1.3 | No | Supported | Not implemented |

#### 3.5.3 Multiplexers

| Muxer | c-ipfs | Kubo v0.43 | Status |
|---|---|---|---|
| yamux | Legacy + v2 basic frames | Yes | Partial (no window/ping/goaway in v2) |
| mplex | No | Yes (fallback) | Not implemented |

#### 3.5.4 Multistream-select / Identify

- **Outbound chain:** `TCP → /multistream/1.0.0 → /noise → Noise → /multistream/1.0.0 → /yamux/1.0.0` works (per `PROTOCOL_GAPS.md`).
- **Bridge stream bug:** resolved by classifying bridge streams as `STREAM_TYPE_BRIDGE` (`c-libp2p/net/stream.c`, `transport/stream_bridge.c`).
- **Identify v2:** encoder present (`c-libp2p/v2/src/identify/identify_v2.c`) encoding fields 1–6; not wired into daemon; peerstore extended with `public_key`/`listen_addrs`.
- **P0 remaining:** inbound listener (`core/null.c`) still SECIO; daemon dial path not consistently v2; Identify not wired.

### 3.6 Routing (Kademlia DHT)

- **Evidence:** `routing/k_routing.c`, `routing/online.c`, `c-libp2p/routing/dht*.c`.
- **Status:** Partial.
- **Implemented:** message encoding, FIND_NODE/GET_VALUE/PUT_VALUE, iterative lookup with alpha=3 and XOR candidate ordering, backoff.
- **Gaps:** standard bucket splitting/maintenance, provider record expiration + re-provide, signed provider records validation, bootstrap robustness (dnsaddr partially done).
- **Kubo reference:** `routing/`, `core/commands/dht.go`, `core/commands/routing.go`.

### 3.7 Bitswap

- **Evidence:** `exchange/bitswap/*`.
- **Status:** Partial.
- **Implemented:** message encoding with want-have/want-block/cancel/presence; wantlist queue; future; peer request queue.
- **Gaps:** session deduplication, ledgers, scheduling, verification, retry policy, provider/DHT fallback. Multi-node tests disabled (timing).
- **Kubo reference:** `exchange/bitswap/`, `core/commands/bitswap.go`.

### 3.8 Naming (IPNS / DNSLink)

- **IPNS:** V1 + partial V2 (signing/verification, embedded pubkey, LMDB for large records). Gaps: v2 protobuf `data` field completeness, EOL/validity enforcement, pubsub distribution, recursive resolution, cache policy.
- **DNSLink:** resolver with `_dnslink` fallback and gateway integration; recursive DNSADDR bootstrap partially implemented (`transport/dnsaddr_resolver.c`).
- **Kubo reference:** `namesys/*`, `core/commands/name.go`, `core/commands/resolve.go`, `dnslink/*`.

### 3.9 HTTP Gateway

- **Evidence:** `core/api.c` (gateway path).
- **Status:** Partial but advanced.
- **Implemented:** `/ipfs/<cid>`, `/ipns/<name>` (incl. DNSLink), directory listings, `index.html`, trailing-slash redirects, `?format=raw`, HEAD/OPTIONS, Range (206/416), CORS, Kubo extension headers (ETag, X-Ipfs-Path, X-Ipfs-Roots, X-Content-Type-Options, Cache-Control, Accept-Ranges).
- **Gaps:** content-type negotiation (Accept header), CAR/trustless streaming, IPNS gateway specifics, immutable cache semantics verification, large-file streaming (current code materializes whole content in memory).
- **Kubo reference:** `core/corehttp/gateway*.go`.

### 3.10 HTTP API (/api/v0)

Implemented endpoints (from `core/http_request.c`):
`name/*`, `object/*`, `dht`/`dht/findprovs`, `swarm/*`, `id`, `version`, `block/get`, `block/put`, `cat`, `get`, `add`, `dag/get`, `dag/put`, `ls`, `pin/add`, `pin/ls`, `repo/gc`.

Missing vs Kubo (root subcommands / RPC): `bitswap/*`, `block/stat/rm`, `bootstrap/*`, `cid/*`, `commands`, `config/*`, `dag/resolve`, `dht/get`, `dht/put`, `dht/findpeer`, `dht/query`, `diag/*`, `files/*`, `filestore/*`, `key/*`, `log/*`, `mount`, `name/pubsub/*`, `object/data/links/patch/new/put/stat`, `p2p/*`, `pin/rm/verify`, `provide/*`, `pubsub/*`, `refs/*`, `resolve`, `repo/*` (fsck, stat, version, gc already), `routing/*`, `stats/*`, `shutdown`, `swarm/addrs/*`, `swarm/peers`, `swarm/connect`, `swarm/disconnect` (connect may be CLI only), `update`, `version/deps`, `multibase/*`.

### 3.11 CLI

c-ipfs top-level verbs: `init`, `add`, `object get`, `cat`, `dns`, `daemon`, `ping`, `get`, `name`, `swarm`, `id`, `nostr`.  
Kubo exposes 30+ (see §2 of Kubo `--help`). Large surface gap; many commands have no CLI equivalent (config, repo, pin, block, dag, cid, multibase, bootstrap, dht, routing, bitswap, pubsub, p2p, files, filestore, provide, stats, diag, log, key, mount, refs, resolve, update, version, commands, shutdown).

### 3.12 PubSub / GossipSub

- **Status:** Scaffold (`pubsub/gossipsub.c`, `pubsub/ipns_pubsub.c`) not functional.
- **Kubo reference:** `core/commands/pubsub.go`, `p2p/pubsub/*`.

### 3.13 Resource management

- **Status:** Not started. No connection/bandwidth/protocol limits.

### 3.14 Fuzzing / security hardening

- **Status:** Not started. No fuzz targets; parser limits unverified.

### 3.15 FFI progress callbacks

- **Status:** Partial (`ipfs_ffi_unixfs_add_bytes_with_progress` reported). Chunked progress present.

## 4. Test evidence

- Test binary `test/test_ipfs` registers 204 tests; 45 disabled (multi-node timing / CI crashes).
- Disabled tests include bitswap multi-node retrieval, core API cat/name resolution, DHT interop-ish tests.
- Kubo interop script (`test/scripts/test_kubo_interop.sh`) validates: daemon startup, `swarm connect`, add→cat both directions, DAG-CBOR vectors. Currently blocked at swarm connect due to P0 libp2p gaps.
- Kubo RPC interop test exists: `test/routing/test_kubo_rpc_interop.c`.

## 5. Priority recommendations (next work)

1. **P0 (blockers):** Wire v2 Noise into inbound listener (`core/null.c`); make daemon dial path consistently v2; wire Identify v2 into daemon with populated peerstore. Verify `swarm connect` to Kubo succeeds.
2. **P0:** Confirm bridge-stream fix end-to-end against Kubo (run interop script).
3. **P1:** Kademlia standard compliance (buckets, re-provide); Bitswap sessions/cancel/presence; Yamux window/ping/goaway.
4. **P1:** IPNS v2 completeness; DAG-CBOR full codec; DNSADDR bootstrap tests.
5. **P2:** QUIC/WebSocket real dial/listen; GossipSub; resource manager; fuzz targets; expand HTTP API + CLI surface to Kubo parity.
6. **Cross-cutting:** Enable disabled multi-node tests only after root causes fixed; add Kubo interop tests for each newly claimed capability (per evidence policy).

## 6. Compliance posture vs evidence policy

Per `ipfs-compatibility-matrix.json`, "A capability is conformant only when its matrix entry cites an interoperable test against the target implementation." On that standard, **no feature is currently conformant** — all are `partial`/`planned` with source evidence only. The matrix should not be upgraded to `conformant` until the Kubo interop harness passes for the relevant capability.

## 7. Artifacts reviewed

- `PROTOCOL_GAPS.md`, `PROGRESS_UPDATE.md`, `RESUME_PLAN.md`, `NEXT_PLAN.md`, `TODO.md`
- `ipfs-compatibility-profile.json`, `ipfs-compatibility-matrix.json`
- `test/scripts/test_kubo_interop.sh`, `test/kubo_interop_vectors.json`
- Kubo `core/commands/root.go`, `core/corehttp/commands.go`
- c-ipfs `main/main.c`, `core/http_request.c`, `core/api.c`, `transport/*`, `ipld/dag_cbor.c`, `namesys/*`, `exchange/bitswap/*`, `routing/*`, `c-libp2p/v2/*`
