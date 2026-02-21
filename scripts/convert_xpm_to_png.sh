#!/usr/bin/env bash
# Convert all XPM files under bitmaps/ to PNG under assets/images/.
#
# Requires: ImageMagick (convert).
#
# ImageMagick 6.x does not recognize numbered X11 color names (e.g. blue1,
# red2, gray26).  This script pre-processes XPM files that use such colors,
# replacing named colors with hex equivalents from /usr/share/X11/rgb.txt
# before feeding them to convert.
#
# Usage:
#   bash scripts/convert_xpm_to_png.sh             # convert all
#   bash scripts/convert_xpm_to_png.sh --dry-run   # preview only
#   bash scripts/convert_xpm_to_png.sh --force      # overwrite existing PNGs

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$REPO_ROOT/bitmaps"
DST_DIR="$REPO_ROOT/assets/images"

DRY_RUN=0
FORCE=0

for arg in "$@"; do
    case "$arg" in
        --dry-run) DRY_RUN=1 ;;
        --force)   FORCE=1 ;;
        --help|-h)
            echo "Usage: $(basename "$0") [--dry-run] [--force]"
            echo "  --dry-run  Show what would be converted, but don't do it"
            echo "  --force    Overwrite PNGs even if newer than source XPM"
            exit 0
            ;;
        *) echo "Unknown option: $arg" >&2; exit 1 ;;
    esac
done

# Check that ImageMagick convert is available.
if ! command -v convert >/dev/null 2>&1; then
    echo "ERROR: ImageMagick 'convert' not found. Install with: apt install imagemagick" >&2
    exit 1
fi

# --- X11 named-color fixup ---------------------------------------------------
#
# Numbered X11 color names that appear in the XPM files but are not recognized
# by ImageMagick 6.x.  Values sourced from /usr/share/X11/rgb.txt.
#
# Build a sed script that replaces each name with its hex equivalent.
# The pattern matches the XPM color assignment: <tab>c <name>"
# and replaces it with:                        <tab>c #RRGGBB"

build_color_sed() {
    local rgb_file="/usr/share/X11/rgb.txt"
    if [[ ! -f "$rgb_file" ]]; then
        # Fallback: hardcode the 25 colors we know about.
        rgb_file=""
    fi

    # Colors used in the XPM files that ImageMagick 6.x doesn't recognize.
    local -A color_hex=(
        [aquamarine4]="#458B74"
        [blue1]="#0000FF"
        [blue2]="#0000EE"
        [blue3]="#0000CD"
        [gray26]="#424242"
        [gray30]="#4D4D4D"
        [gray40]="#666666"
        [gray50]="#7F7F7F"
        [gray65]="#A6A6A6"
        [gray80]="#CCCCCC"
        [gray97]="#F7F7F7"
        [grey30]="#4D4D4D"
        [pink2]="#EEA9B8"
        [pink3]="#CD919E"
        [pink4]="#8B636C"
        [purple1]="#9B30FF"
        [purple4]="#551A8B"
        [red1]="#FF0000"
        [red2]="#EE0000"
        [red3]="#CD0000"
        [red4]="#8B0000"
        [tan2]="#EE9A49"
        [tan4]="#8B5A2B"
        [yellow3]="#CDCD00"
        [yellow4]="#8B8B00"
    )

    local sed_args=()
    for name in "${!color_hex[@]}"; do
        local hex="${color_hex[$name]}"
        # Match: <tab>c <name>"  ->  <tab>c #RRGGBB"
        sed_args+=(-e "s/\tc ${name}\"/\tc ${hex}\"/g")
    done
    printf '%s\n' "${sed_args[@]}"
}

# Pre-populate the sed arguments once.
mapfile -t SED_ARGS < <(build_color_sed)

# Check if an XPM file uses any named colors that need fixup.
needs_fixup() {
    grep -qP '\tc (aquamarine4|blue[123]|gray(2[6]|[345][0]|50|65|80|97)|grey30|pink[234]|purple[14]|red[1234]|tan[24]|yellow[34])"' "$1"
}

converted=0
skipped=0
errors=0

while IFS= read -r -d '' xpm_path; do
    # Compute output path: bitmaps/balls/ball1.xpm -> assets/images/balls/ball1.png
    rel="${xpm_path#"$SRC_DIR"/}"
    png_path="$DST_DIR/${rel%.xpm}.png"

    # Skip if PNG exists, is newer than XPM, and --force not set.
    if [[ $FORCE -eq 0 && -f "$png_path" && "$png_path" -nt "$xpm_path" ]]; then
        skipped=$((skipped + 1))
        continue
    fi

    if [[ $DRY_RUN -eq 1 ]]; then
        echo "[dry-run] $rel -> ${png_path#"$REPO_ROOT"/}"
        converted=$((converted + 1))
        continue
    fi

    # Ensure output directory exists.
    mkdir -p "$(dirname "$png_path")"

    # If the file uses unrecognized named colors, pre-process through sed.
    if needs_fixup "$xpm_path"; then
        if sed "${SED_ARGS[@]}" "$xpm_path" | convert - -background none png32:"$png_path" 2>/dev/null; then
            converted=$((converted + 1))
        else
            echo "ERROR: failed to convert $rel (with color fixup)" >&2
            errors=$((errors + 1))
        fi
    else
        if convert "$xpm_path" -background none png32:"$png_path" 2>/dev/null; then
            converted=$((converted + 1))
        else
            echo "ERROR: failed to convert $rel" >&2
            errors=$((errors + 1))
        fi
    fi
done < <(find "$SRC_DIR" -name '*.xpm' -print0 | sort -z)

echo ""
echo "--- Summary ---"
echo "Converted: $converted"
echo "Skipped (up-to-date): $skipped"
echo "Errors: $errors"

if [[ $errors -gt 0 ]]; then
    exit 1
fi
