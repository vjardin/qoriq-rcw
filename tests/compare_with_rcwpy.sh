#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2026 Free Mobile - Vincent Jardin
#
# Cross-validation: compile all pbiformat=2 boards with both rcw.py and
# qoriq-rcw, then compare the binary outputs byte-for-byte.
#
# This script is for development validation only.
#
# Locating the upstream NXP RCW reference tree (rcw.py + per-board .rcw):
#   1. RCW_REF_DIR environment variable (highest priority)
#   2. -Drcw-ref-dir=<path> meson option (forwarded to the test as
#      RCW_REF_DIR)
#   3. ../../rcw relative to this script (fallback)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RCW_DIR="${RCW_REF_DIR:-${SCRIPT_DIR}/../../rcw}"
RCW_PY="${RCW_DIR}/rcw.py"
QORIQ_RCW="${MESON_BUILD_ROOT:-${SCRIPT_DIR}/../build}/qoriq-rcw"
TMPDIR=$(mktemp -d)

trap 'rm -rf "$TMPDIR"' EXIT

if [ ! -x "$QORIQ_RCW" ]; then
	echo "ERROR: qoriq-rcw not found at $QORIQ_RCW" >&2
	exit 1
fi

if ! command -v python3 &>/dev/null; then
	echo "SKIP: python3 not found" >&2
	exit 77
fi

if [ ! -d "$RCW_DIR" ] || [ ! -f "$RCW_PY" ]; then
	echo "ERROR: NXP rcw.py reference tree not found." >&2
	echo "       Expected directory: $RCW_DIR" >&2
	echo "       Expected script:    $RCW_PY" >&2
	echo "" >&2
	echo "This cross-validation test requires the upstream NXP RCW" >&2
	echo "repository (rcw.py and the per-board .rcw source files)." >&2
	echo "Clone it from:" >&2
	echo "" >&2
	echo "    git clone git@github.com:nxp-qoriq/rcw.git" >&2
	echo "    # or: git clone https://github.com/nxp-qoriq/rcw.git" >&2
	echo "" >&2
	echo "Then point this test at the clone using one of:" >&2
	echo "" >&2
	echo "  1. At meson configure time:" >&2
	echo "       meson setup build -Drcw-ref-dir=/path/to/rcw" >&2
	echo "" >&2
	echo "  2. At test time, via environment variable:" >&2
	echo "       RCW_REF_DIR=/path/to/rcw meson test -C build" >&2
	echo "" >&2
	echo "  3. By cloning into the default location:" >&2
	echo "       (cd $(cd "$SCRIPT_DIR/../.." && pwd) \\" >&2
	echo "        && git clone https://github.com/nxp-qoriq/rcw.git)" >&2
	exit 1
fi

PASS=0
FAIL=0
SKIP=0

# Boards that use pbiformat=2
BOARDS="ls1028ardb ls1028aqds
        ls1088ardb ls1088aqds
        ls2088ardb ls2088ardb_rev1.1 ls2088aqds
        lx2160ardb lx2160ardb_rev2 lx2160aqds lx2160aqds_rev2
        lx2162aqds"

for board in $BOARDS; do
	board_dir="${RCW_DIR}/${board}"
	if [ ! -d "$board_dir" ]; then
		echo "SKIP: $board (directory not found)"
		SKIP=$((SKIP + 1))
		continue
	fi

	for rcw_file in $(cd "$board_dir" && find . -name '*.rcw' -path '*/*/*.rcw' | sort); do
		rel="${board}/${rcw_file#./}"
		py_out="$TMPDIR/py.bin"
		c_out="$TMPDIR/c.bin"

		py_cmd="(cd $board_dir && python3 $RCW_PY -i $rcw_file -o $py_out)"
		c_cmd="(cd $board_dir && $QORIQ_RCW -i $rcw_file -o $c_out)"

		echo "[$rel]"
		echo "  PY:  $py_cmd"
		echo "  C:   $c_cmd"

		# Compile with rcw.py
		if ! (cd "$board_dir" && python3 "$RCW_PY" -i "$rcw_file" -o "$py_out") 2>/dev/null; then
			echo "  SKIP: rcw.py failed"
			SKIP=$((SKIP + 1))
			continue
		fi

		# Compile with qoriq-rcw
		if ! (cd "$board_dir" && "$QORIQ_RCW" -i "$rcw_file" -o "$c_out") 2>/dev/null; then
			echo "  FAIL: qoriq-rcw failed"
			FAIL=$((FAIL + 1))
			continue
		fi

		# Compare
		if cmp -s "$py_out" "$c_out"; then
			echo "  PASS"
			PASS=$((PASS + 1))
		else
			echo "  FAIL: output differs"
			FAIL=$((FAIL + 1))
		fi

		rm -f "$py_out" "$c_out"
	done
done

echo ""
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"

if [ $FAIL -gt 0 ]; then
	exit 1
fi
exit 0
