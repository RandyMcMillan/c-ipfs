# c-ipfs Protocol Implementation Gaps

Generated: 2026-09-04

## Critical Blockers (Fixed)
- [x] **v2/legacy libp2p symbol collision** — `libp2p_peerstore_new` et al. from
  `c-libp2p/v2/libp2p_v2.a` shadowed legacy implementations, causing segfaults
  in bitswap engine. Fixed by linking only required v2 objects (`noise.o`,
  `multistream.o`) after `-lp2p`.
- [x] **c-libnostr shared-library `-fPIC` linker error** — c-libnostr CMake
  builds both `libnostr.so` and `libnostr.a`. Linking `libsecp256k1.a`
  (built without `-fPIC`) into the shared library failed on Linux CI.
  Fixed by building only the `nostr_static` target.
- [x] **c-libp2p/v2 `yamux_stub.o` not built** — `main/Makefile` and
  `test/Makefile` reference `../c-libp2p/v2/src/stub/yamux_stub.o`, but the
  v2 Makefile only built objects in `SRCS`. Fixed by adding `src/stub/yamux_stub.o`
  to the v2 `all` and `clean` targets.
- [x] **Cross-platform `prepare` target deleting archives** — The `prepare`
  target searched for non-ELF files and deleted `.a` archives, causing full
  c-libnostr and secp256k1 rebuilds on every `make all`. Fixed by removing
  `.a`/`.la`/`.lo` from the find patterns.
- [x] **nostril secp256k1 host-triplet detection broken on Linux** — The
  `nostril` Makefile target grepped for `^host_triplet=` which does not exist
  in secp256k1 `config.log` (it uses `host='...'`). This caused Linux CI to
  purge a valid secp256k1 build. Fixed by grepping `^host='` and checking OS
  name in addition to architecture.

## P2P Transport Stack
- [~] **QUIC transport** — `transport/quic_transport.c` compiles and links.
  lsquic engine stub is present; needs end-to-end handshake verification.
- [~] **WebSocket transport** — `transport/ws_transport.c` compiles and links.
  libwebsockets integration is stubbed; needs event loop wiring.
- [~] **Transport registry dial dispatch** — `transport/registry.c`,
  `transport/dnsaddr_resolver.c`, and `transport/swarm_dialer_v2_bridge.c`
  compile and link. The registry iterates transports, but bitswap engine
  still uses the legacy TCP-only path for the actual block exchange.
- [~] **Swarm dialer protocol upgrade** — `security/secio_noise_fallback.c`
  provides Noise-first with SECIO fallback. Yamux wrapper is linked via
  `transport/v2_stream_wrapper.c`. Full end-to-end verify against Kubo
  pending.

## libp2p Security & Multiplexing
- [~] **Noise handshake completion** — `libp2p_noise_handshake_raw()` is linked
  and called from `security/secio_noise_fallback.c`. Needs live peer test.
- [~] **SECIO deprecation** — `security/secio_noise_fallback.c` implements
  Noise-first with SECIO fallback. Legacy SECIO code remains for old peers.
- [~] **Yamux integration** — `transport/v2_stream_wrapper.c` and
  `transport/swarm_dialer_v2_bridge.c` wrap v2 yamux stubs into the legacy
  stream lifecycle. Needs integration test.
- [ ] **Identify protocol v2** — `identify_v2.o` exists but is not linked into
  the main binary. Need to send/recv identify over yamux streams post-handshake.

## Nostr / Hybrid Protocol
- [x] **c-libnostr symbol collision resolution** — All internal `nostr/*.o`
  symbols renamed to `ipfs_nostr_*` prefix. `c-libnostr/build/libnostr.a`
  now links cleanly into the main binary.
- [~] **NIP-34 git integration** — CLI flags and internal event builders are
  implemented in `cmd/ipfs/nostr.c` and `nostr/git.c`. Relay push/pull
  still needs c-libnostr relay client wiring.
- [~] **Nostr relay send/receive** — c-libnostr is linked. `NOSTR_FEATURE_RELAY`
  is OFF in the top-level Makefile to keep build times low; enabling it
  requires libwebsockets + cJSON linked into c-libnostr.
- [~] **Hybrid content routing** — `routing/nostr_hybrid_routing.c` implements
  CID announce/resolve over Nostr kind-30023 events. Needs live relay test.

## Repository & Versioning
- [~] **Repo version mismatch** — `repo/fsrepo/fs_repo_version.c` targets
  version 18 and implements a migration stub. Need to verify against Kubo.
- [ ] **Repo lock portability** — `fs_repo_lock()` uses `flock(LOCK_EX | LOCK_NB)`
  which works on POSIX but may not interact correctly with Kubo repo locks
  (Kubo uses go-fslock with different semantics).

## DHT & Routing
- [~] **DHT provide/get offline errors** — `routing/dht_server_api.c` adds a
  stub DHT RPC server thread. Provide/get still return mock data; needs
  live Kademlia table integration.
- [~] **Bootstrap peer resolution** — `transport/dnsaddr_resolver.c` resolves
  `/dnsaddr/` domains to IPv4 multiaddrs. Needs integration into the
  bootstrap dial sequence.
- [x] **Kubo interoperability HTTP RPC** — `core/api_kubo_rpc.c` serves
  `/api/v0/version` and `/api/v0/id` on port 5011. Wired into daemon
  lifecycle (starts in pthread, stops gracefully via poll loop).

## Testing & CI
- [x] **Silent smoke test failures** — All shell test blocks use
  `set -euo pipefail`; failures now propagate correctly.
- [x] **macOS CI matrix** — `macos-latest` is available via workflow_dispatch
  in `.github/workflows/ci.yml`. Periodic runner added in
  `.github/workflows/ci-periodic.yml`.
- [x] **Test binary segfault / exit 1** — v2 collision fixed; `test_ipfs` no
  longer segfaults at startup.  However, the suite crashes in CI with heap
  corruption (see below).
- [ ] **Test binary heap corruption in CI** — `test_ipfs` crashes
  non-deterministically in GitHub Actions Ubuntu runners with either:
  - `Fatal glibc error: malloc.c:4376 assertion failed` (exit 134, SIGABRT)
  - `Segmentation fault` (exit 139, SIGSEGV)
  Crashes occur during tests that create repos and exercise the HTTP API
  (`test_core_api_*` and `test_daemon_startup_shutdown`).  The crashes do
  **not** reproduce locally (macOS arm64).  All affected tests are disabled
  from the default suite as a temporary measure.  Root cause is likely a
  heap-corruption bug in repo initialization, RSA key generation, or HTTP
  request processing that only manifests under Ubuntu glibc's stricter
  malloc checking.
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

## Submodule Build & Dependency Gaps
- [ ] **c-libp2p v2 yamux symbol collision** — `src/conn/yamux.c` and
  `src/stub/yamux_stub.c` define functions with the same names
  (`libp2p_yamux_session_new`, `libp2p_yamux_stream_open`, etc.) but
  incompatible signatures.  `main/Makefile` links `yamux_stub.o` explicitly
  and does **not** link `libp2p_v2.a`, so the collision is avoided for now.
  Long-term: reconcile the two yamux APIs or remove one.
- [ ] **c-libnostr relay feature disabled** — `NOSTR_FEATURE_RELAY=OFF` is
  hard-coded in the top-level Makefile to minimize build time.  Enabling it
  requires linking libwebsockets and cJSON into c-libnostr, then ensuring
  the resulting `libnostr.a` does not collide with internal `nostr/*.o`
  symbols.
- [ ] **BoringSSL vs OpenSSL coexistence** — lsquic links BoringSSL; c-ipfs
  links OpenSSL (via `-lcurl -lssl -lcrypto`).  Both libraries define the
  same public symbols (libssl, libcrypto).  On macOS this works because
  BoringSSL is linked as static archives placed explicitly on the link line.
  On Linux CI the same approach seems to work, but symbol shadowing risks
  remain if lsquic calls BoringSSL functions that behave differently from
  OpenSSL equivalents.
- [ ] **CMake vs Makefile hybrid build** — c-libnostr, lsquic, BoringSSL, and
  libwebsockets use CMake; c-ipfs and c-libp2p use hand-rolled Makefiles.
  The top-level Makefile orchestrates CMake sub-builds with ad-hoc rules.
  A unified build system (e.g., top-level CMake or Meson) would eliminate
  ordering bugs like the yamux_stub omission.

## Specifications Not Yet Implemented
- [ ] **libp2p spec: QUIC-v1** — RFC 9000 + libp2p-TLS 1.3
- [ ] **libp2p spec: WebSocket** — RFC 6455 with multistream security
- [ ] **libp2p spec: Noise XX** — Noise Framework with static key extension
- [ ] **NIP-34** — Git stuff over Nostr (partial: CLI flags done, relay
  integration pending)
- [ ] **NIP-65** — Relay list metadata (c-libnostr supports it, not wired)
- [ ] **IPNS over PubSub** — `pubsub/ipns_pubsub.o` exists but needs DHT
  fallback and Kubo interop verification.
