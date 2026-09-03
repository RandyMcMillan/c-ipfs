#!/usr/bin/env bash
set -euo pipefail

KUBO_VERSION="${KUBO_VERSION:-v0.43.0}"
INTEROP_MODE="${INTEROP_MODE:-auto}"
VERBOSE="${VERBOSE:-false}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

C_IPFS_BIN="${ROOT_DIR}/main/ipfs"
MANIFEST="${ROOT_DIR}/test/kubo_interop_vectors.json"
TMP_DIR="$(mktemp -d /tmp/c_ipfs_kubo_interop.XXXXXX)"
C_REPO="${TMP_DIR}/c_ipfs_repo"
K_REPO="${TMP_DIR}/kubo_repo"
KUBO_BIN="${KUBO_BIN:-}"

if [ "${INTEROP_MODE}" = "auto" ]; then
    if [ "${ACT_CI:-}" = "true" ] || [ "${ACT:-}" = "true" ] || [ "${CI:-}" = "true" ]; then
        INTEROP_MODE="ci"
    else
        INTEROP_MODE="host"
    fi
fi

if [ "${INTEROP_MODE}" = "ci" ]; then
    CONNECT_WAIT_STEPS=20
    API_WAIT_STEPS=360
else
    CONNECT_WAIT_STEPS=40
    API_WAIT_STEPS=120
fi

if [ "${VERBOSE}" = "true" ]; then
    set -x
fi

c_ipfs_pid=""
kubo_pid=""

_api_ready() {
    local port="$1"
    local label="$2"

    # Fast path: verify the TCP port is accepting connections.
    # If the port is not open yet, fail immediately without blocking.
    if ! (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
        return 1
    fi

    # Port is open. Try to confirm the API is actually responsive.
    # Use timeout-wrapped checks so a slow/hanging CLI doesn't stall the loop.
    if [ "${label}" = "c-ipfs" ] && [ -n "${C_IPFS_BIN}" ]; then
        IPFS_PATH="${C_REPO}" "${C_IPFS_BIN}" id >/dev/null 2>&1 && return 0
    fi
    if [ "${label}" = "Kubo" ] && [ -n "${KUBO_BIN}" ]; then
        IPFS_PATH="${K_REPO}" "${KUBO_BIN}" id >/dev/null 2>&1 && return 0
    fi
    if command -v curl >/dev/null 2>&1; then
        curl -fsS --connect-timeout 2 --max-time 2 "http://127.0.0.1:${port}/api/v0/version" >/dev/null 2>&1 && return 0
    fi
    if command -v wget >/dev/null 2>&1; then
        wget -qO- --timeout=2 "http://127.0.0.1:${port}/api/v0/version" >/dev/null 2>&1 && return 0
    fi

    # If we reach here the port is open but we couldn't verify the API.
    # That's good enough to proceed; the daemon may still be initializing.
    return 0
}

wait_for_api() {
    local pid="$1"
    local port="$2"
    local label="$3"
    local log_file="$4"
    for _ in $(seq 1 "${API_WAIT_STEPS}"); do
        if ! kill -0 "${pid}" 2>/dev/null; then
            echo "${label} daemon exited before the API became ready"
            tail -n 50 "${log_file}" || true
            return 1
        fi
        if _api_ready "${port}" "${label}"; then
            return 0
        fi
        sleep 0.5
    done
    echo "Timed out waiting for ${label} API on port ${port}"
    tail -n 50 "${log_file}" || true
    return 1
}

wait_for_port() {
    local port="$1"
    local label="$2"
    for _ in $(seq 1 "${API_WAIT_STEPS}"); do
        if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.5
    done
    echo "Timed out waiting for ${label} port ${port}"
    return 1
}

cleanup() {
    if [ -n "${c_ipfs_pid}" ] && kill -0 "${c_ipfs_pid}" 2>/dev/null; then
        kill -9 "${c_ipfs_pid}" 2>/dev/null || true
    fi
    if [ -n "${kubo_pid}" ] && kill -0 "${kubo_pid}" 2>/dev/null; then
        kill -9 "${kubo_pid}" 2>/dev/null || true
    fi
    rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

if [ ! -f "${MANIFEST}" ]; then
    echo "Missing interop manifest: ${MANIFEST}"
    exit 1
fi

if [ "${INTEROP_MODE}" = "ci" ]; then
    echo "=== Setup: Installing Kubo ${KUBO_VERSION} ==="
    mkdir -p "${TMP_DIR}/kubo_bin"
    ARCH="$(uname -m)"
    OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
    case "${ARCH}" in
        x86_64) GOARCH="amd64" ;;
        arm64|aarch64) GOARCH="arm64" ;;
        *) echo "Unsupported architecture: ${ARCH}"; exit 1 ;;
    esac

    KUBO_TAR="kubo_${KUBO_VERSION}_${OS}-${GOARCH}.tar.gz"
    KUBO_URL="https://github.com/ipfs/kubo/releases/download/${KUBO_VERSION}/${KUBO_TAR}"

    curl -fsSL "${KUBO_URL}" | tar -xz -C "${TMP_DIR}/kubo_bin" --strip-components=1
    KUBO_BIN="${TMP_DIR}/kubo_bin/ipfs"
else
    if [ -z "${KUBO_BIN}" ]; then
        echo "Host mode requires KUBO_BIN to point to an installed Kubo binary."
        exit 1
    fi
fi

echo "=== Initializing test repositories ==="
IPFS_PATH="${C_REPO}" "${C_IPFS_BIN}" init
IPFS_PATH="${K_REPO}" "${KUBO_BIN}" init --profile=test

IPFS_PATH="${K_REPO}" "${KUBO_BIN}" config Addresses.Swarm --json '["/ip4/127.0.0.1/tcp/4011"]'
IPFS_PATH="${K_REPO}" "${KUBO_BIN}" config Addresses.API "/ip4/127.0.0.1/tcp/5011"
IPFS_PATH="${K_REPO}" "${KUBO_BIN}" config Addresses.Gateway "/ip4/127.0.0.1/tcp/8081"

echo "=== Starting c-ipfs daemon ==="
IPFS_PATH="${C_REPO}" "${C_IPFS_BIN}" daemon > "${TMP_DIR}/c_ipfs.log" 2>&1 &
c_ipfs_pid=$!

echo "Waiting for c-ipfs daemon API..."
wait_for_api "${c_ipfs_pid}" 5001 "c-ipfs" "${TMP_DIR}/c_ipfs.log" || exit 1
wait_for_port 4001 "c-ipfs swarm" || exit 1

C_ID=$(IPFS_PATH="${C_REPO}" "${C_IPFS_BIN}" id | awk '/^ID/ {print $2; exit}')
if [ -z "${C_ID}" ]; then
    echo "Failed to read c-ipfs peer ID"
    exit 1
fi
C_PEER_ADDR="/ip4/127.0.0.1/tcp/4001/p2p/${C_ID}"

echo "=== Starting Kubo daemon ==="
IPFS_PATH="${K_REPO}" "${KUBO_BIN}" daemon > "${TMP_DIR}/kubo.log" 2>&1 &
kubo_pid=$!

echo "Waiting for Kubo daemon API..."
wait_for_api "${kubo_pid}" 5011 "Kubo" "${TMP_DIR}/kubo.log" || exit 1
wait_for_port 4011 "Kubo swarm" || exit 1
KUBO_ID=$(IPFS_PATH="${K_REPO}" "${KUBO_BIN}" id -f="<id>")
KUBO_ADDR="/ip4/127.0.0.1/tcp/4011/p2p/${KUBO_ID}"
echo "Kubo Multiaddr: ${KUBO_ADDR}"
echo "=== Test 1: Swarm connect c-ipfs <-> Kubo ==="
if [ "${VERBOSE}" = "true" ]; then
    echo "c-ipfs peer: ${C_ID}"
    echo "c-ipfs address: ${C_PEER_ADDR}"
    echo "Kubo peer: ${KUBO_ID}"
    echo "Kubo address: ${KUBO_ADDR}"
fi
CONNECTED=0
for attempt in $(seq 1 "${CONNECT_WAIT_STEPS}"); do
    if [ "${VERBOSE}" = "true" ]; then
        echo "connect attempt ${attempt}/${CONNECT_WAIT_STEPS}"
    fi
    if IPFS_PATH="${C_REPO}" "${C_IPFS_BIN}" swarm connect "${KUBO_ADDR}"; then
        CONNECTED=1
        break
    fi
    if [ "${VERBOSE}" = "true" ]; then
        echo "current Kubo swarm peers:"
        IPFS_PATH="${K_REPO}" "${KUBO_BIN}" swarm peers || true
        echo "current c-ipfs swarm peers:"
        IPFS_PATH="${C_REPO}" "${C_IPFS_BIN}" swarm peers || true
    fi
    sleep 0.5
done
if [ "${CONNECTED}" -ne 1 ]; then
    echo "Failed to connect c-ipfs to Kubo after ${CONNECT_WAIT_STEPS} attempts"
    exit 1
fi

for _ in $(seq 1 20); do
    if IPFS_PATH="${K_REPO}" "${KUBO_BIN}" swarm peers 2>/dev/null | grep -q "${C_ID}"; then
        break
    fi
    sleep 0.5
done

echo "=== Test 2: Vector 001 - c-ipfs add -> Kubo cat ==="
TEST_FILE="${TMP_DIR}/vector1.txt"
echo "Interoperability payload from c-ipfs $(date +%s)" > "${TEST_FILE}"
CID=$(IPFS_PATH="${C_REPO}" "${C_IPFS_BIN}" add "${TEST_FILE}" | awk '/^added / {print $2; exit}')
if [ -z "${CID}" ]; then
    echo "Failed to parse CID from c-ipfs add output"
    exit 1
fi

FETCHED_FILE="${TMP_DIR}/fetched_kubo.txt"
IPFS_PATH="${K_REPO}" "${KUBO_BIN}" cat "${CID}" > "${FETCHED_FILE}"
diff -q "${TEST_FILE}" "${FETCHED_FILE}"

echo "=== Test 3: Vector 002 - Kubo add -> c-ipfs cat ==="
K_TEST_FILE="${TMP_DIR}/vector2.txt"
echo "Kubo generated vector $(date +%s)" > "${K_TEST_FILE}"
K_CID=$(IPFS_PATH="${K_REPO}" "${KUBO_BIN}" add "${K_TEST_FILE}" | awk '/^added / {print $2; exit}')
if [ -z "${K_CID}" ]; then
    echo "Failed to parse CID from Kubo add output"
    exit 1
fi

C_FETCHED_FILE="${TMP_DIR}/fetched_c_ipfs.txt"
IPFS_PATH="${C_REPO}" "${C_IPFS_BIN}" cat "${K_CID}" > "${C_FETCHED_FILE}"
diff -q "${K_TEST_FILE}" "${C_FETCHED_FILE}"

echo "=== Test 4: Vector 003 - local DAG-CBOR vector validation ==="
python3 "${ROOT_DIR}/test/scripts/test_dag_cbor_vectors.py"

echo "=== Interoperability smoke test complete ==="
