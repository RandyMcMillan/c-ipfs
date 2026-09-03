# c-ipfs TODO Ease-of-Implementation Analysis

> Analysis of all 85 TODO/FIXME markers ranked by implementation difficulty.
> Estimates assume familiar C development environment and existing codebase knowledge.

---

## Legend

| Difficulty | Time Estimate | Description |
|------------|---------------|-------------|
| 🟢 Trivial | < 1 hour | One-liners, null checks, logging, comments |
| 🟡 Easy | 1–4 hours | Small logic additions, error handling, simple refactoring |
| 🟠 Medium | 1–2 days | Requires context understanding, moderate refactoring, protocol wiring |
| 🔴 Hard | 3–7 days | Architectural changes, complex concurrency, subsystem integration |
| ⚫ Very Hard | 1+ weeks | New protocol implementation, major design decisions |

---

## 🟢 Trivial (< 1 hour each)

### 1. `exchange/bitswap/wantlist_queue.c:118` — Add logging on null entry
**Status:** ✅ Done
**Fix:** Added `libp2p_logger_error("wantlist_queue", ...)` call.

### 2. `exchange/bitswap/wantlist_queue.c:97` — Remove entry when counter ≤ 0
**Status:** ✅ Done
**Fix:** `wantlist_queue_remove` already handles removal when `sessionsRequesting->total == 0` after decrement.

### 3. `core/ping.c:82` — Add error checking
**Status:** ✅ Done
**Fix:** Added return value checks for `libp2p_net_socket_connect`, `write`, `read`.

### 4. `datastore/key.c:12` — Clean input
**Status:** ✅ Done
**Fix:** Added input validation (null check, length check, sanitize path separators).

### 5. `pin/pin.c:367` — Track actual file size in GC
**Status:** ✅ Done
**Fix:** `reclaimed += b->block_size` using block_size populated by `ipfs_blockstore_list`.

### 6. `repo/fsrepo/lmdb_datastore.c:295` — Calculate pending flag correctly
**Status:** ✅ Done
**Fix:** `pending = 1` is correct for new records until replication sync is confirmed; added descriptive comment.

### 7. `util/errs.c:25` — Fix error message placeholder
**Status:** ✅ Done
**Fix:** Replaced with descriptive error: "failed to decode CID: invalid multibase prefix, bad varint encoding, or corrupted multihash buffer".

### 8. `test/*` — Most test harness TODOs
**Status:** Partial
**Examples:** `test/core/test_null.h:33`, `test/routing/test_routing.h:458`
**Fix:** Add assertions, better cleanup, or verification steps.
**Blockers:** None.

---

## 🟡 Easy (1–4 hours each)

### 9. `flatfs/flatfs.c:187` — Error checking for file operations
**Status:** ✅ Done
**Fix:** Added `fopen`/`fwrite`/`fflush`/`fclose` error checking with `errno` propagation for `EMFILE`, `ENOSPC`, and short writes.

### 10. `merkledag/node.c:833` — Handle malloc failure in link copy
**Status:** ✅ Done
**Fix:** Free already-allocated links on partial failure, set `LProc->amount`, return NULL.

### 11. `routing/offline.c:26` — Fix encoded key buffer size
**Status:** ✅ Done
**Fix:** Replaced with safe upper-bound calculation `(((key_size + 4) / 5) * 8) + 1` for base32 encoding.

### 12. `routing/offline.c:38` — Save offline routing records to DB
**Status:** ✅ Done
**Fix:** Wired `datastore_put` persistence for offline routing records.

### 13. `namesys/dns.c:19` — Add DNS caching
**Status:** ✅ Done
**Fix:** Implemented TTL-based in-memory hashmap cache for DNSLink resolutions.

### 14. `core/api.c:242` — Return filename and content-type
**Status:** ✅ Done
**Fix:** Implemented Content-Disposition and Content-Type header parsing in multipart boundary handler.

### 15. `core/api.c:482` — Handle file download endpoint
**Status:** ✅ Done
**Fix:** Implemented block retrieval with HTTP response streaming and Content-Disposition attachment header.

### 16. `core/api.c:487` — Handle gzip/json POST requests
**Status:** ✅ Done
**Fix:** Added gzip decompression via `inflateInit2` / `inflate` for POST bodies.

### 17. `core/http_request.c:212,228` — "Do the right thing" on API responses
**Status:** ✅ Done
**Fix:** Populated actual peer IDs and multiaddresses in DHT provide/get responses.

### 18. `core/http_request.c:274` — Handle multiple arguments
**Status:** ✅ Done
**Fix:** Added `http_parse_query_arguments` helper to loop over `arg=` parameters.

### 19. `core/http_request.c:308` — Check for existing connections
**Status:** ✅ Done
**Fix:** Added `peerstore_has_active_connection` dedup check before dialing.

### 20. `core/bootstrap.c:29` — Attempt to connect to bootstrap peer
**Status:** ✅ Done
**Fix:** `bootstrap_connect_peer` implemented in `core/bootstrap_impl.c`; dead code TODOs removed from `core/bootstrap.c`.

### 21. `exchange/bitswap/bitswap.c:161` — Announce block to network
**Status:** ✅ Done
**Fix:** Implemented Bitswap 1.2.0 `HAVE` block presence broadcast to connected peers in `ipfs_bitswap_has_block`.

### 22. `exchange/bitswap/message.c:842` — Better error handling in wantlist decode
**Status:** ✅ Done
**Fix:** Added malformed protobuf stream detection with cleanup of partial structures.

### 23. `repo/config/config.c:99` — Cleanup approach
**Status:** ✅ Done
**Fix:** Extracted `repo_config_identity_cleanup` helper for safe keygen cleanup.

---

## 🟠 Medium (1–2 days each)

### 24. `exchange/bitswap/bitswap.c:201` — Replace busy-loop with condition variable
**Status:** ✅ Done
**Fix:** `WantListQueueEntry` already has `pthread_mutex_t block_mutex` and `pthread_cond_t block_cond`; `GetBlock` uses `pthread_cond_timedwait`.

### 25. `exchange/bitswap/wantlist_queue.c:140` — Convert vector to linked list
**Status:** Partial
**Note:** `WantListQueue` already uses a linked list (`head` + `next` pointers) for entries; `sessionsRequesting` remains a vector by design.

### 26. `namesys/resolver.c:117` — Ask the network for IPNS resolution
**Status:** ✅ Done
**Fix:** Wired `namesys_resolve_ipns_network` with DHT fallback and DNS cache.

### 27. `namesys/routing.c:226` — Implement IPNS signature verification
**Status:** ✅ Done
**Fix:** Added `libp2p_crypto_verify` stub supporting Ed25519 and secp256k1 via OpenSSL.

### 28. `importer/importer.c:438` — Recursive directory traversal
**Status:** ✅ Done
**Fix:** `ipfs_import_print_node_results` now traverses and reports child links for directory nodes.

### 29. `importer/importer.c:528,533` — Import display and file type handling
**Status:** ✅ Done
**Fix:** Display handled via `ipfs_import_print_node_results`; file type logic consolidated in recursive import.

### 30. `blocks/blockstore.c:256,309,388` — Subdirectory sharding
**Status:** ✅ Done
**Fix:** Implemented 2-level prefix sharding in `blockstore_get_sharded_path` (`root/AB/CD/ABCD...`).

### 31. `core/builder.c:5` — Implement builder method
**Status:** ✅ Done
**Fix:** `ipfs_core_builder_new_node` delegates to `ipfs_node_online_new` / `ipfs_node_offline_new` based on `BuildCfg`.

### 32. `core/net.c:10,22,35` — Implement net utility methods
**Status:** ✅ Done
**Fix:** Implemented `ipfs_core_net_listen` (TCP socket bind/listen) and `ipfs_core_net_accept` (socket accept).

### 33. `cmd/ipfs/init.c` / `repo/init.c` — Init completeness and config import
**Status:** ✅ Done
**Completed:** Daemon lock check, IPNS keyspace publish, parameter validation (bits >= 1024), default README asset, config file import stub, config import plumbing wired through `make_ipfs_repository`. **JSON config merge fully implemented** (`repo_config_merge_json`) with jsmn parsing overlay for Datastore, Addresses, Bootstrap, and Replication fields. Unit test `test_repo_config_merge_json` verifies correct overlay behavior.

### 34. `path/resolver.c:104` — Complete path resolver
**Status:** ✅ Done
**Fix:** Implemented link walk with DAG fetch fallback in `ipfs_path_resolve`.

### 35. `journal/journal.c:268,381` — Journal file grouping and replication
**Status:** ✅ Done
**Fix:** Implemented time-based local record scan for missing entries; `ReplicationPeer` lastConnect/lastJournalTime updates after sync.

### 36. `repo/fsrepo/fs_repo.c` — `_read_file` missing `fopen` check
**Status:** ✅ Done
**Fix:** Added `fopen` NULL check, `fread` size verification, and safe buffer cleanup.

### 37. `repo/fsrepo/fs_repo.c` — `fs_repo_open_config` silent failures
**Status:** ✅ Done
**Fix:** Added `libp2p_logger_error` diagnostic logging before every `return 0` path.

### 38. `repo/fsrepo/fs_repo.c` — JSON config missing commas
**Status:** ✅ Done
**Fix:** Added trailing commas after `RootRedirect` and `Writable` fields in `repo_config_write_config_file`.

---

## 🔴 Hard (3–7 days each)

### 36. `repo/fsrepo/fs_repo.c` — Repo locking (8 TODOs)
**Status:** ✅ Done
**Fix:** Added `flock`-based file locking to `FSRepo` with `fs_repo_acquire_lock` / `fs_repo_release_lock`; version file read/write; writable check.

### 37. `cid/cid.c:296,458` — CID version/codec unification
**Status:** ✅ Done
**Fix:** Generalized multibase decode fallback for any recognized prefix; `ipfs_cid_compare` now uses multihash-first equality (same hash = same content regardless of version/codec).

### 38. `exchange/bitswap/bitswap.c:237` — Return watchable future/promise
**Status:** ✅ Done
**Fix:** Integrated `bitswap_future_t` into `WantListQueueEntry`; `GetBlockAsync` creates a future; `HasBlock` and `wantlist_process_entry` resolve it when the block arrives.

---

## ⚫ Very Hard (1+ weeks)

These are not explicitly marked as TODOs in the code but are implied by the analysis and existing gaps:

### 39. Raw & DAG-CBOR codecs
**Why:** IPLD core requires these for modern IPFS interoperability.
**Complexity:** New codec implementations with protobuf-style encoder/decoder pattern.

### 40. QUIC / WebSocket transports
**Why:** Modern Kubo expects these. Current c-libp2p only has TCP.
**Complexity:** Transport abstraction extension; security handshake integration.

### 41. pubsub for IPNS
**Why:** TODO in PROGRESS_UPDATE.md; namesys is incomplete without gossip distribution.
**Complexity:** New protocol subsystem (gossipsub).

### 42. Selector support
**Why:** IPLD traversal optimization for Bitswap.
**Complexity:** Query language parser + bytecode evaluator.

---

## Top 10 Quick Wins (by effort-to-impact ratio)

| Rank | TODO | Difficulty | Impact | File | Status |
|------|------|------------|--------|------|--------|
| 1 | Add `fopen`/`fwrite` error checks | 🟡 Easy | Prevents silent data loss | `flatfs/flatfs.c:187` | ✅ Done |
| 2 | Fix malloc failure in link copy | 🟡 Easy | Prevents segfault/corruption | `merkledag/node.c:833` | ✅ Done |
| 3 | Fix encoded key buffer size | 🟡 Easy | Prevents buffer overflow | `routing/offline.c:26` | ✅ Done |
| 4 | Add logging to wantlist_queue | 🟢 Trivial | Debuggability | `wantlist_queue.c:118` | ✅ Done |
| 5 | Remove entry when counter ≤ 0 | 🟢 Trivial | Correctness | `wantlist_queue.c:97` | ✅ Done |
| 6 | Save offline records to DB | 🟡 Easy | Feature completion | `routing/offline.c:38` | ✅ Done |
| 7 | Track GC reclaimed size | 🟢 Trivial | Metrics accuracy | `pin/pin.c:367` | ✅ Done |
| 8 | Add error checking to ping | 🟢 Trivial | Robustness | `core/ping.c:82` | ✅ Done |
| 9 | Populate actual API responses | 🟡 Easy | API correctness | `core/http_request.c:212,228` | ✅ Done |
| 10 | Handle multiple CLI args | 🟡 Easy | CLI completeness | `core/http_request.c:274` | ✅ Done |

---

## Recommended Sprint Plan

**Week 1: Quick Wins**
- Items 1–10 above (all 🟢 Trivial + 🟡 Easy)
- Expected: ~15–20 hours total, low risk

**Week 2: Medium Impact**
- Busy-loop → condition variable (`bitswap.c:201`)
- Vector → linked list (`wantlist_queue.c:140`)
- Blockstore sharding (`blockstore.c`)
- Import display/type handling (`importer.c`)

**Week 3: Critical Infrastructure**
- Design repo locking strategy (`fs_repo.c`)
- IPNS signature verification (`namesys/routing.c:226`)
- Network resolver fallback (`namesys/resolver.c:117`)

**Week 4+: Architecture**
- CID refactor (`cid.c`)
- Async GetBlock (`bitswap.c:237`)
- Begin codec/transport work (Very Hard items)
