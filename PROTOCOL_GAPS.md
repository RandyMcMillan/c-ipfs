# Major Protocol Gaps — c-ipfs vs Kubo v0.43.0

**Compiled:** 2026-09-04  
**Purpose:** Single-source reference for every remaining gap that blocks full spec compliance and Kubo interoperability. Each gap includes the specific file/area where work is needed, the Kubo behavior we must match, and an estimated complexity.

---

## How to read this file

- **P0** — Blocks Kubo interop today. Fix first.
- **P1** — Required for a production-grade node. Fix second.
- **P2** — Nice-to-have optimizations and extended protocol support.
- **Status:** `not started` | `scaffold` | `partial` | `needs wiring` | `done`

---

## P0 — Kubo Interop Blockers

### 1. Security Protocol: Noise XX (or TLS 1.3)
- **Status:** `partial` — v2 Noise XX compiles cleanly in `c-libp2p/v2/src/conn/noise.c` (ChaCha20-Poly1305 + X25519 DH + HKDF). **Identity payload callbacks are now implemented and match Kubo.** Wired into outbound dialer via `transport/noise_v2_bridge.c`; **inbound listener still uses legacy SECIO.**
- **Root cause:** The legacy c-libp2p stack only speaks SECIO. Kubo v0.43.0 removed SECIO entirely.
- **What Kubo does:** Offers `/noise` as the only security protocol on incoming connections. Expects initiators to perform a Noise_XX_25519_ChaChaPoly_SHA256 handshake with libp2p-specific payloads (protobuf-encoded RSA public key + signature over `noise-libp2p-static-key:` + static X25519 key).
- **What we need:**
  1. ✅ Add libp2p-specific payload extensions — Done in `transport/noise_v2_bridge.c` and `c-libp2p/v2/src/conn/noise_callbacks.c`. RSA identity key protobuf encoding + signature with `noise-libp2p-static-key:` prefix matches Kubo/go-libp2p exactly.
  2. Wire v2 Noise into the **inbound listener** path (`core/null.c` `ipfs_null_listen`) so accepted connections run multistream → Noise → Yamux → Identify instead of raw SECIO.
  3. Replace or augment the legacy `libp2p_conn_dialer_join_swarm` path in the main daemon so it uses the v2 stack consistently.
- **Files:** `c-libp2p/v2/src/conn/noise.c`, `c-libp2p/v2/src/conn/noise_callbacks.c`, `transport/noise_v2_bridge.c`, `core/null.c`, `core/daemon.c`, `core/bootstrap_impl.c`
- **Complexity:** High

### 2. Multistream Select 2.0 / Early Muxer Negotiation
- **Status:** `partial` — v2 multistream handshake works (`c-libp2p/v2/src/conn/multistream.c`), but there is no negotiation of Yamux vs mplex before the security handshake.
- **Root cause:** The v2 stack currently hardcodes Yamux after Noise/SECIO. Kubo expects the security handshake to happen *inside* a multistream-negotiated stream, and then the muxer to be negotiated next.
- **What Kubo does:** `/multistream/1.0.0` → `/noise` → `/yamux/1.0.0` (or `/mplex/6.7.0`).
- **What we need:** Chain the negotiations correctly in `swarm_connect`: TCP → multistream header → security protocol → multistream again → muxer protocol.
- **Files:** `c-libp2p/v2/src/swarm/swarm.c`, `c-libp2p/v2/src/conn/multistream.c`
- **Complexity:** Medium

### 3. Identify Protocol (/ipfs/id/1.0.0)
- **Status:** `partial` — `libp2p_identify_send_response` now encodes fields 1-6 (publicKey, listenAddrs, protocols, observedAddr, protocolVersion, agentVersion) using c-protobuf. `Peerstore` struct extended with `public_key` and `listen_addrs`. **Not wired into the live daemon yet; only used by v2 test swarm.**
- **Root cause:** v2 identify encoder exists but daemon listener still uses v1 identify.
- **What Kubo does:** After Yamux is established, opens a new stream and sends an Identify protobuf message containing: listen addrs, protocols, observed addr, agent version, public key.
- **What we need:**
  1. ✅ Implement Identify protobuf encode/decode — Done in `c-libp2p/v2/src/identify/identify_v2.c`.
  2. Populate local node's public key and listen addrs into the v2 `Peerstore` before handshake.
  3. Parse the remote identify response and populate the peerstore (receive side is scaffolded).
  4. Wire v2 Identify into the live daemon path (currently v1 identify is used via `libp2p_identify_build_protocol_handler`).
- **Files:** `c-libp2p/v2/src/identify/identify_v2.c`, `c-libp2p/v2/include/libp2p/peer/peerstore.h`, `c-libp2p/v2/src/peer/peerstore.c`, `core/null.c`
- **Complexity:** Medium

### 4. Repo Version Migration (12 → 18)
- **Status:** `done` — `IPFS_REPO_VERSION` is already 18 in `include/ipfs/repo/fsrepo/fs_repo.h`, matching Kubo v0.43.0. `repo/fsrepo/fs_repo_version.c` implements best-effort migration (writes v18 if missing, upgrades older versions). `fs_repo.c` accepts versions 12-18 with a compatibility warning.
- **Root cause:** Previously thought to be 12; actually already at 18.
- **What Kubo does:** Refuses to open repos with version < 18. Has migration tools (`ipfs repo fsck`, `ipfs daemon --migrate`).
- **What we need:** ✅ Already compatible. No further work required.
- **Files:** `include/ipfs/repo/fsrepo/fs_repo.h`, `repo/fsrepo/fs_repo_version.c`, `repo/fsrepo/fs_repo.c`
- **Complexity:** N/A

---

## P1 — Production-Grade Node Requirements

### 5. Yamux Window Updates & Ping/GoAway
- **Status:** `partial` — v2 Yamux sends/receives data frames but ignores WINDOW_UPDATE, PING, and GO_AWAY.
- **Root cause:** `c-libp2p/v2/src/conn/yamux.c` only implements DATA frames with SYN/FIN flags.
- **What Kubo does:** Sends WINDOW_UPDATE after receiving data to grant more send credit. Expects PING responses. Uses GO_AWAY for graceful shutdown.
- **What we need:** Add frame-type dispatch in `yamux_stream_read` and a background thread/sender for WINDOW_UPDATE.
- **Files:** `c-libp2p/v2/src/conn/yamux.c`
- **Complexity:** Medium

### 6. DNSADDR Bootstrap Resolution
- **Status:** `scaffold` — `repo/config/bootstrap_peers.c` lists dnsaddr entries in comments but only loads the direct IP4 peer.
- **Root cause:** c-multiaddr does not have a `dnsaddr` protocol code.
- **What Kubo does:** Resolves `/dnsaddr/bootstrap.libp2p.io` via DNS TXT records to discover actual peer multiaddrs at runtime.
- **What we need:**
  1. Add `dnsaddr` (code 56) to `c-libp2p/c-multiaddr/protocols.c` and `protoutils.c`.
  2. Implement DNS TXT resolution in `core/bootstrap_dns.c` (user provided a resolver scaffold).
  3. Wire resolved IP4 multiaddrs into the bootstrap loop.
- **Files:** `c-libp2p/c-multiaddr/protocols.c`, `c-libp2p/c-multiaddr/protoutils.c`, `core/bootstrap_impl.c`, `repo/config/bootstrap_peers.c`
- **Complexity:** Low-Medium

### 7. Kademlia DHT Standard Compliance
- **Status:** `partial` — DHT messages exist but iterative lookup logic is not fully spec-compatible.
- **Root cause:** `routing/k_routing.c` and `c-libp2p/routing/dht.c` use a simplified lookup that doesn't always return the K closest peers.
- **What Kubo does:** Strictly follows the libp2p Kademlia spec: alpha=3 parallel queries, K=20 bucket size, proper bucket splitting, XOR distance metric.
- **What we need:**
  1. Verify XOR distance computation matches the spec.
  2. Implement alpha-parallelism for FIND_NODE.
  3. Add provider record expiration and re-provide logic.
- **Files:** `routing/k_routing.c`, `c-libp2p/routing/dht.c`, `c-libp2p/routing/dht_protocol.c`
- **Complexity:** High

### 8. Bitswap 1.2.0 Session & Presence
- **Status:** `partial` — WANT/HAVE messages exist but no session deduplication or cancel logic.
- **Root cause:** `exchange/bitswap/` implements basic message encoding but lacks the Bitswap 1.2.0 want-manager session layer.
- **What Kubo does:** Sessions deduplicate wants across multiple concurrent `ipfs cat/get` calls. Sends CANCEL when wants are satisfied. Uses HAVE/DONT_HAVE for block presence.
- **What we need:** Refactor `want_manager.c` and `wantlist_queue.c` to support sessions, cancellations, and presence broadcast.
- **Files:** `exchange/bitswap/want_manager.c`, `exchange/bitswap/wantlist_queue.c`, `exchange/bitswap/engine.c`
- **Complexity:** High

### 9. DAG-CBOR Codec
- **Status:** `not started`
- **Root cause:** Only DAG-PB and raw codecs exist.
- **What Kubo does:** Stores and traverses DAG-CBOR objects natively (used by IPNS, some IPLD apps).
- **What we need:** Add a CBOR encoder/decoder in `ipld/dag_cbor.c`. Validate against Kubo's `dag put` / `dag get`.
- **Files:** `ipld/dag_cbor.c` (exists but stubbed)
- **Complexity:** Medium

### 10. Signed IPNS Records (v2)
- **Status:** `partial` — `namesys/publisher.c` and `namesys/resolver.c` handle basic IPNS but not the modern v2 record format.
- **Root cause:** The codebase uses an older IPNS protobuf format.
- **What Kubo does:** Publishes and resolves IPNS records with v2 signatures (extra validation fields, EOL/validity).
- **What we need:** Update `namesys/pb.c` and `namesys/verify.c` to handle v2 records.
- **Files:** `namesys/pb.c`, `namesys/verify.c`, `namesys/publisher.c`, `namesys/resolver.c`
- **Complexity:** Medium

---

## P2 — Extended Protocol Support & Optimizations

### 11. QUIC Transport (Real Dial/Listen)
- **Status:** `scaffold` — `transport/quic_transport.c` has stubs. lsquic + BoringSSL compile but are not linked into the main binary.
- **What we need:** Implement `quic_dial` using lsquic's client API. Integrate with the transport registry.
- **Files:** `transport/quic_transport.c`, `transport/registry.c`
- **Complexity:** High

### 12. WebSocket Transport (Real Dial/Listen)
- **Status:** `scaffold` — `transport/ws_transport.c` has stubs. libwebsockets compiles.
- **What we need:** Implement `ws_dial` using libwebsockets client API. Integrate with the transport registry.
- **Files:** `transport/ws_transport.c`, `transport/registry.c`
- **Complexity:** Medium

### 13. GossipSub / PubSub
- **Status:** `scaffold` — `pubsub/gossipsub.c` exists but is not functional.
- **What we need:** Implement the libp2p gossipsub v1.1 protocol for pubsub-based IPNS and mesh networking.
- **Files:** `pubsub/gossipsub.c`, `pubsub/ipns_pubsub.c`
- **Complexity:** High

### 14. HTTP Gateway
- **Status:** `not started`
- **What we need:** Implement a Kubo-compatible HTTP gateway at `/ipfs/<cid>` and `/ipns/<name>` with proper content-type detection, range requests, and CORS.
- **Files:** `core/api.c`, `core/http_request.c` (new gateway handlers)
- **Complexity:** Medium

### 15. Resource Manager / Connection Gating
- **Status:** `not started`
- **What Kubo does:** Limits concurrent connections, inbound/outbound bandwidth, and protocol-specific streams via the go-libp2p resource manager.
- **What we need:** Add connection limits, memory limits, and per-protocol stream caps.
- **Files:** New module `resource/` or additions to `core/daemon.c`
- **Complexity:** Medium

### 16. Fuzzing & Security Hardening
- **Status:** `not started`
- **What we need:** Fuzz targets for CID parsing, multistream negotiation, protobuf decoding, and HTTP API inputs.
- **Files:** New `fuzz/` directory
- **Complexity:** Medium

### 17. FFI Progress Callbacks
- **Status:** `partial` — `importer/progress.c` defines `import_progress_cb` and `importer_report_progress`, but the FFI `unixfs_add_bytes` does not yet expose progress reporting. Chunking for large files (> 256 KB) is also not yet implemented in the FFI path.
- **What we need:** Add `ipfs_ffi_unixfs_add_bytes_with_progress` to the FFI header; chunk large inputs and fire the callback after each chunk.
- **Files:** `ffi/ffi.c`, `include/ipfs/ffi/ffi.h`, `importer/importer.c`
- **Complexity:** Low-Medium

---

## Quick Reference: What Works Right Now

| Feature | Status | Evidence |
|---------|--------|----------|
| C FFI library interface | Done | 6 FFI tests pass; API mirrors Kubo FFI |
| CIDv0/v1, multihash, multibase | Partial | Unit tests pass |
| DAG-PB blocks | Partial | Unit tests pass |
| UnixFS import/export | Partial | Unit tests pass |
| FlatFS / LMDB repo | Partial | Unit tests pass |
| TCP transport + multistream | Partial | Legacy stack works; v2 multistream handshake works |
| SECIO | Partial | Legacy only; deprecated |
| Yamux | Partial | Legacy stack works; v2 basic frames work |
| `ipfs init` | Fixed | Empty-dir bug resolved |
| `ipfs swarm peers` | Done | Returns JSON peer list |
| `ipfs add/cat/get/id` | Partial | Basic smoke tests pass |
| Bitswap message encoding | Partial | No session layer |
| DHT message encoding | Partial | No standard-compliant iterative lookup |
| Noise XX | Partial | Identity payload callbacks implemented and match Kubo; wired into outbound dialer; inbound listener still uses SECIO |

---

## Recommended Next Session Order

1. **Wire v2 Noise into the inbound listener** — replace the SECIO path in `core/null.c` `ipfs_null_listen` so accepted connections run multistream → Noise → Yamux → Identify.
2. **Fix multistream → security → muxer chaining** — ensure Kubo's negotiation order is matched exactly on both dial and listen paths.
3. **Wire Identify v2 into the daemon** — populate v2 `Peerstore` with public key and listen addrs from `Identity`/`RepoConfig`, then use `libp2p_identify_send_response` on inbound Yamux streams.
4. **Run Kubo interop harness again** — verify `swarm connect` succeeds end-to-end with Noise + Identify.
5. **Wire FFI progress callbacks** — expose `ipfs_ffi_unixfs_add_bytes_with_progress` and chunk large files in the FFI path.
