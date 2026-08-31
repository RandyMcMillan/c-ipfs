#!/bin/bash
# Self-host the c-ipfs repository using IPFS
# Usage: ./scripts/selfhost.sh [IPFS_PATH]

set -e

IPFS_PATH="${1:-$HOME/.ipfs}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "=== c-ipfs Self-Host ==="
echo "IPFS_PATH: $IPFS_PATH"
echo "Repo root: $REPO_ROOT"

# Init if needed
if [ ! -d "$IPFS_PATH" ]; then
    echo "Initializing IPFS repo..."
    IPFS_PATH="$IPFS_PATH" "$REPO_ROOT/main/ipfs" init
fi

# Build a clean tarball of tracked source files (no build artifacts)
ARCHIVE="/tmp/c-ipfs-source.tar.gz"
echo "Archiving tracked source files to $ARCHIVE..."
cd "$REPO_ROOT"
git ls-files --recurse-submodules | tar -czf "$ARCHIVE" -T -

# Compute SHA-256 and size
SHA256=$(shasum -a 256 "$ARCHIVE" | awk '{print $1}')
SIZE=$(stat -f%z "$ARCHIVE" 2>/dev/null || stat -c%s "$ARCHIVE" 2>/dev/null)

echo "Archive SHA-256: $SHA256"
echo "Archive size:    $SIZE bytes"

# Add the archive to IPFS
echo "Adding archive to IPFS..."
CID=$(IPFS_PATH="$IPFS_PATH" "$REPO_ROOT/main/ipfs" add "$ARCHIVE" 2>&1 | tail -1 | awk '{print $2}')

echo "Archive CID: $CID"
echo ""

# Create a nostr event announcing the self-hosted CID
echo "Nostr announcement event (kind 1064):"
"$REPO_ROOT/main/ipfs" nostr publish --cid "$CID" --content "c-ipfs source archive self-hosted"

echo ""
echo "PIP manifest event (kind 39078):"
"$REPO_ROOT/main/ipfs" nostr manifest --cid "$CID" --sha256 "$SHA256" --size "$SIZE" --path "c-ipfs" --encoding "tar.gz"

echo ""
echo "To retrieve:"
echo "  ipfs get $CID"
echo "  ipfs nostr sync --cid $CID --sha256 $SHA256"
echo "Gateway: https://ipfs.io/ipfs/$CID"
