# Resume Plan: c-ipfs → Kubo v0.43.0 Interoperability

**Last updated:** 2026-10-19
**Current baseline:** master @ 45da93f (c-ipfs), c-libp2p @ 25adf5e.

## Current state — what just landed

- **Outbound `swarm connect` to Kubo works.** `ipfs swarm connect /ip4/127.0.0.1/tcp/<port>/p2p/<kubo-peer>` exits 0 against Kubo v0.43.1.
- **Noise XX handshake** completes in both directions (outbound dialer and inbound listener).
- **Yamux** now opens streams correctly with `WINDOW_UPDATE+SYN` and stores inbound streams in session slots.
- **Bridge streams** are classified as `STREAM_TYPE_BRIDGE` so the swarm thread does not misread their context.
- **Debug prints** are kept and gated by `DEBUG=1`.
- **CI** runs `test_kubo_noise_handshake.sh` and the new `test_kubo_swarm_connect.sh` in `.github/workflows/kubo-interop.yml`.

## Known rough edges

- After `swarm connect` succeeds, the daemon log still shows non-fatal Yamux read/write errors. These look like thread-safety/read races on the Noise bridge and do not affect the exit code, but they should be cleaned up next.
- Identify v2 is encoded/decoded but the legacy daemon path still uses v1 identify over Yamux. Kubo receives enough info for the connection; full peerstore population is not complete.

## Phase A — Finish P0 interop blockers

### A1. Complete Noise XX daemon path ✅ DONE
- **Files:** `core/null.c` (inbound listener), `core/daemon.c`, `core/bootstrap_impl.c`, `transport/noise_v2_bridge.c`, `c-libp2p/conn/dialer.c`, `c-libp2p/yamux/yamux.c`, `c-libp2p/yamux/session.c`
- **Status:** Accepted connections and outbound dials both run multistream → Noise → Yamux → Identify.
- **Verify:** `./test/scripts/test_kubo_swarm_connect.sh` passes.

### A2. Wire Identify v2 into daemon — PARTIAL
- **Files:** `c-libp2p/v2/src/identify/identify_v2.c`, `c-libp2p/v2/src/peer/peerstore.c`, `core/null.c`
- **Action:** Populate v2 Peerstore with public key + listen addrs from Identity/RepoConfig; call `libp2p_identify_send_response` on inbound Yamux streams.
- **Verify:** New test `test_identify_v2_roundtrip`; interop peer info exchange.

### A3. Verify bridge-stream fix end-to-end ✅ DONE
- **Files:** `c-libp2p/net/stream.c`, `transport/stream_bridge.c`
- **Status:** `STREAM_TYPE_BRIDGE` is recognized; multistream acks over Noise are processed correctly.
- **Verify:** `./test/scripts/test_kubo_noise_handshake.sh` passes.

### A4. Clean up post-connect Yamux noise
- **Files:** `transport/noise_v2_bridge.c`, `transport/stream_bridge.c`, `c-libp2p/yamux/yamux.c`
- **Action:** Investigate and quiet the non-fatal Yamux read/write errors that appear after `join_swarm returning success`. Likely thread-safety around the v2 Noise stream when the swarm thread reads while the dialer thread writes.
- **Verify:** Run `./test/scripts/test_kubo_swarm_connect.sh` with `DEBUG=1`; log should be free of repeated `[Error][yamux]`/`[Error][swarm]` messages after the connection.

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
- **Files:** `c-libp2p/v2/src/conn/yamux.c`, `c-libp2p/yamux/yamux.c`
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
