# Resume — Screen State Machine: bugs proven & closed with Z + TDD

**Branch:** `docs/screen-state-z-spec` — PR
[#174](https://github.com/jmf-pobox/xboing-c/pull/174).
**Date opened:** 2026-07-04. **Completed:** 2026-07-11.

## Outcome

Three screen-state defects were **found, proven, fixed, and re-proven**
using the Z model (`docs/specs/2026-07-04-screen-state-machine.tex`) and
probcli, each with a red→green regression test and local code review.

| Invariant | Defect | Fix | Commit | ADR |
|-----------|--------|-----|--------|-----|
| `SafeHighscore` | attract Highscore showed a blank board on unprivileged installs (empty global board) despite real personal scores | `mode_highscore_enter` falls back to the personal board on the attract path (new `highscore_io_count`) | `d524783` | ADR-054 |
| `SafeAttract` | `game_active` leaked out of a game-over Highscore into the attract screens (finish-timer **and** C key), so the next Space returned to Intro instead of starting a game | single clear in `mode_intro_enter` (every attract exit from Highscore lands on Intro); removed the now-redundant clear in the Space handler | `3153cae` | ADR-055 |
| `SafeGame` | a silent level-load failure entered GAME with an empty grid, which `game_rules_check` dropped straight to an empty BONUS | `start_new_game` returns failure and `mode_game_enter` refuses GAME → returns to title | `2f7124e` | ADR-056 |

Fixed model: all three negated-invariant goals are **unreachable in one
exhaustive pass** (76 states, *No counter example found. ALL states
visited*). Debug ctest 61/61 and ASan/UBSan 61/61 green.

## How z-spec earned its keep

- **Found a second leak path.** The `SafeAttract` model conflated the
  finish-timer and the C cycle key into one `AttractAdvance` operation,
  which forced discovery that the C key leaked `game_active` too — not
  just the timer. Both are now covered.
- **Right probcli idiom.** Folding an invariant into the Z state schema
  makes ProB treat it as a *constraint* (violating transitions get
  disabled, surfacing as a spurious deadlock), not a checked property.
  The working method is to model-check toward the invariant's **negation**
  as a `-goal`; a reachable witness proves the bug, and *"No counter
  example found"* after the fix proves closure. Documented in the spec's
  Safety Invariants section.
- **Model/code lock-step.** Every fix updated the operation's
  postcondition in the `.tex`, re-ran `fuzz -t` and the probcli goal, and
  shipped with an ADR citing `original/<file>.c:<line>`.

## Process followed (per docs/WORKFLOW.md)

For each defect: probcli proof → RED test (up the pyramid) → smallest
root-cause fix → probcli re-proof + green test → local review
(`jdc` code, `gjm` tests, `jck` vision/original-fidelity) → address every
finding → `make` gates → ADR → commit. `jck` approved each state-machine
change against the 1996 source.

## Next steps

- **PR #174 is open and in review.** The three screen-state fixes and the
  mission-process doc overhaul ship together in one PR off
  `docs/screen-state-z-spec`, driven through the `docs/GIT.md` review loop.
- **Separate investigation:** *why* a level load would fail on the brew
  build (packaging / levels-dir resolution) is a distinct concern from the
  state machine degrading safely. `SafeGame` guarantees no empty-BONUS;
  it does not diagnose the install issue. Worth a bead if it recurs.
- **Local gates run in CI:** `format-check`, `cppcheck`, `markdownlint`,
  `deb-lint` are not installed on this Mac and run in CI on the PR.

## Handy commands

```bash
# formal (run from docs/specs/)
probcli 2026-07-04-screen-state-machine.tex -model_check \
  -goal "mode = mGame & blocksLoaded = zfalse"   # => No counter example found
fuzz -t 2026-07-04-screen-state-machine.tex

# build/test
cmake --build build && ctest --test-dir build --output-on-failure
make asan-test
```
