# c-ipfs P0/P1 Protocol Gap Plan

Last updated: 2026-10-17

## Done

- [x] P0.1 Gateway: range requests, directory listings, index.html, trailing-slash redirects, `?format=raw`, HEAD/OPTIONS, Kubo extension headers, and tests.
- [x] P0.92 DNSLink: fix resolver bugs, `_dnslink.` fallback, wire `/ipns/<domain>` into gateway, add mocked test.
- [x] P0.12 HTTP API JSON: integrate `core/api_kubo_rpc.c` with real `/api/v0` dispatcher (`id`, `version`), default to dedicated port, add `id`/`version` tests, commit and push.
- [x] P0.9 Yamux: implement WINDOW_UPDATE handling, automatic receive-window replenishment, SYN-ACK on accept, PING request/response, GO_AWAY handling, and safe stream/session close.
- [x] P0.9 Yamux tests: `test_yamux_v2_stream_roundtrip`, `test_yamux_v2_ping`, `test_yamux_v2_goaway`, `test_yamux_v2_window_update` all pass.
- [x] P0.2 v2 wire protocol handshake integration: `ipfs_swarm_connect_v2_bridge()`, inbound `core/v2_listen.c` background session thread, `test_swarm_connect_v2_bridge`.
- [x] P0.3 / P0.4 IPNS V1/V2 resolution hardening: unified `ipns_entry_valid()`, EOL/validity, timegm fix, resolver tests.
- [x] P0.11 DNSADDR resolver maturation: injectable resolvers, mocked tests.
- [x] Stability: fix missing null-terminator in `nostr/event.c`.
- [x] Full default `test/test_ipfs` suite passes (157+ tests).
- [x] P0.9.5 Exponential backoff for DHT/routing: `dht_backoff_delay_us()` in c-libp2p, kademlia integration, test added.
- [x] P0.8 Noise interop round-trip: local test verifies Noise + Yamux + Identify (agent + protocol version).
- [x] Heap/ASan fixes: HTTP request `data` init, boundary size correction, gateway suffix overflow, boundary size guards, daemon api_stop before free.
- [x] HTTP API expansion: `/api/v0/block/stat`, `/api/v0/object/stat`, `/api/v0/object/data`, `/api/v0/object/links`, `/api/v0/repo/version`, `/api/v0/config/show`, `/api/v0/bootstrap/list`, `/api/v0/resolve`, `/api/v0/pin/rm`, `/api/v0/dht/get`, `/api/v0/dht/put`, `/api/v0/dht/findpeer`, `/api/v0/diag/sys`, `/api/v0/stats/repo`, `/api/v0/stats/bw`, `/api/v0/commands`, `/api/v0/version/deps`, `/api/v0/swarm/addrs`, `/api/v0/shutdown`, `/api/v0/key/list`.
- [x] Fuzz harnesses: `fuzz/fuzz_cid.c`, `fuzz/fuzz_varint.c`.
- [x] RESUME_PLAN.md added.

## In Progress

- [ ] P0.13 / P0.14 Bitswap 1.1/1.2.0: sessions, cancel, ledger accounting.
- [ ] P1 Kademlia standard compliance: bucket maintenance, provider re-provide.
- [ ] P1 IPNS v2 completeness: EOL enforcement, pubsub distribution.
- [ ] P1 DAG-CBOR full codec.
- [ ] P2 QUIC/WebSocket real dial/listen.
- [ ] P2 GossipSub, resource manager, fuzz targets.
- [ ] P2 HTTP API/CLI expansion to Kubo parity.

## Open Questions

- Should the Kubo-compatible RPC server eventually share port 5001 with `api_start`?
- c-libp2p submodule is separate; push there first, then update pointer.