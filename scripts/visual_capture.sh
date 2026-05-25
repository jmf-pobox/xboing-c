#!/usr/bin/env bash
#
# visual_capture.sh — Deterministic sub-state screenshot capture.
#
# Launches xboing with -visual-capture, reads XBOING_SNAPSHOT signals
# from stdout, and captures the window at each sub-state transition.
#
# Usage:
#   scripts/visual_capture.sh original <mode|all> <output-dir>
#   scripts/visual_capture.sh modern   <mode|all> <output-dir>
#
# Prereqs: imagemagick (import), x11-utils (xwininfo, xprop)

set -euo pipefail

VARIANT="${1:?Usage: visual_capture.sh <original|modern> <mode|all> <output-dir>}"
MODE="${2:?Usage: visual_capture.sh <original|modern> <mode|all> <output-dir>}"
OUT_DIR="${3:?Usage: visual_capture.sh <original|modern> <mode|all> <output-dir>}"

die() { echo "ERROR: $*" >&2; exit 1; }

require() {
    command -v "$1" >/dev/null 2>&1 || die "missing '$1'"
}

require import
require xwininfo
require xprop

[[ -n "${DISPLAY:-}" ]] || die "DISPLAY not set"

case "$VARIANT" in
    original) BINARY="original/xboing" ; EXTRA_ARGS="-usedefcmap" ;;
    modern)   BINARY="build/xboing"    ; EXTRA_ARGS="" ;;
    *)        die "Unknown variant '$VARIANT' — use 'original' or 'modern'" ;;
esac

[[ -x "$BINARY" ]] || die "$BINARY not found or not executable"

mkdir -p "$OUT_DIR"

find_xboing_window() {
    local target_pid="$1"
    xwininfo -root -tree 2>/dev/null \
        | awk '/"XBoing" "XBoing"/ { print $1 }' \
        | while read -r win_id; do
            local pid_line pid
            pid_line=$(xprop -id "$win_id" _NET_WM_PID 2>/dev/null)
            pid=$(echo "$pid_line" | awk -F' = ' '/_NET_WM_PID/ {print $2}')
            if [[ "$pid" == "$target_pid" ]]; then
                echo "$win_id"
                return
            fi
        done
}

FIFO="$(mktemp -u --suffix=.vc-fifo)"
mkfifo "$FIFO"

XPID=""
cleanup() {
    [[ -n "$XPID" ]] && kill "$XPID" 2>/dev/null && wait "$XPID" 2>/dev/null
    rm -f "$FIFO"
}
trap cleanup EXIT

$BINARY $EXTRA_ARGS -visual-capture "$MODE" >"$FIFO" 2>/dev/null &
XPID=$!

WIN_ID=""
COUNT=0

while IFS= read -r -t 300 line < "$FIFO"; do
    case "$line" in
        XBOING_SNAPSHOT\ *)
            name="${line#XBOING_SNAPSHOT }"

            # name is mode/substate/seq — create subdir per mode
            mode_dir="${name%%/*}"
            rest="${name#*/}"
            filename="${rest//\//-}.png"
            mkdir -p "$OUT_DIR/$mode_dir"

            if [[ -z "$WIN_ID" ]]; then
                WIN_ID="$(find_xboing_window "$XPID")"
                [[ -n "$WIN_ID" ]] || die "Could not find XBoing window for PID $XPID"
            fi

            import -window "$WIN_ID" "$OUT_DIR/$mode_dir/$filename"
            size=$(stat -c%s "$OUT_DIR/$mode_dir/$filename")
            echo "  captured $name → $mode_dir/$filename ($size bytes)"
            COUNT=$((COUNT + 1))
            ;;
        XBOING_SNAPSHOT_DONE)
            echo "Done. $COUNT images captured in $OUT_DIR"
            break
            ;;
    esac
done

kill "$XPID" 2>/dev/null || true
wait "$XPID" 2>/dev/null || true
XPID=""
