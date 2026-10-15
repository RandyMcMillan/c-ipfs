#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
NOSTRIL_DIR="${REPO_ROOT}/nostril"
SECRET="0000000000000000000000000000000000000000000000000000000000000001"

cd "${NOSTRIL_DIR}"
make -k >/dev/null 2>&1 || make

if ! test -x ./nostril; then
    echo "FAIL: nostril binary was not built"
    exit 1
fi

failures=0
check() {
    if [ "$1" -ne 0 ]; then
        failures=$((failures + 1))
    fi
}

# A signed text note must be accepted by c-ipfs when the event is copied verbatim.
NOTE_JSON="$(./nostril --sec "${SECRET}" --kind 1 --tag t nostr-test-report --content "c-ipfs nostril smoke test")"
printf '%s\n' "$NOTE_JSON"
if ! "${REPO_ROOT}/main/ipfs" nostr verify --event "$NOTE_JSON" | grep -q "signature valid"; then
    echo "FAIL: note signature was not valid"
    failures=$((failures + 1))
fi

command -v python3 >/dev/null || { echo "FAIL: python3 required"; exit 1; }
python3 - "$NOTE_JSON" <<'PY'
import json, sys
event = json.loads(sys.argv[1])
assert event["kind"] == 1
assert event["sig"] and len(event["sig"]) == 128
assert any(tag[0] == "t" and len(tag) >= 2 and tag[1] == "nostr-test-report" for tag in event["tags"])
PY
check "$?"

# NIP-34 style patch event generation.
PATCH_JSON="$(./nostril --sec "${SECRET}" --kind 1617 \
    --tag a "30617:79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798:c-ipfs" \
    --tag subject "test nostril nip34 patch" \
    --tag r "0000000000000000000000000000000000000000000000000000000000000001" \
    --tag p "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798" \
    --tag t "patch" \
    --content "diff --git a/test.txt b/test.txt")"
python3 - "$PATCH_JSON" <<'PY'
import json, sys
event = json.loads(sys.argv[1])
assert event["kind"] == 1617
tags = {tag[0] for tag in event["tags"] if tag}
assert {"a", "subject", "r", "p", "t"} <= tags
assert event["content"].startswith("diff --git")
PY
check "$?"

if [ "$failures" -ne 0 ]; then
    echo "FAIL: nostril integration tests failed"
    exit 1
fi

echo "nostril integration tests passed"
