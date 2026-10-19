# c-ipfs swarm connect Kubo interop — TODOs

## Current status (2026-10-19)

- [x] Fix `bridge_read_raw` buffering for frame-based v2 streams
- [x] Add multistream ready wait in dialer after noise (15s timeout)
- [x] Add SIGPIPE handling in daemon startup
- [x] Fix `libp2p_stream_get_latest_stream` for bridge streams (`STREAM_TYPE_BRIDGE`)
- [x] Fix Ed25519 Noise identity-signature verification to use the `noise-libp2p-static-key:` prefix
- [x] Fix Noise_XX nonce handling so each Encrypt/Decrypt in the same `temp_k` session increments the ChaCha20-Poly1305 nonce, matching `github.com/flynn/noise`
- [x] Gate debug prints on `DEBUG=1` env var via reusable `libp2p/utils/debug.h` / `ipfs/utils/debug.h` wrappers; keep prints for future agents
- [x] Add Kubo/Go-libp2p Noise handshake compatibility test (`test/scripts/test_kubo_noise_handshake.sh`) and wire it into GitHub CI (`.github/workflows/kubo-interop.yml`)
- [x] Commit `c-libp2p` submodule changes and update submodule pointer
- [ ] End-to-end `swarm connect` to a live Kubo peer

## Notes

- `STREAM_TYPE_BRIDGE` is now recognized in both `c-libp2p/net/stream.c` and `transport/stream_bridge.c`.
- `core/null.c` accepts inbound TCP connections via `ipfs_v2_listen_handler`, which runs multistream → Noise XX → Yamux → Identify.
- The Noise XX handshake against a live Kubo v0.43.0 (Ed25519 identity) now completes successfully. The responder logs `[net] Connected`, which the new CI test asserts.
- After the handshake, the legacy dialer still times out during the post-Noise multistream/yamux/identify exchange, so `ipfs swarm connect` exits non-zero. This is the remaining gap.
- Debug prints are intentionally preserved and gated by `DEBUG=1`. Future agents should keep them gated rather than deleting them.

## Next steps

1. Debug why the legacy dialer does not complete multistream → yamux → identify over the Noise bridge after the handshake succeeds.
2. Verify `ipfs swarm connect` to a live Kubo v0.43.0 peer once muxer/identify negotiation succeeds.

## Test setup

- Kubo repo: `/tmp/kubo_test_repo` (swarm 4001, API 5001)
- c-ipfs repo: `/tmp/c_ipfs_test_repo` (swarm 4002, API 5002)
- c-ipfs log: `/tmp/c-ipfs-test3.log`
- Kubo peer ID: `12D3KooWFTbVVSYo6bxHayFwhbc6CKSmWBsSmAWUCaAitWn3wtzi`
- New CI test: `test/scripts/test_kubo_noise_handshake.sh` (uses ephemeral ports and an isolated temp repo)

## Key files modified this session

- `c-libp2p/v2/src/conn/noise.c` — correct nonce increments for Encrypt/Decrypt under `temp_k`
- `c-libp2p/conn/dialer.c`, `c-libp2p/yamux/yamux.c`, `transport/stream_bridge.c`, `transport/v2_stream_wrapper.c` — debug prints gated by `DEBUG=1`
- `c-libp2p/include/libp2p/utils/debug.h`, `include/ipfs/utils/debug.h` — reusable `DEBUG` env-var wrappers
- `test/fixtures/noise_responder/` — minimal Go-libp2p responder fixture
- `test/scripts/test_kubo_noise_handshake.sh` — Kubo Noise handshake compatibility test
- `.github/workflows/kubo-interop.yml` — runs the new test in CI
- `TODOs.md`, `PROTOCOL_GAPS.md` — status updated
