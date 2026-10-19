#!/bin/bash
# Debug helper: run c-ipfs swarm connect against a real Kubo peer with DEBUG=1.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
KUBO_BIN="${KUBO_BIN:-/Users/Shared/github/kubo/cmd/ipfs/ipfs}"

KUBO_REPO="$(mktemp -d)"
CIPFS_REPO="$(mktemp -d)"
KUBO_LOG="$(mktemp)"
CIPFS_LOG="$(mktemp)"

cleanup() {
    if [ -n "${KUBO_PID:-}" ]; then
        kill "$KUBO_PID" 2>/dev/null || true
        wait "$KUBO_PID" 2>/dev/null || true
    fi
    if [ -n "${CIPFS_PID:-}" ]; then
        kill "$CIPFS_PID" 2>/dev/null || true
        wait "$CIPFS_PID" 2>/dev/null || true
    fi
    cp "$KUBO_LOG" /tmp/kubo_debug.log 2>/dev/null || true
    cp "$CIPFS_LOG" /tmp/cipfs_debug.log 2>/dev/null || true
    rm -f "$KUBO_LOG" "$CIPFS_LOG"
    rm -rf "$KUBO_REPO" "$CIPFS_REPO"
}
trap cleanup EXIT

# Pick free ports
KUBO_SWARM=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
KUBO_API=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
KUBO_GW=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
CIPFS_API=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
CIPFS_SWARM=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
CIPFS_GW=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')

# Start Kubo
export IPFS_PATH="$KUBO_REPO"
"$KUBO_BIN" init --profile=test >/dev/null 2>&1
"$KUBO_BIN" config Addresses.API "/ip4/127.0.0.1/tcp/$KUBO_API"
"$KUBO_BIN" config Addresses.Gateway "/ip4/127.0.0.1/tcp/$KUBO_GW"
"$KUBO_BIN" config --json Addresses.Swarm "[\"/ip4/127.0.0.1/tcp/$KUBO_SWARM\"]"
"$KUBO_BIN" config --json Discovery.MDNS.Enabled false
"$KUBO_BIN" config --json Bootstrap "[]"

echo "Starting Kubo daemon (swarm=$KUBO_SWARM, API=$KUBO_API)..."
"$KUBO_BIN" daemon > "$KUBO_LOG" 2>&1 &
KUBO_PID=$!

for i in $(seq 1 30); do
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
echo "Kubo peer ID: $KUBO_ID"

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

echo "Starting c-ipfs daemon (swarm=$CIPFS_SWARM, API=$CIPFS_API)..."
DEBUG=1 "$REPO_ROOT/main/ipfs" daemon > "$CIPFS_LOG" 2>&1 &
CIPFS_PID=$!

for i in $(seq 1 30); do
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

echo "Running swarm connect..."
set +e
DEBUG=1 "$REPO_ROOT/main/ipfs" swarm connect "/ip4/127.0.0.1/tcp/$KUBO_SWARM/p2p/$KUBO_ID"
RC=$?
set -e

sleep 3

echo ""
echo "===== c-ipfs daemon log (tail 200 lines) ====="
tail -200 "$CIPFS_LOG"
echo ""
echo "===== Kubo daemon log (tail 80 lines) ====="
tail -80 "$KUBO_LOG"
echo ""
echo "swarm connect exit code: $RC"
exit "$RC"
