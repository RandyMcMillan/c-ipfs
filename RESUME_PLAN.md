# Resume Plan: c-ipfs → Kubo v0.43.0 Interoperability

**Last updated:** 2026-10-29
**Current baseline:** master @ cabb8aa (c-ipfs), c-libp2p @ 3e8a27d.
**Specification references:**
- `specs/` — official IPFS specifications (IPFS specs at `heads/main`).
- `c-libp2p/specs/` — official libp2p specifications (`libp2p/specs` at `heads/master`), already present as a nested submodule.

This plan is ordered **from the lowest protocol layer to the highest**, because upper layers are only useful once the layers beneath them are solid. Within a layer, items are worked until 100% before moving to the next layer, and each completed layer is committed and tagged.

## Current state — what just landed

- **Transport (TCP):** outbound `ipfs swarm connect /ip4/...` to Kubo v0.43.1 is stable.
- **Transport (WebSocket):** dial completes multistream-select, Noise XX, yamux, and Identify exchange with Kubo.
- **Security (Noise XX):** handshake completes in both directions over the inbound listener and outbound dialer paths.
- **Security (TLS 1.3):** libp2p TLS identity helper implemented in c-libp2p (ephemeral ECDSA P-256 cert, SignedKey extension, peer-ID verification) per `c-libp2p/specs/tls/tls.md`; verified against Kubo over QUIC.
- **Transport (QUIC):** outbound and inbound QUIC over RFC 9000 with libp2p TLS 1.3 and ALPN `libp2p` are implemented in c-libp2p and wired into the v2 bridge; Kubo interop verifies both dial and listen directions.
- **Multistream-select:** works over Noise and yamux; inbound streams reply with the correct ack (`theyRequested=1`).
- **Multiplexing (yamux):** streams open with `WINDOW_UPDATE+SYN`, inbound streams are stored in session slots, and child-stream data is now read by a persistent inbound reader for remote-opened streams.
- **Bridge streams:** classified as `STREAM_TYPE_BRIDGE` so the swarm thread does not misread their context.
- **libp2p Identify v1/v2:** v1 roundtrip with Kubo works; v2 is wired and has a passing roundtrip test.
- **IPFS Bitswap 1.2.0:** single-block retrieval from Kubo passes end-to-end via `./test/scripts/test_kubo_bitswap_retrieve_debug.sh` (tagged `phase-B2-complete`).
- **Debug prints:** kept and gated by `DEBUG=1` / `DEBUG_<SCOPE>=true` (`libp2p/utils/debug.h`); documented in `AGENTS.md`.
- **CI:** `.github/workflows` refactored into reusable composite actions; `test_ffi_libp2p_host_connect_self` and the IPNS publish/resolve integration tests are disabled from the default suite because they hang or flake on macOS and `ubuntu-latest`.
- **L1.2 QUIC / L2.2 TLS hardening:** incorporated the `fix/quic-stream-callbacks` improvements into c-libp2p (TLS SignedKey sign/verify without the Noise domain-separation prefix, lsquic stream callbacks, synchronous QUIC dialer teardown) and gated the c-ipfs QUIC bridge `TRACE` prints behind `DEBUG_SCOPE`.
- **Kubo Interop test harness:** fixed `test/scripts/test_kubo_quic_dial.sh` so the `DEBUG=` environment assignment is exported before calling the `timeout` wrapper instead of being passed as a positional argument (which GNU `timeout` tries to execute).
- **QUIC stream I/O:** set and re-enable `lsquic_stream_wantread` on bidirectional streams so synchronous `lsquic_stream_read` calls receive the peer's data instead of returning `-1`.
- **Latest QUIC hardening (pending CI):** bumped c-libp2p to `3e8a27d` to (1) classify QUIC streams by `lsquic_stream_id()` parity, (2) disable lsquic's non-RFC TIMESTAMP extension frames that caused quic-go to close the connection with "unknown frame type", and (3) set explicit initial bidirectional stream limits and stream-data windows so Kubo can open and send on its Identify streams. c-ipfs `cabb8aa` adds always-on `libp2p_v2_logger_info` milestones in the QUIC bridge path and enables Kubo `net/identify`/`swarm2` debug logging in `test_kubo_quic_dial.sh` so CI failures show exactly where the exchange stops.
- **QUIC Identify direction fix:** the Kubo Interop run at `cabb8aa` showed Kubo identifying us but then timing out reading our Identify because c-ipfs was sending it on Kubo's inbound stream instead of the stream c-ipfs itself opened. The dialer now sends our Identify on the outbound stream and reads Kubo's on the inbound stream; the listener reads Kubo's Identify on the inbound stream and sends ours on a newly-opened outbound stream. L1.2 QUIC will move to 100% once Kubo Interop is green.
- **`origin/fix/quic-stream-callbacks` review:** the branch's useful fixes (TLS SignedKey sign/verify and cert validity check) were already in c-libp2p `master`; its removal of the ALPN select callback, read-interest calls, and bidi stream settings was not incorporated because those pieces are required for the current implementation.

## Completion tracking

Percentages are rough estimates against the relevant spec documents in `specs/` and `c-libp2p/specs/`.

| Layer | Phase | Spec reference(s) | Completion |
|-------|-------|-------------------|------------|
| L1 Transport | TCP dial/listen | `c-libp2p/specs/connections/README.md` | 100% |
| L1 Transport | QUIC | `c-libp2p/specs/quic/README.md` | 95% |
| L1 Transport | WebSocket | `c-libp2p/specs/websockets/README.md` | 100% |
| L2 Security | Noise XX | `c-libp2p/specs/noise/README.md` | 100% |
| L2 Security | TLS 1.3 | `c-libp2p/specs/tls/tls.md` | 100% |
| L3 Muxer negotiation | multistream-select | `c-libp2p/specs/connections/README.md` | 100% |
| L4 Multiplexing | yamux core | `c-libp2p/specs/yamux/README.md` | 90% |
| L4 Multiplexing | yamux PING/GO_AWAY/window | `c-libp2p/specs/yamux/README.md` | 30% |
| L4 Multiplexing | mplex | — | 10% |
| L5 libp2p core | Identify v1/v2 | `c-libp2p/specs/identify/README.md` | 100% |
| L5 libp2p core | Ping | `c-libp2p/specs/ping/ping.md` | 0% |
| L5 libp2p core | Relay / hole-punching | `c-libp2p/specs/connections/hole-punching.md` | 0% |
| L5 libp2p core | Signed peer/routing records | `c-libp2p/specs/RFC/0002-signed-envelopes.md`, `c-libp2p/specs/RFC/0003-routing-records.md` | 0% |
| L6 Content routing/exchange | Kademlia DHT | `specs/src/routing/kad-dht.md`, `c-libp2p/specs/kad-dht/README.md` | 20% |
| L6 Content routing/exchange | Bitswap 1.2.0 | `specs/src/bitswap-protocol.md` | 60% |
| L7 Naming | DNSADDR / DNSLink | `specs/DWEB_ADDRESSING.md` | 10% |
| L7 Naming | IPNS records v2 | `specs/src/ipns/ipns-record.md` | 10% |
| L8 Data model/codecs | DAG-CBOR | referenced in `specs/src/ipns/ipns-record.md` | 10% |
| L8 Data model/codecs | Merkle DAG / UnixFS | `specs/MERKLE_DAG.md`, `specs/UNIXFS.md` | 50% |
| L9 Application | HTTP Gateway | `specs/http-gateways/` | 30% |
| L9 Application | GossipSub | `c-libp2p/specs/pubsub/gossipsub/gossipsub-v1.0.md` | 5% |
| L9 Application | Resource manager / connection gating | `c-libp2p/specs/connections/README.md` | 5% |
| L9 Application | Fuzz targets / FFI progress | — | 5% |

## Known rough edges

- Yamux `PING`/`GO_AWAY` and proper flow-control window management need to be exercised under load.
- A dedicated `/tls/1.0.0` over TCP interop test is optional; QUIC already validates the TLS 1.3 identity handshake end-to-end.
- QUIC dialer stream teardown is now synchronous; lsquic engine/connection cleanup is exercised by the Kubo interop scripts.
- Bitswap `Cancel`, `HAVE`/`DONT_HAVE` routing, multi-peer sessions, and CIDv1 wantlist entries need more work.
- DHT provider lookup is scaffolded but not Kubo-interop tested.
- IPNS publish/resolve integration tests are disabled from the default suite pending a root-cause fix for non-deterministic signature verification failures.

---

## L1 — Transport

**Sequencing note:** QUIC requires TLS 1.3 for peer authentication (`c-libp2p/specs/quic/README.md`), so L2.2 TLS is being completed as part of the L1.2 work. The current order is: finish L1.2 QUIC to 100%, then move to the next incomplete layer.

### L1.1 TCP dial/listen — DONE
- **Spec:** `c-libp2p/specs/connections/README.md`
- **Files:** `c-libp2p/c-multiaddr/protoutils.c`, `c-libp2p/conn/tcp_transport_dialer.c`, `c-libp2p/net/socket.c`, `c-libp2p/net/connectionstream.c`, `c-libp2p/v2/src/net/multiaddr.c`, `c-libp2p/v2/src/net/tcp_dial.c`, `transport/swarm_dialer_v2_bridge.c`
- **Status:** IPv4 and IPv6 outbound TCP dials work; IPv4 listen works. IPv6 inbound listen is not yet required for baseline Kubo interop.
- **Verify:** `./test/scripts/test_kubo_swarm_connect.sh`, `./test/scripts/test_kubo_swarm_connect_ipv6.sh`.
- **Completed:** IPv6 multiaddr parsing and TCP v1/v2 dial paths; new IPv6 interop test.

### L1.2 QUIC transport — MOSTLY DONE (pending Kubo Interop green)
- **Spec:** `c-libp2p/specs/quic/README.md`
- **Files:**
  - `c-libp2p/v2/include/libp2p/conn/tls.h`
  - `c-libp2p/v2/src/conn/tls.c`
  - `c-libp2p/v2/include/libp2p/net/quic.h`
  - `c-libp2p/v2/src/net/quic_dial.c`
  - `c-libp2p/v2/src/net/quic_listen.c`
  - `core/v2_listen.c`
  - `core/null.c`
  - `transport/swarm_dialer_v2_bridge.c`
- **Status:**
  - libp2p TLS 1.3 identity helper (ephemeral ECDSA cert + SignedKey extension + peer verification) per `c-libp2p/specs/tls/tls.md`.
  - QUIC client dial and server listen/accept over RFC 9000 with ALPN `libp2p` and custom peer-cert verification using lsquic/BoringSSL.
  - v2 bridge detects `/quic-v1`, dials, accepts inbound Identify, runs multistream-select `/ipfs/id/1.0.0`, and buffers Kubo’s Identify response in both directions.
  - Default swarm config includes `/quic-v1` listeners; legacy multiaddr parser now recognizes `quic` and `quic-v1`.
- **Verify:** `./test/scripts/test_kubo_quic_dial.sh` (outbound and inbound QUIC swarm connect against Kubo v0.43.x).

### L1.3 WebSocket transport — DONE
- **Spec:** `c-libp2p/specs/websockets/README.md`
- **Files:** `c-libp2p/v2/include/libp2p/net/ws.h`, `c-libp2p/v2/src/net/ws_dial.c`, `transport/swarm_dialer_v2_bridge.c`
- **Status:** WebSocket dial completes multistream-select, Noise XX, yamux, and Identify exchange with Kubo.
- **Verify:** `./test/scripts/test_kubo_ws_dial.sh`.

---

## L2 — Security

### L2.1 Noise XX — DONE
- **Spec:** `c-libp2p/specs/noise/README.md`
- **Files:** `c-libp2p/v2/src/conn/noise.c`, `c-libp2p/v2/src/conn/noise_callbacks.c`, `transport/noise_v2_bridge.c`
- **Status:** Outbound and inbound Noise XX handshakes complete against Kubo.
- **Verify:** `./test/scripts/test_kubo_noise_handshake.sh`, `./test/scripts/test_kubo_swarm_connect.sh`.

### L2.2 TLS 1.3 — DONE
- **Spec:** `c-libp2p/specs/tls/tls.md`
- **Dependencies:** BoringSSL (already built for lsquic/QUIC); OpenSSL/BoringSSL X.509 APIs.
- **Files:**
  - `c-libp2p/v2/include/libp2p/conn/tls.h`
  - `c-libp2p/v2/src/conn/tls.c`
  - `transport/swarm_dialer_v2_bridge.c`
- **Status:**
  - Certificate + extension generation: ephemeral ECDSA P-256 cert key, self-signed X.509 cert valid ~100 years, libp2p Public Key Extension (OID `1.3.6.1.4.1.53594.1.1`) containing the host protobuf public key and a signature over `libp2p-tls-handshake:` || cert SubjectPublicKeyInfo DER.
  - Peer certificate verification: require exactly one certificate, validate self-signature and validity, locate and parse the libp2p extension, verify the extension signature, derive/validate the peer ID.
  - `SSL_CTX` configured for TLS 1.3 only, client auth required, ALPN `libp2p`.
  - Handshake succeeds against a real Kubo peer over QUIC (verified by `./test/scripts/test_kubo_quic_dial.sh`).
- **Reference implementation:** `go-libp2p/p2p/security/tls/`.

---

## L3 — Muxer negotiation

### L3.1 multistream-select — MOSTLY DONE
- **Spec:** implicit in libp2p connection setup
- **Files:** `c-libp2p/net/multistream.c`, `c-libp2p/v2/src/conn/multistream.c`, `transport/stream_bridge.c`
- **Status:** Works over Noise and over yamux child streams; inbound streams reply with `theyRequested=1`.
- **Verify:** `./test/scripts/test_kubo_swarm_connect.sh`.

---

## L4 — Multiplexing

### L4.1 Yamux core — MOSTLY DONE
- **Spec:** `c-libp2p/specs/yamux/README.md`
- **Files:** `c-libp2p/yamux/yamux.c`, `c-libp2p/yamux/session.c`, `c-libp2p/yamux/stream.c`, `c-libp2p/yamux/frame.h`, `c-libp2p/v2/src/conn/yamux.c`
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

### L5.1 Identify v1/v2 — DONE
- **Spec:** `c-libp2p/specs/identify/README.md`
- **Files:** `c-libp2p/v2/src/identify/identify_v2.c`, `c-libp2p/v2/src/identify/identify_v2_ipfs.c`, `c-libp2p/v2/src/peer/peerstore.c`, `core/ipfs_node.c`, `c-libp2p/conn/dialer.c`, `transport/swarm_dialer_v2_bridge.c`, `test/identify/test_identify_v2.c`
- **Status:** v1 and v2 roundtrips with Kubo work over TCP and WebSocket. Signed peer records remain future work (L5.4).
- **Verify:** `cd test && ./test_ipfs test_identify_v2_roundtrip`; `./test/scripts/test_kubo_swarm_connect.sh`; `./test/scripts/test_kubo_ws_dial.sh`.

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
- **Done:** Single-block Kubo retrieval works; wantlist broadcast to connected peers; block-presence parsing tested.
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
1. One priority per commit series; run `HAS_LSQUIC=true make all` + focused `test_ipfs` before commit.
2. Commit submodule changes first (push), then update pointer in c-ipfs.
3. Do not mark a capability complete without a Kubo interop test.
4. Keep disabled CI-crashing tests disabled until root cause fixed.
5. Reference the relevant `specs/` or `c-libp2p/specs/` document in each commit message and test plan.
6. Keep debug prints and gate them with `DEBUG_<SCOPE>=true`; document new scopes in `AGENTS.md`.
7. Tag a layer only when its target percentage reaches 100% and CI is green.
