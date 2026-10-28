#!/bin/bash
# Focused CI test: c-ipfs swarm connect to a Kubo peer over IPv6 loopback.
# Skips gracefully if the host does not have a usable IPv6 loopback.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

KUBO_BIN="${KUBO_BIN:-}"
if [ -z "${KUBO_BIN}" ]; then
    if [ -x "/Users/Shared/github/kubo/cmd/ipfs/ipfs" ]; then
        KUBO_BIN="/Users/Shared/github/kubo/cmd/ipfs/ipfs"
    elif [ -x "/opt/homebrew/bin/ipfs" ]; then
        KUBO_BIN="/opt/homebrew/bin/ipfs"
    else
        echo "KUBO_BIN must point to a Kubo ipfs binary"
        exit 1
    fi
fi

# Quick IPv6 loopback check
if ! python3 - <<PY
import socket
try:
    s = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    s.bind(("::1", 0))
    s.close()
except OSError:
    raise SystemExit(1)
PY
then
    echo "SKIP: IPv6 loopback is not available"
    exit 0
fi

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
    rm -f "$KUBO_LOG" "$CIPFS_LOG"
    rm -rf "$KUBO_REPO" "$CIPFS_REPO"
}
trap cleanup EXIT

pick_port() {
    python3 -c 'import socket; s=socket.socket(socket.AF_INET6, socket.SOCK_STREAM); s.bind(("::1", 0)); print(s.getsockname()[1]); s.close()'
}

KUBO_SWARM=$(pick_port)
KUBO_API=$(pick_port)
KUBO_GW=$(pick_port)
CIPFS_API=$(pick_port)
CIPFS_SWARM=$(pick_port)
CIPFS_GW=$(pick_port)

# Start Kubo on IPv6 loopback
export IPFS_PATH="$KUBO_REPO"
"$KUBO_BIN" init --profile=test >/dev/null 2>&1
"$KUBO_BIN" config Addresses.API "/ip6/::1/tcp/$KUBO_API"
"$KUBO_BIN" config Addresses.Gateway "/ip6/::1/tcp/$KUBO_GW"
"$KUBO_BIN" config --json Addresses.Swarm "[\"/ip6/::1/tcp/$KUBO_SWARM\"]"
"$KUBO_BIN" config --json Discovery.MDNS.Enabled false
"$KUBO_BIN" config --json Bootstrap "[]"

"$KUBO_BIN" daemon > "$KUBO_LOG" 2>&1 &
KUBO_PID=$!

KUBO_ID=""
for _ in $(seq 1 30); do
    RESP=$(curl -sf -X POST "http://[::1]:$KUBO_API/api/v0/id" 2>/dev/null || true)
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
    echo "Kubo API did not become ready over IPv6"
    cat "$KUBO_LOG"
    exit 1
fi

# Start c-ipfs on IPv4 (we only need IPv6 outbound dial for this test)
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

if ! DEBUG="${DEBUG:-}" "$REPO_ROOT/main/ipfs" swarm connect "/ip6/::1/tcp/$KUBO_SWARM/p2p/$KUBO_ID"; then
    echo "IPv6 swarm connect failed"
    echo "===== c-ipfs log tail ====="
    tail -80 "$CIPFS_LOG"
    echo "===== Kubo log tail ====="
    tail -40 "$KUBO_LOG"
    exit 1
fi

echo "PASS: IPv6 swarm connect to Kubo succeeded"
exit 0
