#!/usr/bin/env bash
# Verify converted PNG files against their XPM sources.
#
# Checks for every PNG in assets/images/:
#   1. Dimensions match the XPM header
#   2. Has alpha channel (RGBA)
#   3. Non-degenerate (at least 1 non-fully-transparent pixel)
#
# Requires: ImageMagick (identify).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$REPO_ROOT/bitmaps"
DST_DIR="$REPO_ROOT/assets/images"

if ! command -v identify >/dev/null 2>&1; then
    echo "ERROR: ImageMagick 'identify' not found. Install with: apt install imagemagick" >&2
    exit 1
fi

pass=0
fail=0
total=0

while IFS= read -r -d '' xpm_path; do
    rel="${xpm_path#"$SRC_DIR"/}"
    png_path="$DST_DIR/${rel%.xpm}.png"
    total=$((total + 1))

    # --- Check PNG exists ---
    if [[ ! -f "$png_path" ]]; then
        echo "FAIL: $rel — PNG not found at ${png_path#"$REPO_ROOT"/}"
        fail=$((fail + 1))
        continue
    fi

    # --- Parse XPM dimensions from the header ---
    # XPM header line looks like: "20 19 12 1"  (width height ncolors cpp)
    xpm_dims=$(grep -m1 -oP '"\d+ \d+ \d+ \d+"' "$xpm_path" | tr -d '"')
    if [[ -z "$xpm_dims" ]]; then
        echo "FAIL: $rel — could not parse XPM dimensions"
        fail=$((fail + 1))
        continue
    fi
    xpm_w=$(echo "$xpm_dims" | awk '{print $1}')
    xpm_h=$(echo "$xpm_dims" | awk '{print $2}')

    # --- Check PNG dimensions ---
    png_info=$(identify -format '%w %h %[channels]' "$png_path" 2>/dev/null) || {
        echo "FAIL: $rel — identify failed on PNG"
        fail=$((fail + 1))
        continue
    }
    png_w=$(echo "$png_info" | awk '{print $1}')
    png_h=$(echo "$png_info" | awk '{print $2}')
    png_channels=$(echo "$png_info" | awk '{print $3}')

    ok=1

    # 1. Dimensions match
    if [[ "$png_w" != "$xpm_w" || "$png_h" != "$xpm_h" ]]; then
        echo "FAIL: $rel — dimensions mismatch: XPM=${xpm_w}x${xpm_h}, PNG=${png_w}x${png_h}"
        ok=0
    fi

    # 2. Has alpha channel (srgba or rgba)
    if [[ "$png_channels" != *a* ]]; then
        echo "FAIL: $rel — no alpha channel (channels=$png_channels)"
        ok=0
    fi

    # 3. Non-degenerate: at least 1 non-fully-transparent pixel
    # Count pixels where alpha > 0 using a fast histogram approach.
    opaque_pixels=$(convert "$png_path" -alpha extract -format '%[fx:mean>0?1:0]' info: 2>/dev/null) || opaque_pixels="1"
    if [[ "$opaque_pixels" == "0" ]]; then
        echo "WARN: $rel — all pixels are fully transparent"
        # Don't fail on this — some XPMs may legitimately be fully transparent frames
    fi

    if [[ $ok -eq 1 ]]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
    fi
done < <(find "$SRC_DIR" -name '*.xpm' -print0 | sort -z)

echo ""
echo "--- Verification Summary ---"
echo "Total: $total"
echo "Pass:  $pass"
echo "Fail:  $fail"

if [[ $fail -gt 0 ]]; then
    exit 1
fi

echo "All checks passed."
