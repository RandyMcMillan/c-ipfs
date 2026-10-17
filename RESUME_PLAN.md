# Resume Plan: c-ipfs → Kubo v0.43.0 Interoperability

**Baseline:** master @ 1535690 (DHT backoff helper + test). All submodule pointers current.

## Phase A — Finish P0 interop blockers

### A1. Complete Noise XX daemon path
- **Files:** `core/null.c` (inbound listener), `core/daemon.c`, `core/bootstrap_impl.c`, `transport/noise_v2_bridge.c`
- **Action:** Replace SECIO on accepted connections with multistream → Noise → Yamux → Identify; use v2 stack consistently in daemon dial path.
- **Verify:** `test_kubo_interop.sh` reaches API on :5011; `swarm connect` to a Kubo peer succeeds.

### A2. Wire Identify v2 into daemon
- **Files:** `c-libp2p/v2/src/identify/identify_v2.c`, `c-libp2p/v2/src/peer/peerstore.c`, `core/null.c`
- **Action:** Populate v2 Peerstore with public key + listen addrs from Identity/RepoConfig; call `libp2p_identify_send_response` on inbound Yamux streams.
- **Verify:** New test `test_identify_v2_roundtrip`; interop peer info exchange.

### A3. Verify bridge-stream fix end-to-end
- **Files:** `c-libp2p/net/stream.c`, `transport/stream_bridge.c`
- **Action:** Rebuild, run daemon, confirm `swarm connect` over Noise completes (no timeout).
- **Verify:** Kubo interop smoke.

## Phase B — P1 production-grade

### B1. Kademlia standard compliance
- **Files:** `routing/k_routing.c`, `routing/online.c`, `c-libp2p/routing/dht.c`, `c-libp2p/routing/dht_protocol.c`
- **Action:** alpha=3 parallelism, K=20 buckets, XOR ordering, provider re-provide/expiration.
- **Tests:** `test_routing_iterative_find_peer`.

### B2. Bitswap 1.2.0 sessions/presence
- **Files:** `exchange/bitswap/want_manager.c`, `wantlist_queue.c`, `engine.c`, `message.c`, `network.c`
- **Action:** Session dedup, CANCEL, HAVE/DONT_HAVE presence.
- **Tests:** `test_bitswap_message_120`.

### B3. Yamux window/ping/goaway
- **Files:** `c-libp2p/v2/src/conn/yamux.c`
- **Action:** Frame-type dispatch, WINDOW_UPDATE, PING, GO_AWAY.

### B4. DNSADDR bootstrap tests
- **Files:** `transport/dnsaddr_resolver.c`, `repo/config/bootstrap_peers.c`
- **Action:** End-to-end targeted tests for recursive resolution.

### B5. Signed IPNS records v2
- **Files:** `namesys/pb.c`, `namesys/publisher.c`, `namesys/resolver.c`, `namesys/verify.c`
- **Action:** v2 protobuf data field, signing/verification, EOL/validity.
- **Tests:** `test_ipns_sign_verify`, `test_ipns_record_select`.

### B6. DAG-CBOR codec
- **Files:** `ipld/dag_cbor.c`
- **Action:** Encoder/decoder; validate via `dag put`/`dag get`.

## Phase C — P2 extended

- QUIC transport (`transport/quic_transport.c`, `registry.c`)
- WebSocket transport (`transport/ws_transport.c`)
- GossipSub (`pubsub/gossipsub.c`)
- HTTP Gateway (range, content-type, DNSLink)
- Resource manager / connection gating
- Fuzz targets (`fuzz/`)
- FFI progress callbacks

## Working rules
1. One priority per commit series; run `make all` + focused `test_ipfs` before commit.
2. Commit submodule changes first (push), then update pointer in c-ipfs.
3. Do not mark a capability complete without a Kubo interop test.
4. Keep disabled CI-crashing tests disabled until root cause fixed.