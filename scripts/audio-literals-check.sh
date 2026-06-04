#!/usr/bin/env bash
# audio-literals-check.sh — fail CI if source-grep extraction of
# sdl2_audio_play() literals drifts from k_known_literals[] in
# tests/test_audio_name_validation.c.
#
# Implements peer review N1: the manually-maintained list is the
# weak link in Component 3b; this lint enforces it.

set -euo pipefail

cd "$(dirname "$0")/.."

actual_file=$(mktemp)
expected_file=$(mktemp)
trap "rm -f '$actual_file' '$expected_file'" EXIT

scripts/audio-literals.sh > "$actual_file"

# Extract literals from k_known_literals[] array.  Grabs every
# quoted string between `static const char *const k_known_literals[]`
# and the terminating NULL.
awk '
    /static const char \*const k_known_literals\[\]/ { in_arr = 1; next }
    in_arr {
        while (match($0, /"[^"]+"/)) {
            print substr($0, RSTART+1, RLENGTH-2)
            $0 = substr($0, RSTART+RLENGTH)
        }
        if (/NULL/) { in_arr = 0 }
    }
' tests/test_audio_name_validation.c | sort -u > "$expected_file"

if ! diff -u "$expected_file" "$actual_file"; then
    echo
    echo "ERROR: audio literals in source drift from k_known_literals[]." >&2
    echo "  - Lines starting with '-' are in the test array but missing from source." >&2
    echo "  - Lines starting with '+' are in source but missing from the test array." >&2
    echo "Update tests/test_audio_name_validation.c::k_known_literals[]" >&2
    echo "to match scripts/audio-literals.sh output, then re-run." >&2
    exit 1
fi

echo "audio-literals-check: OK ($(wc -l < "$actual_file") literals match)"
