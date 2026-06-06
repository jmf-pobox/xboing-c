# Review: bonus-renderer rewrite spec

**Date:** 2026-06-06
**Reviewer:** jdc (peer review for xboing-c-hlf step 4)
**Subject:** docs/specs/2026-06-06-bonus-renderer-rewrite.md

## Verdict

APPROVED WITH CHANGES

Three spec claims need correction before implementation starts. One blocking
(setter must be called before `bonus_system_begin`, not documented that way
in step 3), one blocking (wrong `message_system` call signature), one
non-blocking (the ball border question deserves a definitive answer, not a
shrug).

## Blocking findings

### BF-1: `message_system_set_current` does not exist — wrong API name

The spec (Change 3) says to call `message_system_set_current(ctx->message,
"- Bonus Tally -")`. That function does not exist.

The public API (`include/message_system.h:57`) is:

```c
void message_system_set(message_system_t *ctx, const char *text,
                        int auto_clear, int frame);
```

Two extra parameters are required: `auto_clear` and `frame`. The original
(`original/bonus.c:232`) passes `True` for the `UpdateFlag` parameter of
`SetCurrentMessage`, which in the original implementation means "display
immediately." The closest modern equivalent is:

```c
message_system_set(ctx->message, "- Bonus Tally -", 0,
                   attract_frame_counter);
```

`auto_clear=0` keeps the message until replaced — matching the original's
behavior where `"- Bonus Tally -"` stays for the entire bonus sequence.
`auto_clear=1` with `MESSAGE_CLEAR_DELAY=2000` frames would also work for
a bonus sequence that takes fewer than 2000 attract frames, but 0 is the
safer default (original uses a persistent set, not a timed one).

The spec's hedge ("API verification: needs a one-line grep before
implementation — API name uncertain") was correct to flag this, but the
spec should have resolved it before being written. Implementation will fail
to compile as written.

### BF-2: Setter must be called before `bonus_system_begin`, not after

The spec (Change 4, Implementation order step 2) calls the setter, then
`bonus_system_begin`. That order is correct. But Change 4 does not clearly
state the constraint: if the order is reversed, `bonus_system_begin`
(`src/bonus_system.c:189`) captures `ctx->initial_coin_count = ctx->coin_count`
from the stale pre-set value, and `bonus_system_compute_total` (`src/bonus_system.c:196-197`)
computes the wrong score. The implementation is one line swap away from a
silent wrong-score bug.

Fix: the spec must explicitly document the ordering constraint. The
implementation order in Change 4 is already correct, but the comment in
the code must read:

```c
/* Set coins BEFORE begin — begin() captures initial_coin_count
 * and computes the bonus total from ctx->coin_count at that
 * point (bonus_system.c:189, 196). */
bonus_system_set_coins(ctx->bonus, ctx->bonus_count);
bonus_system_begin(ctx->bonus, &env, 0);
```

Without this comment, the next person reverting to "alphabetical order"
will produce a silent bug.

## Non-blocking findings

### NB-1: The ball border question is unresolved — spec should close it

The spec removes `draw_ball_border` and `SPR_TITLE_SMALL` citing the
goldens. The review brief asks whether the original draws them and whether
the goldens are misleading.

The original (`original/bonus.c:209-228`) calls `DrawBallBorder` and
`DrawSmallIntroTitle` on `mainWindow` before `XUnmapWindow(playWindow)`.
The border positions (`BORDER_LEFT=55`, `BORDER_TOP=73`,
`original/bonus.c:97-100`) land within the play area rectangle that
`playWindow` normally occludes. After `XUnmapWindow`, `mainWindow` is
exposed there. The ball border SHOULD be visible on `mainWindow`.

The 32 original goldens (`tests/golden/original/bonus-{1..4}/bonus/`) show
no ball border in any captured substate. The most plausible explanation:
the `WindowFadeEffect` loop (`original/bonus.c:224-225`) runs in-process
and synchronously fades `playWindow` to black by drawing black rectangles
of increasing opacity into `playWindow`. During this fade the ball border
on `mainWindow` is gradually revealed — but the snapshot is captured AFTER
`ResetBonus` sets `BonusState=BONUS_TEXT` and the `vc_check` fires. By
that time `XUnmapWindow` has already run, and the space background (set by
`ClearMainWindow` → `XClearWindow`) is what's behind the unmapped play
area. The ball sprites drawn by `DrawBallBorder` on `mainWindow` should
still be there.

This is unresolved. However the correct answer is: trust 32 authentic
goldens over static-analysis of X11 draw ordering. The goldens were
captured via the real `CheckGameRules` entry path (post-BF-1 fix in
xboing-c-z0n). All four scenarios, all 8 substates, no ball border visible.
The spec's removal of the border is correct.

If someone wants a definitive explanation: check whether `ClearMainWindow`
erases with the `spacePixmap` background (`XSetWindowBackground` +
`XClearWindow`), which would re-tile over any previous draws on `mainWindow`
between the `SetupBonusScreen` call and the actual Expose event that forces
a redraw. X11 preserving window content across `XClearWindow` depends on
`backing-store` hints. The composited desktop may also interfere. This
rabbit hole doesn't change the verdict — the goldens are correct.

**Conclusion:** the spec's removal is correct. The comment updating
(`original/bonus.c:DrawBallBorder / DrawSmallIntroTitle` provenance lines)
should happen as proposed.

### NB-2: `draw_ball_border` and `SPR_TITLE_SMALL` become dead after Change 1

After removing the calls in `game_render_bonus`, `draw_ball_border` has no
callers (`src/game_render_ui.c:939` definition, line 1036 sole call).
`SPR_TITLE_SMALL` has no callers (`src/game_render_ui.c:1042` is the only
use across `src/game_render.c` and `src/game_render_ui.c`). The spec flags
dead-code removal as out-of-scope and deferred.

That deferral is acceptable for the static helper `draw_ball_border` — it's
file-scoped and `-Wunused-function` will catch it at build time, making the
debt visible. The spec must address this before implementation because it
will cause a compiler error under `-Werror`. Either remove `draw_ball_border`
in this same PR (it's three lines of deletion) or suppress with
`(void)draw_ball_border` — but the latter is ugly. Just delete it.
`SPR_TITLE_SMALL` is an enum value in the sprite catalog, not a static
function, so it won't produce a warning.

**Action required:** Remove `draw_ball_border` static helper in Change 1.
It has no other callers. Dead static functions under `-Werror` break the
build.

### NB-3: `game_render_timer` guard is safe but context-dependent

`game_render_timer` (`src/game_render.c:912-934`) guards on
`ctx->time_bonus_total <= 0` before drawing. During `SDL2ST_BONUS`,
`ctx->time_bonus_total` holds the value from the just-completed level (set
at level load in `game_rules_next_level`, `src/game_rules.c:200-201`). It
is NOT reset to 0 when entering bonus mode. So for levels with a timer,
the timer will display on the bonus screen — matching the original goldens
which show `03:00` in the bottom-right for scenario-1.

No issue. Verified: the spec's "verified by reading the function bodies"
claim is correct for `game_render_timer`.

### NB-4: `bonus_count` reset timing is correct — verify the claim

Spec (Change 4) says the sync at `mode_bonus_enter` "handles savegame load
and natural gameplay uniformly." True. The reset at `src/game_rules.c:231`
is inside `game_rules_next_level`, which runs from `bonus_cb_on_finished`
AFTER the bonus screen finishes. So `ctx->bonus_count` holds the
just-completed level's count when `mode_bonus_enter` fires. The setter call
reads the correct value. Verified.

The `mode_game_enter` path (`src/game_modes.c:99`) also resets
`ctx->bonus_count = 0` at game start. So a fresh game always starts clean.
Verified.

### NB-5: `test_set_coins_then_begin_uses_set_value` test name — precision

The spec proposes `test_set_coins_then_begin_uses_set_value` which asserts
`initial_coin_count == set_count`. That test is valuable. But it should
also assert `g_score_added == bonus_system_compute_total(set_count, ...)` to
verify the score computation uses the setter value, not just the initial
count getter. One assertion covers the storage; the other covers the
computation path. Both are one-liners; add both.

## Verified claims

- **"None assume `mode == SDL2ST_GAME`"** — confirmed. `game_render_score`
  (`src/game_render.c:648`) reads `score_system_get(ctx->score)` — mode-agnostic.
  `game_render_lives` (`src/game_render.c:536`) reads `ctx->lives_left` —
  valid in bonus. `game_render_messages` (`src/game_render.c:893`) reads
  `message_system_get_text(ctx->message)` — mode-agnostic.
  `game_render_timer` (`src/game_render.c:912`) reads `ctx->time_bonus_total`
  and `ctx->time_remaining` — both set at level load, valid in bonus.
  `game_render_specials` (`src/game_render.c:951`) reads `ctx->special` and
  `ctx->paddle` — both valid in bonus.

- **`mode_bonus_enter` location at ~line 703** — confirmed at
  `src/game_modes.c:703`. `bonus_system_begin` call is at line 725.
  The setter insertion point is between the `env` struct initialization and
  the `bonus_system_begin` call. No ordering conflicts.

- **`draw_ball_border` sole caller at line 1036** — confirmed.
  `SPR_TITLE_SMALL` sole use at line 1042. Both in `game_render_bonus` only.

- **`ctx->bonus_count` reset before bonus entry is NOT the issue** — confirmed.
  Reset occurs in `game_rules_next_level` (`src/game_rules.c:231`) which
  runs from `bonus_cb_on_finished` (`src/game_callbacks.c:672`) AFTER the
  bonus screen completes. At `mode_bonus_enter` time, `ctx->bonus_count`
  holds the correct value.

- **`bonus_system_begin` captures `coin_count` at line 189** — confirmed.
  `ctx->initial_coin_count = ctx->coin_count` runs before
  `bonus_system_compute_total` at line 196. Setter must precede begin.

- **`bonus_system_inc_coins` is only called from tests in production code** —
  confirmed. Only callers: `tests/test_bonus_system.c` and
  `src/game_callbacks.c:296` (BONUS_BLK pickup handler). Wait — this needs
  correction.

## Disputed claims

### DC-1: "`bonus_system_inc_coins` called only from tests, never from production code"

The spec (Change 4 rationale, bullet 3) states `bonus_system_inc_coins`
remains a tests-only helper. This is wrong.

`src/game_callbacks.c:296-297`:

```c
ctx->bonus_count++;
if (ctx->bonus_count == 10)
```

That increments `ctx->bonus_count` — the GAME CONTEXT counter — not
`bonus_system_inc_coins`. So the accumulator is `ctx->bonus_count` and
`bonus_system_inc_coins` is indeed never called from production gameplay.
But the spec's framing was unclear enough to require a double-check.

Confirmed: `bonus_system_inc_coins` is called only from `tests/test_bonus_system.c`.
The spec's implementation intent (use the setter at mode entry, not
`inc_coins` during gameplay) is sound. The rationale text is misleading but
the conclusion is correct.

## Recommendations for implementation

1. **BF-1 fix:** Replace the non-existent `message_system_set_current` call
   with `message_system_set(ctx->message, "- Bonus Tally -", 0, attract_frame_counter)`.
   Verify `attract_frame_counter` is accessible at that point in
   `mode_bonus_enter` — it is (used on line 737 for `bonus_system_update`
   in the same function scope).

2. **BF-2 fix:** Add the ordering comment shown above. The call order in
   the spec (setter before begin) is already correct — the comment prevents
   future breakage.

3. **NB-2 fix:** Delete the `draw_ball_border` static function in the same
   change that removes its call. The function is 30 lines of dead code
   and will fail to compile under `-Werror -Wunused-function` once its only
   call is removed.

4. **NB-5 enhancement:** Add the score-computation assertion to
   `test_set_coins_then_begin_uses_set_value`. One line.

5. **`bonus_system_set_coins` clamping:** The spec uses `if (ctx == NULL || count < 0) return;`.
   That's the right guard. No upper bound clamping is needed — the original
   never caps `numBonus`, and `bonus_system_compute_total` handles the
   `> BONUS_MAX_COINS` super-bonus threshold internally. Don't add a
   `BONUS_MAX_COINS` clamp in the setter.

6. **Implementation order as written is correct.** Steps 1-5 in the spec
   are the right sequence. The HUD addition (Change 2) is safe to do at
   any point in the sequence since no HUD renderer assumes `SDL2ST_GAME`.
