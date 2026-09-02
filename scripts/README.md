===============================================================================
                    DAG-CBOR Tag 42 Link Encoder CLI
===============================================================================

A lightweight, standalone C utility that encodes raw binary multihashes and
CIDs into IPLD-compliant DAG-CBOR Tag 42 Link byte strings according to the
official IPLD DAG-CBOR Specification:
https://github.com/ipld/specs/tree/master/blocklayer/codecs/dag-cbor

-------------------------------------------------------------------------------
1. OVERVIEW
-------------------------------------------------------------------------------

In IPLD's DAG-CBOR codec, links referencing other CIDs are encoded using
CBOR Tag 42 (0xD8 0x2A) wrapping a byte string prefixed with a single
multibase binary tag (0x00).

Structure:
  [0xD8, 0x2A] + [CBOR Major Type 2 Byte String Header] + [0x00] + [Binary CID]

-------------------------------------------------------------------------------
2. COMPILATION
-------------------------------------------------------------------------------

Build the executable using gcc or clang:

  gcc -Wall -Wextra -O2 dag_cbor_encode.c -o dag_cbor_encode

Or via the project Makefile:

  make scripts

-------------------------------------------------------------------------------
3. USAGE & FLAGS
-------------------------------------------------------------------------------

Usage:
  ./dag_cbor_encode <hex_multihash_or_cid_bytes> [FLAGS]

Flags:
  (Default)     Outputs the raw hex string of the encoded DAG-CBOR link.
  --formatted   Outputs a human-readable, formatted byte array with byte counts.
  --binary      Emits raw binary stream directly to stdout.

Examples:

  * Default Hex Output:
    ./dag_cbor_encode 01701220e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    Output: d82a58250001701220e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855

  * Formatted Byte Array Output:
    ./dag_cbor_encode 01701220e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 --formatted
    Output:
    Formatted Bytes (41 bytes):
    0xD8 0x2A 0x58 0x25 0x00 0x01 0x70 0x12 0x20 0xE3 0xB0 0xC4 0x42 0x98 0xFC
    0x1C 0x14 0x9A 0xFB 0xF4 0xC8 0x99 0x6F 0xB9 0x24 0x27 0xAE 0x41 0xE4 0x64
    0x9B 0x93 0x4C 0xA4 0x95 0x99 0x1B 0x78 0x52 0xB8 0x55

  * Save Binary Payload to File:
    ./dag_cbor_encode 01701220e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 --binary > link.cbor

-------------------------------------------------------------------------------
4. SPECIFICATION TEST VECTORS
-------------------------------------------------------------------------------

Vector 1: Standard CIDv1 Raw (SHA2-256)
  String CID: bafkreifjjcie6lypi6ny7amxnfftagclb26yan63gah9ib223vda
  Raw Binary CID (Hex Input):
    01701220e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
  Expected DAG-CBOR Output (Hex):
    d82a58250001701220e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855

  Structural Breakdown:
    - d8 2a : CBOR Major Type 6 (Tag), Value 42
    - 58 25 : CBOR Major Type 2 (Byte String), length 0x25 (37 bytes)
    - 00    : Multibase Binary Tag (0x00)
    - 01701220e3b0... : 36-byte CIDv1 Payload (0x01 raw codec + 0x70 sha2-256 + 32-byte hash)

Vector 2: Short Multihash Payload (< 24 Bytes Payload Header)
  Raw Multihash Input (18 Bytes Hex):
    12100102030405060708090a0b0c0d0e0f10
  Expected DAG-CBOR Output (Hex):
    d82a530012100102030405060708090a0b0c0d0e0f10

  Structural Breakdown:
    - d8 2a : Tag 42
    - 53    : Inline Byte String Header (0x40 | 19 bytes total payload)
    - 00    : Multibase Binary Tag
    - 1210... : 18-byte Multihash payload

Vector 3: Medium CIDv1 Dag-PB Payload
  String CID: bafybeicg22634333...
  Raw Binary CID (36 Bytes Hex):
    017012200123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
  Expected DAG-CBOR Output (Hex):
    d82a582500017012200123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef

-------------------------------------------------------------------------------
5. AUTOMATED VERIFICATION SCRIPT
-------------------------------------------------------------------------------

Save as test.sh and run with `bash test.sh`:

  #!/usr/bin/env bash
  set -euo pipefail

  TEST_IN="01701220e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
  EXPECTED="d82a58250001701220e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"

  RESULT=$(./dag_cbor_encode "$TEST_IN")

  if [ "$RESULT" = "$EXPECTED" ]; then
      echo "SUCCESS: DAG-CBOR Tag 42 encoding matches canonical IPLD spec."
  else
      echo "ERROR: Vector mismatch!"
      echo "Got:      $RESULT"
      echo "Expected: $EXPECTED"
      exit 1
  fi

-------------------------------------------------------------------------------
6. PYTHON TEST SUITE
-------------------------------------------------------------------------------

A comprehensive Python test suite is provided at:

  test/scripts/test_dag_cbor_vectors.py

It compiles the encoder, runs multiple specification vectors, and verifies
invalid-input rejection. Run it from the project root:

  python3 test/scripts/test_dag_cbor_vectors.py

===============================================================================
