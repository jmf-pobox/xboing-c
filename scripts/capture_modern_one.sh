#!/usr/bin/env bash
#
# capture_modern_one.sh — slice-experiment helper.
#
# Captures a single modern-xboing window screenshot at the boot
# presents/intro state and at a stable post-boot state.  Throwaway
# code intended only for the LLM-comparison thin slice
# (m-2026-05-07-001).  Real Phase 2 will replace this with a proper
# capture_modern.sh that supports state setup, animation keyframes,
# software renderer + pinned render_alpha (sjl B-3, B-4).
#
# Usage:
#   scripts/capture_modern_one.sh <wait_seconds> <out_png>
#
# Example:
#   scripts/capture_modern_one.sh 1 .tmp/slice-experiment/modern-presents-early.png
#   scripts/capture_modern_one.sh 8 .tmp/slice-experiment/modern-intro-late.png

set -euo pipefail

WAIT_S="${1:?wait_seconds required}"
OUT_PNG="${2:?out_png required}"

XBOING_BIN="${XBOING_BIN:-build/xboing}"

[[ -x "$XBOING_BIN" ]] || { echo "ERROR: $XBOING_BIN not found"; exit 1; }
[[ -n "${DISPLAY:-}" ]] || { echo "ERROR: DISPLAY not set"; exit 1; }
command -v xwininfo >/dev/null || { echo "ERROR: xwininfo missing"; exit 1; }
command -v import >/dev/null || { echo "ERROR: import missing"; exit 1; }

mkdir -p "$(dirname "$OUT_PNG")"

# Run modern xboing in background.
"$XBOING_BIN" >/dev/null 2>&1 &
XBPID=$!

cleanup() {
    kill "$XBPID" 2>/dev/null || true
    wait "$XBPID" 2>/dev/null || true
}
trap cleanup EXIT

sleep "$WAIT_S"

# Modern WM_NAME is "XBoing" (set by SDL_CreateWindow at
# src/sdl2_renderer.c:81, default title at sdl2_renderer.c:32).
# WM class is also "XBoing" but only the inner SDL window matters.
win_id=$(xwininfo -root -tree 2>/dev/null \
    | awk '/0x[0-9a-f]+ "XBoing": / { print $1; exit }')

if [[ -z "$win_id" ]]; then
    echo "ERROR: could not locate modern XBoing window" >&2
    exit 2
fi

import -window "$win_id" "$OUT_PNG"
echo "captured $OUT_PNG ($(stat -c%s "$OUT_PNG") bytes, win=$win_id)"
