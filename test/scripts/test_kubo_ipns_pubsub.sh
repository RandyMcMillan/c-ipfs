#!/usr/bin/env bash
set -euo pipefail

KUBO_VERSION="${KUBO_VERSION:-v0.43.0}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

C_IPFS_BIN="${ROOT_DIR}/main/ipfs"
TMP_DIR="$(mktemp -d /tmp/c_ipfs_kubo_interop.XXXXXX)"
C_REPO="${TMP_DIR}/c_ipfs_repo"
K_REPO="${TMP_DIR}/kubo_repo"
KUBO_BIN="${TMP_DIR}/kubo_bin/ipfs"

c_ipfs_pid=""
kubo_pid=""

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

echo "=== Setup: Preparing Environment for IPNS & PubSub Tests ==="
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
IPFS_PATH="${K_REPO}" "${KUBO_BIN}" config --json Pubsub.Enabled true

echo "=== Starting Kubo daemon with PubSub enabled ==="
IPFS_PATH="${K_REPO}" "${KUBO_BIN}" daemon --enable-pubsub-experiment > "${TMP_DIR}/kubo.log" 2>&1 &
kubo_pid=$!

echo "Waiting for Kubo daemon API..."
until IPFS_PATH="${K_REPO}" "${KUBO_BIN}" id >/dev/null 2>&1; do
    sleep 0.5
done

KUBO_ID=$(IPFS_PATH="${K_REPO}" "${KUBO_BIN}" id -f="<id>")
KUBO_ADDR="/ip4/127.0.0.1/tcp/4011/p2p/${KUBO_ID}"
IPFS_PATH="${C_REPO}" "${C_IPFS_BIN}" swarm connect "${KUBO_ADDR}" || true

echo "=== Test 4: IPNS record resolution ==="
IPNS_TEST_FILE="${TMP_DIR}/ipns_target.txt"
echo "IPNS Target Content Payload" > "${IPNS_TEST_FILE}"
TARGET_CID=$(IPFS_PATH="${C_REPO}" "${C_IPFS_BIN}" add -q "${IPNS_TEST_FILE}" | tail -n 1)

C_KEY_NAME="interop-ipns-key"
IPFS_PATH="${C_REPO}" "${C_IPFS_BIN}" key gen "${C_KEY_NAME}" --type=ed25519
KEY_ID=$(IPFS_PATH="${C_REPO}" "${C_IPFS_BIN}" key list -l | grep "${C_KEY_NAME}" | awk '{print $1}')

IPFS_PATH="${C_REPO}" "${C_IPFS_BIN}" name publish --key="${C_KEY_NAME}" "${TARGET_CID}"
RESOLVED_PATH=$(IPFS_PATH="${K_REPO}" "${KUBO_BIN}" name resolve "${KEY_ID}")

if [ "${RESOLVED_PATH}" != "/ipfs/${TARGET_CID}" ]; then
    echo "FAIL: IPNS Resolution Mismatch. Expected /ipfs/${TARGET_CID}, Got ${RESOLVED_PATH}"
    exit 1
fi

echo "=== Test 5: PubSub message relaying ==="
PUBSUB_TOPIC="/c-ipfs/interop/1.0.0"
PUBSUB_MSG="ping_payload_$(date +%s)"
RECEIVED_MSG_FILE="${TMP_DIR}/pubsub_received.txt"

IPFS_PATH="${K_REPO}" "${KUBO_BIN}" pubsub sub "${PUBSUB_TOPIC}" > "${RECEIVED_MSG_FILE}" 2>&1 &
SUB_PID=$!
sleep 1

IPFS_PATH="${C_REPO}" "${C_IPFS_BIN}" pubsub pub "${PUBSUB_TOPIC}" "${PUBSUB_MSG}"
sleep 2

kill "${SUB_PID}" 2>/dev/null || true

grep -q "${PUBSUB_MSG}" "${RECEIVED_MSG_FILE}"

echo "=== Extended Interoperability Test Suite Passed Successfully ==="
