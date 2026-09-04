#!/usr/bin/env bash
# Local periodic CI runner using act and gh CLI.
# Run this from the repo root, e.g. via cron:
#   0 */6 * * * cd /path/to/c-ipfs && ./scripts/ci_periodic_local.sh >> /tmp/c-ipfs-periodic.log 2>&1

set -euo pipefail

echo "=== c-ipfs periodic local CI run at $(date -Iseconds) ==="

# 1. Run the main CI build in act (ubuntu-latest only by default)
if command -v act >/dev/null 2>&1; then
    echo "--- Running act build ---"
    act -j build --rm 2>&1 || echo "WARN: act build exited non-zero"
else
    echo "SKIP: act not installed (brew install act)"
fi

# 2. Trigger macOS build on GitHub Actions (if gh CLI is authenticated)
if command -v gh >/dev/null 2>&1; then
    echo "--- Triggering macOS CI via gh ---"
    gh workflow run ci.yml \
        --repo "$(git remote get-url origin | sed 's/.*github\.com[:/]//;s/\.git$//')" \
        --ref "$(git branch --show-current)" \
        -f macos_build=true \
        -f clang_build=false 2>&1 || echo "WARN: gh macOS trigger failed"

    echo "--- Triggering Kubo Interop via gh ---"
    gh workflow run kubo-interop.yml \
        --repo "$(git remote get-url origin | sed 's/.*github\.com[:/]//;s/\.git$//')" \
        --ref "$(git branch --show-current)" \
        -f interop_mode=ci \
        -f verbose=false \
        -f kubo_version=v0.43.0 2>&1 || echo "WARN: gh kubo-interop trigger failed"
else
    echo "SKIP: gh not installed (brew install gh)"
fi

echo "=== Done at $(date -Iseconds) ==="
