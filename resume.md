# Resume — Screen State Machine: prove & close bugs with Z + TDD

**Branch:** `docs/screen-state-z-spec` (spec committed `f087c62`, no PR).
**Date opened:** 2026-07-04.

## Where we are

We have a **draft, fuzz-checked, peer-reviewed Z model** of XBoing's
screen state machine:

- `docs/specs/2026-07-04-screen-state-machine.tex` (+ rendered `.pdf`).
- Models the **flat** `SDL2ST_*` mode machine, the attract-cycle
  transition constraint (`attractNext`), the shared inner sub-screen
  systems (`intro`/`demo`/`keys`, each driving two modes), and the
  shared resources: `game_active`, the required-block grid
  (`block_system_still_active`), and high-score board selection
  (`hs_global`/`hs_personal`).
- Peer-reviewed against the current code; every schema cites
  `file:line`. `fuzz -t` clean, PDF builds with no overfull boxes.

The model is a **draft**. It has named three safety invariants and the
reachable violation behind each observed defect. It is *not yet*
machine-proven (probcli) and no tests exist for the invariants.

## What we are doing next

Use **TDD + z-spec together** to *find, prove, fix, and re-prove* bugs
in the screen state machine, and — equally important — to
**demonstrate the value of z-spec** in a screen-recorded session.

### Scope (the properties we must establish)

1. **Input-by-state** — every key press is processed according to the
   current screen (edge-triggered keys once per frame; the right key
   maps to the right transition on the right screen; no cross-screen
   misfire).
2. **Reachability** — each screen can only be reached "the right way"
   (valid transitions only; no back-door path leaves a screen in an
   inconsistent state).
3. **Data integrity per screen** — every screen has the data it
   requires intact on entry (e.g. the high-score screen shows a
   populated board; a started game has a populated block grid).

### The three confirmed candidate defects (from the model)

| Invariant | Defect | Evidence | Fix direction |
|-----------|--------|----------|---------------|
| `SafeGame` | Space → GAME lands in an empty-grid BONUS | silent level-load failure `game_modes.c:128-136` enters GAME with empty grid; unconditional `game_rules_check` `game_rules.c:330-340` | don't enter GAME on failed load; and/or require "level populated" before the bonus check |
| `SafeHighscore` | attract leaderboard empty despite real data | attract defaults to GLOBAL board `game_init.c:340`; empty on unprivileged installs; no attract-path global→personal fallback `game_modes.c:1139-1142` (fallback at `:1134` is post-game-over only) | fall back to personal when the global board is inactive/empty on the attract path |
| `SafeAttract` | first Space advances the loop after a game over | `game_rules.c:294-296` keeps `game_active` true; `highscore_cb_on_finished` `game_callbacks.c:750-759` auto-cycles without clearing it; clear lives in `game_input.c:415` | attract auto-advance (esp. from Highscore) must clear `game_active` when it is not a Space-driven new game |

Note: in a *pure* attract cycle `game_active` is always false, so
`Intro+Space → Instruct` is **intended** PR #172 behaviour, not a bug.

## Goals for the recorded session

1. For each defect: **z-spec finds it → proves it (probcli) → we write
   the failing test → fix → prove it fixed (probcli) → test passes.**
2. **Demonstrate the value of z-spec** — the model checker and the
   test-partition discovery should visibly do work a human would miss.

Use **probcli (`/z-spec:test`, model-check)** where feasible to produce
machine counterexample traces before/after each fix, and
**`/z-spec:partition`** (TTF tactics) where feasible to derive the test
cases from the spec rather than hand-guessing.

## Process — outer/inner loops

```text
# OUTER LOOP: one pass per invariant / defect, hardest-confidence first.
for defect in [SafeHighscore, SafeAttract, SafeGame]:   # cleanest first
    ethos_mission = open_mission(defect)          # ethos + missions where applicable

    # ---- PROVE THE BUG (formal, before touching code) ----
    zspec.check(spec)                             # /z-spec:check  (fuzz clean)
    trace = zspec.model_check(spec, invariant=defect)   # /z-spec:test (probcli)
    assert trace.is_counterexample                # machine-proven the bug exists
    partitions = zspec.partition(spec, op=defect.operation)  # /z-spec:partition (TTF)

    # ---- INNER LOOP: TDD up the test pyramid, red first ----
    for case in partitions ++ [trace.as_test]:
        write_failing_test(case)                  # L1 unit → L2 integration → L3 replay
        run_tests(); assert case.is_RED           # prove the test catches the bug

    # ---- FIX (smallest change that restores the invariant) ----
    edit_code(defect.fix_direction)               # never degrade quality:
                                                  #   DRY, no new warnings, root-cause only
    strengthen_spec(defect)                       # add the missing pre/postcondition to .tex

    # ---- RE-PROVE (formal + tests, both must flip) ----
    zspec.check(spec)
    trace2 = zspec.model_check(spec, invariant=defect)
    assert trace2.no_counterexample               # machine-proven fixed
    run_tests(); assert all_GREEN                 # L1..L3 pass; ASan green

    # ---- REVIEW + LAND (our normal process) ----
    local_code_review(diff)                       # pr-review-toolkit:code-reviewer;
    address_every_valid_finding()                 #   repeat until clean
    make check                                    # full CI parity (format, cppcheck,
                                                  #   markdownlint, ctest, asan, deb-lint)
    close_mission(ethos_mission)
    # PR only if the user asks; this branch carries the work otherwise.

# COMPLETION CRITIC (do not stop early):
#   every invariant machine-proven fixed AND covered by a red→green test.
```

**Inner loop never defers:** a counterexample or a red test is worked
immediately, not queued.
**Outer loop is the only path to "done":** an invariant is closed only
when probcli shows no counterexample *and* a test that was red is green.

## Standing constraints

- **Autonomy:** act autonomously this session; don't stop to ask unless
  genuinely blocked on a user-only decision.
- **Test pyramid** (`docs/TESTING.md`): prefer L1 unit → L2 integration
  → L3 replay; push assertions as low as they will go. The mode suite
  is no-crash only today — the three invariants are unguarded.
- **Never make code quality worse:** root-cause fixes, DRY, zero new
  warnings, no stabilizing flags. Leave each file better.
- **Process** (`docs/WORKFLOW.md`, `docs/GIT.md`): local code-review
  agent on every diff; `make check` before landing; ethos missions +
  worker/evaluator pairing (`.claude/rules/delegation.md`) where the
  work merits a mission. `jck` must approve gameplay-affecting changes.
- **Spec is the source of truth for the contract:** every fix updates
  the `.tex` with the new pre/postcondition, re-runs `fuzz -t`, and
  re-checks with probcli — the model and the code stay in lock-step.

## Handy commands

```bash
# formal
/z-spec:check      docs/specs/2026-07-04-screen-state-machine.tex   # fuzz type-check
/z-spec:test       docs/specs/2026-07-04-screen-state-machine.tex   # probcli animate/check
/z-spec:partition  docs/specs/2026-07-04-screen-state-machine.tex   # TTF test cases
/z-spec:audit      docs/specs/2026-07-04-screen-state-machine.tex   # coverage vs constraints

# build/test (macOS dev box)
cmake --preset debug && cmake --build build
ctest --test-dir build --output-on-failure
make asan-test
```

## Open threads / not-yet-done

- probcli has **not** yet been run on the spec — first action next
  session is to model-check each invariant for a machine counterexample.
- No regression tests exist yet for any of the three invariants.
- Consider filing a `bd` bead per defect before starting each (create →
  in_progress → close on land).
- Unrelated uncommitted working-tree changes remain (`.claude/agents`,
  `.beads`, `.punt-labs`) — not part of this work.
