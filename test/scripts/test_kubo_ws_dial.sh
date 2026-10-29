#!/bin/bash
# Kubo WebSocket transport interop test.
# Verifies that c-ipfs can dial a Kubo peer listening on /ws, complete
# multistream-select, Noise XX, yamux, and receive an Identify response.
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

# Silence telemetry banners in CI.
export IPFS_TELEMETRY="${IPFS_TELEMETRY:-off}"
export DO_NOT_TRACK="${DO_NOT_TRACK:-1}"

KUBO_REPO="$(mktemp -d)"
CIPFS_REPO="$(mktemp -d)"
KUBO_LOG="$(mktemp)"
DIALER_LOG="$(mktemp)"

cleanup() {
    if [ -n "${KUBO_PID:-}" ]; then
        kill "$KUBO_PID" 2>/dev/null || true
        wait "$KUBO_PID" 2>/dev/null || true
    fi
    rm -f "$KUBO_LOG" "$DIALER_LOG"
    rm -rf "$KUBO_REPO" "$CIPFS_REPO"
}
trap cleanup EXIT

pick_port() {
    python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()'
}

KUBO_SWARM=$(pick_port)
KUBO_API=$(pick_port)
KUBO_GW=$(pick_port)

# Build the standalone v2 dialer.
make -C "$REPO_ROOT/test" test_v2_kubo >/dev/null 2>&1

# Start Kubo listening on a WebSocket swarm address.
export IPFS_PATH="$KUBO_REPO"
"$KUBO_BIN" init --profile=test >/dev/null 2>&1
"$KUBO_BIN" config Addresses.API "/ip4/127.0.0.1/tcp/$KUBO_API"
"$KUBO_BIN" config Addresses.Gateway "/ip4/127.0.0.1/tcp/$KUBO_GW"
"$KUBO_BIN" config --json Addresses.Swarm "[\"/ip4/127.0.0.1/tcp/$KUBO_SWARM/ws\"]"
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

# Initialize an isolated c-ipfs repo for the v2 dialer.
export IPFS_PATH="$CIPFS_REPO"
"$REPO_ROOT/main/ipfs" init >/dev/null 2>&1

TARGET="/ip4/127.0.0.1/tcp/$KUBO_SWARM/ws"
echo "Dialing Kubo peer $KUBO_ID over $TARGET using v2 stack..."

set +e
DEBUG="${DEBUG:-}" "$REPO_ROOT/test/test_v2_kubo" "$CIPFS_REPO" "$TARGET" > "$DIALER_LOG" 2>&1
DIAL_RC=$?
set -e

if [ "$DIAL_RC" -ne 0 ]; then
    echo "FAIL: v2 WebSocket dial failed (rc=$DIAL_RC)"
    echo "===== v2 dialer log ====="
    cat "$DIALER_LOG"
    echo "===== Kubo log tail ====="
    tail -60 "$KUBO_LOG"
    exit 1
fi

echo "PASS: WebSocket dial + Noise + yamux + Identify to Kubo succeeded"
exit 0
