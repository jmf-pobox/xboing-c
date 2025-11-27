#!/bin/bash
# XBoing launcher for modern Linux
# Automatically sets required options

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

exec ./xboing -usedefcmap -sound "$@"
