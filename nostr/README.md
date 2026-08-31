# c-ipfs Nostr Hybrid Protocol

This module implements a hybrid **nostr + ipfs + git** protocol for decentralized discovery, storage, and versioning.

## Architecture

- **Nostr** — discovery, identity (Schnorr/secp256k1), social signaling
- **IPFS** — content-addressed storage and transport
- **Git** — versioning, branching, patch-based collaboration

## Event Kinds

### IPFS Hybrid Kinds (1064–1067)

| Kind | Name | Purpose |
|------|------|---------|
| 1064 | `IPFS_CONTENT` | Announce CID availability |
| 1065 | `IPFS_PROVIDER` | Node provider record |
| 1066 | `IPFS_PIN_REQUEST` | Request remote pin |
| 1067 | `IPFS_PIN_CONFIRM` | Confirm remote pin |

### NIP-34 Git Kinds

| Kind | Name | Purpose |
|------|------|---------|
| 30617 | `GIT_REPO` | Repository announcement |
| 30618 | `GIT_STATE` | Repository state with RBSR fingerprint |
| 1617 | `GIT_PATCH` | Format-patch submission |
| 1621 | `GIT_ISSUE` | Issue / PR markdown |
| 1630 | `GIT_STATUS_OPEN` | Status: open |
| 1631 | `GIT_STATUS_MERGED` | Status: merged |
| 1632 | `GIT_STATUS_CLOSED` | Status: closed |
| 1633 | `GIT_STATUS_DRAFT` | Status: draft |
| 10317 | `GIT_GRASP_LIST` | Relay recommendations |

## RBSR — Range-Based Set Reconciliation

The `GIT_STATE` (kind 30618) events carry an **RBSR fingerprint** instead of raw refs for large repositories. RBSR allows two peers to efficiently agree on which refs differ using binary bisection of range fingerprints (count + XOR checksum).

See `include/ipfs/rbsr.h` and `rbsr/rbsr.c` for the C implementation.

## CLI Usage

```bash
# Announce IPFS content
ipfs nostr publish --cid Qm... --content "hello world"

# Announce a git repo backed by IPFS
ipfs nostr repo --id myrepo --name "My Repo" --cid QmRepoCID

# Publish repo state with RBSR
printf 'refs/heads/main abcdef...\n' > refs.txt
ipfs nostr state --repo <pubkey>:myrepo --refs refs.txt

# Publish a patch
ipfs nostr patch --repo <pubkey>:myrepo --subject "fix bug" --body "..."

# Publish an issue
ipfs nostr issue --repo <pubkey>:myrepo --subject "found bug" --body "..."

# Publish relay recommendations
ipfs nostr grasp --relay wss://relay.damus.io --relay wss://relay.nostr.info
```

## Self-Hosting

```bash
./scripts/selfhost.sh
```

This archives tracked source files, adds them to IPFS, and emits a signed nostr event (kind 1064) announcing the CID.

## Dependencies

- secp256k1 (Schnorr signatures)
- nostril SHA-256 + hex utilities
