# C-IPFS
IPFS implementation in C, (not just an API client library).

## Quick start for users:
* **ipfs init** to create an ipfs repository on your machine
* **ipfs add MyFile.txt** to add a file to the repository, will return with a hash that can be used to retrieve the file.
* **ipfs cat _hash_** to retrieve a file from the repository

## Nostr Hybrid Protocol

This fork adds a **nostr + ipfs + git** hybrid protocol for decentralized discovery, storage, and versioning.

### CLI

```bash
# Announce IPFS content over nostr (kind 1064)
ipfs nostr publish --cid Qm... --content "hello world"

# Announce a git repo backed by IPFS (kind 30617)
ipfs nostr repo --id myrepo --name "My Repo" --cid QmRepoCID

# Publish repo state with RBSR fingerprint (kind 30618)
ipfs nostr state --repo <pubkey>:myrepo --refs refs.txt

# Publish relay recommendations (kind 10317)
ipfs nostr grasp --relay wss://relay.damus.io

# Publish patches and issues (kind 1617 / 1621)
ipfs nostr patch --repo <pubkey>:myrepo --subject "fix" --body "..."
ipfs nostr issue --repo <pubkey>:myrepo --subject "bug" --body "..."

# Verify a nostr event signature
ipfs nostr verify --event '{"id":"...","pubkey":"...","sig":"..."}'

# Reuse an existing secret key
ipfs nostr publish --cid Qm... --seckey 0000...0001
```

### Self-Hosting

```bash
./scripts/selfhost.sh
```

Archives tracked source files, adds the tarball to IPFS, and emits a signed nostr event announcing the CID.

### RBSR — Range-Based Set Reconciliation

Repo state events carry an RBSR fingerprint instead of raw refs. Two peers can efficiently agree on which refs differ using binary bisection of range fingerprints (count + XOR checksum). See `rbsr/` and `nostr/README.md`.

## For techies (ipfs spec docs):
* [getting started](https://github.com/ipfs/specs/blob/master/overviews/implement-ipfs.md)
* [specifications](https://github.com/ipfs/specs)
* [getting started](https://github.com/ipfs/community/issues/177)
* [libp2p](https://github.com/libp2p/specs)

## Kubo compatibility target

[`ipfs-compatibility-profile.json`](ipfs-compatibility-profile.json) is the
versioned interoperability target for this implementation. The corresponding
[`ipfs-compatibility-matrix.json`](ipfs-compatibility-matrix.json) records the
current source-backed audit, gaps, and evidence. A feature is conformant only
after its matrix entry has interoperability test evidence.

## Prerequisites: To compile the C version you will need, all included as submodules:
* [lmdb](https://github.com/jmjatlanta/lmdb)
* [c-protobuf](https://github.com/Agorise/c-protobuf)
* [c-multihash](https://github.com/Agorise/c-multihash)
* [c-multiaddr](https://github.com/Agorise/c-multiaddr)
* [c-libp2p](https://github.com/Agorise/c-libp2p)

And of course this project at https://github.com/Agorise/c-ipfs

## How to compile the C version:
```
git submodule update --init --recursive
make all
```

## CI

GitHub Actions builds on Ubuntu (20.04, 22.04, latest) and macOS (13, latest) with smoke tests for init, add/cat, nostr publish/repo/state/patch/issue/grasp/verify, and IPFS sync loops.
