#!/usr/bin/env bash
set -euo pipefail

KUBO_VERSION="${KUBO_VERSION:-v0.43.0}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

C_IPFS_BIN="${ROOT_DIR}/main/ipfs"
MANIFEST="${ROOT_DIR}/test/kubo_interop_vectors.json"
TMP_DIR="$(mktemp -d /tmp/c_ipfs_kubo_interop.XXXXXX)"
C_REPO="${TMP_DIR}/c_ipfs_repo"
K_REPO="${TMP_DIR}/kubo_repo"
KUBO_BIN="${TMP_DIR}/kubo_bin/ipfs"

c_ipfs_pid=""
kubo_pid=""

wait_for_api() {
    local pid="$1"
    local port="$2"
    local label="$3"
    local log_file="$4"
    for _ in $(seq 1 60); do
        if ! kill -0 "${pid}" 2>/dev/null; then
            echo "${label} daemon exited before the API became ready"
            tail -n 50 "${log_file}" || true
            return 1
        fi
        if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.5
    done
    echo "Timed out waiting for ${label} API on port ${port}"
    tail -n 50 "${log_file}" || true
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
KUBO_ID=$(IPFS_PATH="${K_REPO}" "${KUBO_BIN}" id -f="<id>")
KUBO_ADDR="/ip4/127.0.0.1/tcp/4011/p2p/${KUBO_ID}"
echo "Kubo Multiaddr: ${KUBO_ADDR}"
echo "=== Test 1: Swarm connect c-ipfs <-> Kubo ==="
IPFS_PATH="${K_REPO}" "${KUBO_BIN}" swarm connect "${C_PEER_ADDR}" || true

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
