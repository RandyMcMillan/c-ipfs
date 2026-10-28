# Resume Plan: c-ipfs → Kubo v0.43.0 Interoperability

**Last updated:** 2026-10-28
**Current baseline:** master @ 45da93f (c-ipfs), c-libp2p @ 25adf5e.
**Specification references:**
- `specs/` — official IPFS specifications (IPFS specs at `heads/main`).
- `c-libp2p/specs/` — official libp2p specifications (`libp2p/specs` at `heads/master`), already present as a nested submodule.

## Current state — what just landed

- **Outbound `swarm connect` to Kubo is stable.** `ipfs swarm connect /ip4/127.0.0.1/tcp/<port>/p2p/<kubo-peer>` exits 0 against Kubo v0.43.1; the post-connect Yamux/Noise race is resolved.
- **Noise XX handshake** completes in both directions (outbound dialer and inbound listener).
- **Yamux** now opens streams correctly with `WINDOW_UPDATE+SYN` and stores inbound streams in session slots.
- **Bridge streams** are classified as `STREAM_TYPE_BRIDGE` so the swarm thread does not misread their context.
- **v2 Noise stream serialization:** `pthread_mutex_t rw_mutex` protects frame read/write in `c-libp2p/v2/src/conn/noise.c`.
- **Bridge stream serialization:** `transport/stream_bridge.c` uses a recursive mutex around `bridge_read`, `bridge_read_raw`, and `bridge_write`.
- **Outbound Identify v1** sends the protobuf-encoded RSA public key and `ListenAddrs` from repo config; inbound Identify v1 populates the legacy peerstore with received listen addresses.
- **Identify v2 roundtrip test** `test_identify_v2_roundtrip` passes.
- **Debug prints** are kept and gated by `DEBUG=1`.
- **CI** runs `test_kubo_noise_handshake.sh` and the new `test_kubo_swarm_connect.sh` in `.github/workflows/kubo-interop.yml`.
- **Bitswap 1.2.0 Kubo retrieval** passes end-to-end via `./test/scripts/test_kubo_bitswap_retrieve_debug.sh` (tagged `phase-B2-complete`).

## Completion tracking

Percentages are rough estimates against the relevant spec documents in `specs/` and `c-libp2p/specs/`.

| Phase | Spec reference(s) | Completion |
|-------|-------------------|------------|
| A1 Noise XX daemon path | `c-libp2p/specs/noise/README.md`, `c-libp2p/specs/connections/README.md` | 100% |
| A2 Identify v1/v2 | `c-libp2p/specs/identify/README.md` | 90% (v1 roundtrip stable, v2 wired, signed peer records pending) |
| A3 Bridge stream fix | — | 100% |
| A4 Yamux/Noise post-connect | `c-libp2p/specs/yamux/README.md` | 75% (streams, window update, data flow work; PING/GO_AWAY partial) |
| **B2 Bitswap 1.2.0** | `specs/src/bitswap-protocol.md` | **65%** (single-block Kubo retrieval works; HAVE/DONT_HAVE routing, Cancel, sessions, CIDv1 edge cases remain) |
| B1 Kademlia | `specs/src/routing/kad-dht.md`, `c-libp2p/specs/kad-dht/README.md` | 20% (basic DHT scaffold, not Kubo-tested) |
| B3 Yamux window/ping/goaway | `c-libp2p/specs/yamux/README.md` | 30% |
| B4 DNSADDR bootstrap | — | 10% |
| B5 IPNS records v2 | `specs/src/ipns/ipns-record.md` | 10% |
| B6 DAG-CBOR | `specs/src/ipns/ipns-record.md` (referenced) | 10% |
| C QUIC transport | `c-libp2p/specs/quic/README.md` | 5% |
| C WebSocket transport | `c-libp2p/specs/websockets/README.md` | 5% |
| C GossipSub | `c-libp2p/specs/pubsub/gossipsub/gossipsub-v1.0.md` | 5% |
| C HTTP Gateway | `specs/http-gateways/` | 30% (basic gateway paths, range/DNSLink pending) |

## Known rough edges

- Bitswap `Cancel`, `HAVE`/`DONT_HAVE` routing, multi-peer sessions, and CIDv1 wantlist entries need more work.

## Phase A — Finish P0 interop blockers

### A1. Complete Noise XX daemon path ✅ DONE
- **Files:** `core/null.c` (inbound listener), `core/daemon.c`, `core/bootstrap_impl.c`, `transport/noise_v2_bridge.c`, `c-libp2p/conn/dialer.c`, `c-libp2p/yamux/yamux.c`, `c-libp2p/yamux/session.c`
- **Status:** Accepted connections and outbound dials both run multistream → Noise → Yamux → Identify.
- **Verify:** `./test/scripts/test_kubo_swarm_connect.sh` passes.

### A2. Wire Identify v2 into daemon ✅ DONE
- **Files:** `c-libp2p/v2/src/identify/identify_v2.c`, `c-libp2p/v2/src/identify/identify_v2_ipfs.c`, `c-libp2p/v2/src/peer/peerstore.c`, `core/ipfs_node.c`, `c-libp2p/conn/dialer.c`, `test/identify/test_identify_v2.c`
- **Status:**
  - Legacy v1 Identify handler is built with protobuf-encoded RSA public key and repo config swarm addresses.
  - Outbound dialer sends the full Identify protobuf and updates the peerstore with remote `ListenAddrs`.
  - v2 `libp2p_identify_receive()` derives the peer ID from the received public key and adds listen addrs to the v2 `Libp2pPeer`.
  - New roundtrip test passes.
- **Verify:** `cd test && ./test_ipfs test_identify_v2_roundtrip`; `./test/scripts/test_kubo_swarm_connect.sh`.

### A3. Verify bridge-stream fix end-to-end ✅ DONE
- **Files:** `c-libp2p/net/stream.c`, `transport/stream_bridge.c`
- **Status:** `STREAM_TYPE_BRIDGE` is recognized; multistream acks over Noise are processed correctly.
- **Verify:** `./test/scripts/test_kubo_noise_handshake.sh` passes.

### A4. Clean up post-connect Yamux noise — DONE
- **Spec:** `c-libp2p/specs/connections/README.md`, `c-libp2p/specs/yamux/README.md`, `c-libp2p/specs/identify/README.md`, `c-libp2p/specs/noise/README.md`.
- **Files:** `transport/noise_v2_bridge.c`, `transport/stream_bridge.c`, `c-libp2p/yamux/yamux.c`, `c-libp2p/yamux/session.c`, `c-libp2p/conn/dialer.c`, `c-libp2p/net/multistream.c`
- **Done:**
  - Added v2 Noise stream `rw_mutex`.
  - Made `transport/stream_bridge.c` mutex recursive and locked read/write paths.
  - Made the outbound dialer drive the multistream-over-yamux handshake and Identify exchange synchronously, then hand the peer to the swarm only after `Identify` completes.
  - Fixed `yamux_get_session_stream` to key off the canonical yamux stream id so inbound stream upgrades (multistream -> identify) resolve the correct session slot after the child `Stream` is replaced.
  - Sent Identify v1 protocol id and protobuf as separate multistream-select messages, matching Kubo's expectation.
  - Inbound yamux streams now reply with the multistream ack (`theyRequested=1`) instead of re-initiating.
- **Verify:** `./test/scripts/test_kubo_swarm_connect.sh` passes (stable across 3 consecutive runs).

## Phase B — P1 production-grade

**Current priority: B2 Bitswap 1.2.0 end-to-end Kubo interop.** The remaining B1/B3-B6 items are listed in dependency order after B2.

Use the official specs in both `specs/` (IPFS-layer) and `c-libp2p/specs/` (libp2p-layer) to drive each item. Key references:
- IPFS: `specs/src/routing/kad-dht.md`, `specs/src/bitswap-protocol.md`, `specs/src/ipns/ipns-record.md`, `specs/UNIXFS.md`, `specs/MERKLE_DAG.md`, `specs/http-gateways/`
- libp2p: `c-libp2p/specs/kad-dht/README.md`, `c-libp2p/specs/yamux/README.md`, `c-libp2p/specs/identify/README.md`, `c-libp2p/specs/connections/README.md`, `c-libp2p/specs/pubsub/gossipsub/gossipsub-v1.0.md`

### B1. Kademlia standard compliance
- **Spec:** `specs/src/routing/kad-dht.md` and `c-libp2p/specs/kad-dht/README.md` (Routing Table, Lookup Process, Peer Routing, Provider Record Routing, RPC Messages).
- **Files:** `routing/k_routing.c`, `routing/online.c`, `c-libp2p/routing/dht.c`, `c-libp2p/routing/dht_protocol.c`
- **Action:**
  - Bucket size `k = 20`.
  - Iterative lookup with concurrency `α = 10` and resilience `β = 3`.
  - XOR distance ordering.
  - `FIND_NODE`, `FIND_VALUE`, `ADD_PROVIDER`, `GET_PROVIDERS` RPC handling.
  - Provider record re-provide/expiration (records expire after ~48h; DHT servers should re-provide before expiration).
  - Routing table refresh every ~10 minutes; ping unresponsive nodes and remove them.
- **Tests:** `test_routing_iterative_find_peer` against a local Kubo peer.

### B2. Bitswap 1.2.0 sessions/presence — IN PROGRESS
- **Spec:** `specs/src/bitswap-protocol.md` (Bitswap 1.2.0).
- **Files:** `exchange/bitswap/want_manager.c`, `wantlist_queue.c`, `engine.c`, `message.c`, `network.c`
- **Action:**
  - Session deduplication across peers.
  - `Cancel` messages when a block is no longer wanted.
  - `HAVE`/`DONT_HAVE` presence responses (Bitswap 1.2.0).
  - Support CIDv1 in wantlist entries.
  - 2 MiB per-block limit.
- **Current state:**
  - `test/scripts/test_kubo_bitswap_retrieve.sh` is in place; content retrieval now routes through `exchange->GetBlock` and the wantlist queue broadcasts to connected peers before falling back to DHT provider lookup.
  - The retrieval test currently hangs after `swarm connect`; next steps are protocol version negotiation (`/ipfs/bitswap/1.1.0` vs `1.2.0`), `HAVE`/`DONT_HAVE` handling, and CID encoding in the wantlist.
- **Tests:** `test/scripts/test_kubo_bitswap_retrieve.sh` against Kubo.

### B3. Yamux window/ping/goaway
- **Spec:** `c-libp2p/specs/yamux/README.md`.
- **Files:** `c-libp2p/v2/src/conn/yamux.c`, `c-libp2p/yamux/yamux.c`
- **Action:**
  - Correct frame-type dispatch for `WINDOW_UPDATE`, `PING`, `GO_AWAY`.
  - Send and respond to `PING` frames.
  - Handle `GO_AWAY` gracefully.
  - Proper window management for flow control.
- **Verify:** Kubo interop stress test with multiple concurrent streams.

### B4. DNSADDR bootstrap tests
- **Files:** `transport/dnsaddr_resolver.c`, `repo/config/bootstrap_peers.c`
- **Action:** End-to-end targeted tests for recursive `dnsaddr` TXT resolution and conversion to multiaddrs.
- **Verify:** Resolve a known DNSADDR domain and compare against Kubo.

### B5. Signed IPNS records v2
- **Spec:** `specs/src/ipns/ipns-record.md` (Record Fields, Record Creation, Record Verification).
- **Files:** `namesys/pb.c`, `namesys/publisher.c`, `namesys/resolver.c`, `namesys/verify.c`
- **Action:**
  - `IpnsEntry` protobuf with `data` field as DAG-CBOR.
  - DAG-CBOR fields: `Value`, `ValidityType = 0`, `Validity` (RFC 3339), `Sequence`, `TTL`, optional `pubKey`, `signatureV2`.
  - Sign with the node's private key; verify with public key or IPNS name.
  - EOL/validity checking and sequence-number-based record selection.
- **Tests:** `test_ipns_sign_verify`, `test_ipns_record_select`.

### B6. DAG-CBOR codec
- **Spec:** IPLD DAG-CBOR spec referenced in `specs/src/ipns/ipns-record.md`.
- **Files:** `ipld/dag_cbor.c`
- **Action:** Encoder/decoder for the CBOR subset required by DAG-CBOR; validate via `dag put`/`dag get`.
- **Verify:** Roundtrip test for the DAG-CBOR document used by IPNS records.

## Phase C — P2 extended

- QUIC transport (`transport/quic_transport.c`, `registry.c`) — `c-libp2p/specs/quic/README.md`
- WebSocket transport (`transport/ws_transport.c`) — `c-libp2p/specs/websockets/README.md`
- GossipSub (`pubsub/gossipsub.c`) — `c-libp2p/specs/pubsub/gossipsub/gossipsub-v1.0.md`
- HTTP Gateway (range, content-type, DNSLink) — `specs/http-gateways/`
- Resource manager / connection gating — `c-libp2p/specs/connections/README.md`
- Fuzz targets (`fuzz/`)
- FFI progress callbacks

## Working rules
1. One priority per commit series; run `make all` + focused `test_ipfs` before commit.
2. Commit submodule changes first (push), then update pointer in c-ipfs.
3. Do not mark a capability complete without a Kubo interop test.
4. Keep disabled CI-crashing tests disabled until root cause fixed.
5. Reference the relevant `specs/` or `c-libp2p/specs/` document in each Phase B/C commit message and test plan.
