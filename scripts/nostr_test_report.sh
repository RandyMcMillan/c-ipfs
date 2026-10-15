#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NOSTRIL_DIR="${REPO_ROOT}/nostril"
NOSTRIL_BIN="${NOSTRIL_DIR}/nostril"
PUBLISH=0
RUN_TESTS=1
OUTPUT_DIR="${TMPDIR:-/tmp}/c-ipfs-nostr"
TEST_NAMES=("test_api_gateway" "test_namesys_verify_ipns_record_rsa" "test_ffi_unixfs_add_bytes_with_progress")
if [ -n "${NOSTR_TESTS:-}" ]; then read -ra TEST_NAMES <<< "${NOSTR_TESTS}"; fi
RELAYS="${NOSTR_RELAYS:-wss://relay.damus.io wss://relay.nostr.info}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --run-tests) RUN_TESTS=1 ;;
        --skip-tests) RUN_TESTS=0 ;;
        --publish) PUBLISH=1 ;;
        --relays)
            shift; RELAYS="$1" ;;
        --output)
            shift; OUTPUT_DIR="$1" ;;
        --publish-relays)
            shift; RELAYS="$1" ;;
        *)
            echo "Usage: $0 [--run-tests|--skip-tests] [--publish] [--relays \"...\"] [--output dir]" >&2
            exit 2
            ;;
    esac
    shift
done

mkdir -p "${OUTPUT_DIR}"
TEST_LOG="${OUTPUT_DIR}/test-results.log"
CONTENT_FILE="${OUTPUT_DIR}/report-content.txt"
EVENT_FILE="${OUTPUT_DIR}/test-report-event.json"

: > "${TEST_LOG}"
test_status=0
if [ "$RUN_TESTS" -eq 1 ]; then
    (
        cd "${REPO_ROOT}/test"
        if ! make test_ipfs >/dev/null 2>&1; then
            make >/dev/null 2>&1 || true
        fi
    )
    set +e
    test_status=$(
        cd "${REPO_ROOT}/test"
        python3 - "$TEST_LOG" "${TEST_NAMES[@]}" <<'PYTHON_RUNNER'
import os
import subprocess
import sys

log_path = sys.argv[1]
names = sys.argv[2:]
with open(log_path, "w", encoding="utf-8") as handle:
    try:
        proc = subprocess.run(["./test_ipfs", *names], stdout=handle, stderr=subprocess.STDOUT, timeout=290)
        raise SystemExit(proc.returncode)
    except subprocess.TimeoutExpired:
        handle.write("\nTimed out running test subset\n")
        raise SystemExit(124)
PYTHON_RUNNER
    )
    set -e
fi

passed=$(grep -c ' success!' "${TEST_LOG}" || true)
failed=$(grep -c '^\*\* Uh oh!' "${TEST_LOG}" || true)
total=$((passed + failed))
summary="c-ipfs focused tests: passed=${passed} failed=${failed} total=${total} exit=${test_status}"
export TEST_SUMMARY="${summary}"

if [ ! -x "${NOSTRIL_BIN}" ]; then
    make -C "${NOSTRIL_DIR}" >/dev/null
fi


python3 - "${TEST_LOG}" "${CONTENT_FILE}" <<'PYTHON_GENERATOR'
import os
import sys

log_path, out_path = sys.argv[1], sys.argv[2]
try:
    with open(log_path, "r", encoding="utf-8", errors="replace") as handle:
        log = handle.read()
except FileNotFoundError:
    log = ""

if len(log) > 8000:
    log = log[-8000:]

text = [
    "c-ipfs nostr test report",
    "git-head: " + os.environ.get("GIT_SHA", "unknown"),
    "results: " + os.environ.get("TEST_SUMMARY", ""),
    "log-tail:",
    log,
]
with open(out_path, "w", encoding="utf-8") as handle:
    handle.write("\n".join(text))
PYTHON_GENERATOR

CONTENT="$(cat "${CONTENT_FILE}")"
RELAY_ARGS=()
for relay in ${RELAYS}; do
    RELAY_ARGS+=(--relay "$relay")
done

SECRET_ARGS=()
if [ -n "${NOSTR_SECKEY:-}" ]; then
    if ! printf '%s' "${NOSTR_SECKEY}" | grep -Eq '^[0-9a-fA-F]{64}$'; then
        echo "NOSTR_SECKEY must be 64 hexadecimal characters" >&2
        exit 2
    fi
    SECRET_ARGS=(--sec "${NOSTR_SECKEY}")
fi

EVENT_JSON="$("${NOSTRIL_BIN}" "${SECRET_ARGS[@]}" --kind 1 \
    --tag t nostr-test-report \
    --tag t c-ipfs \
    --content "${CONTENT}")"
printf '%s\n' "${EVENT_JSON}" > "${EVENT_FILE}"

cd "${NOSTRIL_DIR}"
if ! ../main/ipfs nostr verify --event "${EVENT_JSON}" | grep -q "signature valid"; then
    echo "Generated test report event did not verify" >&2
    exit 1
fi

if [ "$PUBLISH" = "1" ]; then
    if python3 "${REPO_ROOT}/scripts/nostr_publish_event.py" --event "${EVENT_FILE}" "${RELAY_ARGS[@]}"; then
        echo "Published nostr test report to ${RELAYS}"
    else
        echo "Warning: nostr test report was not accepted by any relay" >&2
    fi
else
    python3 "${REPO_ROOT}/scripts/nostr_publish_event.py" --dry-run --event "${EVENT_FILE}" "${RELAY_ARGS[@]}" || true
fi

echo "${summary}"
exit 0
