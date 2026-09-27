# Releasing XBoing

This is the runbook for cutting a release: tag push, the `.deb` +
Homebrew bottle artifacts CI produces, and the manual step that bumps
the Homebrew tap. Read `docs/CI.md` first for the workflow map this
runbook drives.

## Overview

Pushing a `v*` tag (strict `vMAJOR.MINOR.PATCH`, no
pre-release/build suffix — see "Strict semver" below) to
`.github/workflows/release.yml` runs, in order:

1. `build` — builds the `.deb`, runs quality gates, computes the tag
   tarball's sha256.
2. `publish` — creates the GitHub Release, attaches the `.deb`.
3. `provenance` — SLSA L3 attestation for the `.deb`.
4. `smoke-deb` / `smoke-brew` — install the published `.deb` and a
   tag-pinned Homebrew formula (source build) on every shipping OS,
   verify version + headless launch.
5. `bottle` — builds a prebuilt Homebrew bottle on each of
   `macos-14`, `ubuntu-latest`, `ubuntu-24.04-arm` and uploads the
   `.bottle.tar.gz` + `.bottle.json` to the Release.
6. `bottle-notes` — reads the three `.bottle.json` files back off the
   Release, assembles the tap's `bottle do ... end` block with the
   real per-platform sha256s, and appends it to the Release notes.

Steps 1–4 exist before this document; steps 5–6 are what makes
`brew install jmf-pobox/xboing/xboing` pour a prebuilt bottle instead
of compiling from source (bead xboing-157).

## Cutting a release

```bash
git tag v1.0.12
git push origin v1.0.12
```

Watch the `Release` workflow run to completion, then open the
Release page — the notes now end with a "Homebrew bottle (tap
maintainer action required)" section containing a ready-to-paste
`bottle do ... end` block.

## Phase 2b: bumping the tap (manual, current)

The Homebrew tap (`jmf-pobox/homebrew-xboing`) is a separate repo.
This project's CI cannot push to it without a `HOMEBREW_TAP_TOKEN`
secret, which does not exist yet (see Phase 2a below) — so today a
human does the bump:

1. Open the just-published GitHub Release, copy the `bottle do`
   block from the notes (rendered inside a ` ```ruby ` fence).
2. In `jmf-pobox/homebrew-xboing`, open `Formula/xboing.rb`.
3. Replace the existing `bottle do ... end` block (or the `head`-only
   stanza, on the very first bottled release) with the copied block.
4. Also update the `url` / `sha256` (or `head`) stanza to point at
   this tag, same as any ordinary tap bump — the bottle block alone
   does not pin the source.
5. Commit and push directly to the tap repo. There is no PR review
   gate on the tap today.
6. Verify: `brew update && brew install jmf-pobox/xboing/xboing` on a
   clean machine (or `brew reinstall` on one that already has it)
   and confirm brew reports "Pouring" a bottle, not "==> Installing
   xboing" followed by a `cmake`/`make` build log.

## Phase 2a: automated tap bump (future)

Once a `HOMEBREW_TAP_TOKEN` (a PAT scoped to
`jmf-pobox/homebrew-xboing`, `contents: write`) is provisioned as a
repo secret here, `bottle-notes` can be extended (or a new job added)
to `git clone` the tap, write the composed formula, and open a PR
against it automatically — the same shape `brew bump-formula-pr` or a
custom checkout-edit-commit-push step would take. This mission
deliberately does not build that path: provisioning the token is an
account-level decision the repo owner has to make, not something CI
can bootstrap for itself. Until then, Phase 2b's manual copy-paste
is the whole mechanism, and it is intentionally low-tech: a maintainer
reading the Release notes is a strictly stronger check than an
unattended push into a repo with no PR review.

## Bottle baseline: what a bottle actually covers

A Homebrew bottle is not portable to every machine running the same
OS family — it is pinned to the exact combination CI built it on:

- **macOS**: the bottle tag encodes the OS *codename*
  (`arm64_sonoma` for the `macos-14` runner, which is macOS 14
  "Sonoma" — not "14", and not "sequoia"/macOS 15). A user on macOS
  15 "Sequoia" installing this tap sees `brew` refuse the `sonoma`
  bottle and fall back to a source build, because bottles do not
  cross macOS major versions. When GitHub bumps the `macos-14`
  runner image's OS version, `arm64_sonoma` bottles will not install
  from the tap without also re-cutting a release. There is no macOS
  Intel bottle in this scope — the tap is Apple Silicon only,
  matching the `macos-14` (arm64) runner.
- **Linux**: `x86_64_linux` / `aarch64_linux` bottles are built on
  `ubuntu-latest` / `ubuntu-24.04-arm` and link against that
  runner image's glibc. Homebrew's Linux bottles assume a glibc no
  older than the build machine's — a much older distro (or a
  statically-thin container) can fail to load the bottle's shared
  objects. This is a known Linuxbrew constraint, not something this
  release pipeline works around; see Homebrew's own docs on
  "glibc bottle compatibility" if a user reports this.
- Anyone outside the bottled combination gets the pre-existing
  source-build path automatically — Homebrew's normal fallback when
  no bottle matches, not a special case this pipeline adds.

## Strict semver and exercising the `bottle` job

`build`'s metadata step (release.yml, "Resolve and validate release
metadata") enforces strict `^[0-9]+\.[0-9]+\.[0-9]+$` on the tag —
`v1.0.12-rc1` or anything with a suffix is rejected before the `.deb`
build even starts. `bottle` (`needs: [build, publish]`) inherits this
gate transitively and has no independent trigger: it pins its
formula to `https://github.com/<repo>/archive/refs/tags/<tag>.tar.gz`,
which only resolves for a tag that actually exists and that `build`
already validated.

**Decision for this mission: no `workflow_dispatch` was added.** A
manual-dispatch trigger on a branch ref would still fail at `build`'s
semver gate (branch names are not `vX.Y.Z`), so the only way to make
`workflow_dispatch` useful here would be to accept an existing tag as
an input and skip straight to `bottle`/`bottle-notes` — a second,
narrower workflow, not a toggle on this one. That is more surface
area than this mission's scope justifies. **Verifying the `bottle`
and `bottle-notes` jobs therefore requires pushing a real `v*`
release tag** — there is no dry-run path. The first real exercise of
this path should be treated as the "gate seen failing" rehearsal per
`docs/CI.md` §10: watch it on the next tagged release and record the
outcome there.

## Local sanity check

`make bottle` builds a bottle from the current working tree's `HEAD`
(via the formula's `head` stanza, not a tagged tarball) into a
throwaway local tap. It requires Homebrew on `PATH` and is not part
of `make check` — it is slow and mutates `$(brew --repository)`. Use
it to confirm the formula and bottle machinery work before trusting
a release run to exercise them for real:

```bash
make bottle
```
