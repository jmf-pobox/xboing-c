#!/usr/bin/env bash
#
# capture_original.sh — Visual-fidelity Phase 1: capture reference PNGs
# from the legacy 1996 X11 binary `original/xboing`.
#
# Run ONCE.  Commit the resulting PNGs to tests/golden/original/.
# CI does not invoke this script — it consumes the committed goldens.
#
# Each capture starts a fresh xboing instance, runs `original/xboing
# -snapshot N` (the patch added in this PR; see original/main.c
# handleEventLoop and original/init.c ParseCommandLine), waits for
# the snapshot wait window, then ImageMagick `import -window <id>`
# grabs the xboing window framebuffer.  `-snapshot N` advances the
# game N frames after the initial map then sleeps 2 s — that 2 s
# window is when we capture.
#
# This script captures against whatever X server is at $DISPLAY.  In
# CI / clean-room captures, set up Xvfb beforehand:
#
#     Xvfb :99 -screen 0 700x630x24 -nolisten tcp &
#     DISPLAY=:99 scripts/capture_original.sh
#
# On a developer machine, just running the script captures from the
# existing display (xboing draws to a dedicated window — we capture
# by window ID, not the desktop root, so window-manager decorations
# and other desktop content are excluded).
#
# Prereqs (Ubuntu 24.04):
#   sudo apt install xvfb xfonts-75dpi xfonts-100dpi imagemagick x11-utils
#
# Without xfonts-75dpi/100dpi, X11 falls back to the `fixed` bitmap
# font and original/xboing renders different glyphs than on a
# developer machine (per sjl review Q1).  x11-utils provides xwininfo
# (window-id lookup).
#
# Usage:
#   scripts/capture_original.sh [output-dir]
#
# Default output-dir is tests/golden/original/.

set -euo pipefail

OUT_DIR="${1:-tests/golden/original}"
SNAPSHOT_BIN="${SNAPSHOT_BIN:-original/xboing}"

# State catalog: <name> <snapshot-frames> <description>
#
# Snapshot frame counts are picked empirically by inspecting what the
# game's natural state machine produces at that frame count starting
# from MODE_PRESENTS.  Adjust by capturing then visually verifying.
#
# Frame count 1 = "as soon as the window first paints," useful as a
# baseline.  Higher counts let later state-machine transitions land.
STATES=(
    "presents-early    1     Earliest visible frame (presents flag/earth)"
    "presents-mid      200   Presents screen mid-animation"
    "intro-stable      1500  Intro screen, past presents transition"
    "intro-late        2500  Intro screen, deeper attract sequence"
)

# --- Helpers ---------------------------------------------------------------

die() { echo "ERROR: $*" >&2; exit 1; }

require() {
    local cmd="$1"
    command -v "$cmd" >/dev/null 2>&1 || die "missing '$cmd' — see prereq comment in this script"
}

# Find the X11 window ID of an XBoing window.  Returns the ID on stdout.
# The window's WM_NAME is "- XBoing II -" but its class is "XBoing"
# (per original/init.c XSetWMProperties).  Mutter wraps it in an
# x11-frames decoration window of the same name on GNOME, so we
# specifically pick the inner window with class "XBoing" by parsing
# `xwininfo -root -tree`.
find_xboing_window() {
    xwininfo -root -tree 2>/dev/null \
        | awk '/"XBoing" "XBoing"/ { print $1; exit }'
}

# --- Pre-flight ------------------------------------------------------------

require import
require xwininfo
[[ -n "${DISPLAY:-}" ]] || die "DISPLAY not set — start Xvfb (see comment) or run on a desktop"
[[ -x "$SNAPSHOT_BIN" ]] || die "expected $SNAPSHOT_BIN to exist and be executable (run 'make original-build')"

mkdir -p "$OUT_DIR"

# --- Capture loop ----------------------------------------------------------

XBOING_PID=""

cleanup() {
    if [[ -n "$XBOING_PID" ]]; then
        kill "$XBOING_PID" 2>/dev/null || true
        wait "$XBOING_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

for state_line in "${STATES[@]}"; do
    name="$(echo "$state_line" | awk '{print $1}')"
    frames="$(echo "$state_line" | awk '{print $2}')"
    desc="$(echo "$state_line" | cut -d' ' -f3- | sed 's/^ *//')"

    echo
    echo "Capturing: $name  (frames=$frames)"
    echo "  $desc"

    out_png="$OUT_DIR/${name}.png"

    # Launch xboing in snapshot mode.  -usedefcmap is required on
    # TrueColor displays (BadMatch otherwise — original/ defaults to
    # PseudoColor).  After advancing $frames frames the snapshot mode
    # prints "XBOING_SNAPSHOT_READY" on stdout and sleeps 2 s; we
    # block on that line then capture immediately, eliminating timing
    # guesswork.
    fifo="$(mktemp -u --suffix=.fifo)"
    mkfifo "$fifo"
    "$SNAPSHOT_BIN" -usedefcmap -snapshot "$frames" >"$fifo" 2>/dev/null &
    XBOING_PID=$!

    # Wait for the READY signal with a generous timeout.  3 ms init +
    # 2 ms/frame * snapshotFrames is the worst-case binary lifetime
    # before READY; bound to 30 s as a safety net.
    ready_timeout_s=30
    ready_seen=0
    while IFS= read -r -t "$ready_timeout_s" line < "$fifo"; do
        if [[ "$line" == "XBOING_SNAPSHOT_READY" ]]; then
            ready_seen=1
            break
        fi
    done
    rm -f "$fifo"

    if [[ "$ready_seen" -ne 1 ]]; then
        echo "WARN: did not see READY signal for $name within ${ready_timeout_s}s; skipping"
        kill "$XBOING_PID" 2>/dev/null || true
        wait "$XBOING_PID" 2>/dev/null || true
        XBOING_PID=""
        continue
    fi

    # Verify the process is still running (should be in its 2 s sleep).
    if ! kill -0 "$XBOING_PID" 2>/dev/null; then
        echo "WARN: xboing exited before capture window for $name (frame count too low?)"
        XBOING_PID=""
        continue
    fi

    # Find the xboing window and capture by ID (not root — that would
    # include the developer's whole desktop).
    win_id="$(find_xboing_window)"
    if [[ -z "$win_id" ]]; then
        echo "WARN: could not locate XBoing window for $name; skipping"
        kill "$XBOING_PID" 2>/dev/null || true
        wait "$XBOING_PID" 2>/dev/null || true
        XBOING_PID=""
        continue
    fi

    import -window "$win_id" "$out_png"
    echo "  -> $out_png ($(stat -c%s "$out_png") bytes, window $win_id)"

    # Wait for xboing's exit (after its 2 s sleep finishes).
    wait "$XBOING_PID" 2>/dev/null || true
    XBOING_PID=""
done

echo
echo "Captured $(ls "$OUT_DIR"/*.png 2>/dev/null | wc -l) reference PNG(s) in $OUT_DIR"
