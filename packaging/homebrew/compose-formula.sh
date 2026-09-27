#!/usr/bin/env bash
# Compose a stable, tag-pinned Homebrew formula from the in-repo HEAD-only
# formula (packaging/homebrew/xboing.rb).
#
# The in-repo formula carries only a `head` stanza. Release-time jobs
# (smoke-brew, bottle) need a formula pinned to a specific tag tarball, so
# this rewrites the `head` line into `url` + `sha256`. Both the smoke-brew
# and bottle jobs in .github/workflows/release.yml call this — one
# implementation instead of duplicated inline awk.
#
# Usage:
#   compose-formula.sh <src_formula> <url> <sha256> <out_formula>
#
# Fails loudly if the rewrite did not emit a `url` stanza (e.g. the `head`
# line pattern in the source formula drifted), so the error is actionable
# here rather than as a confusing "no source" error at `brew install`.
set -euo pipefail

if [ "$#" -ne 4 ]; then
  echo "usage: $(basename "$0") <src_formula> <url> <sha256> <out_formula>" >&2
  exit 2
fi

src="$1"
url="$2"
sha256="$3"
out="$4"

[ -f "$src" ] || { echo "error: source formula not found: $src" >&2; exit 1; }

# Replace the `head "..."` stanza with `url` + `sha256`, preserving the
# leading indentation so the composed formula stays well-formed regardless
# of harmless whitespace edits to the source formula.
awk -v url="$url" -v sha256="$sha256" '
  /^[[:space:]]*head[[:space:]]/ {
    match($0, /^[[:space:]]*/)
    indent = substr($0, 1, RLENGTH)
    print indent "url \"" url "\""
    print indent "sha256 \"" sha256 "\""
    next
  }
  { print }
' "$src" > "$out"

# Post-condition: the url stanza must be present, else brew install (without
# --HEAD) would fall back on an empty formula.
url_re="${url//./\\.}"
if ! grep -qE '^[[:space:]]*url[[:space:]]+"'"${url_re}"'"' "$out"; then
  echo "::error::compose-formula.sh produced no url stanza — the 'head' line pattern in ${src} may have drifted" >&2
  cat "$out" >&2
  exit 1
fi

echo "composed pinned formula: $out"
