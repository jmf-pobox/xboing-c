#!/bin/bash
# XBoing launcher for modern Linux
# Automatically sets required options
# Note: -usedefcmap is now auto-detected for TrueColor displays but
# retained here for backward compatibility with older builds.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

exec ./xboing -usedefcmap -sound "$@" -speed 9
