# Resume — Bonus Screen Visual Fidelity (renderer rewrite complete)

**Last updated:** 2026-06-06
**Branch:** `feat/bonus-modern-capture` (uncommitted; not pushed)
**Tip of master:** `7571c1e feat(bonus): capture infra + 32 original goldens + renderer rewrite (#143)`
**Active epic:** `xboing-c-hlf` (in_progress, P1) — child of `xboing-c-y7s`

## Where we are

`xboing-c-hlf` step 4 (modern renderer rewrite) is **implementation
complete**. Visual fidelity essentially achieved on the
representative `bonus-1/end-text` pair (LLM judge verdict:
`equivalent`). Quality gates green: format-check, cppcheck,
ctest debug 57/57, asan-test 57/57, markdownlint 86/0.

Remaining: user visual verification, then commit and PR.

## What was done this session (2026-06-06)

### Original-side capture restored (`xboing-c-52s`, `xboing-c-z0n` — closed)

- TEMPORARY `DoEndText` wait reverted to `LINE_DELAY * 2`.
- Force-entry helper `setup_bonus_capture_scenario` in
  `original/main.c` drives the real `CheckGameRules` path.
- `bonus_state_name` + `MODE_BONUS` branch in `vc_check`.
- `-bonus-scenario N` CLI flag, `bonus` mode in `-visual-capture`.
- 32 authentic goldens at `tests/golden/original/bonus-{1..4}/`.

Research and review: `docs/research/2026-06-06-bonus-capture-real-entry.md`,
`docs/reviews/2026-06-06-bonus-capture-real-entry-review.md`.

### Modern-side renderer rewrite (`xboing-c-hlf` step 4)

Spec, peer review, ADRs, then implementation:

- `docs/specs/2026-06-06-bonus-renderer-rewrite.md`
- `docs/reviews/2026-06-06-bonus-renderer-rewrite-review.md`
- `docs/DESIGN.md` ADR-039 (goldens are authority over original-
  source draw analysis) and ADR-040 (bonus-coin sync at mode
  entry, not per pickup).

Implementation:

- `src/game_render_ui.c::game_render_bonus` — removed
  `draw_ball_border` static helper, its call, and the
  `SPR_TITLE_SMALL` XBOING pixmap block.
- `src/game_render.c:1217` — added `SDL2ST_BONUS` to the outer
  HUD shell mode list (renders score, lives, message, specials,
  timer over the bonus content).
- `src/game_modes.c::mode_bonus_enter` — call
  `bonus_system_set_coins(ctx->bonus, ctx->bonus_count)` before
  `bonus_system_begin` (ordering documented inline citing ADR-040
  and `bonus_system.c:189,196`); also set
  `message_system_set(ctx->message, "- Bonus Tally -", 0,
  attract_frame_counter)`.
- `include/bonus_system.h` + `src/bonus_system.c` — new
  `bonus_system_set_coins(ctx, count)` public API with NULL /
  negative-count guard.
- `tests/test_bonus_system.c` — three new tests covering
  set/overwrite, NULL/negative guard, and the set-then-begin
  ordering contract (60/60 bonus_system tests pass).
- `tools/gen_bonus_fixtures.c` — added `bonus_count` and
  `level_time` to per-scenario fixture data so the modern
  capture path renders bonus coins and the timer.
- `original/main.c::setup_bonus_capture_scenario` —
  per-scenario `lives_left` matching `gen_bonus_fixtures.c`.

### Modern-side capture infrastructure (also `xboing-c-hlf`)

- `Makefile` — `bonus-fixtures`, `modern-bonus`,
  `modern-bonus-all` targets.
- `scripts/visual_capture.sh` — modern variant + `bonus` mode
  sets `XDG_DATA_HOME` to the scenario fixture and appends
  `-load`.
- `tests/visual-check-manifest.yaml` — extended with 4 bonus
  pairs (scenario 1 title + end-text, scenario 3 end-text,
  scenario 4 end-text).

### Quality

- `make format-check`: clean
- `make cppcheck-src`: clean (45 files, 0 errors)
- `make test` (debug): 57/57 pass, 1 disabled (offscreen — known)
- `make asan-test`: 57/57 pass under ASan + UBSan
- `make lint`: 86 files, 0 errors
- Code review on diff: clean, no critical/important findings

### Visual fidelity vs goldens (LLM judge)

`.tmp/bonus-final-report.md`:

- `bonus-1/end-text`: verdict `equivalent` — LLM judge cannot
  distinguish modern from original.
- `bonus-1/title`: still `diff`, but the findings are
  capture-timing differences (different ticks captured between
  original and modern), not renderer bugs.
- `bonus-3/end-text`: 1 finding — lives count diff. Documented
  capture-path artifact: `CheckAndAddExtraLife` static
  accumulator adds +1 life on tick 1 in the original-side
  capture path for scenarios with score ≥ 100,000. Modern
  fixture path doesn't fire that, so shows fixture value
  exactly. Not a renderer bug.
- `bonus-4/end-text`: 1 finding — same lives capture-path
  artifact.

## State of the working tree

### Staged for deletion (the 32 wrong PR #143 goldens)

`tests/golden/original/bonus-{1..4}/bonus/*.png` — `git rm`-ed,
replaced by fresh authentic captures (still untracked).

### Untracked

- `tests/golden/original/bonus-{1..4}/bonus/*.png` — 32 authentic captures
- `docs/research/2026-06-06-bonus-capture-real-entry.md`
- `docs/reviews/2026-06-06-bonus-capture-real-entry-review.md`
- `docs/specs/2026-06-06-bonus-renderer-rewrite.md`
- `docs/reviews/2026-06-06-bonus-renderer-rewrite-review.md`
- `tests/fixtures/bonus/scenario-{1..4}/xboing/*.dat`
- `tools/gen_bonus_fixtures.c`

### Modified

- `original/{main.c, init.c, bonus.c, include/bonus.h}`
- `src/{game_render.c, game_render_ui.c, game_modes.c, bonus_system.c, game_init.c, game_main.c, sdl2_cli.c, sdl2_font.c}`
- `include/{bonus_system.h, game_context.h, sdl2_cli.h, sdl2_font.h}`
- `tests/{test_bonus_system.c, CLAUDE.md, visual-check-manifest.yaml}`
- `Makefile`, `CMakeLists.txt`, `scripts/visual_capture.sh`
- `docs/{BUILDING.md, TESTING.md, DESIGN.md}`
- `.claude/rules/tests.md`

## What's next

1. **User visual verification** — open at least one modern/original
   pair in eog side-by-side, confirm parity.
2. **Commit** — single feature commit on this branch.
3. **PR** — push, open PR, follow 2-loop review workflow in
   `docs/GIT.md`.
4. **`xboing-c-hlf` close** after merge.

## Open beads at session pause

```text
xboing-c-hlf  P1  in_progress  Visual fidelity: end-of-level (bonus) screen
              ├── ✓ xboing-c-52s (closed)
              └── ✓ xboing-c-z0n (closed)
xboing-c-y7s  P1  open         [epic] Visual fidelity audit (parent of hlf)
```
