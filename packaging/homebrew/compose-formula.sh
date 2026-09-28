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
#   compose-formula.sh <src_formula> <url> <sha256> <out_formula> [version]
#
# <version> is optional. Homebrew normally infers `version` from the URL
# (e.g. a GitHub tag tarball URL like .../refs/tags/v1.2.3.tar.gz), which
# is why release.yml's tag-pinned callers omit it. A local file:// tarball
# path has no such pattern, so `version` comes back nil and `brew install`
# fails with "invalid attribute ... version (nil)" — pass <version>
# explicitly in that case (see `make bottle`) and this emits an explicit
# `version "..."` line.
#
# Fails loudly if the rewrite did not emit a `url` stanza (e.g. the `head`
# line pattern in the source formula drifted), so the error is actionable
# here rather than as a confusing "no source" error at `brew install`.
set -euo pipefail

if [ "$#" -ne 4 ] && [ "$#" -ne 5 ]; then
  echo "usage: $(basename "$0") <src_formula> <url> <sha256> <out_formula> [version]" >&2
  exit 2
fi

src="$1"
url="$2"
sha256="$3"
out="$4"
version="${5:-}"

# Validate the values that get interpolated into the generated formula's
# Ruby source before writing them. All current callers pass trusted input
# (a GitHub tag-archive URL or a repo-local file:// path, a shasum hex
# digest, a semver), so this changes nothing today — it stops a FUTURE
# caller from smuggling code through url/sha256/version and breaking out
# of the Ruby string into arbitrary formula code (flagged by Cursor on
# PR #230).
#
# Reject embedded newlines/carriage-returns in every interpolated value
# FIRST. `grep` matches line-by-line, so a value like "<valid-url>\n\";
# system('id'); url \"" would pass the single-line allowlist below on its
# first line while smuggling a second line into the Ruby string (Cursor
# PR #230). After this guard each value is one line, so the greps are sound.
for _v in "$url" "$sha256" "$version"; do
  case "$_v" in
    *$'\n'* | *$'\r'*)
      echo "::error::compose-formula.sh: url/sha256/version must not contain a newline" >&2
      exit 1
      ;;
  esac
done

# url is ALLOWLISTED, not denylisted: a double-quoted Ruby string also
# interpolates #{...}, #@ivar and #$global, so a denylist that misses '#'
# lets `url "...#{system('id')}..."` run when the formula loads. Permit
# only the characters that appear in http(s) and file:// URLs; everything
# else — '#', '{', quotes, '$', backticks, whitespace — is rejected.
if ! printf '%s' "$url" | grep -qE '^[A-Za-z0-9:/._~%?=@&+-]+$'; then
  echo "::error::compose-formula.sh: url has characters unsafe for formula Ruby (allowed: A-Za-z0-9 : / . _ ~ % ? = @ & + -): $url" >&2
  exit 1
fi
if ! printf '%s' "$sha256" | grep -qE '^[A-Fa-f0-9]{64}$'; then
  echo "::error::compose-formula.sh: sha256 is not a 64-char hex digest: $sha256" >&2
  exit 1
fi
if [ -n "$version" ] && ! printf '%s' "$version" | grep -qE '^[A-Za-z0-9._+~-]+$'; then
  echo "::error::compose-formula.sh: version has characters unsafe for formula Ruby: $version" >&2
  exit 1
fi

[ -f "$src" ] || { echo "error: source formula not found: $src" >&2; exit 1; }

# Replace the `head "..."` stanza with `url` + `sha256` (+ `version` when
# given), preserving the leading indentation so the composed formula stays
# well-formed regardless of harmless whitespace edits to the source
# formula.
awk -v url="$url" -v sha256="$sha256" -v version="$version" '
  /^[[:space:]]*head[[:space:]]/ {
    match($0, /^[[:space:]]*/)
    indent = substr($0, 1, RLENGTH)
    print indent "url \"" url "\""
    print indent "sha256 \"" sha256 "\""
    if (version != "") {
      print indent "version \"" version "\""
    }
    next
  }
  { print }
' "$src" > "$out"

# Post-condition: the url stanza must be present, else brew install (without
# --HEAD) would fall back on an empty formula. Match as a FIXED string, not
# a regex: the url allowlist permits `?` and `+` (both ERE operators), so
# building a pattern from the url would misfire on a valid URL that contains
# them (Copilot PR #230). The composed line is `<indent>url "<url>"`, so the
# literal substring `url "<url>"` is present exactly when the stanza was
# written.
if ! grep -qF -- "url \"${url}\"" "$out"; then
  echo "::error::compose-formula.sh produced no url stanza — the 'head' line pattern in ${src} may have drifted" >&2
  cat "$out" >&2
  exit 1
fi

echo "composed pinned formula: $out"
