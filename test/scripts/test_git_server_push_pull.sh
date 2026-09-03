#!/usr/bin/env bash
set -euo pipefail

VERBOSE="${VERBOSE:-false}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

TMP_DIR="$(mktemp -d "${ROOT_DIR}/tmp/git_server_push_pull.XXXXXX")"
IMAGE_TAG="c-ipfs-git-server-test"
CONTAINER_NAME="c-ipfs-git-server-test"
KEYS_DIR="${TMP_DIR}/keys"
REPOS_DIR="${TMP_DIR}/repos"
SSH_DIR="${TMP_DIR}/ssh"
WORK_DIR="${TMP_DIR}/work"
REMOTE_URL="ssh://git@127.0.0.1:2222/git-server/repos/push-pull.git"
GIT_SSH_COMMAND="ssh -i ${SSH_DIR}/id_ed25519 -o IdentitiesOnly=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"

if [ "${VERBOSE}" = "true" ]; then
    set -x
fi

cleanup() {
    if docker ps -a --format '{{.Names}}' | grep -qx "${CONTAINER_NAME}"; then
        docker rm -f "${CONTAINER_NAME}" >/dev/null 2>&1 || true
    fi
    if [ -d "${TMP_DIR}" ]; then
        # Docker may have created root-owned files in the bind mount;
        # use sudo when available (GitHub Actions Ubuntu runners have passwordless sudo)
        sudo rm -rf "${TMP_DIR}" 2>/dev/null || rm -rf "${TMP_DIR}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

mkdir -p "${ROOT_DIR}/tmp"
mkdir -p "${KEYS_DIR}" "${REPOS_DIR}" "${SSH_DIR}" "${WORK_DIR}"

docker build --platform linux/amd64 -t "${IMAGE_TAG}" "${ROOT_DIR}/docker/git-server" >/dev/null

ssh-keygen -q -t ed25519 -N "" -f "${SSH_DIR}/id_ed25519"
cp "${SSH_DIR}/id_ed25519.pub" "${KEYS_DIR}/client.pub"

git init --bare "${REPOS_DIR}/push-pull.git" >/dev/null

docker rm -f "${CONTAINER_NAME}" >/dev/null 2>&1 || true
docker run --platform linux/amd64 -d --name "${CONTAINER_NAME}" -p 127.0.0.1:2222:22 \
    -v "${KEYS_DIR}:/git-server/keys" \
    -v "${REPOS_DIR}:/git-server/repos" \
    "${IMAGE_TAG}" >/dev/null

wait_for_git_server() {
    local attempts=60
    local i
    for i in $(seq 1 "${attempts}"); do
        if GIT_SSH_COMMAND="${GIT_SSH_COMMAND}" git ls-remote "${REMOTE_URL}" >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
    done
    echo "Timed out waiting for git server"
    docker logs "${CONTAINER_NAME}" || true
    return 1
}

wait_for_git_server

git init "${WORK_DIR}/source" >/dev/null
cd "${WORK_DIR}/source"
git config user.email "ci@example.com"
git config user.name "CI Test"
echo "git push/pull smoke test" > payload.txt
git add payload.txt
git commit -m "initial commit" >/dev/null
git branch -M main
git remote add origin "${REMOTE_URL}"
GIT_SSH_COMMAND="${GIT_SSH_COMMAND}" git push -u origin main >/dev/null

GIT_SSH_COMMAND="${GIT_SSH_COMMAND}" git clone -b main "${REMOTE_URL}" "${WORK_DIR}/clone" >/dev/null
cmp payload.txt "${WORK_DIR}/clone/payload.txt"

echo "updated content" >> payload.txt
git add payload.txt
git commit -m "update payload" >/dev/null
GIT_SSH_COMMAND="${GIT_SSH_COMMAND}" git push origin main >/dev/null

cd "${WORK_DIR}/clone"
GIT_SSH_COMMAND="${GIT_SSH_COMMAND}" git fetch origin main >/dev/null
git reset --hard FETCH_HEAD >/dev/null
cmp "${WORK_DIR}/source/payload.txt" "${WORK_DIR}/clone/payload.txt"

echo "Git server push/pull smoke test passed"
