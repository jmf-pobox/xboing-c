# Spec: modern bonus-screen renderer rewrite

**Date:** 2026-06-06
**Author:** Claude Agento (xboing-c-hlf, step 4 of renderer-rewrite plan)
**Inputs:**

- `docs/research/2026-06-06-bonus-capture-real-entry.md` (jck)
- `docs/reviews/2026-06-06-bonus-capture-real-entry-review.md` (jdc)
- Authentic original goldens: `tests/golden/original/bonus-{1..4}/bonus/`
- Modern captures: `.tmp/visual-check/modern/bonus-{1..4}/bonus/`
- LLM judge report: `.tmp/bonus-only-report.md`

## Problem

The modern bonus screen (`src/game_render_ui.c::game_render_bonus`)
was reshaped in the 2026-06-05/06 session against synthetic goldens
that bypassed the real `CheckGameRules` entry path. The reference
those goldens established is wrong. Now that authentic goldens
exist (32 PNGs, 4 scenarios × 8 substates), the modern renderer
must be re-aligned.

Four concrete deltas, consistent across all 4 scenarios, confirmed
by LLM judge and pixel-level inspection:

1. **Decorative ball border around the entire screen** — modern
   draws it, original goldens show no border.
2. **Gold XBOING title pixmap at the top** — modern draws it,
   original goldens show no pixmap.
3. **HUD subwindows missing** — original goldens render score
   (top-left), lives + level number (top-right), specials panel
   (bottom-middle), timer (bottom-right), and the
   `- Bonus Tally -` message (bottom-left) on top of the bonus
   content. Modern renders none of these.
4. **Bonus coin count is always zero** — modern shows
   `Sorry, no bonus coins collected.` for every scenario regardless
   of the fixture's `bonus_count` field. Root cause:
   `bonus_system->coin_count` is never set from
   `game_ctx->bonus_count`. `bonus_system_inc_coins` is called only
   from tests, never from production code.

## Non-goals

- No changes to the `bonus_system` state machine, timing, or
  computation (`bonus_system_compute_total`, `bonus_system_update`,
  `bonus_system_skip`).
- No changes to the original-side capture path (`xboing-c-z0n`
  already shipped that).
- No changes to the level-transition flow after `BONUS_STATE_FINISH`
  (that path already works).
- No changes to the modern fixture format (`tools/gen_bonus_fixtures.c`
  already writes `bonus_count` per `docs/research/...`).

## Design

### Change 1 — Remove ball border + XBOING pixmap from bonus renderer

File: `src/game_render_ui.c::game_render_bonus`

Remove:

- `draw_ball_border(ctx);` call (currently around line 1036).
- The full `sdl2_texture_get(ctx->texture, SPR_TITLE_SMALL, &title)`
  block + the `SDL_RenderCopy` for the XBOING pixmap (around lines
  1041-1050).
- The **`draw_ball_border` static helper definition** at line
  ~939 (per review NB-2: it has no other callers and `-Werror
  -Wunused-function` will fail the build otherwise). Drop the
  function and its `bonus.c:DrawBallBorder` block-comment header.
- Update the surrounding comment to drop the
  `bonus.c:DrawBallBorder` / `DrawSmallIntroTitle` provenance lines
  (those describe what's no longer drawn).

### Change 2 — Render HUD outer shell when in `SDL2ST_BONUS`

File: `src/game_render.c`

The outer-shell block at lines 1217-1229 renders the always-on HUD
elements (score, lives, messages, timer, specials) for attract
modes. The list does not include `SDL2ST_BONUS`. Add it:

```c
if (effective == SDL2ST_INTRO || effective == SDL2ST_INSTRUCT ||
    effective == SDL2ST_DEMO || effective == SDL2ST_PREVIEW ||
    effective == SDL2ST_KEYS || effective == SDL2ST_KEYSEDIT ||
    effective == SDL2ST_HIGHSCORE || effective == SDL2ST_BONUS)
```

The HUD renderers (`game_render_score`, `game_render_lives`,
`game_render_messages`, `game_render_timer`,
`game_render_specials`) each read state from `ctx` that is still
valid during `SDL2ST_BONUS` (the savegame-loaded or
gameplay-final state). Verified by reading each function's first
20 lines — none assume `mode == SDL2ST_GAME`.

The `deveyes` exception (`effective == SDL2ST_INTRO ||
effective == SDL2ST_KEYS`) does not apply to bonus.

### Change 3 — Set `- Bonus Tally -` message on bonus entry

File: `src/game_modes.c::mode_bonus_enter`

The original calls
`SetCurrentMessage(display, messWindow, "- Bonus Tally -", True);`
inside `DrawTitleText` (`original/bonus.c:232`), which runs on the
first tick of `BONUS_TEXT`. Modern equivalent (jdc review BF-1
clarification): the API at `include/message_system.h:57` is

```c
void message_system_set(message_system_t *ctx, const char *text,
                        int auto_clear, int frame);
```

Add after `bonus_system_begin`:

```c
/* "- Bonus Tally -" persists through the entire bonus sequence
 * (original/bonus.c:232 — passes True for UpdateFlag, which means
 * "show immediately, no timed clear").  auto_clear=0 matches.
 * attract_frame_counter is in scope from the local declaration
 * at the head of the function. */
message_system_set(ctx->message, "- Bonus Tally -", 0,
                   attract_frame_counter);
```

### Change 4 — Plumb `ctx->bonus_count` into `bonus_system`

The cleanest sync point is `mode_bonus_enter` (single canonical
point, covers both gameplay collection and savegame load).

File: `include/bonus_system.h`

Add a public setter:

```c
/* Set the bonus coin count, typically from the game-context
 * accumulator that game_callbacks maintains during gameplay.
 * Called once at mode entry before bonus_system_begin so that
 * initial_coin_count and the bonus-row sprites reflect the
 * collected total. */
void bonus_system_set_coins(bonus_system_t *ctx, int count);
```

File: `src/bonus_system.c`

```c
void bonus_system_set_coins(bonus_system_t *ctx, int count)
{
    if (ctx == NULL || count < 0)
        return;
    ctx->coin_count = count;
}
```

File: `src/game_modes.c::mode_bonus_enter` — before
`bonus_system_begin`:

```c
/* Set coins BEFORE begin — begin() captures
 * initial_coin_count and computes the bonus total from
 * ctx->coin_count at that point (bonus_system.c:189, 196).
 * Reversing this order silently produces wrong initial
 * counts AND a wrong total score. */
bonus_system_set_coins(ctx->bonus, ctx->bonus_count);
bonus_system_begin(ctx->bonus, &env, 0);
```

The comment is mandatory per review BF-2 — without it a future
"clean up the order" pass produces a silent score bug.

Rationale for sync-at-entry over sync-at-collection:

- Single sync point — simpler invariant.
- Handles savegame load and natural gameplay uniformly.
- `bonus_system_inc_coins` remains a tests-only helper, no
  surprise calls into bonus_system from gameplay code.
- The original's `IncNumberBonus` is called during gameplay
  (`game_callbacks` equivalent), but the modern split keeps
  `game_ctx->bonus_count` as the authoritative count and uses
  `bonus_system` as a renderer-side view. The single-point sync
  fits that split.

## Tests

### Existing tests that must still pass

- `tests/test_bonus_system.c` — all `inc_coins`/`dec_coins`
  tests should be unaffected (new setter doesn't replace them).
- Modern build: `make build` clean, `make test` 57/58 pass
  (the 1 disabled test is unrelated).

### New unit tests

`tests/test_bonus_system.c`:

- `test_set_coins_sets_count` — call setter, assert `get_coins`
  returns the value.
- `test_set_coins_floors_negative` — setter with negative
  argument is a no-op.
- `test_set_coins_then_begin_uses_set_value` — call setter, then
  begin, assert `initial_coin_count == set_count` AND assert
  `g_score_added == bonus_system_compute_total(set_count, env...)`
  (review NB-5: storage path AND computation path both verified
  in one test).

### Visual fidelity (the actual acceptance gate)

1. `make modern-bonus-all INTERVAL=200` — capture all 4 scenarios.
2. Compare manually against `tests/golden/original/bonus-N/bonus/`
   for each scenario.
3. Run `make visual-check` (extended manifest covers bonus pairs).
4. LLM judge should report findings reduce to minor positioning
   / font deltas — no more `[major] layout` HUD-missing or
   `[major] sprite` ball-border findings.

## Phase 2 additions — sound mapping + animation pacing (post-spec, user-driven)

After initial implementation passed visual fidelity, the maintainer
flagged that the work is not just pixel placement: sound effects
and per-step animation pacing for coins and bullets also need to
match the original. Audit of `src/bonus_system.c` against
`original/bonus.c playSoundFile` calls revealed:

| Branch | Original (file:line) | Modern (before fix) |
|---|---|---|
| `do_bonuses` timer-ran-out | `Doh4` (bonus.c:292) | `wzzz` |
| `do_bonuses` no-coins | `Doh1` (bonus.c:315) | `wzzz` |
| `do_level` no-level-bonus | `Doh2` (bonus.c:421) | none |
| `do_bullets` no-bullets | `Doh3` (bonus.c:450) | `wzzz` |
| `do_bullets` per-bullet | `key` (bonus.c:473) | none |
| `do_time_bonus` no-time-bonus | `Doh4` (bonus.c:520) | none |

All seven mismatches fixed. The `Doh1`/`Doh2`/`Doh3`/`Doh4` /
`key` audio files exist at `sounds/*.au` — the prior code used a
generic `wzzz` placeholder instead.

For animation pacing, the modern `bonus_system_update` runs
`ATTRACT_FRAME_MULTIPLIER = 6` sub-frames per game tick.
The pre-fix coin/bullet loops dropped one item per sub-frame call,
burning through the whole row in a single visual frame.

`BONUS_STEP_DELAY = 24` (defined in `include/bonus_system.h`)
applied via `set_bonus_wait` between each coin/bullet step in
`do_bonuses` / `do_bullets`. Derivation: modern default speed
gives a game tick of `SDL2L_TICK_UNIT_US * (10 - SDL2L_DEFAULT_SPEED)
= 1500 µs * 5 = 7.5 ms`. With 6 `bonus_system_update` calls per
game tick, each sub-frame = 1.25 ms. Original `SLOW_SPEED = 30 ms`
per coin/bullet → 30 / 1.25 = 24 sub-frames per step.

`BONUS_LINE_DELAY = 2400` (revised 2026-06-06 from initial value
of 100). Original `LINE_DELAY=100` game ticks × `SLOW_SPEED=30 ms`
= 3 seconds of WAIT between content states — readable rhythm for
the player. The earlier modern value of 100 sub-frames was 24×
too short; maintainer manually confirmed the bonus screen flashed
by in ~2 seconds, unreadable. 2400 sub-frames × 1.25 ms = 3 s,
matching original wall-clock. Total scenario duration ~25 s.

`BONUS_INIT_DELAY = 120` for the initial TEXT→SCORE snap
(original `bonus.c:257` uses `frame + 5` at SLOW_SPEED = 150 ms).

Both scale identically with the user's speed setting (1-9 in
modern, equivalent in original): faster speed → shorter delays.

New tests in `tests/test_bonus_system.c` (Group 7, 8 cases):

- `test_sound_per_coin_bonus` — `bonus` fires N times for N coins.
- `test_sound_no_coins_doh1` — `Doh1` fires, `wzzz` absent.
- `test_sound_timer_void_doh4` — `Doh4` fires twice (do_bonuses + do_time_bonus paths).
- `test_sound_no_level_bonus_doh2` — `Doh2` fires.
- `test_sound_no_bullets_doh3` — `Doh3` fires, `wzzz` absent.
- `test_sound_per_bullet_key` — `key` fires N times for N bullets.
- `test_sound_end_text_applause` — `applause` fires exactly once.
- `test_per_coin_pacing` — at most 1 coin drops per tick.

Test harness extension: `g_sound_history[]` buffer captures every
sound name fired during a sequence; `sound_history_contains` /
`sound_history_count` helpers enable per-branch assertions.

## Known capture-side divergence (not a renderer issue)

`CheckAndAddExtraLife` (`original/level.c:370-383`) uses
`static int ballInc = 0`. On the very first tick after force-entry,
scenarios with `score >= NEW_LIVE_SCORE_INC (100000L)` cross the
threshold and gain +1 life before `SetupBonusScreen` runs. The
modern fixture path loads directly into `SDL2ST_BONUS` and does not
fire `CheckAndAddExtraLife`, so the modern capture shows the
fixture's `lives_left` value exactly. This produces a 1-life
diff on scenarios 3 (lives 3 vs 2) and 4 (lives 2 vs 1) between
the two capture paths. The LLM judge flags this as a `[major]
sprite` finding but it is a real game-state divergence between the
two capture mechanisms, not a renderer bug. Either capture path is
internally consistent. Documented in
`docs/reviews/2026-06-06-bonus-capture-real-entry-review.md` NB-2.

## Risks

1. **The HUD outer shell may render fields that don't exist at
   bonus mode entry.** Mitigation: each HUD renderer is read-only
   on `ctx` fields that are persistent across modes (score,
   level_number, lives, time_remaining, specials). Verified by
   reading the function bodies. If a renderer dereferences a
   mode-specific subsystem (e.g., gameplay-only block grid), it
   will be visible immediately and reverted.

2. **`- Bonus Tally -` message may not display because the message
   system has mode-specific filtering.** Mitigation: trace through
   `message_system` to confirm it accepts arbitrary text in any
   mode before calling.

3. **The setter pattern conflicts with `bonus_system_inc_coins`
   semantics if both are called.** Mitigation: in tests, only
   `inc/dec` is used. In production, only the setter is used.
   The setter overrides any prior count, which is the desired
   behavior at mode entry (the canonical sync point).

4. **`SPR_TITLE_SMALL` or `draw_ball_border` may have other
   callers and removing them from `game_render_bonus` doesn't
   make them dead.** Mitigation: grep both before removal;
   leave them in place if referenced elsewhere (preserves the
   sprite catalog and other callers).

## Implementation order

1. Add `bonus_system_set_coins` to header + .c (smallest
   surface, no callers yet).
2. Call setter from `mode_bonus_enter` before
   `bonus_system_begin`.
3. Add `SDL2ST_BONUS` to the outer-shell condition in
   `game_render.c`.
4. Remove `draw_ball_border` + XBOING pixmap from
   `game_render_bonus`.
5. Add `message_system_set_current` call in `mode_bonus_enter`.
6. Build + ctest (verify nothing breaks).
7. Capture modern bonus scenarios; visual diff vs goldens.
8. LLM judge re-run; verify the four `[major]` findings are
   gone or substantially reduced.
9. Code review pass on the diff.
10. Address review findings.

## Definition of Done

- `make check` clean (format, cppcheck, lint, test, asan-test,
  deb-lint).
- Visual fidelity vs all 4 original goldens: HUD subwindows
  present, no ball border, no XBOING pixmap, bonus coin row
  shows N sprites for scenarios with N coins.
- LLM judge: no `[major]` findings of the four kinds in section
  Problem.
- `pr-review-toolkit:code-reviewer` clean on diff.
- User visual verification on at least one scenario.
