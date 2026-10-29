# Resume Plan: c-ipfs → Kubo v0.43.0 Interoperability

**Last updated:** 2026-10-28
**Current baseline:** master @ 0933e45 (c-ipfs), c-libp2p @ d637e73.
**Specification references:**
- `specs/` — official IPFS specifications (IPFS specs at `heads/main`).
- `c-libp2p/specs/` — official libp2p specifications (`libp2p/specs` at `heads/master`), already present as a nested submodule.

This plan is ordered **from the lowest protocol layer to the highest**, because upper layers are only useful once the layers beneath them are solid.

## Current state — what just landed

- **Transport (TCP):** outbound `ipfs swarm connect /ip4/...` to Kubo v0.43.1 is stable.
- **Security (Noise XX):** handshake completes in both directions over the inbound listener and outbound dialer paths.
- **Multistream-select:** works over Noise and yamux; inbound streams reply with the correct ack (`theyRequested=1`).
- **Multiplexing (yamux):** streams open with `WINDOW_UPDATE+SYN`, inbound streams are stored in session slots, and child-stream data is now read by a persistent inbound reader for remote-opened streams.
- **Bridge streams:** classified as `STREAM_TYPE_BRIDGE` so the swarm thread does not misread their context.
- **libp2p Identify v1/v2:** v1 roundtrip with Kubo works; v2 is wired and has a passing roundtrip test.
- **IPFS Bitswap 1.2.0:** single-block retrieval from Kubo passes end-to-end via `./test/scripts/test_kubo_bitswap_retrieve_debug.sh` (tagged `phase-B2-complete`).
- **Debug prints:** kept and gated by `DEBUG=1`.
- **CI:** runs `test_kubo_noise_handshake.sh`, `test_kubo_swarm_connect.sh`, and the new Kubo bitswap retrieval test in `.github/workflows/kubo-interop.yml`.

## Completion tracking

Percentages are rough estimates against the relevant spec documents in `specs/` and `c-libp2p/specs/`.

| Layer | Phase | Spec reference(s) | Completion |
|-------|-------|-------------------|------------|
| L1 Transport | TCP dial/listen | `c-libp2p/specs/connections/README.md` | 100% |
| L1 Transport | QUIC | `c-libp2p/specs/quic/README.md` | 5% |
| L1 Transport | WebSocket | `c-libp2p/specs/websockets/README.md` | 5% |
| L2 Security | Noise XX | `c-libp2p/specs/noise/README.md` | 100% |
| L2 Security | TLS 1.3 | `c-libp2p/specs/tls/tls.md` | 0% |
| L3 Muxer negotiation | multistream-select | — | 90% |
| L4 Multiplexing | yamux core | `c-libp2p/specs/yamux/README.md` | 75% |
| L4 Multiplexing | yamux PING/GO_AWAY/window | `c-libp2p/specs/yamux/README.md` | 30% |
| L4 Multiplexing | mplex | — | 10% |
| L5 libp2p core | Identify v1/v2 | `c-libp2p/specs/identify/README.md` | 90% |
| L5 libp2p core | Ping | `c-libp2p/specs/ping/ping.md` | 0% |
| L5 libp2p core | Relay / hole-punching | `c-libp2p/specs/connections/hole-punching.md` | 0% |
| L5 libp2p core | Signed peer/routing records | `c-libp2p/specs/RFC/0002-signed-envelopes.md`, `c-libp2p/specs/RFC/0003-routing-records.md` | 0% |
| L6 Content routing/exchange | Kademlia DHT | `specs/src/routing/kad-dht.md`, `c-libp2p/specs/kad-dht/README.md` | 20% |
| L6 Content routing/exchange | Bitswap 1.2.0 | `specs/src/bitswap-protocol.md` | 65% |
| L7 Naming | DNSADDR / DNSLink | `specs/DWEB_ADDRESSING.md` | 10% |
| L7 Naming | IPNS records v2 | `specs/src/ipns/ipns-record.md` | 10% |
| L8 Data model/codecs | DAG-CBOR | referenced in `specs/src/ipns/ipns-record.md` | 10% |
| L8 Data model/codecs | Merkle DAG / UnixFS | `specs/MERKLE_DAG.md`, `specs/UNIXFS.md` | 50% |
| L9 Application | HTTP Gateway | `specs/http-gateways/` | 30% |
| L9 Application | GossipSub | `c-libp2p/specs/pubsub/gossipsub/gossipsub-v1.0.md` | 5% |
| L9 Application | Resource manager / connection gating | `c-libp2p/specs/connections/README.md` | 5% |
| L9 Application | Fuzz targets / FFI progress | — | 5% |

## Known rough edges

- Yamux `PING`/`GO_AWAY` and proper flow-control window management are not implemented.
- Bitswap `Cancel`, `HAVE`/`DONT_HAVE` routing, multi-peer sessions, and CIDv1 wantlist entries need more work.
- DHT provider lookup is scaffolded but not Kubo-interop tested.

---

## L1 — Transport

**Sequencing note:** L1.2 QUIC cannot be completed until L2.2 TLS 1.3 is ready,
because QUIC uses TLS 1.3 for peer authentication (`c-libp2p/specs/quic/README.md`).
Therefore L1.3 WebSocket (which runs Noise + yamux over TCP) is the next
lowest-layer item, followed by L2.2 TLS, then L1.2 QUIC.

### L1.1 TCP dial/listen — DONE
- **Spec:** `c-libp2p/specs/connections/README.md`
- **Files:** `c-libp2p/c-multiaddr/protoutils.c`, `c-libp2p/conn/tcp_transport_dialer.c`, `c-libp2p/net/socket.c`, `c-libp2p/net/connectionstream.c`, `c-libp2p/v2/src/net/multiaddr.c`, `c-libp2p/v2/src/net/tcp_dial.c`, `transport/swarm_dialer_v2_bridge.c`
- **Status:** IPv4 and IPv6 outbound TCP dials work; IPv4 listen works. IPv6 inbound listen is not yet required for baseline Kubo interop.
- **Verify:** `./test/scripts/test_kubo_swarm_connect.sh`, `./test/scripts/test_kubo_swarm_connect_ipv6.sh`.
- **Completed:** IPv6 multiaddr parsing and TCP v1/v2 dial paths; new IPv6 interop test.

### L1.2 QUIC transport
- **Spec:** `c-libp2p/specs/quic/README.md`
- **Files:** `transport/quic_transport.c`, `registry.c`
- **Action:** lsquic/BoringSSL build optional; wire up QUIC dial/listen and multistream/Noise/yamux over QUIC.

### L1.3 WebSocket transport
- **Spec:** `c-libp2p/specs/websockets/README.md`
- **Files:** `transport/ws_transport.c`
- **Action:** Complete WebSocket dial/listen path.

---

## L2 — Security

### L2.1 Noise XX — DONE
- **Spec:** `c-libp2p/specs/noise/README.md`
- **Files:** `c-libp2p/v2/src/conn/noise.c`, `c-libp2p/v2/src/conn/noise_callbacks.c`, `transport/noise_v2_bridge.c`
- **Status:** Outbound and inbound Noise XX handshakes complete against Kubo.
- **Verify:** `./test/scripts/test_kubo_noise_handshake.sh`, `./test/scripts/test_kubo_swarm_connect.sh`.

### L2.2 TLS 1.3
- **Spec:** `c-libp2p/specs/tls/tls.md`
- **Submodules:** `tls/` (TinyTls) for TLS 1.3 primitives
- **Files:** `transport/tls_transport.c` (new)
- **Action:** Implement libp2p TLS 1.3 handshake with peer-ID certificate validation.

---

## L3 — Muxer negotiation

### L3.1 multistream-select — MOSTLY DONE
- **Spec:** implicit in libp2p connection setup
- **Files:** `c-libp2p/net/multistream.c`, `transport/stream_bridge.c`
- **Status:** Works over Noise and over yamux child streams; inbound streams reply with `theyRequested=1`.
- **Verify:** `./test/scripts/test_kubo_swarm_connect.sh`.

---

## L4 — Multiplexing

### L4.1 Yamux core — MOSTLY DONE
- **Spec:** `c-libp2p/specs/yamux/README.md`
- **Files:** `c-libp2p/yamux/yamux.c`, `c-libp2p/yamux/session.c`, `c-libp2p/yamux/stream.c`, `c-libp2p/yamux/frame.h`
- **Status:** Stream open/close, `WINDOW_UPDATE+SYN`, inbound stream slotting, and persistent inbound reader for remote-opened streams work.
- **Verify:** `./test/scripts/test_kubo_swarm_connect.sh`, `./test/scripts/test_kubo_bitswap_retrieve_debug.sh`.

### L4.2 Yamux PING / GO_AWAY / window management
- **Spec:** `c-libp2p/specs/yamux/README.md`
- **Files:** `c-libp2p/v2/src/conn/yamux.c`, `c-libp2p/yamux/yamux.c`
- **Action:**
  - Correct frame-type dispatch for `WINDOW_UPDATE`, `PING`, `GO_AWAY`.
  - Send and respond to `PING` frames.
  - Handle `GO_AWAY` gracefully.
  - Proper window management for flow control.
- **Verify:** Kubo interop stress test with multiple concurrent streams.

### L4.3 mplex scaffold
- **Files:** `c-libp2p/mplex/`
- **Action:** Finish mplex as a fallback muxer.

---

## L5 — libp2p core protocols

### L5.1 Identify v1/v2 — MOSTLY DONE
- **Spec:** `c-libp2p/specs/identify/README.md`
- **Files:** `c-libp2p/v2/src/identify/identify_v2.c`, `c-libp2p/v2/src/identify/identify_v2_ipfs.c`, `c-libp2p/v2/src/peer/peerstore.c`, `core/ipfs_node.c`, `c-libp2p/conn/dialer.c`, `test/identify/test_identify_v2.c`
- **Status:** v1 roundtrip with Kubo works; v2 wired and roundtrip test passes. Signed peer records pending.
- **Verify:** `cd test && ./test_ipfs test_identify_v2_roundtrip`; `./test/scripts/test_kubo_swarm_connect.sh`.

### L5.2 Ping
- **Spec:** `c-libp2p/specs/ping/ping.md`
- **Files:** `core/ping.c`
- **Action:** Implement ping protocol over a new stream and expose `ipfs ping`.

### L5.3 Relay / hole-punching
- **Spec:** `c-libp2p/specs/connections/hole-punching.md`
- **Files:** `transport/relay*.c`
- **Action:** Circuit relay v2 and hole-punching support.

### L5.4 Signed peer records and routing records
- **Spec:** `c-libp2p/specs/RFC/0002-signed-envelopes.md`, `c-libp2p/specs/RFC/0003-routing-records.md`
- **Files:** `c-libp2p/record/`
- **Action:** Produce and validate signed peer records; wire into Identify and DHT.

---

## L6 — Content routing and exchange

### L6.1 Kademlia DHT
- **Spec:** `specs/src/routing/kad-dht.md` and `c-libp2p/specs/kad-dht/README.md`
- **Files:** `routing/k_routing.c`, `routing/online.c`, `c-libp2p/routing/dht.c`, `c-libp2p/routing/dht_protocol.c`
- **Action:**
  - Bucket size `k = 20`.
  - Iterative lookup with concurrency `α = 10` and resilience `β = 3`.
  - XOR distance ordering.
  - `FIND_NODE`, `FIND_VALUE`, `ADD_PROVIDER`, `GET_PROVIDERS` RPC handling.
  - Provider record re-provide/expiration (~48h).
  - Routing table refresh every ~10 minutes; ping and evict unresponsive nodes.
- **Tests:** `test_routing_iterative_find_peer` against a local Kubo peer.

### L6.2 Bitswap 1.2.0 — IN PROGRESS / PARTIAL
- **Spec:** `specs/src/bitswap-protocol.md`
- **Files:** `exchange/bitswap/want_manager.c`, `wantlist_queue.c`, `engine.c`, `message.c`, `network.c`
- **Done:** Single-block Kubo retrieval works; wantlist broadcast to connected peers.
- **Remaining:**
  - Session deduplication across peers.
  - `Cancel` messages when a block is no longer wanted.
  - `HAVE`/`DONT_HAVE` presence responses.
  - CIDv1 in wantlist entries.
  - 2 MiB per-block limit enforcement.
- **Tests:** `./test/scripts/test_kubo_bitswap_retrieve_debug.sh` against Kubo.

---

## L7 — Naming

### L7.1 DNSADDR bootstrap / DNSLink
- **Files:** `transport/dnsaddr_resolver.c`, `repo/config/bootstrap_peers.c`, `dnslink/`
- **Action:** Recursive `dnsaddr` TXT resolution and conversion to multiaddrs; DNSLink gateway resolution.
- **Verify:** Resolve a known DNSADDR domain and compare against Kubo.

### L7.2 Signed IPNS records v2
- **Spec:** `specs/src/ipns/ipns-record.md`
- **Files:** `namesys/pb.c`, `namesys/publisher.c`, `namesys/resolver.c`, `namesys/verify.c`
- **Action:**
  - `IpnsEntry` protobuf with `data` field as DAG-CBOR.
  - DAG-CBOR fields: `Value`, `ValidityType = 0`, `Validity` (RFC 3339), `Sequence`, `TTL`, optional `pubKey`, `signatureV2`.
  - Sign with the node's private key; verify with public key or IPNS name.
  - EOL/validity checking and sequence-number-based record selection.
- **Tests:** `test_ipns_sign_verify`, `test_ipns_record_select`.

---

## L8 — Data model and codecs

### L8.1 DAG-CBOR
- **Spec:** IPLD DAG-CBOR spec referenced in `specs/src/ipns/ipns-record.md`
- **Files:** `ipld/dag_cbor.c`
- **Action:** Encoder/decoder for the CBOR subset required by DAG-CBOR; validate via `dag put`/`dag get`.
- **Verify:** Roundtrip test for the DAG-CBOR document used by IPNS records.

### L8.2 Merkle DAG / UNIXFS
- **Spec:** `specs/MERKLE_DAG.md`, `specs/UNIXFS.md`
- **Files:** `merkledag/`, `unixfs/`, `importer/`
- **Action:** Correct DAG-PB and UnixFS parsing, chunking, and traversal; large-file sharding.

---

## L9 — Application layer

### L9.1 HTTP Gateway
- **Spec:** `specs/http-gateways/`
- **Files:** `core/api*.c`, `http/`
- **Action:** Range requests, content-type, DNSLink gateway, subdomain gateway.

### L9.2 GossipSub
- **Spec:** `c-libp2p/specs/pubsub/gossipsub/gossipsub-v1.0.md`
- **Files:** `pubsub/gossipsub.c`
- **Action:** Implement GossipSub v1.0 pubsub mesh.

### L9.3 Resource manager / connection gating
- **Spec:** `c-libp2p/specs/connections/README.md`
- **Files:** `core/daemon.c`, `transport/registry.c`
- **Action:** Per-protocol/stream/connection limits and gating.

### L9.4 Fuzz targets and FFI progress callbacks
- **Files:** `fuzz/`, `ffi/`
- **Action:** Expand fuzzers; add FFI progress callbacks.

---

## Working rules
1. One priority per commit series; run `make all` + focused `test_ipfs` before commit.
2. Commit submodule changes first (push), then update pointer in c-ipfs.
3. Do not mark a capability complete without a Kubo interop test.
4. Keep disabled CI-crashing tests disabled until root cause fixed.
5. Reference the relevant `specs/` or `c-libp2p/specs/` document in each commit message and test plan.
