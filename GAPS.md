# c-ipfs Protocol Implementation Gaps

Generated: 2026-09-04

## Critical Blockers (Fixed)
- [x] **v2/legacy libp2p symbol collision** — `libp2p_peerstore_new` et al. from
  `c-libp2p/v2/libp2p_v2.a` shadowed legacy implementations, causing segfaults
  in bitswap engine. Fixed by linking only required v2 objects (`noise.o`,
  `multistream.o`) after `-lp2p`.

## P2P Transport Stack
- [ ] **QUIC transport** — `transport/quic_transport.c` is a stub. Needs:
  - lsquic engine lifecycle tied to swarm dialer
  - libp2p-TLS 1.3 handshake (not Noise) per spec
  - Multiaddr parsing for `/quic-v1` and `/webtransport`
  - Integration with `transport/registry.c` for `dial()` dispatch
- [ ] **WebSocket transport** — `transport/ws_transport.c` is a stub. Needs:
  - libwebsockets event loop integration (or threaded wrapper)
  - `/ws` and `/wss` multiaddr parsing
  - TLS wrapper for secure websockets
  - Registry integration
- [ ] **Transport registry dial dispatch** — `registry.dial()` currently only
  handles TCP. Needs to iterate registered transports (TCP, QUIC, WS) and
  attempt each based on multiaddr protocol stack.
- [ ] **Swarm dialer protocol upgrade** — `libp2p_swarm_connect()` in v2 is not
  linked into legacy swarm. The legacy `exchange/bitswap/engine.c` still uses
  the old TCP-only dial path. Need to bridge v2 multistream-SECIO-Yamux
  upgrade into legacy `Stream` lifecycle.

## libp2p Security & Multiplexing
- [ ] **Noise handshake completion** — `libp2p_noise_handshake_raw()` is linked
  but never invoked from the main dial path. Need to negotiate `/noise` via
  multistream before calling the v2 bridge.
- [ ] **SECIO deprecation** — Legacy code still uses SECIO. Kubo dropped SECIO
  years ago. Need to default to Noise and fall back to SECIO only for old
  peers.
- [ ] **Yamux integration** — `libp2p_yamux_session_new` etc. are available from
  v2 but not wired into legacy connection handling. Need a yamux wrapper
  around the legacy `Stream` struct.
- [ ] **Identify protocol v2** — `identify_v2.o` exists but is not linked into
  the main binary. Need to send/recv identify over yamux streams post-handshake.

## Nostr / Hybrid Protocol
- [ ] **c-libnostr symbol collision resolution** — Internal `nostr/event.o` and
  `c-libnostr/build/libnostr.a` collide on `nostr_event_sign`,
  `nostr_event_verify`, `nostr_key_generate`, etc. Long-term fix: rename
  internal symbols to `ipfs_nostr_*` prefix or migrate fully to c-libnostr.
- [ ] **NIP-34 git integration** — CLI flags `--maintainer`, `--topic`,
  `--participant` are implemented in `cmd/ipfs/nostr.c`, but the git-over-nostr
  relay push/pull path is not wired to c-libnostr relay client.
- [ ] **Nostr relay send/receive** — c-libnostr has relay protocol support
  (requires cJSON + libwebsockets). Need to enable `NOSTR_FEATURE_RELAY=ON`
  and integrate with IPFS content routing announcements.
- [ ] **Hybrid content routing** — No code yet for announcing IPFS CIDs over
  Nostr kind-1 or kind-30023 events, or resolving CIDs via Nostr relay queries.

## Repository & Versioning
- [ ] **Repo version mismatch** — Desktop Kubo expects repo version 18;
  c-ipfs init creates version 12. Need to bump repo config version and
  implement migration path.
- [ ] **Repo lock portability** — `fs_repo_lock()` uses `flock(LOCK_EX | LOCK_NB)`
  which works on POSIX but may not interact correctly with Kubo repo locks
  (Kubo uses go-fslock with different semantics).

## DHT & Routing
- [ ] **DHT provide/get offline errors** — Tests `test_core_api_cat` and
  `test_core_api_dht_findprovs` fail with "[Error][offline] Unable to call API
  for dht publish." The daemon starts but DHT is not brought online.
- [ ] **Bootstrap peer resolution** — Tests use hardcoded localhost peers.
  Need DNS resolution for `/dnsaddr/bootstrap.libp2p.io` and fallback to
  known IPv4 bootstrap nodes.
- [ ] **Kubo interoperability** — `test_kubo_interop.sh` times out waiting for
  API on port 5011. The C daemon may not be exposing the HTTP RPC API in a
  way Kubo expects, or the port binding is failing silently.

## Testing & CI
- [ ] **Silent smoke test failures** — Some CI steps may fail without
  propagating exit codes. Need `set -euo pipefail` in all shell test blocks.
- [ ] **macOS CI matrix** — `macos-latest` is available via workflow_dispatch
  but not exercised automatically. The periodic workflow added in
  `.github/workflows/ci-periodic.yml` addresses this.
- [ ] **Test binary segfault latent** — While the v2 collision is fixed, the
  test binary still exits with code 1 (not 0). Need to identify which
  specific test fails.
- [ ] **act local CI** — `make act-build` works but the Kubo interop job
  inside act still fails. Need to debug act container networking or port
  forwarding for the interop harness.

## Build System
- [ ] **CMake migration** — c-libnostr and several submodules use CMake;
  c-ipfs still uses hand-rolled Makefiles. A top-level CMakeLists.txt would
  unify dependency discovery, feature flags, and cross-compilation.
- [ ] **BoringSSL alignment** — lsquic requires BoringSSL, but the rest of the
  project links OpenSSL. Symbol conflicts possible. Need isolated linking
  or full migration to BoringSSL.
- [ ] **nostril secp256k1 CFLAGS** — act containers use older GCC that rejects
  `-std=gnu23`. The Makefile already forces `-std=c99` for secp256k1
  configure, but this needs verification in CI.

## Specifications Not Yet Implemented
- [ ] **libp2p spec: QUIC-v1** — RFC 9000 + libp2p-TLS 1.3
- [ ] **libp2p spec: WebSocket** — RFC 6455 with multistream security
- [ ] **libp2p spec: Noise XX** — Noise Framework with static key extension
- [ ] **NIP-34** — Git stuff over Nostr (partial: CLI flags done, relay
  integration pending)
- [ ] **NIP-65** — Relay list metadata (c-libnostr supports it, not wired)
- [ ] **IPNS over PubSub** — `pubsub/ipns_pubsub.o` exists but needs DHT
  fallback and Kubo interop verification.
