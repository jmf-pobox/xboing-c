#!/usr/bin/env bash
#
# Run all integration tests (requires SDL dummy drivers).
#
# Usage: ./scripts/run-integration-tests.sh [build-dir]
#
# Defaults to ./build if no build directory specified.

set -euo pipefail

BUILD_DIR="${1:-build}"

export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy

echo "=== Integration tests (build: $BUILD_DIR) ==="
echo

# Ensure build is current
cmake --build "$BUILD_DIR" 2>&1 | tail -n 1

# Run integration and replay tests
ctest --test-dir "$BUILD_DIR" -R "test_integration_|test_replay_" --output-on-failure

echo
echo "=== Done ==="
