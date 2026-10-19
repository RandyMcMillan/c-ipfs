# c-ipfs swarm connect Kubo interop — TODOs

## Current status (2026-10-19)

- [x] Fix `bridge_read_raw` buffering for frame-based v2 streams
- [x] Add multistream ready wait in dialer after noise (15s timeout)
- [x] Add SIGPIPE handling in daemon startup
- [x] Fix `libp2p_stream_get_latest_stream` for bridge streams (`STREAM_TYPE_BRIDGE`)
- [x] Remove temporary `DEBUG:` prints from `c-libp2p/conn/dialer.c`
- [x] Rebuild and run focused v2 bridge test (`test_swarm_connect_v2_bridge`)
- [x] Focused tests pass; full default suite shows 3 flaky/pre-existing failures when run together (`156/159`)
- [x] Fix Ed25519 Noise identity-signature verification to use the `noise-libp2p-static-key:` prefix
- [ ] End-to-end `swarm connect` to a live Kubo peer
- [ ] Commit submodule and parent-repo changes

## Notes

- `STREAM_TYPE_BRIDGE` is now recognized in both `c-libp2p/net/stream.c` and `transport/stream_bridge.c`.
- `core/null.c` accepts inbound TCP connections via `ipfs_v2_listen_handler`, which runs multistream → Noise XX → Yamux → Identify.
- A stale `namesys/verify.o` caused `test_namesys_resolver_bad_signature_rejected` to fail; forcing a rebuild of `namesys/verify.c` resolved it and the full suite now passes.
- The Noise XX handshake against a live Kubo v0.43.1 (Ed25519 identity) now completes, but the legacy dialer stalls at `Multistream over noise not ready.` while waiting for Kubo's muxer-selection ack.

## Next steps

1. Debug why the legacy dialer does not receive the multistream ack over the Noise bridge after the handshake succeeds.
2. Verify `ipfs swarm connect` to a live Kubo v0.43.0/0.43.1 peer once muxer negotiation succeeds.
3. Clean up any remaining temporary debug prints across the dialer/listener paths.
4. Commit the `c-libp2p` submodule changes, update the submodule pointer, and commit the parent repo.

## Test setup

- Kubo repo: `/tmp/kubo_test_repo` (swarm 4001, API 5001)
- c-ipfs repo: `/tmp/c_ipfs_test_repo` (swarm 4002, API 5002)
- c-ipfs log: `/tmp/c-ipfs-test3.log`
- Kubo peer ID: `12D3KooWFTbVVSYo6bxHayFwhbc6CKSmWBsSmAWUCaAitWn3wtzi`

## Key files modified this session

- `c-libp2p/conn/dialer.c` — removed temporary `DEBUG:` prints
- `c-libp2p/v2/src/conn/noise_callbacks.c` — Ed25519 verify now uses the `noise-libp2p-static-key:` prefix
- `transport/noise_v2_bridge.c` — Ed25519 verify now uses the `noise-libp2p-static-key:` prefix
- `namesys/verify.c` — recompiled (no source change; stale object was the issue)
- `TODOs.md`, `IPFS.md`, `PROTOCOL_GAPS.md` — status updated
