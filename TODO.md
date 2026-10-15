# c-ipfs P0 Protocol Gap Plan

Last updated: 2026-10-15

## Done

- [x] P0.1 Gateway: range requests, directory listings, index.html, trailing-slash redirects, `?format=raw`, HEAD/OPTIONS, Kubo extension headers, and tests.
- [x] P0.92 DNSLink: fix resolver bugs, `_dnslink.` fallback, wire `/ipns/<domain>` into gateway, add mocked test.
- [x] P0.12 HTTP API JSON: integrate `core/api_kubo_rpc.c` with real `/api/v0` dispatcher (`id`, `version`), default to dedicated RPC port, add `id`/`version` tests, commit and push.
- [x] P0.9 Yamux: implement WINDOW_UPDATE handling, automatic receive-window replenishment, SYN-ACK on accept, PING request/response, GO_AWAY handling, and safe stream/session close.

## In Progress

- [ ] P0.9 Yamux tests
  - `test_yamux_v2_stream_roundtrip` — passed.
  - `test_yamux_v2_ping` — passed.
  - `test_yamux_v2_goaway` — passed after fixing UAF on goaway path.
  - `test_yamux_v2_window_update` — redesigned to use a server-side "go" probe so the client drains incoming WINDOW_UPDATE frames; needs rebuild + run.

## Next

1. **Verify Yamux work**
   - Rebuild `c-libp2p/v2` and `test/test_ipfs`.
   - Run focused Yamux tests: `./test_ipfs test_yamux_v2_stream_roundtrip test_yamux_v2_ping test_yamux_v2_goaway test_yamux_v2_window_update`.
   - Run full default suite: `make all && cd test && make clean all && ./test_ipfs`.
   - If green, commit the Yamux changes in the `c-libp2p` submodule, push the submodule, then update the submodule pointer in c-ipfs and push c-ipfs.

2. **P0.2 v2 wire protocol handshake integration**
   - Implement `ipfs_swarm_connect_v2_bridge()` in `transport/swarm_dialer_v2_bridge.c`:
     - Parse `/ip4/<ip>/tcp/<port>` (and `/p2p/<id>` if present).
     - TCP dial via `libp2p_net_tcp_dial`.
     - Multistream negotiate `/noise`.
     - Noise XX initiator handshake using RSA identity callbacks.
     - Yamux client session.
     - Identify exchange.
     - Return a bridge holding the Yamux session and first stream.
   - Make inbound `core/v2_listen.c` keep the Yamux session alive to accept multiple streams (Identify, Bitswap, etc.).
   - Add a focused test that starts a local v2 listener and dials it via the v2 bridge.

3. **IPNS hardening (P0.3/P0.4)**
   - Review `namesys/verify.c`, `namesys/resolver.c`, `namesys/namesys.c` for V1/V2 record validation gaps.
   - Ensure V2 signature validation and record lifetime checks match Kubo semantics.
   - Add/update tests in `test_namesys_verify.h`.

4. **Final verification and push**
   - `make all` at repo root.
   - Full `test/test_ipfs` default suite passes.
   - Commit each logical change with descriptive messages.
   - Push c-ipfs master and any c-libp2p submodule changes.

## Open Questions

- Should the Kubo-compatible RPC server eventually share port 5001 with `api_start`? Currently using a dedicated port to avoid conflict.
- The c-libp2p submodule is a separate repo; Yamux/Noise/v2 changes must be committed/pushed there before the c-ipfs submodule pointer can be updated.
