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
    original) BINARY="./xboing" ; EXTRA_ARGS="-usedefcmap" ; RUN_DIR="original" ;;
    modern)   BINARY="./${BUILD_DIR:-build}/xboing" ; EXTRA_ARGS="" ; RUN_DIR="." ;;
    *)        die "Unknown variant '$VARIANT' — use 'original' or 'modern'" ;;
esac

# Only the original binary parses -bonus-scenario.  The env var keeps
# the script CLI surface unchanged for both variants.
if [[ "$VARIANT" == "original" && -n "${BONUS_SCENARIO:-}" ]]; then
    EXTRA_ARGS="$EXTRA_ARGS -bonus-scenario $BONUS_SCENARIO"
fi

# Modern bonus capture: load a pre-generated savegame v2 fixture.
# The fixture has score/level/lives/specials set per scenario and an
# empty block grid; on load the modern binary enters SDL2ST_GAME and
# game_rules_check transitions to SDL2ST_BONUS on the next tick.
# See tools/gen_bonus_fixtures.c and docs/TESTING.md "savegame
# fixture pattern".
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [[ "$VARIANT" == "modern" && "$MODE" == bonus* ]]; then
    SCENARIO="${BONUS_SCENARIO:-1}"
    FIXTURE_ROOT="$REPO_ROOT/tests/fixtures/bonus/scenario-$SCENARIO"
    [[ -f "$FIXTURE_ROOT/xboing/save-info.dat" ]] || \
        die "fixture missing: $FIXTURE_ROOT/xboing/save-info.dat (run 'make bonus-fixtures')"
    [[ -f "$FIXTURE_ROOT/xboing/save-level.dat" ]] || \
        die "fixture missing: $FIXTURE_ROOT/xboing/save-level.dat (run 'make bonus-fixtures')"
    export XDG_DATA_HOME="$FIXTURE_ROOT"
    EXTRA_ARGS="$EXTRA_ARGS -load"
fi

[[ -x "${RUN_DIR}/${BINARY#./}" ]] || die "${RUN_DIR}/${BINARY#./} not found or not executable"

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

find_xboing_window() {
    local target_pid="$1"
    xwininfo -root -tree 2>/dev/null \
        | awk 'tolower($0) ~ /xboing/ { print $1 }' \
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

mkdir -p .tmp
FIFO_DIR="$(mktemp -d .tmp/visual-capture.XXXXXX)"
FIFO="$FIFO_DIR/fifo"

XPID=""
cleanup() {
    [[ -n "$XPID" ]] && kill "$XPID" 2>/dev/null && wait "$XPID" 2>/dev/null
    rm -f "$FIFO"
    rmdir "$FIFO_DIR" 2>/dev/null || true
}
trap cleanup EXIT

mkfifo "$FIFO"

(cd "$RUN_DIR" && exec "$BINARY" $EXTRA_ARGS -visual-capture "$MODE") >"$FIFO" 2>/dev/null &
XPID=$!

WIN_ID=""
COUNT=0

while IFS= read -r -t 300 line; do
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
done < "$FIFO"

echo "Done. $COUNT images captured in $OUT_DIR"

kill "$XPID" 2>/dev/null || true
wait "$XPID" 2>/dev/null || true
XPID=""
