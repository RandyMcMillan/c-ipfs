#!/usr/bin/env python3
"""
DAG-CBOR Tag 42 Link Test Vector Comparison

Builds the scripts/dag_cbor_encode CLI utility and verifies its output
against known-correct test vectors.
"""

import os
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
ENCODER_SRC = os.path.join(PROJECT_ROOT, "scripts", "dag_cbor_encode.c")
ENCODER_BIN = os.path.join(PROJECT_ROOT, "scripts", "dag_cbor_encode")

# Test vectors: (description, hex_input, expected_hex_output)
TEST_VECTORS = [
    (
        "sha256 multihash (34 bytes)",
        "01701220e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "d82a58250001701220e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    ),
    (
        "small 4-byte payload (<24 byte-string)",
        "aabbccdd",
        "d82a4500aabbccdd",
    ),
    (
        "identity multihash (sha256, 32-byte digest, standard CID)",
        "1220e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "d82a5823001220e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    ),
]


def build_encoder():
    """Compile the dag_cbor_encode utility."""
    if not os.path.exists(ENCODER_SRC):
        print(f"FAIL: encoder source not found: {ENCODER_SRC}")
        return False

    # Clean previous build
    if os.path.exists(ENCODER_BIN):
        os.remove(ENCODER_BIN)

    cmd = ["gcc", "-Wall", "-Wextra", "-O2", "-std=c99", "-o", ENCODER_BIN, ENCODER_SRC]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAIL: compilation failed:\n{result.stderr}")
        return False
    return True


def run_encoder(hex_input):
    """Run the encoder and return stripped stdout."""
    result = subprocess.run(
        [ENCODER_BIN, hex_input],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return None, result.stderr
    return result.stdout.strip(), None


def run_test_vectors():
    passed = 0
    failed = 0

    for desc, hex_input, expected in TEST_VECTORS:
        output, err = run_encoder(hex_input)
        if err:
            print(f"FAIL [{desc}]: encoder error: {err}")
            failed += 1
            continue

        if output.lower() == expected.lower():
            print(f"PASS [{desc}]")
            passed += 1
        else:
            print(f"FAIL [{desc}]")
            print(f"  input:    {hex_input}")
            print(f"  expected: {expected}")
            print(f"  actual:   {output}")
            failed += 1

    return passed, failed


def test_invalid_inputs():
    """Verify the encoder rejects bad input."""
    passed = 0
    failed = 0

    invalid_cases = [
        ("odd-length hex", "abc"),
        ("non-hex chars", "zzzz"),
        ("empty string", ""),
    ]

    for desc, bad_input in invalid_cases:
        result = subprocess.run(
            [ENCODER_BIN, bad_input],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print(f"PASS [reject {desc}]")
            passed += 1
        else:
            print(f"FAIL [reject {desc}]: should have returned non-zero")
            failed += 1

    return passed, failed


def main():
    print("=" * 60)
    print("  DAG-CBOR Tag 42 Link — Test Vector Comparison")
    print("=" * 60)
    print()

    print("Building encoder...")
    if not build_encoder():
        sys.exit(1)
    print()

    print("Running specification test vectors...")
    p1, f1 = run_test_vectors()
    print()

    print("Running invalid-input rejection tests...")
    p2, f2 = test_invalid_inputs()
    print()

    total_pass = p1 + p2
    total_fail = f1 + f2

    print("=" * 60)
    print(f"Results: {total_pass} passed, {total_fail} failed")
    print("=" * 60)

    sys.exit(0 if total_fail == 0 else 1)


if __name__ == "__main__":
    main()
