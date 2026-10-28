#!/bin/bash
# Focused CI test: c-ipfs retrieves a file from a Kubo peer over Bitswap.
# Exits 0 if the retrieved bytes match the original file.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

# Allow CI to override the Kubo binary; fall back to a local build for dev runs.
KUBO_BIN="${KUBO_BIN:-}"
if [ -z "${KUBO_BIN}" ]; then
    if [ -x "/Users/Shared/github/kubo/cmd/ipfs/ipfs" ]; then
        KUBO_BIN="/Users/Shared/github/kubo/cmd/ipfs/ipfs"
    else
        echo "KUBO_BIN must point to a Kubo ipfs binary"
        exit 1
    fi
fi

KUBO_REPO="$(mktemp -d)"
CIPFS_REPO="$(mktemp -d)"
KUBO_LOG="$(mktemp)"
CIPFS_LOG="$(mktemp)"
TEST_FILE="$(mktemp)"
RETRIEVED_FILE="$(mktemp)"

cleanup() {
    if [ -n "${KUBO_PID:-}" ]; then
        kill "$KUBO_PID" 2>/dev/null || true
        wait "$KUBO_PID" 2>/dev/null || true
    fi
    if [ -n "${CIPFS_PID:-}" ]; then
        kill "$CIPFS_PID" 2>/dev/null || true
        wait "$CIPFS_PID" 2>/dev/null || true
    fi
    rm -f "$KUBO_LOG" "$CIPFS_LOG" "$TEST_FILE" "$RETRIEVED_FILE"
    rm -rf "$KUBO_REPO" "$CIPFS_REPO"
}
trap cleanup EXIT

pick_port() {
    python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()'
}

KUBO_SWARM=$(pick_port)
KUBO_API=$(pick_port)
KUBO_GW=$(pick_port)
CIPFS_API=$(pick_port)
CIPFS_SWARM=$(pick_port)
CIPFS_GW=$(pick_port)

# Create a deterministic test file
echo "hello from kubo bitswap interop" > "$TEST_FILE"

# Start Kubo
export IPFS_PATH="$KUBO_REPO"
"$KUBO_BIN" init --profile=test >/dev/null 2>&1
"$KUBO_BIN" config Addresses.API "/ip4/127.0.0.1/tcp/$KUBO_API"
"$KUBO_BIN" config Addresses.Gateway "/ip4/127.0.0.1/tcp/$KUBO_GW"
"$KUBO_BIN" config --json Addresses.Swarm "[\"/ip4/127.0.0.1/tcp/$KUBO_SWARM\"]"
"$KUBO_BIN" config --json Discovery.MDNS.Enabled false
"$KUBO_BIN" config --json Bootstrap "[]"

"$KUBO_BIN" daemon > "$KUBO_LOG" 2>&1 &
KUBO_PID=$!

KUBO_ID=""
for _ in $(seq 1 30); do
    RESP=$(curl -sf -X POST "http://127.0.0.1:$KUBO_API/api/v0/id" 2>/dev/null || true)
    if [ -n "$RESP" ]; then
        KUBO_ID=$(echo "$RESP" | python3 -c 'import json,sys; print(json.load(sys.stdin)["ID"])')
        break
    fi
    if ! kill -0 "$KUBO_PID" 2>/dev/null; then
        echo "Kubo exited prematurely"
        cat "$KUBO_LOG"
        exit 1
    fi
    sleep 1
done

if [ -z "${KUBO_ID:-}" ]; then
    echo "Kubo API did not become ready"
    cat "$KUBO_LOG"
    exit 1
fi

# Add the test file to Kubo and capture the CID
CID=$("$KUBO_BIN" add --quieter "$TEST_FILE" 2>/dev/null)
if [ -z "$CID" ]; then
    echo "Failed to add test file to Kubo"
    cat "$KUBO_LOG"
    exit 1
fi

# Start c-ipfs
export IPFS_PATH="$CIPFS_REPO"
"$REPO_ROOT/main/ipfs" init >/dev/null 2>&1
python3 - "${CIPFS_API}" "${CIPFS_GW}" "${CIPFS_SWARM}" <<'PY'
import json, os, sys
cfg_path = os.path.join(os.environ['IPFS_PATH'], 'config')
with open(cfg_path, 'r') as f:
    cfg = json.load(f)
cfg['Addresses']['API'] = f'/ip4/127.0.0.1/tcp/{sys.argv[1]}'
cfg['Addresses']['Gateway'] = f'/ip4/127.0.0.1/tcp/{sys.argv[2]}'
cfg['Addresses']['Swarm'] = [f'/ip4/127.0.0.1/tcp/{sys.argv[3]}']
cfg['Bootstrap'] = []
with open(cfg_path, 'w') as f:
    json.dump(cfg, f, indent=1)
PY

DEBUG="${DEBUG:-}" "$REPO_ROOT/main/ipfs" daemon > "$CIPFS_LOG" 2>&1 &
CIPFS_PID=$!

for _ in $(seq 1 30); do
    if curl -sf -X POST "http://127.0.0.1:$CIPFS_API/api/v0/id" >/dev/null 2>&1; then
        break
    fi
    if ! kill -0 "$CIPFS_PID" 2>/dev/null; then
        echo "c-ipfs exited prematurely"
        cat "$CIPFS_LOG"
        exit 1
    fi
    sleep 1
done

# Explicitly connect to Kubo (more stable than bootstrap during daemon startup)
if ! DEBUG="${DEBUG:-}" "$REPO_ROOT/main/ipfs" swarm connect "/ip4/127.0.0.1/tcp/$KUBO_SWARM/p2p/$KUBO_ID" >> "$CIPFS_LOG" 2>&1; then
    echo "c-ipfs swarm connect failed"
    echo "===== c-ipfs log tail ====="
    tail -80 "$CIPFS_LOG"
    exit 1
fi

# Give the Bitswap peer request queue a moment to settle
sleep 1

# Retrieve the file through c-ipfs
if ! curl -sf "http://127.0.0.1:$CIPFS_API/api/v0/cat?arg=$CID" > "$RETRIEVED_FILE"; then
    echo "c-ipfs cat failed for CID $CID"
    echo "===== c-ipfs log tail ====="
    tail -100 "$CIPFS_LOG"
    echo "===== Kubo log tail ====="
    tail -40 "$KUBO_LOG"
    exit 1
fi

if ! diff -q "$TEST_FILE" "$RETRIEVED_FILE" >/dev/null; then
    echo "Retrieved content does not match original for CID $CID"
    echo "===== original ====="
    cat "$TEST_FILE"
    echo "===== retrieved ====="
    cat "$RETRIEVED_FILE"
    echo "===== c-ipfs log tail ====="
    tail -100 "$CIPFS_LOG"
    echo "===== Kubo log tail ====="
    tail -40 "$KUBO_LOG"
    exit 1
fi

echo "PASS: retrieved $CID from Kubo over Bitswap"
exit 0
