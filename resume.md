# Resume — Level Editor Parity (xboing-di8)

**Branch:** `feat/editor-window-width` -> PR #176.
**Status:** Complete; PR #176 open and being driven to merge (docs/GIT.md review loop).
**Next action:** review loop drives PR #176 to merge, then post-merge cleanup + close xboing-di8.

## One-line state

The whole level editor was brought to parity with the 1996 original
(started as a window-width fix, expanded to a full audit-driven effort).
Everything the user flagged across ~5 macOS screenshots is fixed. The only
thing left is the user's final visual confirmation, then the PR.

## Where to pick up

1. **User rebuilds + runs on their Mac** (`cmake --build build && ./build/xboing`,
   press `E` to enter the editor). Verify: palette swatch selection lands
   correctly; Shift+left-click erases (right-click reports point value; middle
   still erases); faint red grid shows *under* the blocks; red 2px borders
   frame the palette panel and the Active box; no redundant "Level N" text at
   the top.
2. **If anything's off** — screenshot it, fix it first (design→implement→review
   mission per the pattern below).
3. **When the user says it's good** → open the PR and drive it to merge per
   `docs/GIT.md`:
   - `make check` gates that run in CI on push: clang-format, cppcheck,
     markdownlint, dpkg-buildpackage, lintian. (NONE are installed on this
     Mac — `ctest` debug + asan are the only local gates; both are green
     61/61 on every commit.)
   - `git push -u origin feat/editor-window-width`
   - Open PR via `mcp__plugin_github_github__create_pull_request`, request
     Copilot review, arm the 2-min `/loop` per `docs/GIT.md`, address review
     rounds, squash-merge, post-merge cleanup.
   - **The user's visual confirmation gates the PR** — do not open it before
     they OK it.

## What's on the branch (commit order)

```text
30c14ab docs(spec): editor window-width design
b8392a0 docs(audit): editor parity audit — 9 blockers incl. save-path corruption
ffa9e56 docs(spec): editor parity implementation blueprint — 12 staged fixes
c490555 fix(editor): widen logical canvas + window in lockstep (palette not clipped)
ebb0863 fix(editor): save the level's real time bonus, not a hardcoded 120
cdda7dc fix(editor): preserve counter hit-count and RANDOM_BLK on save
2c0e91b feat(editor): real modal dialogues for save/load/set-time/name/clear/quit
1e54c0e feat(editor): two-column palette, all 30 tools, Active indicator
b7c74e4 feat(editor): show the HUD in editor mode with correct content
c25217b fix(editor): don't render the paddle and ball outside play-test
938847d feat(editor): right-click inspects a block's hit points instead of erasing
d7dec44 fix(editor): palette click selects the swatch you click
41e24dd feat(editor): red grid lines + red borders around the palette panel
d0e375d feat(editor): Shift+left-click erases (reachable without a middle button)
dad2289 docs(editor): ADRs (059-062) + correct 4 design-spec errors
bf6d4a3 fix(editor): remove the redundant 'Level N' text at the top
```

(Plus `94ce784` docs(resume) which this file overwrites.)

## Authoritative docs

- `docs/audits/2026-07-11-editor-parity.md` — the gap audit (9 blockers, 6
  majors, 4 minors, jdc-verified).
- `docs/specs/2026-07-11-editor-parity.md` — the implementation blueprint
  (corrected in `dad2289`).
- `docs/DESIGN.md` ADR-057 (width) … ADR-062 (editor file-error handling).

## Key data-corruption bugs fixed (the reason this mattered)

The modern editor was silently corrupting levels on re-save:

- hardcoded 120s time bonus overwrote the real value (only 6/80 levels are
  120; corrupted 92.5% on re-save) — `ebb0863`.
- counter-block hit-count saved as 0, RANDOM_BLK saved as its resolved color
  instead of `?` — `cdda7dc`.
- save/load/quit confirms were stubs that auto-confirmed (no safety net) —
  `2c0e91b`.

## Design errors the implementation-review gate caught (all corrected)

The jck-approved design was directionally right but wrong on 4 specifics that
only surfaced against real code — each fixed + spec corrected in `dad2289`:

1. §1.6 editor dialogue re-center → WRONG (original never moves inputWindow;
   keep bx=92; stage 4 was a no-op).
2. §4.2 on_error citation → the cited lines are `ShutDown`, not `ErrorMessage`;
   modern shows a transient message (ADR-062).
3. §3.1 palette vertical centering → design formula omitted it; added.
4. §2.4/stage 8 RANDOM "- R -" overlay → MIS-ANALYSIS; the block_type-keyed
   behavior was already faithful ("- R -" is a one-tick placement artifact,
   not a persistent label). Implemented then **reverted**.

## Erase mapping (user decision, ADR-058)

Original 3-button Sun mouse: left=draw, middle=erase, right=inspect. Modern
machines lack a middle button, so: right-click stays inspect (faithful),
middle-click erase kept (for those who have it), **Shift+left-click erase
added** as the portable binding. Documented in the man page (`xboing.man.in`
LEVEL EDITOR) and ADR-058.

## Open beads

- `xboing-di8` (P2, in_progress) — the editor parity effort. Code complete;
  keep in_progress until the PR merges.
- `xboing-oyt` (P3, open) — editor/attract screens show a leftover attract
  *fake* display score (top-left "2"/"3"). Cosmetic; user deprioritized
  ("probably fine as is"). Root: `score_system_set_display(..., attract_fake_score++)`
  at `src/game_modes.c:397`; original resets via `SetTheScore(0L)` (main.c:944).
- `xboing-dlg` (P3, open) — ethos seed `test` archetype uses Go-only globs
  (`*_test.go`), unusable for this C repo's `tests/test_*.c`; had to use the
  `implement` archetype for all test missions. Lives in the ethos repo.
- `xboing-c40` — editor grid lines. **DONE** in `41e24dd` (user asked to
  include it after seeing it's faintly in the original). Close it if not
  already closed.
- `xboing-dq5` — the ADR/spec doc pass. **DONE** in `dad2289`. Close it.

## Process notes (how each stage ran)

Every gated unit was an ethos mission: design → implement → review, worker ≠
evaluator, leader (claude) writes contracts + reviews but does NOT write
production code. Pairings used: `jdc`→`jck`/`gjm` (C), `sjl`→`jck`/`jdc`
(render), `jck` for all faithfulness gates. The mission store's write-set
enforcement caught 3 under-scoped contracts (rename cascades, link fix) —
each re-scoped honestly rather than fudged. Missions m-2026-07-11-016..028
and m-2026-07-12-001..015 (several failed/superseded — see `.punt-labs/ethos/
missions.jsonl`).

## Gotchas for whoever resumes

- **macOS has no X11** — the scripted golden pipeline (`make golden-screen`/
  `modern-screen`/`visual-check`, `visual_capture.sh`) needs X11 and only runs
  on Ubuntu. Interactive visual verification is the user running the build on
  their Mac + manual screenshots. Do NOT propose building a new capture tool —
  the tooling exists (leader learned this the hard way).
- **CMake static-lib relink**: after changing a `.c` compiled into a static
  lib, `cmake --preset debug` + rebuild to force relink (see `docs/TESTING.md`).
- `.claude/agents/*.md` show as modified in `git status` — pre-session drift,
  unrelated, leave them.
- format/cppcheck/markdownlint/deb-lint are NOT installed on this Mac → CI only.
