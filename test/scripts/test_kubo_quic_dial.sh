#!/bin/bash
# Focused CI test: c-ipfs QUIC dial and listen against a real Kubo peer.
# Exits 0 if both outbound and inbound QUIC connections succeed.
set -euo pipefail

# Silence Kubo's one-time telemetry notice in CI and local harness runs.
export IPFS_TELEMETRY="${IPFS_TELEMETRY:-off}"
export DO_NOT_TRACK="${DO_NOT_TRACK:-1}"

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

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

# Kill a child process without hanging if it ignores SIGTERM.
safe_kill() {
    local pid="${1:-}"
    [ -n "$pid" ] || return 0
    if kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
        local n
        for n in $(seq 1 50); do
            kill -0 "$pid" 2>/dev/null || break
            sleep 0.1
        done
    fi
    if kill -0 "$pid" 2>/dev/null; then
        kill -9 "$pid" 2>/dev/null || true
        sleep 0.2
    fi
    wait "$pid" 2>/dev/null || true
}

cleanup() {
    safe_kill "${KUBO_PID:-}"
    safe_kill "${CIPFS_PID:-}"
    rm -f "$KUBO_LOG" "$CIPFS_LOG"
    rm -rf "$KUBO_REPO" "$CIPFS_REPO"
}
trap cleanup EXIT

pick_port() {
    python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()'
}

KUBO_SWARM_TCP=$(pick_port)
KUBO_SWARM_QUIC=$(pick_port)
KUBO_API=$(pick_port)
KUBO_GW=$(pick_port)
CIPFS_API=$(pick_port)
CIPFS_SWARM_TCP=$(pick_port)
CIPFS_SWARM_QUIC=$(pick_port)
CIPFS_GW=$(pick_port)

# Start Kubo with a QUIC listener.
export IPFS_PATH="$KUBO_REPO"
"$KUBO_BIN" init --profile=test >/dev/null 2>&1
"$KUBO_BIN" config Addresses.API "/ip4/127.0.0.1/tcp/$KUBO_API"
"$KUBO_BIN" config Addresses.Gateway "/ip4/127.0.0.1/tcp/$KUBO_GW"
"$KUBO_BIN" config --json Addresses.Swarm "[\"/ip4/127.0.0.1/tcp/$KUBO_SWARM_TCP\",\"/ip4/127.0.0.1/udp/$KUBO_SWARM_QUIC/quic-v1\"]"
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

# Start c-ipfs with a QUIC listener.
export IPFS_PATH="$CIPFS_REPO"
"$REPO_ROOT/main/ipfs" init >/dev/null 2>&1
python3 - "${CIPFS_API}" "${CIPFS_GW}" "${CIPFS_SWARM_TCP}" "${CIPFS_SWARM_QUIC}" <<'PY'
import json, os, sys
cfg_path = os.path.join(os.environ['IPFS_PATH'], 'config')
with open(cfg_path, 'r') as f:
    cfg = json.load(f)
cfg['Addresses']['API'] = f'/ip4/127.0.0.1/tcp/{sys.argv[1]}'
cfg['Addresses']['Gateway'] = f'/ip4/127.0.0.1/tcp/{sys.argv[2]}'
cfg['Addresses']['Swarm'] = [
    f'/ip4/127.0.0.1/tcp/{sys.argv[3]}',
    f'/ip4/127.0.0.1/udp/{sys.argv[4]}/quic-v1',
]
cfg['Bootstrap'] = []
with open(cfg_path, 'w') as f:
    json.dump(cfg, f, indent=1)
PY

if command -v gdb >/dev/null 2>&1; then
    DEBUG="${DEBUG:-}" gdb -batch -ex "handle SIGPIPE nostop noprint pass" -ex run -ex "bt 30" -ex "thread apply all bt 12" --args "$REPO_ROOT/main/ipfs" daemon > "$CIPFS_LOG" 2>&1 &
else
DEBUG="${DEBUG:-}" "$REPO_ROOT/main/ipfs" daemon > "$CIPFS_LOG" 2>&1 &
fi
CIPFS_PID=$!

CIPFS_ID=""
for _ in $(seq 1 30); do
    RESP=$(curl -sf -X POST "http://127.0.0.1:$CIPFS_API/api/v0/id" 2>/dev/null || true)
    if [ -n "$RESP" ]; then
        CIPFS_ID=$(echo "$RESP" | python3 -c 'import json,sys; print(json.load(sys.stdin)["ID"])')
        break
    fi
    if ! kill -0 "$CIPFS_PID" 2>/dev/null; then
        echo "c-ipfs exited prematurely"
        cat "$CIPFS_LOG"
        exit 1
    fi
    sleep 1
done

if [ -z "${CIPFS_ID:-}" ]; then
    echo "c-ipfs API did not become ready"
    cat "$CIPFS_LOG"
    exit 1
fi

echo "Kubo ID:    $KUBO_ID"
echo "c-ipfs ID:  $CIPFS_ID"

# Test 1: c-ipfs dials Kubo over QUIC (outbound).
if ! DEBUG="${DEBUG:-}" "$REPO_ROOT/main/ipfs" swarm connect "/ip4/127.0.0.1/udp/$KUBO_SWARM_QUIC/quic-v1/p2p/$KUBO_ID"; then
    echo "FAIL: c-ipfs outbound QUIC swarm connect failed"
    echo "===== c-ipfs log tail ====="
    sleep 3
    tail -150 "$CIPFS_LOG"
    echo "===== Kubo log tail ====="
    tail -60 "$KUBO_LOG"
    exit 1
fi
echo "PASS: c-ipfs outbound QUIC swarm connect succeeded"

# Test 2: Kubo dials c-ipfs over QUIC (inbound).
if ! "$KUBO_BIN" swarm connect "/ip4/127.0.0.1/udp/$CIPFS_SWARM_QUIC/quic-v1/p2p/$CIPFS_ID"; then
    echo "FAIL: Kubo outbound QUIC swarm connect to c-ipfs failed"
    echo "===== c-ipfs log tail ====="
    tail -100 "$CIPFS_LOG"
    echo "===== Kubo log tail ====="
    tail -60 "$KUBO_LOG"
    exit 1
fi
echo "PASS: Kubo inbound QUIC swarm connect to c-ipfs succeeded"

exit 0
