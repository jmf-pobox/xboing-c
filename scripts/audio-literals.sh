#!/usr/bin/env bash
# audio-literals.sh — extract sorted unique string literals passed to
# sdl2_audio_play() in src/.  Used by `make audio-literals-check` to
# detect drift between source call sites and the manually-maintained
# k_known_literals[] array in tests/test_audio_name_validation.c.
#
# A drift means: someone added a new sdl2_audio_play("X") call site
# (or renamed one) without registering "X" in the test.  The test
# would then silently miss validating that "X" exists in sounds/.
#
# Spec: docs/specs/2026-06-03-sfx-testability.md (Component 4 / N1).

set -euo pipefail

cd "$(dirname "$0")/.."

# Extract every quoted literal passed as the second argument to
# sdl2_audio_play.  Skip call sites in src/sdl2_audio.c (the
# implementation itself) and in src/block_sound.c (covered by
# block_sound_name).
#
# Limitation: single-line `sdl2_audio_play(ctx, "X")` only.  A call
# split across lines (e.g. `sdl2_audio_play(ctx,\n  "X")`) will not
# match.  No existing call site is split this way; if clang-format
# ever wraps a long line, audio-literals-check would silently miss
# the literal — fail loudly here would require -z + multiline pattern.
grep -rho 'sdl2_audio_play([^,]*,[[:space:]]*"[^"]*"' \
    --include='*.c' \
    --exclude=sdl2_audio.c \
    --exclude=block_sound.c \
    src/ \
    | grep -oP '"[^"]*"' \
    | tr -d '"' \
    | sort -u
