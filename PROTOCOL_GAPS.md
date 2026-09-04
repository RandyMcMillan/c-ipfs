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
- **Status:** `partial` — v2 Noise XX compiles cleanly in `c-libp2p/v2/src/conn/noise.c` (ChaCha20-Poly1305 + X25519 DH + HKDF). **Not wired into the live daemon yet.**
- **Root cause:** The legacy c-libp2p stack only speaks SECIO. Kubo v0.43.0 removed SECIO entirely.
- **What Kubo does:** Offers `/noise` as the only security protocol on incoming connections. Expects initiators to perform a Noise_XX_25519_ChaChaPoly_SHA256 handshake with libp2p-specific payloads (Ed25519 identity key + signature over static X25519 key).
- **What we need:**
  1. Add libp2p-specific payload extensions in `noise.c` (sign static X25519 key with Ed25519 identity key, verify remote signature).
  2. Wire `libp2p_noise_handshake` into the live daemon path (currently only called from v2 `swarm.c`).
  3. Replace or augment the legacy `libp2p_conn_dialer_join_swarm` path in the main daemon so it uses the v2 stack.
- **Files:** `c-libp2p/v2/src/conn/noise.c`, `c-libp2p/v2/src/swarm/swarm.c`, `core/daemon.c`, `core/bootstrap_impl.c`
- **Complexity:** High

### 2. Multistream Select 2.0 / Early Muxer Negotiation
- **Status:** `partial` — v2 multistream handshake works (`c-libp2p/v2/src/conn/multistream.c`), but there is no negotiation of Yamux vs mplex before the security handshake.
- **Root cause:** The v2 stack currently hardcodes Yamux after Noise/SECIO. Kubo expects the security handshake to happen *inside* a multistream-negotiated stream, and then the muxer to be negotiated next.
- **What Kubo does:** `/multistream/1.0.0` → `/noise` → `/yamux/1.0.0` (or `/mplex/6.7.0`).
- **What we need:** Chain the negotiations correctly in `swarm_connect`: TCP → multistream header → security protocol → multistream again → muxer protocol.
- **Files:** `c-libp2p/v2/src/swarm/swarm.c`, `c-libp2p/v2/src/conn/multistream.c`
- **Complexity:** Medium

### 3. Identify Protocol (/ipfs/id/1.0.0)
- **Status:** `scaffold` — `libp2p_identify_send_response` is referenced in v2 but not implemented.
- **Root cause:** No identify encoder/decoder exists in the v2 layer.
- **What Kubo does:** After Yamux is established, opens a new stream and sends an Identify protobuf message containing: listen addrs, protocols, observed addr, agent version, public key.
- **What we need:**
  1. Implement Identify protobuf encode/decode using c-protobuf.
  2. Send the local node's peer ID, multiaddrs, and supported protocols.
  3. Parse the remote identify response and populate the peerstore.
- **Files:** `c-libp2p/v2/src/identify/` (new directory), `c-libp2p/v2/src/swarm/swarm.c`
- **Complexity:** Medium

### 4. Repo Version Migration (12 → 18)
- **Status:** `not started`
- **Root cause:** c-ipfs hardcodes repo version 12. Kubo v0.43.0 uses repo version 18.
- **What Kubo does:** Refuses to open repos with version < 18. Has migration tools (`ipfs repo fsck`, `ipfs daemon --migrate`).
- **What we need:**
  1. Audit what changed between repo version 12 and 18 (datastore spec, config keys, keystore format, etc.).
  2. Bump `repo/version` to 18.
  3. Implement a migration path or at minimum accept version 18 in `fs_repo_is_initialized`.
- **Files:** `repo/fsrepo/fs_repo.c`, `repo/init.c`, `cmd/ipfs/init.c`
- **Complexity:** Medium

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

---

## Quick Reference: What Works Right Now

| Feature | Status | Evidence |
|---------|--------|----------|
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
| Noise XX | Partial | v2 compiles cleanly; identity payload + wiring remaining |

---

## Recommended Next Session Order

1. **Wire v2 Noise into the daemon** — replace the SECIO path in `core/daemon.c` or `c-libp2p/conn/dialer.c` with the v2 stack.
2. **Fix multistream → security → muxer chaining** — ensure Kubo's negotiation order is matched exactly.
3. **Implement Identify send/parse** — populate peerstore after handshake.
4. **Bump repo version to 18** — unblock running c-ipfs against existing Kubo repos.
5. **Run Kubo interop harness again** — verify `swarm connect` succeeds end-to-end.
