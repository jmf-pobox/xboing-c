#!/usr/bin/env bash
# Verify an installed xboing binary is a genuine bottle pour, not a
# disguised source build: right architecture for the CURRENT runner, right
# version, and it launches headless. release.yml's `bottle` job runs this
# against the binary poured from a freshly-built *.bottle.tar.gz (after
# uninstalling the build-only toolchain), so a pass here proves the bottle
# runs with no compiler/dev-headers present — not just that "brew install"
# exited 0, which a source-build fallback would also do.
#
# This script does NOT check whether brew poured a bottle or built from
# source — the caller checks that separately, before this script ever
# runs, via brew's own install log ("==> Pouring ...bottle.tar.gz") AND
# the install receipt (`brew info --json=v2 ... | jq
# '.formulae[0].installed[0].poured_from_bottle'`), which is Homebrew's
# structured record of the fact (Tab#poured_from_bottle) and doesn't
# depend on free-text log wording. This script only characterizes the
# resulting binary.
#
# Usage: verify-bottle.sh <expected_version> <binary_path>
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "usage: $(basename "$0") <expected_version> <binary_path>" >&2
  exit 2
fi

expected_version="$1"
binary="$2"

resolved="$(command -v "$binary" 2>/dev/null || true)"
if [ -z "$resolved" ]; then
  if [ -x "$binary" ]; then
    resolved="$binary"
  else
    echo "::error::binary not found or not executable: $binary" >&2
    exit 1
  fi
fi

# --- Architecture -----------------------------------------------------
# `file` reports the binary's actual machine type. Compare against THIS
# RUNNER's arch (uname -m) rather than a value passed in, so the same
# script works unmodified across the arm64-macOS, x86_64-Linux, and
# aarch64-Linux matrix legs (release.yml's `bottle` job matrix) — a
# mismatch here means brew poured (or built) a binary for the wrong
# architecture, e.g. a mixed-up cross-arch bottle.
arch="$(uname -m)"
case "$arch" in
  x86_64) pattern='x86[_-]64' ;;
  arm64 | aarch64) pattern='arm64|aarch64' ;;
  *)
    echo "::error::unrecognized runner arch from uname -m: $arch" >&2
    exit 1
    ;;
esac

file_out="$(file "$resolved")"
echo "file: $file_out"
if ! printf '%s' "$file_out" | grep -qE "$pattern"; then
  echo "::error::binary arch mismatch: expected pattern '$pattern' (runner uname -m=$arch), file says: $file_out" >&2
  exit 1
fi

# --- Version ------------------------------------------------------------
# Must match the tag-derived version exactly (same prefix-match convention
# as smoke-deb/smoke-brew above in release.yml), not just "some version".
out="$("$resolved" -version)"
echo "$out"
if ! printf '%s' "$out" | grep -qE "^xboing ${expected_version}([[:space:]]|$)"; then
  echo "::error::version mismatch: expected xboing ${expected_version}, got: $out" >&2
  exit 1
fi

# --- Headless launch -----------------------------------------------------
# Must run to the timeout, not exit early. xboing's game_create returns
# success even when init failed (SDL init, missing assets), so exit 0
# would hide a broken pour rather than prove it works — see smoke-deb's
# identical note in release.yml.
set +e
if command -v timeout >/dev/null 2>&1; then
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout --signal=TERM 3 "$resolved"
elif command -v gtimeout >/dev/null 2>&1; then
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy gtimeout --signal=TERM 3 "$resolved"
else
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy perl -e 'alarm 3; exec @ARGV' "$resolved"
fi
rc=$?
set -e
echo "xboing headless exit code: $rc"
# 124 = GNU timeout hit (Linux); 142 = 128 + SIGALRM(14), perl fallback
# (macOS, which ships neither timeout nor gtimeout by default).
if [ "$rc" -ne 124 ] && [ "$rc" -ne 142 ]; then
  echo "::error::headless smoke launch did not hit the timeout (rc=$rc) — binary exited on its own, which means game_create silently failed" >&2
  exit 1
fi

echo "verify-bottle.sh: PASS (arch=$arch, version=$expected_version, headless launch confirmed)"
