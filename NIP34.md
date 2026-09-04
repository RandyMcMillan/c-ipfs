# NIP-34 Git Stuff — Implementation Plan

## Overview

NIP-34 defines decentralized git collaboration over Nostr. This document tracks
c-ipfs's NIP-34 compliance and maps each required event kind to its
implementation status.

## Event Kinds

| Kind | Name | Status | File |
|------|------|--------|------|
| 30617 | Git Repository | **Partial** | `nostr/git.c` |
| 30618 | Git Repository State | **Partial** | `nostr/git.c` |
| 1617 | Git Patch | **Partial** | `nostr/git.c` |
| 1621 | Git Issue | **Partial** | `nostr/git.c` |
| 1630-1633 | Status | **Done** | `nostr/git.c` |
| 10317 | Grasp List | **Done** | `nostr/git.c` |

## Gaps & Next Steps

### 1. Git Repository (kind 30617)

**Implemented tags:** `d`, `name`, `description`, `web`, `clone`, `euc`

**Missing tags:**
- `p` — maintainers / collaborators (pubkey hex)
- `t` — topics

**CLI gap:** `ipfs nostr repo` lacks `--maintainer` and `--topic` flags.

### 2. Git Patch (kind 1617)

**Implemented tags:** `a` (repo reference), `r` (euc)

**Missing tags:**
- `subject` — patch subject line
- `p` — participants / reviewers

**CLI gap:** `ipfs nostr patch` lacks `--participant` flag.

### 3. Git Issue (kind 1621)

**Implemented tags:** `a`, `subject`

**Missing tags:**
- `p` — participants / assignees

**CLI gap:** `ipfs nostr issue` lacks `--participant` flag.

### 4. Git Repository State (kind 30618)

Current implementation stores an RBSR fingerprint in content. NIP-34 expects
raw git refs in the content (e.g. `{"refs/heads/main":"abc123..."}`).

**Action:** Support both formats — RBSR for efficiency, raw refs for spec
compliance. Add a `--raw-refs` CLI flag.

## Implementation Order

1. Add `maintainers[]` and `topics[]` to `NostrGitRepo` struct.
2. Add `participants[]` to `NostrGitPatch` struct.
3. Wire `subject` into patch events.
4. Update CLI argument parsing for all new flags.
5. Add NIP-34 smoke tests to CI.
6. Document the hybrid RBSR extension in README.

## Acceptance Criteria

- [ ] `ipfs nostr repo --maintainer <pubkey> --topic <topic>` produces valid
      kind-30617 events with `p` and `t` tags.
- [ ] `ipfs nostr patch --participant <pubkey>` produces valid kind-1617 events
      with `subject` and `p` tags.
- [ ] `ipfs nostr issue --participant <pubkey>` produces valid kind-1621 events
      with `p` tags.
- [ ] CI smoke tests exercise all new flags.
- [ ] Events validate against a NIP-34 reference checker (if available).
