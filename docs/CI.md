# CI Standard Compliance

This document records how xboing-c satisfies `punt-kit/standards/ci.md`
and where each invariant is enforced. New contributors: read the standard
first. This file records **what and where**, not why — the standard has
the why.

## Workflow map

| File | Triggers | Purpose |
|------|----------|---------|
| `.github/workflows/lint.yml` | Push to master, PRs | `clang-format` + `cppcheck` static gates |
| `.github/workflows/test.yml` | Push to master, PRs, nightly | Build matrix + install-deb + install-brew matrix |
| `.github/workflows/docs.yml` | `**/*.md` on master + PRs | markdownlint |
| `.github/workflows/release.yml` | `v*` tag push | Build .deb, publish Release, SLSA provenance, smoke-install both artifacts |

## §1 — Per-OS floor

Both shipping install paths run on both shipping OSes:

- **`.deb`** — Ubuntu (`install-deb` on PRs + `smoke-deb` on release)
- **Homebrew formula** — macOS 14 AND Linuxbrew on Ubuntu
  (`install-brew` matrix on PRs + `smoke-brew` matrix on release)

Source-level correctness runs on both OSes for the debug preset:
`build (ubuntu-latest, debug)` + `build (macos-14, debug)`. ASan stays
Ubuntu-only — clang asan on macOS catches the same memory bugs the gcc
asan leg already covers.

## §2–§3 — CI runs what ships, verify through the installed artifact

`install-deb` and `install-brew` don't test source builds — they test the
paths users actually run:

1. Produce the packaging artifact the exact way a release would.
2. Install it with the exact command a user would.
3. Launch the installed binary and verify (a) the version string matches
   the packaged version and (b) headless smoke launch exits at the
   timeout, not on its own (see below).

The smoke-launch predicate refuses exit code 0 on purpose. xboing exits
0 when `game_create` fails (SDL init, missing assets), so accepting 0
would let a broken install pass a check meant to prove init works. The
only accepted exit codes are:

- `124` on Linux — GNU timeout without `--preserve-status`
- `142` on macOS — perl `alarm` delivers SIGALRM (14), shell exit 128+14

## §8 — Branch protection required-status-checks

The master ruleset (id 13438261) requires:

- `build (ubuntu-latest, debug, build)`
- `build (ubuntu-latest, asan, build-asan)`
- `build (macos-14, debug, build)`
- `install-deb`
- `install-brew (macos-14)`
- `install-brew (ubuntu-latest)`
- `clang-format`
- `cppcheck`
- `Cursor Automation: Find vulnerabilities`

Update this list AND the ruleset in lockstep whenever a new leg is added
or a matrix expansion changes an existing name.

`markdownlint` is not required — `docs.yml` is path-conditional, so
requiring it would block any PR that doesn't touch `.md`.

## §10 — Gate seen failing

> A new gate is not trusted until it has been observed failing.

### Rehearsal record: `release.yml` debmeta upstream gate

The `debmeta` step in `release.yml` (added in commit `af5b6a1`) compares
`debian/changelog` upstream against the tag. It was observed failing
2026-08-30 by pushing scratch tag `v0.99.99` against ci/release-yml-
determinism HEAD (`a3cb2f7`) with `debian/changelog` still at `1.0.11`.

- Workflow run: <https://github.com/jmf-pobox/xboing-c/actions/runs/33342662966>
- Failure output: `debian/changelog upstream (1.0.11) != tag (0.99.99)`
- Job exit code: 1 at the intended gate
- Cleanup: tag deleted from origin same session; no Release created (workflow
  aborted at build stage before publish)

### Other gates in `release.yml`

- **Strict-semver validation** (meta step) — implicit rehearsal: pushing
  any tag not matching `^v[0-9]+\.[0-9]+\.[0-9]+$` fails at line 50. Not
  formally rehearsed; the regex is simple enough that inspection suffices.
- **Formula rewrite post-condition** (smoke-brew) — grep check that the
  awk rewrite produced a `url` line. Untested; would require an
  indent-drift in `packaging/homebrew/xboing.rb` to observe.

Add a rehearsal record here whenever a new gate ships in a release-time
workflow.

## §9 — Security hardening

Actions pinned by commit SHA, never floating tags:

- `actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683` (v4.2.2)
- `actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02` (v4.6.2)
- `actions/download-artifact@d3f86a106a0bac45b974a628896c90dbdf5c8093` (v4.3.0)
- `DavidAnson/markdownlint-cli2-action@05f32210e84442804257b2a6f20b273450ec8265` (v19.1.0)
- `slsa-framework/slsa-github-generator/.github/workflows/generator_generic_slsa3.yml@f7dd8c54c2067bafc12ca7a55595d5ee9b75204a` (v2.1.0)

Workflow-level `permissions: read-all` on all four workflows. Individual
jobs escalate only what they need (e.g. `publish` gets `contents: write`;
`provenance` gets `id-token: write` for Sigstore OIDC).

Unpinned residual: Linuxbrew bootstrap runs `curl | bash` of
`https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh` (the
official installer). Documented as residual by Cursor Automation on
PR #208; pinning to a commit SHA is a follow-up.
