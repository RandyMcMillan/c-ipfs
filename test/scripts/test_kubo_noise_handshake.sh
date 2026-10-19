#!/bin/bash
# Kubo/Go-libp2p Noise handshake compatibility test.
# Starts a minimal Go responder and verifies that c-ipfs can complete a
# Noise handshake (visible as "[net] Connected" in the responder log).

set -euo pipefail

# Silence Kubo's one-time telemetry notice in CI and local harness runs.
export IPFS_TELEMETRY="${IPFS_TELEMETRY:-off}"
export DO_NOT_TRACK="${DO_NOT_TRACK:-1}"

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FIXTURE_DIR="$REPO_ROOT/test/fixtures/noise_responder"
RESPONDER_LOG="$(mktemp)"

# Pick a free ephemeral port so multiple CI runs / developers don't collide.
PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')

# Use an isolated repo for this test
TEST_REPO="$(mktemp -d)"
export IPFS_PATH="$TEST_REPO"

cleanup() {
    if [ -n "${DAEMON_PID:-}" ]; then
        kill "$DAEMON_PID" 2>/dev/null || true
        wait "$DAEMON_PID" 2>/dev/null || true
    fi
    if [ -n "${RESPONDER_PID:-}" ]; then
        kill "$RESPONDER_PID" 2>/dev/null || true
        wait "$RESPONDER_PID" 2>/dev/null || true
    fi
    rm -f "$RESPONDER_LOG" "${DAEMON_LOG:-}"
    rm -rf "$TEST_REPO"
}
trap cleanup EXIT

echo "Building Go noise responder..."
cd "$FIXTURE_DIR"
if [ ! -f go.sum ]; then
    go mod tidy >/dev/null 2>&1
fi
go build -o noise_responder .

echo "Starting responder on port $PORT..."
./noise_responder "$PORT" > "$RESPONDER_LOG" 2>&1 &
RESPONDER_PID=$!

# Wait for responder to print its peer ID
PEER_ID=""
for i in $(seq 1 30); do
    PEER_ID=$(grep -oE 'Peer ID: .+' "$RESPONDER_LOG" | sed 's/Peer ID: //' || true)
    if [ -n "$PEER_ID" ]; then
        break
    fi
    sleep 1
done

if [ -z "$PEER_ID" ]; then
    echo "Responder did not start in time"
    cat "$RESPONDER_LOG"
    exit 1
fi

echo "Responder peer ID: $PEER_ID"

echo "Initializing c-ipfs repo..."
"$REPO_ROOT/main/ipfs" init >/dev/null 2>&1

# Pick free ports for c-ipfs daemon API, swarm, and gateway so tests don't
# collide with a developer's running Kubo/c-ipfs instance.
API_PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
SWARM_PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
GATEWAY_PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
export API_PORT SWARM_PORT GATEWAY_PORT
python3 - <<PY
import json, os
config_path = os.path.join(os.environ['IPFS_PATH'], 'config')
with open(config_path, 'r') as f:
    cfg = json.load(f)
cfg['Addresses']['API'] = f'/ip4/127.0.0.1/tcp/{os.environ["API_PORT"]}'
cfg['Addresses']['Gateway'] = f'/ip4/127.0.0.1/tcp/{os.environ["GATEWAY_PORT"]}'
cfg['Addresses']['Swarm'] = [f'/ip4/127.0.0.1/tcp/{os.environ["SWARM_PORT"]}']
cfg['Bootstrap'] = []
cfg['Discovery']['MDNS']['Enabled'] = False
with open(config_path, 'w') as f:
    json.dump(cfg, f, indent=1)
PY

DAEMON_LOG="$(mktemp)"
echo "Starting c-ipfs daemon (API=$API_PORT, swarm=$SWARM_PORT)..."
"$REPO_ROOT/main/ipfs" daemon > "$DAEMON_LOG" 2>&1 &
DAEMON_PID=$!

# Wait for API to be ready
for i in $(seq 1 30); do
    if curl -sf "http://127.0.0.1:$API_PORT/api/v0/id" >/dev/null 2>&1; then
        break
    fi
    if ! kill -0 "$DAEMON_PID" 2>/dev/null; then
        echo "Daemon exited prematurely"
        cat "$DAEMON_LOG"
        exit 1
    fi
    sleep 1
done

echo "Running c-ipfs swarm connect..."
set +e
"$REPO_ROOT/main/ipfs" swarm connect "/ip4/127.0.0.1/tcp/$PORT/p2p/$PEER_ID" >/dev/null 2>&1
SWARM_RC=$?
set -e

# Give the responder a moment to log the connection outcome
sleep 2

# Stop the daemon
kill "$DAEMON_PID" 2>/dev/null || true
wait "$DAEMON_PID" 2>/dev/null || true
echo "keeping $DAEMON_LOG"

echo "Responder log:"
cat "$RESPONDER_LOG"

if grep -q '\[net\] Connected' "$RESPONDER_LOG"; then
    echo "PASS: Noise handshake completed (responder saw Connected)"
    exit 0
else
    echo "FAIL: Noise handshake did not complete"
    exit 1
fi
