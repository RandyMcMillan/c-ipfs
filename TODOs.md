# c-ipfs swarm connect Kubo interop — TODOs

## Current status (2026-09-05)

- [x] Fix `bridge_read_raw` buffering for frame-based v2 streams
- [x] Add multistream ready wait in dialer after noise (15s timeout)
- [x] Add SIGPIPE handling in daemon startup
- [ ] Fix `libp2p_stream_get_latest_stream` for bridge streams
- [ ] Rebuild and restart daemon with latest binary
- [ ] Test swarm connect to Kubo
- [ ] Remove all debug prints (in `c-libp2p/conn/dialer.c`)
- [ ] Commit all changes

## Root cause analysis

`libp2p_stream_get_latest_stream` in `c-libp2p/net/stream.c:71` assumes `STREAM_TYPE_RAW` means `ConnectionContext*`. Bridge streams (`transport/stream_bridge.c`) also use `STREAM_TYPE_RAW` but hold `bridge_context_t`. When the swarm thread reads the multistream response over noise, `libp2p_stream_get_latest_stream` is called on the bridge stream and reads garbage/NULL for `session_context`, so the multistream handler never updates the ack status. The dialer times out waiting for multistream ready.

## Next steps

1. **Fix `libp2p_stream_get_latest_stream`** to fall back to `parent_stream` when a RAW stream's `session_context` is NULL/invalid (or introduce `STREAM_TYPE_BRIDGE`).
2. **Rebuild** `make all` from project root.
3. **Restart** c-ipfs daemon (kill old PID, start fresh).
4. **Test** `./ipfs swarm connect /ip4/127.0.0.1/tcp/4001/p2p/12D3KooWHeRWvdax5Heq5T9UDJQZTwRo12TLm4maorvr2HEAFcWM`.
5. **Clean up** temporary `DEBUG:` prints in `c-libp2p/conn/dialer.c` once swarm connect works.
6. **Commit**.

## Test setup

- Kubo repo: `/tmp/kubo_test_repo` (swarm 4001, API 5001)
- c-ipfs repo: `/tmp/c_ipfs_manual.9tpsRk/c_ipfs_repo` (swarm 4002, API 5002)
- c-ipfs log: `/tmp/c-ipfs.log`
- Kubo peer ID: `12D3KooWHeRWvdax5Heq5T9UDJQZTwRo12TLm4maorvr2HEAFcWM`

## Key files modified this session

- `transport/stream_bridge.c` — bridge buffering, `handle_upgrade` wired through `parent_stream`
- `c-libp2p/conn/dialer.c` — multistream ready wait after noise, timeout 15s, temp debug prints
- `c-libp2p/swarm/swarm.c` — `usleep(50000)` backoff in listen loop
- `core/daemon.c` — `signal(SIGPIPE, SIG_IGN)`
- `c-libp2p/net/connectionstream.c` — removed debug print
- `c-libp2p/conn/tcp_transport_dialer.c` — removed debug prints
- `c-libp2p/utils/thread_pool.c` — removed debug prints
- `ipfs-compatibility-matrix.json` — updated last audited commit, Noise status, gaps
- `ipfs-compatibility-profile.json` — added Noise implementation notes
- `IPFS.md` — updated Phase 4 completed items and current blockers
- `GAPS.md` — updated Noise, Yamux, Identify status
- `PROTOCOL_GAPS.md` — updated P0 items, root cause analysis, next session order
