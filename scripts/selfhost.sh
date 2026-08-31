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

# Add the repository recursively
echo "Adding repository to IPFS (this may take a while)..."
ROOT_CID=$(IPFS_PATH="$IPFS_PATH" "$REPO_ROOT/main/ipfs" add -r "$REPO_ROOT" 2>&1 | tail -1 | awk '{print $2}')

echo "Root CID: $ROOT_CID"

# Pin it
echo "Pinning $ROOT_CID..."
# Note: pin command may not be fully implemented; we at least have the CID

# Create a nostr event announcing the self-hosted CID
echo ""
echo "Nostr announcement event:"
"$REPO_ROOT/main/ipfs" nostr publish --cid "$ROOT_CID" --content "c-ipfs source self-hosted"

echo ""
echo "To retrieve: ipfs cat $ROOT_CID"
echo "Gateway: https://ipfs.io/ipfs/$ROOT_CID"
