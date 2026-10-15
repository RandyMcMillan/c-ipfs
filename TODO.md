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

## In Progress

- [ ] Final verification and push of the P0.2 changes:
  - Wait for full `test/test_ipfs` default suite to pass.
  - Commit c-ipfs side changes and update the `c-libp2p` submodule pointer.
  - Push c-ipfs master.

## Next

1. **IPNS hardening (P0.3/P0.4)**
   - Review `namesys/verify.c`, `namesys/resolver.c`, `namesys/namesys.c` for V1/V2 record validation gaps.
   - Ensure V2 signature validation and record lifetime checks match Kubo semantics.
   - Add/update tests in `test_namesys_verify.h`.

## Open Questions

- Should the Kubo-compatible RPC server eventually share port 5001 with `api_start`? Currently using a dedicated port to avoid conflict.
- The c-libp2p submodule is a separate repo; Yamux/Noise/v2 changes must be committed/pushed there before the c-ipfs submodule pointer can be updated.
