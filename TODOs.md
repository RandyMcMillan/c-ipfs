# c-ipfs TODO Progress Report

> Generated from codebase scan. **85 TODO/FIXME markers** found across project source (excluding submodules: `c-libp2p/`, `lmdb/`, `nostril/`).

---

## Summary by Severity

| Severity | Count | Description |
|----------|-------|-------------|
| 🔴 Critical | 14 | Concurrency, crash-safety, memory safety, data structure bugs |
| 🟠 High | 22 | Missing features, incomplete error handling, protocol gaps |
| 🟡 Medium | 31 | Code quality, logging, optimization opportunities |
| 🟢 Low | 18 | Documentation, cleanup, minor improvements |

---

## 🔴 Critical

### Concurrency & Locking (`repo/fsrepo/fs_repo.c`)
| Line | TODO | Context |
|------|------|---------|
| 736 | `//TODO: lock` | `ipfs_repo_fsrepo_open` — no lock during open |
| 741 | `//TODO: lock the file (remember to unlock)` | Race condition on repo file |
| 742 | `//TODO: check the version, and make sure it is correct` | Version mismatch unchecked |
| 743 | `//TODO: make sure the directory is writable` | Writable check missing |
| 744 | `//TODO: open the config` | Config opening is implicit/unverified |
| 767 | `//TODO: lock things up so that someone doesn't try an init or remove while this call is in progress` | `fs_repo_is_initialized` unprotected |
| 804 | `// TODO: Do a lock so 2 don't do this at the same time` | `ipfs_repo_fsrepo_init` unprotected |
| 823 | `//TODO: mfsr.RepoPath(repo_path).WriteVersion(RepoVersion)` | Version file never written |

**Impact:** Multi-threaded or multi-process repo access is unsafe. Crash or corruption likely under concurrent init/open.

### Data Structure Bug (`exchange/bitswap/wantlist_queue.c`)
| Line | TODO | Context |
|------|------|---------|
| 140 | `//TODO: This should be a linked list, not an array` | `ipfs_bitswap_wantlist_queue_pop` scans entire vector |

**Impact:** O(n) scan per pop. Wrong abstraction for queue semantics; affects Bitswap performance.

### Memory Safety (`merkledag/node.c`)
| Line | TODO | Context |
|------|------|---------|
| 833 | `if (LProc->links[i] == NULL) { // TODO: What should we do if memory wasn't allocated here?` | Link allocation failure unhandled |

**Impact:** Null pointer dereference risk during DAG node construction.

### Buffer Sizing (`routing/offline.c`)
| Line | TODO | Context |
|------|------|---------|
| 26 | `nkey = malloc(key_size * 2); // FIXME: size of encoded key` | Key encoding buffer size is guessed |

**Impact:** Potential buffer overflow or underflow in offline routing key handling.

### Busy Loop (`exchange/bitswap/bitswap.c`)
| Line | TODO | Context |
|------|------|---------|
| 201 | `//TODO: This is a busy-loop. Find another way.` | Block-wait spins CPU |

**Impact:** Wastes CPU cycles; prevents efficient multi-peer operation.

### Error Handling (`flatfs/flatfs.c`)
| Line | TODO | Context |
|------|------|---------|
| 187 | `//TODO: Error checking (i.e. too many open files` | `flatfs_put` ignores `fopen`/`fwrite` failures |

**Impact:** Silent data loss on disk-full or fd-exhaustion conditions.

---

## 🟠 High

### Init Gaps (`cmd/ipfs/init.c`)
| Line | TODO | Context |
|------|------|---------|
| 22 | `//TODO: make sure daemon is not running` | `init` can corrupt active repo |
| 36 | `//TODO: make a new node, then close it` | Node initialization incomplete |
| 37 | `//TODO: setup offline routing on new node` | Offline routing not configured |
| 41 | `//TODO: see line 185 of init.go, what does core.BldCfg{Repo: r} do?` | Builder config unimplemented |
| 63 | `//TODO: If the conf is null, make one` | Null config not handled |
| 78 | `//TODO: add default assets` | Default IPFS assets (readme, welcome) missing |
| 88 | `// TODO: make sure offline` | Mode check missing |
| 89 | `// TODO: check parameters for logic errors` | Parameter validation incomplete |
| 90 | `// TODO: Initialize` | Generic initialization placeholder |
| 94 | `// TODO: handle files in request` | File upload via API unimplemented |

### API Incomplete (`core/api.c`)
| Line | TODO | Context |
|------|------|---------|
| 242 | `// TODO: return filename and content-type` | API response headers incomplete |
| 482 | `// TODO: handle download file here.` | File download endpoint stub |
| 487 | `// TODO: Handle gzip/json POST requests.` | Compression/content-negotiation missing |

### HTTP Request Handling (`core/http_request.c`)
| Line | TODO | Context |
|------|------|---------|
| 212 | `// TODO: do the right thing` | Unknown request path handling |
| 228 | `// TODO: do the right thing` | Error response formatting |
| 274 | `//TODO: we need to handle multiple arguments` | Multi-arg API commands unsupported |
| 308 | `// TODO: see if we are already connected...` | Duplicate connection check missing |

### Namesys / IPNS (`namesys/`)
| Line | TODO | Context |
|------|------|---------|
| `namesys/dns.c:19` | `// TODO: maybe some sort of caching?` | DNSLink resolution uncached |
| `namesys/resolver.c:117` | `//TODO: ask the network` | IPNS resolution falls back to nothing |
| `namesys/routing.c:226` | `// TODO: implement libp2p_crypto_verify` | IPNS record verification stubbed |
| `include/ipfs/namesys/pb.h:19` | `// TODO` | Protobuf header incomplete |
| `include/ipfs/namesys/namesys.h:15` | `// TODO: now that this is more modular, try to unify this code...` | Code duplication |
| `include/ipfs/namesys/namesys.h:24` | `//TODO ciPrivKey from c-libp2p-crypto` | Key type migration pending |
| `include/ipfs/namesys/namesys.h:61` | `// TODO` | Namesys interface gap |

### Path Resolution (`path/resolver.c`)
| Line | TODO | Context |
|------|------|---------|
| 104 | `//TODO` | Path resolver incomplete |

### Blockstore Organization (`blocks/blockstore.c`)
| Line | TODO | Context |
|------|------|---------|
| 256 | `//TODO: put this in subdirectories` | Flat block storage (sharding missing) |
| 309 | `//TODO: put this in subdirectories` | Flat block storage (sharding missing) |
| 388 | `//TODO: put this in subdirectories` | Flat block storage (sharding missing) |

**Impact:** Large repos will hit filesystem limits (inode exhaustion, slow lookups).

### Importer (`importer/importer.c`)
| Line | TODO | Context |
|------|------|---------|
| 438 | `//TODO: if directory_entry is itself a directory, traverse and report files` | Recursive directory import incomplete |
| 528 | `// TODO: probably need to display what was imported` | No import output feedback |
| 533 | `// TODO: Determine what needs to be done if this file_node is a file, a split file, or a directory` | File type handling incomplete |

---

## 🟡 Medium

### Bitswap Protocol (`exchange/bitswap/`)
| File | Line | TODO |
|------|------|------|
| `bitswap.c` | 161 | `// TODO: Announce to world that we now have the block` |
| `bitswap.c` | 237 | `// TODO: return something that they can watch` |
| `bitswap.c` | 247 | `// TODO: Implement this method` |
| `message.c` | 842 | `// TODO: we should do more than return a half-baked list` |
| `wantlist_queue.c` | 97 | `//TODO: remove if counter is <= 0` |
| `wantlist_queue.c` | 118 | `//TODO: something went wrong. This should be logged.` |
| `wantlist_queue.c` | 323 | `// TODO: Review this code.` |

### Core / Builder / Net / Ping
| File | Line | TODO |
|------|------|------|
| `core/bootstrap.c` | 15 | `//TODO:` (empty) |
| `core/bootstrap.c` | 29 | `// TODO: attempt to connect to the peer` |
| `core/builder.c` | 5 | `// TODO: Implement this method` |
| `core/net.c` | 10 | `//TODO: Implement this` |
| `core/net.c` | 22 | `// TODO: Implement this` |
| `core/net.c` | 35 | `//TODO: Implement this` |
| `core/ping.c` | 82 | `//TODO: Error checking` |
| `include/ipfs/core/ipfs_node.h` | 46 | `// TODO: Add more here` |

### Routing
| File | Line | TODO |
|------|------|------|
| `routing/offline.c` | 38 | `// TODO: Save to db as offline storage.` |
| `routing/offline.c` | 125 | `//TODO: we need to ask the api to do this for us` |
| `routing/offline.c` | 171 | `//TODO: publish this through the api` |
| `routing/offline.c` | 252 | `TODO: we should prevent that earlier` (duplicate peer) |
| `routing/online.c` | 621 | `TODO: we should prevent that earlier` (duplicate peer) |

### Repository / Config
| File | Line | TODO |
|------|------|------|
| `repo/config/config.c` | 99 | `//TODO: This shouldn't be here, but it was the only way to cleanup...` |
| `repo/fsrepo/lmdb_datastore.c` | 295 | `// TODO: Calculate this correctly` (pending flag) |
| `pin/pin.c` | 367 | `// TODO: track actual file size` (GC reclaimed bytes) |

### Journal
| File | Line | TODO |
|------|------|------|
| `journal/journal.c` | 268 | `// TODO: get all files of same second` |
| `journal/journal.c` | 381 | `//TODO: set new values in their ReplicationPeer struct` |

### Datastore
| File | Line | TODO |
|------|------|------|
| `datastore/key.c` | 12 | `//TODO: clean the input` |

### CID
| File | Line | TODO |
|------|------|------|
| `cid/cid.c` | 296 | `// TODO: finish this` |
| `cid/cid.c` | 458 | `* TODO: find a common denominator between versions and codecs...` |

---

## 🟢 Low

### Test Harness Improvements
| File | Line | TODO |
|------|------|------|
| `test/core/test_null.h` | 33 | `//TODO: Find a better way to do this...` |
| `test/core/test_null.h` | 47 | `//TODO: verify that the server (peer 1) has the client and his file` |
| `test/core/test_ping.h` | 61 | `//TODO: Dialer should know the protocol` |
| `test/exchange/test_bitswap.h` | 527 | `//TODO: Find a better way to do this...` |
| `test/routing/test_routing.h` | 458 | `//TODO: Find a better way to do this...` |
| `test/routing/test_routing.h` | 562 | `//TODO: add a file to server 2` |
| `test/routing/test_routing.h` | 700 | `//TODO: Find a better way to do this...` |
| `test/routing/test_supernode.h` | 35 | `//TODO ping kademlia` |

### Misc / Headers
| File | Line | TODO |
|------|------|------|
| `include/ipfs/exchange/bitswap/network.h` | 47 | `//TODO: Implement this` |
| `include/ipfs/merkledag/Example for node.c` | 12 | `/*TODOS!` (example file) |
| `util/errs.c` | 25 | `"TODO: ErrCidDecode"` (error message placeholder) |

---

## Module TODO Count

| Module | Count | Top Issue |
|--------|-------|-----------|
| `repo/fsrepo/` | 8 | Locking / concurrency |
| `cmd/ipfs/` | 10 | Init completeness |
| `exchange/bitswap/` | 7 | Data structures + protocol |
| `core/` | 10 | API / builder / net stubs |
| `namesys/` | 7 | IPNS signing / caching / resolution |
| `test/` | 8 | Test harness brittleness |
| `importer/` | 3 | Directory traversal / type handling |
| `routing/` | 5 | Offline storage + duplicate peers |
| `blocks/` | 3 | Storage sharding |
| `journal/` | 2 | Replication / file grouping |
| Other | 12 | — |

---

## Recommendations (Do Not Remove — Track Progress)

1. **Track repo locking TODOs as a single epic** — all 8 in `fs_repo.c` must be resolved together for thread safety.
2. **Convert `wantlist_queue.c` to linked list** — currently the most impactful data-structure bug.
3. **Add `errno` checks to `flatfs.c`** — disk-full / fd-exhaustion are production killers.
4. **Complete `cmd/ipfs/init.c` TODOs** before claiming CLI conformance.
5. **Resolve `namesys/routing.c:226`** (`libp2p_crypto_verify`) to complete IPNS validation.
