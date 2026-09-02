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
**Current:** `//TODO: something went wrong. This should be logged.`
**Fix:** Add `libp2p_logger_error("wantlist_queue", ...)` call.
**Blockers:** None.

### 2. `exchange/bitswap/wantlist_queue.c:97` — Remove entry when counter ≤ 0
**Current:** `//TODO: remove if counter is <= 0`
**Fix:** After `ipfs_bitswap_wantlist_queue_entry_decrement`, check `entry->counter <= 0` and remove from vector.
**Blockers:** None.

### 3. `core/ping.c:82` — Add error checking
**Current:** `//TODO: Error checking`
**Fix:** Check return values of `libp2p_net_socket_connect`, `write`, `read`.
**Blockers:** None.

### 4. `datastore/key.c:12` — Clean input
**Current:** `//TODO: clean the input`
**Fix:** Add input validation (null check, length check, sanitize path separators).
**Blockers:** None.

### 5. `pin/pin.c:367` — Track actual file size in GC
**Current:** `reclaimed += 0; // TODO: track actual file size`
**Fix:** Call `stat()` or use block size from blockstore to accumulate `reclaimed`.
**Blockers:** None.

### 6. `repo/fsrepo/lmdb_datastore.c:295` — Calculate pending flag correctly
**Current:** `journalstore_record->pending = 1; // TODO: Calculate this correctly`
**Fix:** Determine correct condition for pending state (likely based on replication peer count).
**Blockers:** Need to understand journal state machine.

### 7. `util/errs.c:25` — Fix error message placeholder
**Current:** `"TODO: ErrCidDecode"`
**Fix:** Replace with proper error description string.
**Blockers:** None.

### 8. `test/*` — Most test harness TODOs
**Examples:** `test/core/test_null.h:33`, `test/routing/test_routing.h:458`
**Fix:** Add assertions, better cleanup, or verification steps.
**Blockers:** None.

---

## 🟡 Easy (1–4 hours each)

### 9. `flatfs/flatfs.c:187` — Error checking for file operations
**Current:** `//TODO: Error checking (i.e. too many open files`
**Fix:** Check `fopen`/`fwrite` return values; handle `EMFILE`, `ENOSPC` via `errno`.
**Complexity:** Straightforward defensive programming. Need to propagate errors correctly.
**Blockers:** None.

### 10. `merkledag/node.c:833` — Handle malloc failure in link copy
**Current:** `if (LProc->links[i] == NULL) { // TODO: What should we do...`
**Fix:** Free already-allocated links, set `LProc->ammount`, return NULL.
**Complexity:** Cleanup logic pattern. Must avoid leaking partial allocations.
**Blockers:** None.

### 11. `routing/offline.c:26` — Fix encoded key buffer size
**Current:** `nkey = malloc(key_size * 2); // FIXME: size of encoded key`
**Fix:** Calculate exact size from `ipfs_datastore_helper_ds_key_from_binary` or use a safe upper bound.
**Complexity:** Need to understand base32 encoding ratio (5/8 expansion → `key_size * 8/5` padded).
**Blockers:** None.

### 12. `routing/offline.c:38` — Save offline routing records to DB
**Current:** `// TODO: Save to db as offline storage.`
**Fix:** Call `datastore_put` with the encoded key/record instead of freeing.
**Complexity:** Need to verify datastore API usage pattern.
**Blockers:** None.

### 13. `namesys/dns.c:19` — Add DNS caching
**Current:** `// TODO: maybe some sort of caching?`
**Fix:** Add a simple in-memory hashmap cache (TTL-based) for DNSLink resolutions.
**Complexity:** Cache invalidation strategy needed.
**Blockers:** None.

### 14. `core/api.c:242` — Return filename and content-type
**Current:** `// TODO: return filename and content-type`
**Fix:** Parse Content-Disposition/Content-Type headers in multipart boundary finder.
**Complexity:** String parsing within multipart parser.
**Blockers:** None.

### 15. `core/api.c:482` — Handle file download endpoint
**Current:** `// TODO: handle download file here.`
**Fix:** Implement file serving logic (read block, stream to socket).
**Complexity:** Similar to existing `cat` endpoint.
**Blockers:** None.

### 16. `core/api.c:487` — Handle gzip/json POST requests
**Current:** `// TODO: Handle gzip/json POST requests.`
**Fix:** Add Content-Encoding check for gzip; decompress or parse JSON body.
**Complexity:** Can use existing zlib or simple JSON parsing.
**Blockers:** None.

### 17. `core/http_request.c:212,228` — "Do the right thing" on API responses
**Current:** Placeholder JSON templates for DHT responses.
**Fix:** Populate actual response data (peer IDs, addresses) instead of template strings.
**Complexity:** Need to serialize actual routing results.
**Blockers:** None.

### 18. `core/http_request.c:274` — Handle multiple arguments
**Current:** `//TODO: we need to handle multiple arguments`
**Fix:** Loop over argv array instead of hardcoding single-arg access.
**Complexity:** CLI parsing extension.
**Blockers:** None.

### 19. `core/http_request.c:308` — Check for existing connections
**Current:** `// TODO: see if we are already connected...`
**Fix:** Search peerstore before dialing.
**Complexity:** Peer lookup pattern already exists elsewhere.
**Blockers:** None.

### 20. `core/bootstrap.c:29` — Attempt to connect to bootstrap peer
**Current:** `// TODO: attempt to connect to the peer`
**Fix:** Call dialer/connect after adding peer to peerstore.
**Complexity:** Bootstrap logic mostly exists; needs wiring.
**Blockers:** None.

### 21. `exchange/bitswap/bitswap.c:161` — Announce block to network
**Current:** `// TODO: Announce to world that we now have the block`
**Fix:** Call `ipfs_bitswap_want_manager_cancel` or send `HAVE` message to peers.
**Complexity:** Bitswap 1.2.0 `BlockPresence` message already implemented.
**Blockers:** None.

### 22. `exchange/bitswap/message.c:842` — Better error handling in wantlist decode
**Current:** `// TODO: we should do more than return a half-baked list`
**Fix:** Validate protobuf decode results; reject malformed messages.
**Complexity:** Defensive protobuf parsing.
**Blockers:** None.

### 23. `repo/config/config.c:99` — Cleanup approach
**Current:** `//TODO: This shouldn't be here, but it was the only way to cleanup...`
**Fix:** Move cleanup to proper destructor/exit path.
**Complexity:** Refactoring; need to ensure no double-free.
**Blockers:** None.

---

## 🟠 Medium (1–2 days each)

### 24. `exchange/bitswap/bitswap.c:201` — Replace busy-loop with condition variable
**Current:** `while(1) { ... //TODO: This is a busy-loop. Find another way.`
**Fix:** Use `pthread_cond_wait` / `pthread_cond_signal` between wantlist fulfillment and block arrival.
**Complexity:** Requires adding condition variable to `WantListQueueEntry` or `BitswapWantManager`. Must handle spurious wakeups and timeout correctly.
**Blockers:** Understanding want-manager threading model.
**Risk:** Race conditions if not done carefully.

### 25. `exchange/bitswap/wantlist_queue.c:140` — Convert vector to linked list
**Current:** `//TODO: This should be a linked list, not an array`
**Fix:** Replace `libp2p_utils_vector` with a linked list for O(1) pop and O(n) find (same as now, but correct semantics).
**Complexity:** Refactor all queue operations: `new`, `free`, `add`, `remove`, `pop`, `find`. Update mutex usage.
**Blockers:** None.
**Risk:** Memory leaks if free path is incorrect.

### 26. `namesys/resolver.c:117` — Ask the network for IPNS resolution
**Current:** `//TODO: ask the network`
**Fix:** Integrate DHT `GetValue` into resolver fallback chain.
**Complexity:** Need to understand routing API and resolver caching.
**Blockers:** DHT routing must be working.

### 27. `namesys/routing.c:226` — Implement IPNS signature verification
**Current:** `// TODO: implement libp2p_crypto_verify`
**Fix:** Wire up existing secp256k1/Schnorr verification from nostril or c-libp2p crypto.
**Complexity:** Key type detection (RSA vs Ed25519 vs secp256k1); signature format verification.
**Blockers:** Need to verify which crypto backends are actually compiled in.
**Risk:** Security-critical; must be correct.

### 28. `importer/importer.c:438` — Recursive directory traversal
**Current:** `//TODO: if directory_entry is itself a directory, traverse and report files`
**Fix:** Implement recursive directory import (currently only handles flat lists).
**Complexity:** Directory tree walking, depth tracking, cycle detection (symlinks).
**Blockers:** None.

### 29. `importer/importer.c:528,533` — Import display and file type handling
**Current:** Display what was imported; determine file vs split file vs directory.
**Fix:** Add progress callback; implement file-type detection logic.
**Complexity:** UX feature; requires defining output format.
**Blockers:** None.

### 30. `blocks/blockstore.c:256,309,388` — Subdirectory sharding
**Current:** `//TODO: put this in subdirectories`
**Fix:** Implement prefix-based sharding (e.g., first 2 chars of CID → `ab/abcd...`).
**Complexity:** Need to update `put`, `get`, and `delete` paths. Migration path for existing flat stores.
**Blockers:** None.
**Risk:** Data migration concern if production data exists.

### 31. `core/builder.c:5` — Implement builder method
**Current:** `// TODO: Implement this method`
**Fix:** Complete `ipfs_core_builder_new_node` logic.
**Complexity:** Need to understand builder config structure and node initialization order.
**Blockers:** Builder config (`BuildCfg`) is partially defined.

### 32. `core/net.c:10,22,35` — Implement net utility methods
**Current:** Three stub methods.
**Fix:** Implement network helpers (likely interface listing, address detection).
**Complexity:** Platform-specific code (Linux vs macOS vs Windows).
**Blockers:** None.

### 33. `cmd/ipfs/init.c` — Multiple init completeness TODOs (10 items)
**Current:** Daemon check, node creation, offline routing, default assets, parameter validation, file handling.
**Fix:** Implement each missing piece of init flow.
**Complexity:** Multiple small items that add up. Some depend on builder/config completion.
**Blockers:** Builder/config maturity.
**Recommendation:** Tackle individually; some are Trivial, some are Medium.

### 34. `path/resolver.c:104` — Complete path resolver
**Current:** `//TODO`
**Fix:** Implement `/ipfs/` and `/ipns/` path resolution logic.
**Complexity:** Need to integrate with DAG traversal, IPNS resolution, and caching.
**Blockers:** IPNS resolution must be complete first.

### 35. `journal/journal.c:268,381` — Journal file grouping and replication
**Current:** Get all files of same second; set replication peer values.
**Fix:** Implement time-based batching and replication state updates.
**Complexity:** Journal state machine understanding required.
**Blockers:** None.

---

## 🔴 Hard (3–7 days each)

### 36. `repo/fsrepo/fs_repo.c` — Repo locking (8 TODOs)
**Current:** No locking around init, open, is_initialized, config access.
**Fix:** Implement cross-process file locking (e.g., `flock` on repo lockfile) + in-process mutexes.
**Complexity:** Must handle:
- Lock file creation/ownership
- Crash recovery (stale locks)
- Nested lock scenarios (init → open)
- Platform differences (`flock` vs `lockf` vs Windows)
**Blockers:** None.
**Risk:** Deadlocks, data corruption if wrong.
**Recommendation:** This is the highest-impact Critical fix. Design carefully before coding.

### 37. `cid/cid.c:296,458` — CID version/codec unification
**Current:** `TODO: finish this` and `TODO: find a common denominator between versions and codecs`.
**Fix:** Refactor CID internals to support v0/v1 cleanly with codec detection.
**Complexity:** Affects all CID consumers. Must maintain backward compatibility.
**Blockers:** None.
**Risk:** Breaking change to core data type.

### 38. `exchange/bitswap/bitswap.c:237` — Return watchable future/promise
**Current:** `// TODO: return something that they can watch`
**Fix:** Replace synchronous `GetBlock` with async callback or future mechanism.
**Complexity:** API change across all callers. Need cancellation, timeout, error propagation.
**Blockers:** None.
**Risk:** Large refactor surface.

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

| Rank | TODO | Difficulty | Impact | File |
|------|------|------------|--------|------|
| 1 | Add `fopen`/`fwrite` error checks | 🟡 Easy | Prevents silent data loss | `flatfs/flatfs.c:187` |
| 2 | Fix malloc failure in link copy | 🟡 Easy | Prevents segfault/corruption | `merkledag/node.c:833` |
| 3 | Fix encoded key buffer size | 🟡 Easy | Prevents buffer overflow | `routing/offline.c:26` |
| 4 | Add logging to wantlist_queue | 🟢 Trivial | Debuggability | `wantlist_queue.c:118` |
| 5 | Remove entry when counter ≤ 0 | 🟢 Trivial | Correctness | `wantlist_queue.c:97` |
| 6 | Save offline records to DB | 🟡 Easy | Feature completion | `routing/offline.c:38` |
| 7 | Track GC reclaimed size | 🟢 Trivial | Metrics accuracy | `pin/pin.c:367` |
| 8 | Add error checking to ping | 🟢 Trivial | Robustness | `core/ping.c:82` |
| 9 | Populate actual API responses | 🟡 Easy | API correctness | `core/http_request.c:212,228` |
| 10 | Handle multiple CLI args | 🟡 Easy | CLI completeness | `core/http_request.c:274` |

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
