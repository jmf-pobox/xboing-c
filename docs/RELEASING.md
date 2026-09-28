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

## MANUAL post-tap-bump verification (the only real-tap pour check)

The `bottle` job in CI (see "How CI proves the bottle actually pours"
below) never installs from the public tap — the tap has no real
bottle sha256s until step 6 above lands them, so a check that ran
`brew install jmf-pobox/xboing/xboing` in CI, before that commit
exists, would either fail every release or silently fall back to a
source build. The one and only point this repo can verify a real user
gets a real pour from the real tap is here, after the manual bump,
by hand:

1. On a clean machine (no prior xboing install) matching a bottled
   platform — macOS arm64, or Linux x86_64/aarch64 — run:

   ```bash
   brew update
   brew install jmf-pobox/xboing/xboing
   ```

   (On a machine that already has it: `brew reinstall
   jmf-pobox/xboing/xboing` instead.)

2. Confirm the install log says `==> Pouring
   xboing--<version>.<bottle-tag>.bottle.tar.gz` — not `==> Installing
   xboing` followed by a `cmake`/`make` compile log. If it built from
   source, the tap's `bottle do` block, `url`/`sha256` pin, or bottle
   tag doesn't match this machine — recheck step 4 above.
3. `xboing -version` matches the tag; the game launches.

This is a manual step by design (bead xboing-157): CI cannot exercise
the real tap before a human commits the bump, so this is not
automatable away without either provisioning `HOMEBREW_TAP_TOKEN`
(Phase 2a) or accepting a chicken-and-egg gap. Skipping it means the
release ships with an *unverified* claim that installs pour.

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

## How CI proves the bottle actually pours

The `bottle` job cannot install from the public tap (see "MANUAL
post-tap-bump verification" above for why) — so instead of gating on
`brew install jmf-pobox/xboing/xboing`, it builds a bottle and then
pours *that exact local `.bottle.tar.gz`* on the same runner, which
`FromBottleLoader` (Homebrew's formula loader) guarantees always
pours — the bottle's own metadata is embedded in the tarball, there
is no source fallback to load from a bare tarball path. Four checks,
all in `.github/workflows/release.yml`'s `bottle` job unless noted:

1. **Pour assertion.** The "Pour the bottle directly from the local
   tarball" step greps the `brew install` log for `==> Pouring
   *.bottle.tar.gz`, and cross-checks `brew info --json=v2 ... | jq
   '.formulae[0].installed[0].poured_from_bottle'` against the
   install receipt (`true` required) — Homebrew's own structured
   record of the fact (`Tab#poured_from_bottle`), not a second
   free-text grep, because the generic `==> Installing ...` header
   fires identically for both a pour and a source build and cannot
   discriminate between them. The same receipt check runs the other
   direction in "Build the bottle from source" (`poured_from_bottle`
   must be `false` there, since `--build-bottle` disables pouring).
2. **No-toolchain proof.** "Uninstall the source build + the
   build-only toolchain" removes `cmake` and `pkg-config` (the
   formula's `depends_on ... => :build` deps) — deliberately keeping
   the runtime `sdl2*` deps — and asserts via `brew list --formula`
   that they're actually gone. "Verify the poured binary" re-checks
   this immediately before running the binary, so the whole
   pour-to-launch window is covered, not just the moment right after
   uninstalling.
3. **Arch + execute.** `packaging/homebrew/verify-bottle.sh` (called
   from "Verify the poured binary") uses `file` on the installed
   binary and compares against the runner's own `uname -m`, then
   requires an exact `xboing <version>` match and a headless launch
   that runs to a timeout (not an early exit — see the script's
   comments for why exit 0 doesn't prove anything here).
4. **Sequencing.** Steps 1–3 above run against the bottle this same
   job just built on this same runner, never against the tap — the
   tap doesn't have real bottle sha256s until a human completes the
   Phase 2b commit. The **only** check against the real, public tap
   is the manual step in "MANUAL post-tap-bump verification" above,
   run by a human after that commit lands.

`smoke-brew` (source build, matrix `macos-14`/`ubuntu-latest`) is
unchanged by any of this — it is a fallback-path test, not a bottle
test, and stays that way on purpose.

**`packaging/homebrew/xboing.rb` (the in-repo formula) carries no
`bottle do ... end` block, and must not.** Homebrew's `pour_bottle?`
checks `formula.bottled?` — whether a `bottle do` tag matches the
current platform — before it ever looks at `url`/`sha256`. A
placeholder block with fake `sha256`/`root_url` values would make
`brew install` on a matching platform (exactly what `smoke-brew` runs
on `macos-14`/`ubuntu-latest`, unauthenticated, no flags) try to pour
that fake bottle and 404 hard, with **no fallback to the source
`url`**. Only the tap (`jmf-pobox/homebrew-xboing`, a separate repo)
ever carries a `bottle do` block, and only after Phase 2b lands the
real per-platform sha256s that `bottle-notes` assembled — the in-repo
formula is source-only by design, permanently, not just until the
first bottled release.

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
- **Linux**: `x86_64_linux` / `arm64_linux` bottles are built on
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

`make bottle` builds a bottle from the **local working tree's HEAD
commit**: it archives `HEAD` via `git archive`, sha256s the tarball,
and uses `packaging/homebrew/compose-formula.sh` to compose a stable
formula pinned to that local tarball via a `file://` URL — the same
script `release.yml`'s `smoke-brew`/`bottle` jobs use, just pointed at
a local archive instead of a GitHub tag tarball. `brew bottle`
categorically refuses to bottle a `head`-only install (no stable
version to bottle), so a stable `url`+`sha256` formula is mandatory —
this is why the target doesn't just `brew install --HEAD` the
in-repo formula directly. Uncommitted changes are NOT included
(`git archive` only sees committed tree state) — commit locally
first if you need to bottle-test a change. It requires Homebrew on
`PATH` and is not part of `make check` — it is slow and mutates
`$(brew --repository)`. It is idempotent — a prior run's tap/keg is
removed before rebuilding (by bare formula name, since Homebrew's
Cellar keys kegs by name regardless of which tap installed them), so
it can be re-run without manual cleanup. Use it to confirm the
formula and bottle machinery work before trusting a release run to
exercise them for real:

```bash
make bottle
```
