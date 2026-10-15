#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SECRET="0000000000000000000000000000000000000000000000000000000000000001"

cd "${REPO_ROOT}/nostril"
make -k >/dev/null 2>&1 || make

FAILURES=0
PATCH_JSON="$(./nostril --sec "${SECRET}" --kind 1617 \
    --tag a "30617:79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798:c-ipfs" \
    --tag subject "test nostril nip34 patch" \
    --tag r "0000000000000000000000000000000000000000000000000000000000000001" \
    --tag p "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798" \
    --tag t "patch" \
    --content "diff --git a/test.txt b/test.txt")"
python3 - "$PATCH_JSON" <<'PY'
import json
import sys

event = json.loads(sys.argv[1])
assert event["kind"] == 1617
tags = {tag[0] for tag in event["tags"] if tag}
assert {"a", "subject", "r", "p", "t"} <= tags
assert event["content"].startswith("diff --git")
assert event["pubkey"]
assert event["sig"]
PY
check=$?
if [ $check -ne 0 ]; then
    FAILURES=1
fi

if [ "$FAILURES" -ne 0 ]; then
    echo "FAIL: NIP-34 event generation failed"
    exit 1
fi

echo "NIP-34 event generation passed"
