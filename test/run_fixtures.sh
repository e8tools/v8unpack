#!/usr/bin/env bash
# Black-box -parse fixtures: in.cf -> tree, compare with expected/

set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

if [ -z "${1:-}" ]; then
	UNPACK="$SCRIPT_DIR/../bin/Release/v8unpack"
else
	UNPACK=$1
fi

# Resolve relative path against the caller's cwd, then fall back to script dir.
if [ ! -x "$UNPACK" ] && [ ! -f "$UNPACK" ]; then
	if [ -f "$SCRIPT_DIR/$UNPACK" ]; then
		UNPACK="$SCRIPT_DIR/$UNPACK"
	elif [ -f "$SCRIPT_DIR/../$UNPACK" ]; then
		UNPACK="$SCRIPT_DIR/../$UNPACK"
	fi
fi

if [ ! -f "$UNPACK" ]; then
	echo "v8unpack not found: $UNPACK"
	exit 1
fi

FIXTURES="$SCRIPT_DIR/fixtures"

if [ ! -d "$FIXTURES" ]; then
	echo "No fixtures directory: $FIXTURES"
	exit 1
fi

failed=0
passed=0

for case_dir in "$FIXTURES"/*/; do
	[ -d "$case_dir" ] || continue
	name=$(basename "$case_dir")
	in_file="$case_dir/in.cf"
	expected="$case_dir/expected"

	if [ ! -f "$in_file" ] || [ ! -d "$expected" ]; then
		echo "SKIP $name (missing in.cf or expected/)"
		continue
	fi

	out_dir=$(mktemp -d "${TMPDIR:-/tmp}/v8unpack-fix-XXXXXX")
	if ! "$UNPACK" -parse "$in_file" "$out_dir" >/dev/null; then
		echo "FAIL $name (parse returned error)"
		failed=$((failed + 1))
		rm -rf "$out_dir"
		continue
	fi

	if diff -r "$expected" "$out_dir" >/dev/null; then
		echo "PASS $name"
		passed=$((passed + 1))
	else
		echo "FAIL $name"
		diff -r "$expected" "$out_dir" || true
		failed=$((failed + 1))
	fi
	rm -rf "$out_dir"
done

echo "Fixtures: $passed passed, $failed failed"
if [ "$failed" -ne 0 ]; then
	exit 1
fi
if [ "$passed" -eq 0 ]; then
	echo "No fixtures ran"
	exit 1
fi
exit 0
