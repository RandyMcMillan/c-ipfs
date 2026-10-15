# c-ipfs P0 Protocol Gap Plan

Last updated: 2026-10-15

## Done

- [x] P0.1 Gateway: range requests, directory listings, index.html, trailing-slash redirects, `?format=raw`, HEAD/OPTIONS, Kubo extension headers, and tests.
- [x] P0.92 DNSLink: fix resolver bugs, `_dnslink.` fallback, wire `/ipns/<domain>` into gateway, add mocked test.
- [x] P0.12 HTTP API JSON: integrate `core/api_kubo_rpc.c` with real `/api/v0` dispatcher (`id`, `version`), default to dedicated port, add `id`/`version` tests, commit and push.
- [x] P0.9 Yamux: implement WINDOW_UPDATE handling, automatic receive-window replenishment, SYN-ACK on accept, PING request/response, GO_AWAY handling, and safe stream/session close.
- [x] P0.9 Yamux tests: `test_yamux_v2_stream_roundtrip`, `test_yamux_v2_ping`, `test_yamux_v2_goaway`, `test_yamux_v2_window_update` all pass; c-libp2p submodule committed and pushed; pointer updated in c-ipfs.
- [x] P0.2 v2 wire protocol handshake integration:
  - Implement `ipfs_swarm_connect_v2_bridge()` in `transport/swarm_dialer_v2_bridge.c` with TCP dial, `/noise` multistream negotiation, Noise XX initiator handshake, Yamux client session, and Identify exchange.
  - Make inbound `core/v2_listen.c` hand the Yamux session off to a background thread so it stays alive to accept multiple streams.
  - Add `test_swarm_connect_v2_bridge` that starts a local v2 listener and dials it via the bridge.
- [x] P0.3 / P0.4 IPNS V1/V2 resolution hardening:
  - Add unified `ipns_entry_valid()` helper in `namesys/resolver.c` that verifies V2 (`ipns-signature:` + data) and V1 (value+validity+validityType) signatures using embedded public keys.
  - Enforce public-key/peer-ID name binding for embedded keys and EOL/validity checks on both local and network (`GetValue`) resolution paths.
  - Fix `util/time.c` `ipfs_util_time_parse_RFC3339()` to parse UTC timestamps with `timegm()` instead of `mktime()`, so expired records are rejected regardless of local timezone.
  - Add resolver-level tests: V1 RSA round-trip, V2 RSA round-trip, V2 Ed25519 round-trip, expired-record rejection, bad-signature rejection.
- [x] Full default `test/test_ipfs` suite passes (152 tests).

## In Progress

None — all listed P0 gaps are implemented and verified.

## Next

1. Continue down the protocol-gap list (P0.5+ transports such as QUIC/WebTransport, P0.8 Noise interop maturation, P0.13/0.14 Bitswap 1.1/1.2, etc.) or transition to P1 portability/maturity items as prioritized.

## Open Questions

- Should the Kubo-compatible RPC server eventually share port 5001 with `api_start`? Currently using a dedicated port to avoid conflict.
- The c-libp2p submodule is a separate repository; any further submodule fixes must be committed/pushed there before updating the pointer in this repo.
