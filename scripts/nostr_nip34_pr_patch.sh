#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NOSTRIL_DIR="${REPO_ROOT}/nostril"
NOSTRIL_BIN="${NOSTRIL_DIR}/nostril"
PUBLISH=0
OUTPUT_DIR="${TMPDIR:-/tmp}/c-ipfs-nostr-nip34"
BASE_SHA="${GITHUB_BASE_SHA:-}"
HEAD_SHA="${GITHUB_HEAD_SHA:-}"
PR_TITLE="${PR_TITLE:-ipfs patch}"
MAX_PATCH_BYTES="${MAX_PATCH_BYTES:-24576}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --publish) PUBLISH=1 ;;
        --output)
            shift; OUTPUT_DIR="$1" ;;
        --base-sha)
            shift; BASE_SHA="$1" ;;
        --head-sha)
            shift; HEAD_SHA="$1" ;;
        --title)
            shift; PR_TITLE="$1" ;;
        *)
            echo "Usage: $0 [--publish] [--base-sha sha] [--head-sha sha] [--title title] [--output dir]" >&2
            exit 2
            ;;
    esac
    shift
done

mkdir -p "${OUTPUT_DIR}"
REPO_EVENT_FILE="${OUTPUT_DIR}/nip34-repo-event.json"
PATCH_EVENT_FILE="${OUTPUT_DIR}/nip34-patch-event.json"
PATCH_FILE="${OUTPUT_DIR}/patch.txt"

if [ -z "${HEAD_SHA}" ]; then
    HEAD_SHA="HEAD"
fi
HEAD_SHA="$(git -C "${REPO_ROOT}" rev-parse "${HEAD_SHA}")"
if [ -z "${BASE_SHA}" ]; then
    if git -C "${REPO_ROOT}" rev-parse "${HEAD_SHA}^" >/dev/null 2>&1; then
        BASE_SHA="$(git -C "${REPO_ROOT}" rev-parse "${HEAD_SHA}^")"
    else
        echo "No base SHA available for patch event" >&2
        exit 2
    fi
fi
BASE_SHA="$(git -C "${REPO_ROOT}" rev-parse "${BASE_SHA}")"

git -C "${REPO_ROOT}" diff --binary "${BASE_SHA}".."${HEAD_SHA}" > "${PATCH_FILE}"
TRUNCATED_PATCH="$(head -c "${MAX_PATCH_BYTES}" "${PATCH_FILE}")"
SUBJECT="$(git -C "${REPO_ROOT}" log -1 --format=%s "${HEAD_SHA}")"
if [ -n "${PR_TITLE}" ] && [ "${PR_TITLE}" != "ipfs patch" ]; then
    SUBJECT="${PR_TITLE}"
fi
if [ -z "${SUBJECT}" ]; then
    SUBJECT="patch for c-ipfs"
fi

SECRET="${NOSTR_SECKEY:-}"
if [ -n "${SECRET}" ]; then
    if ! printf '%s' "${SECRET}" | grep -Eq '^[0-9a-fA-F]{64}$'; then
        echo "NOSTR_SECKEY must be 64 hexadecimal characters" >&2
        exit 2
    fi
else
    SECRET="0000000000000000000000000000000000000000000000000000000000000001"
fi

if [ ! -x "${NOSTRIL_BIN}" ]; then
    make -C "${NOSTRIL_DIR}" >/dev/null
fi

# Publish a repository identifier before the patch.  NIP-34 patch events need
# a NIP-34 repository reference in an "a" tag.  Using one secret key keeps
# the repository coordinator and patch sender identity consistent.
REPO_EVENT_JSON="$("${NOSTRIL_BIN}" --sec "${SECRET}" \
    --kind 30617 \
    --tag d c-ipfs \
    --tag name c-ipfs \
    --tag description "c-ipfs git repository announced by CI" \
    --tag web "https://github.com/RandyMcMillan/c-ipfs" \
    --tag t "nip34" \
    --content "c-ipfs NIP-34 repository announce")"
REPO_PUBKEY="$(printf '%s' "${REPO_EVENT_JSON}" | python3 -c 'import json,sys; print(json.load(sys.stdin)["pubkey"])')"
REPO_COORD="30617:${REPO_PUBKEY}:c-ipfs"
printf '%s\n' "${REPO_EVENT_JSON}" > "${REPO_EVENT_FILE}"

PATCH_EVENT_JSON="$("${NOSTRIL_BIN}" --sec "${SECRET}" \
    --kind 1617 \
    --tag a "${REPO_COORD}" \
    --tag subject "${SUBJECT}" \
    --tag r "${HEAD_SHA}" \
    --tag p "${REPO_PUBKEY}" \
    --tag t "patch" \
    --tag t "pull-request" \
    --content "${TRUNCATED_PATCH}")"
printf '%s\n' "${PATCH_EVENT_JSON}" > "${PATCH_EVENT_FILE}"

cd "${NOSTRIL_DIR}"
for EVENT_FILE in "${REPO_EVENT_FILE}" "${PATCH_EVENT_FILE}"; do
    EVENT_JSON="$(cat "${EVENT_FILE}")"
    if ! "${REPO_ROOT}/main/ipfs" nostr verify --event "${EVENT_JSON}" | grep -q "signature valid"; then
        echo "Generated NIP-34 event did not verify: ${EVENT_FILE}" >&2
        exit 1
    fi
done

RELAY_ARGS=()
for relay in ${NOSTR_RELAYS:-wss://relay.damus.io wss://relay.nostr.info}; do
    RELAY_ARGS+=(--relay "$relay")
done

if [ "${PUBLISH}" = "1" ]; then
    NOSTR_PUBLISH_MODE=publish python3 "${REPO_ROOT}/scripts/nostr_publish_event.py" --event "${REPO_EVENT_FILE}" "${RELAY_ARGS[@]}" || true
    NOSTR_PUBLISH_MODE=publish python3 "${REPO_ROOT}/scripts/nostr_publish_event.py" --event "${PATCH_EVENT_FILE}" "${RELAY_ARGS[@]}" || true
    echo "Generated and published NIP-34 repository and patch events"
else
    python3 "${REPO_ROOT}/scripts/nostr_publish_event.py" --dry-run --event "${REPO_EVENT_FILE}" "${RELAY_ARGS[@]}" >/dev/null || true
    python3 "${REPO_ROOT}/scripts/nostr_publish_event.py" --dry-run --event "${PATCH_EVENT_FILE}" "${RELAY_ARGS[@]}" >/dev/null || true
    echo "Generated and verified NIP-34 repository and patch events"
fi
