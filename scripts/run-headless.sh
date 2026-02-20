#!/bin/sh
# Run xboing under Xvfb (headless X11 display).
#
# Usage:
#   scripts/run-headless.sh [binary] [extra-args...]
#
# Examples:
#   scripts/run-headless.sh                         # Run default build
#   scripts/run-headless.sh build-asan/xboing       # Run ASan build
#   scripts/run-headless.sh build/xboing -scores    # Print high scores
#
# Requirements: xvfb (apt install xvfb)
#
# The game needs -usedefcmap because XCreateSimpleWindow inherits the
# root window's visual, which conflicts with a custom colormap under
# Xvfb's TrueColor-only framebuffer.

set -e

BINARY="${1:-build/xboing}"
shift 2>/dev/null || true

if ! command -v xvfb-run >/dev/null 2>&1; then
    echo "error: xvfb-run not found. Install with: sudo apt install xvfb" >&2
    exit 1
fi

if [ ! -x "$BINARY" ]; then
    echo "error: $BINARY not found or not executable. Build first." >&2
    exit 1
fi

exec xvfb-run -a -s "-screen 0 1024x768x24" "$BINARY" -usedefcmap "$@"
