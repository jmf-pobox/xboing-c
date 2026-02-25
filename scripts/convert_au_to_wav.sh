#!/usr/bin/env bash
# convert_au_to_wav.sh — Convert Sun .au sound files to WAV format using sox.
#
# Converts all .au files in sounds/ and sounds/originals/ to 16-bit PCM WAV.
# Skips files where a .wav already exists. Exits non-zero if any conversion fails.
#
# Usage: bash scripts/convert_au_to_wav.sh
#
# Requires: sox (apt install sox libsox-fmt-all)

set -euo pipefail

# Must run from repo root
if [[ ! -d sounds ]]; then
    echo "ERROR: sounds/ directory not found. Run from the repository root." >&2
    exit 1
fi

if ! command -v sox &>/dev/null; then
    echo "ERROR: sox is not installed. Install with: apt install sox libsox-fmt-all" >&2
    exit 1
fi

converted=0
skipped=0
failed=0

for dir in sounds sounds/originals; do
    if [[ ! -d "$dir" ]]; then
        echo "WARN: $dir/ not found, skipping."
        continue
    fi

    for au_file in "$dir"/*.au; do
        [[ -f "$au_file" ]] || continue

        wav_file="${au_file%.au}.wav"

        if [[ -f "$wav_file" ]]; then
            skipped=$((skipped + 1))
            continue
        fi

        if sox "$au_file" -e signed-integer -b 16 "$wav_file"; then
            converted=$((converted + 1))
        else
            echo "FAIL: $au_file" >&2
            failed=$((failed + 1))
        fi
    done
done

echo ""
echo "Conversion complete: $converted converted, $skipped skipped, $failed failed."

if [[ $failed -gt 0 ]]; then
    exit 1
fi
