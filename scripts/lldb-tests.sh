#!/usr/bin/env bash
set -u
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TEST_DIR="$ROOT_DIR/test"
TEST_BINARY="$TEST_DIR/test_ipfs"
TEST_SOURCE="$TEST_DIR/testit.c"
MAKE_BIN="${MAKE_BIN:-make}"
LLDB_BIN="${LLDB_BIN:-lldb}"

build_tests() {
	"$MAKE_BIN" -C "$TEST_DIR" all
}

suite_tests() {
	sed -n 's/.*add_test("\([^"]*\)",[^,]*, 1).*/\1/p' "$TEST_SOURCE"
}

all_tests() {
	sed -n 's/.*add_test("\([^"]*\)",.*/\1/p' "$TEST_SOURCE"
}

known_test() {
	local wanted="$1"
	grep -Fq "add_test(\"$wanted\"," "$TEST_SOURCE"
}

run_test() {
	local test_name="$1"
	"$TEST_BINARY" "$test_name"
}

run_test_under_lldb() {
	local test_name="$1"
	"$LLDB_BIN" --batch -o "run $test_name" -o "bt all" -- "$TEST_BINARY"
}

main() {
	if ! build_tests; then
		return 1
	fi

	local tests=()
	local requested=("$@")
	local test_name

	if [ "${#requested[@]}" -gt 0 ]; then
		for test_name in "${requested[@]}"; do
			if ! known_test "$test_name"; then
				printf 'Unknown test: %s\n' "$test_name" >&2
				return 1
			fi
			tests+=("$test_name")
		done
	else
		while IFS= read -r test_name; do
			tests+=("$test_name")
		done < <(suite_tests)
	fi

	if [ "${#tests[@]}" -eq 0 ]; then
		printf 'No tests selected.\n' >&2
		return 1
	fi

	local failed_tests=()
	for test_name in "${tests[@]}"; do
		printf '=== %s ===\n' "$test_name"
		if run_test "$test_name"; then
			continue
		else
			status=$?
			if [ "$status" -ge 128 ]; then
				printf 'Test crashed with exit code %d; rerunning under lldb.\n' "$status" >&2
				run_test_under_lldb "$test_name"
			fi
			failed_tests+=("$test_name:$status")
		fi
	done

	if [ "${#failed_tests[@]}" -eq 0 ]; then
		printf 'All %d selected test(s) passed.\n' "${#tests[@]}"
		return 0
	fi

	printf '\nFailed tests:\n' >&2
	for test_name in "${failed_tests[@]}"; do
		printf '  %s\n' "$test_name" >&2
	done
	return 1
}

main "$@"
