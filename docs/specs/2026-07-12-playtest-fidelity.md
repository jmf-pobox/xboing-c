# Editor Play-Test Fidelity — Implementation Blueprint

Three user-reported bugs, one subsystem: the editor's play-test
round-trip (`EDIT` → `GAME` → `EDIT` via the `P` key).

- `xboing-hay` — lives deplete during play-test and game-over fires
  on depletion; the editor cursor isn't restored on return.
- `xboing-nl4` — `mode_edit_enter`'s reset zeroes the modified flag
  on return from play-test, so the unsaved-work confirm under-fires.
- `xboing-bsz` — board edits aren't preserved across a play-test
  round-trip.

Mission: `m-2026-07-12-024`, worker `jdc`, evaluator `gjm`.

## 1. The original play-test contract

### 1.1 `SetupPlayTest` / `FinishPlayTest` (`original/editor.c:587-645`)

```c
static void SetupPlayTest(Display *display)
{
    EditState = EDIT_TEST;
    snprintf(tempName, sizeof(tempName), "%s", tempnam("/tmp", "xboing-"));
    if (SaveLevelDataFile(display, tempName) == False)
        ShutDown(display, 1, "Sorry, cannot save test play level.");
    if (ReadNextLevel(display, playWindow, tempName, False) == False)
        ShutDown(display, 1, "Sorry, cannot load play test level.");
    RedrawEditorArea(display, playWindow);
    SetLivesLeft(3);
    ...
    ClearAllBalls();
    currentPaddleSize = PADDLE_HUGE;
    ResetPaddleStart(display, playWindow);
    ResetBallStart(display, playWindow);
    ...
}

static void FinishPlayTest(Display *display)
{
    EditState = EDIT_NONE;
    ...
    if (ReadNextLevel(display, playWindow, tempName, False) == False)
        ShutDown(display, 1, "Sorry, cannot load play test level.");
    HandleRandomBlocks(display);
    RedrawEditorArea(display, playWindow);
    unlink(tempName);
}
```

`SetupPlayTest` writes the **current in-memory edited board** to a
scratch temp file (`tempnam("/tmp", ...)`), then immediately reloads
from that same file — a round-trip whose only purpose is to hand the
grid to the same load path the rest of the game uses, not to change
its contents. `FinishPlayTest` reloads from the **same tempName**
again, which restores the board to exactly the state it was in the
instant play-test started — undoing any block damage the test
session caused (broken blocks, consumed bonus blocks, counter-slide
decrements) — then deletes the file. `modified` is never touched by
either function: whatever it was before play-test, it still is after.

### 1.2 `mode` never leaves `MODE_EDIT` during play-test

This is the load-bearing fact that makes the rest of the original's
behavior fall out for free. `EditState` (`EDIT_NONE` / `EDIT_TEST` /
...) is a **sub-state nested inside** `MODE_EDIT` — the outer `mode`
global is set to `MODE_EDIT` exactly once, on entry
(`original/main.c:680`), and is not reset to `MODE_INTRO` until
`DoFinish` on a genuine, full editor exit (`original/editor.c:386`).
Play-test never touches `mode` — confirmed by grepping every
`mode = MODE_*` assignment in `original/*.c`: the only two editor-
adjacent ones are those two. So for the entire play-test session,
`mode == MODE_EDIT` continuously.

### 1.3 `DecExtraLife` / `DeadBall` — infinite lives as a side effect, not a special case

```c
/* original/level.c:346-357 */
void DecExtraLife(Display *display)
{
    if (mode != MODE_EDIT)
        livesLeft--;
    if (livesLeft < 0)
        livesLeft = 0;
    DisplayLevelInfo(display, levelWindow, level);
}
```

```c
/* original/level.c:474-505 */
void DeadBall(Display *display, Window window)
{
    ...
    if (livesLeft <= 0 && GetAnActiveBall() == -1)
        EndTheGame(display, window);
    else
    {
        if (GetAnActiveBall() == -1 && livesLeft > 0)
        {
            ...
            ChangePaddleSize(display, window, PAD_EXPAND_BLK);
            ChangePaddleSize(display, window, PAD_EXPAND_BLK);
            DecExtraLife(display);
            ResetBallStart(display, window);
        }
    }
}
```

Because `mode == MODE_EDIT` for the whole test session,
`DecExtraLife`'s decrement never executes — `livesLeft` stays pinned
at the `3` `SetupPlayTest` set it to (`original/editor.c:601`).
`DeadBall`'s `livesLeft <= 0` game-over check can therefore never
trip during play-test, so `EndTheGame` (→ `MODE_HIGHSCORE`) is never
reached; every ball loss falls through to the "still have lives"
branch: paddle re-expanded to huge, ball reset, no life lost. The
original needed **no dedicated play-test flag** for this because one
global (`mode`) already encoded "we are logically still in the
editor" throughout the whole test session.

### 1.4 Cursor

`ChangePointer` is called from exactly two places in the whole
codebase: `editor.c` (five call sites) and `main.c:1713` (once, at
startup, to hide the cursor for normal play). `SetUpEditor` sets
`playWindow`'s cursor to `CURSOR_PLUS` on editor entry
(`original/editor.c:182`); `DoFinish` sets it to `CURSOR_NONE` on
full editor exit (`:375-376`). **Neither `SetupPlayTest` nor
`FinishPlayTest` touches the cursor at all** — the editor's crosshair
cursor simply stays in place, unmodified, through the whole play-test
session (button-down/up handlers do swap it to `CURSOR_SKULL` for
erase-drag and back, `:535`, `:573`, but that's edit-mode-only mouse
feedback, not play-test-related). There is no cursor behavior in the
original for play-test to restore — the bug's real cause has to be
that the modern game-over hijack described in §2.2 below prevents
play-test from ever reaching its normal "return to editor" exit path
at all.

## 2. The modern play-test path (as shipped) and where it breaks

### 2.1 Entry and exit wiring

`P` inside the editor calls `editor_system_key_input(ctx->editor,
EDITOR_KEY_PLAYTEST)` (`src/game_modes.c:1547-1548`). Inside
`editor_system.c`, the first press sets `ctx->state =
EDITOR_STATE_TEST` and fires `on_playtest_start`
(`src/editor_system.c:1010-1015`); the second press (only handled
while `ctx->state == EDITOR_STATE_TEST`,
`src/editor_system.c:1046-1055`) sets `ctx->state =
EDITOR_STATE_NONE` and fires `on_playtest_end`. The integration
layer wires these directly to state-machine transitions
(`src/game_callbacks.c:1119-1129`):

```c
static void editor_cb_on_playtest_start(void *ud)
{
    game_ctx_t *ctx = ud;
    sdl2_state_transition(ctx->state, SDL2ST_GAME);
}

static void editor_cb_on_playtest_end(void *ud)
{
    game_ctx_t *ctx = ud;
    sdl2_state_transition(ctx->state, SDL2ST_EDIT);
}
```

`mode_game_enter`'s `prev == SDL2ST_EDIT` branch
(`src/game_modes.c:222-253`) resets lives to 3, clears balls, sets
the paddle huge, and refills ammo — "use existing blocks, just place
the ball," i.e. it deliberately does **not** touch `ctx->block`. This
is architecturally different from the original: the modern port has
to switch the *entire top-level state machine* into `SDL2ST_GAME` to
reuse the real gameplay renderer/input/rules pipeline for play-test,
whereas the original's `mode` never left `MODE_EDIT`. That difference
is exactly why the modern port needs an explicit flag where the
original needed none — see §3.1.

### 2.2 `xboing-hay` root cause: no play-test guard on ball death

```c
/* src/game_rules.c:283-325 (game_rules_ball_died) */
void game_rules_ball_died(game_ctx_t *ctx)
{
    if (ball_system_get_active_count(ctx->ball) > 0)
        return;

    ctx->lives_left--;

    if (ctx->lives_left <= 0)
    {
        ...
        sdl2_state_transition(ctx->state, SDL2ST_HIGHSCORE);
        return;
    }
    ...
}
```

Nothing here mirrors `DecExtraLife`'s `mode != MODE_EDIT` guard.
During play-test, `ctx->lives_left` decrements exactly like a real
game and, on the third loss, transitions to `SDL2ST_HIGHSCORE` —
firing real game-over semantics (score submission eligibility,
"GAME OVER" message, audio) for a session that is supposed to be
infinite-lives testing.

**This is also the root cause of the cursor half of `xboing-hay`.**
`mode_edit_enter` already unconditionally restores the cursor on
every entry into `SDL2ST_EDIT`:

```c
/* src/game_modes.c:1353-1360 */
static void mode_edit_enter(sdl2_state_mode_t mode, void *ud)
{
    ...
    if (ctx->cursor)
        sdl2_cursor_set(ctx->cursor, SDL2CUR_PLUS);
```

That line runs on **every** transition into `SDL2ST_EDIT`, including
a normal `P`-key play-test end. The reason the cursor visibly stays
hidden is that the game-over hijack above means the player is never
routed back to `SDL2ST_EDIT` through the normal
`editor_cb_on_playtest_end` path at all — the state machine jumps to
`SDL2ST_HIGHSCORE` instead, and `mode_edit_enter`'s cursor-restore
line never runs. Fixing §3.2 below (skip the decrement/game-over
transition during play-test) removes the hijack, guarantees the
player always returns to `SDL2ST_EDIT` via
`editor_cb_on_playtest_end`, and the existing unconditional
cursor-restore line at `game_modes.c:1360` then does its job with
zero new code. Confirmed no other code path sets `SDL2CUR_NONE`
except `mode_game_enter`'s unconditional
`sdl2_cursor_set(ctx->cursor, SDL2CUR_NONE)` (`game_modes.c:270-271`)
on *entering* `SDL2ST_GAME` — nothing hides it again after that until
the mode changes, and nothing in `EDITOR_STATE_TEST` handling
touches it either (`src/game_modes.c:1512`, explicitly gated to
`EDITOR_STATE_NONE` so it "doesn't fight [play-test]'s cursor").

**Same defect, a second call site: level-complete → `SDL2ST_BONUS`.**
`game_rules_check` (`src/game_rules.c:331-341`) transitions to
`SDL2ST_BONUS` whenever `block_system_still_active` goes false, with
no play-test guard — clearing every required block while
play-testing hijacks the state machine into the real bonus sequence,
the same class of bug as the game-over hijack above, just with a
different terminal screen. The original's equivalent,
`CheckGameRules` (`original/level.c:398-419`), is never even called
during play-test: its only call site is guarded
`if (mode == MODE_GAME)` (`original/main.c:1140-1141`), and per §1.2
`mode` stays `MODE_EDIT` for the whole editor session, play-test
included. This is folded into Stage 2 below (jck round 2 review),
not left as a follow-up — it is the fourth manifestation of the
identical defect (an editor-session action reaching a real-game
terminal screen) the `play_test_active` flag exists to close, and
leaving it out would be an incomplete application of the mechanism
this design itself introduces.

### 2.3 `xboing-nl4` and `xboing-bsz` root cause: one over-broad guard

```c
/* src/game_modes.c:1353-1404 (mode_edit_enter) */
static void mode_edit_enter(sdl2_state_mode_t mode, void *ud)
{
    ...
    if (ctx->cursor)
        sdl2_cursor_set(ctx->cursor, SDL2CUR_PLUS);

    special_system_turn_off(ctx->special);

    if (sdl2_state_previous(ctx->state) != SDL2ST_DIALOGUE)
    {
        sdl2_renderer_set_logical_width(ctx->renderer,
                                        SDL2R_LOGICAL_WIDTH + EDITOR_TOOL_WIDTH);
        editor_system_reset(ctx->editor);
        editor_system_init_palette(ctx->editor, MAX_STATIC_BLOCKS);
        editor_system_set_level_title(ctx->editor, level_system_get_title(ctx->level));
    }
}
```

The guard only excludes `SDL2ST_DIALOGUE` (added by the editor-
dialogue-parity mission, `docs/specs/2026-07-11-editor-parity.md`
§1.5(a)). `sdl2_state_transition` sets `ctx->previous = ctx->current`
*before* switching (`src/sdl2_state.c:126-164`), so when
`editor_cb_on_playtest_end` calls `sdl2_state_transition(ctx->state,
SDL2ST_EDIT)`, `sdl2_state_previous()` reads `SDL2ST_GAME` —
`!= SDL2ST_DIALOGUE`, so the guarded block fires in full on every
single play-test return, exactly as if the player had freshly entered
the editor from the title screen.

`editor_system_reset` zeroes `modified` and `level_number`
directly (`src/editor_system.c:297-308`, `xboing-nl4`'s direct cause)
— but it *also* sets `ctx->state = EDITOR_STATE_LEVEL`
(`:301`), and `editor_system_update`'s next tick dispatches
`EDITOR_STATE_LEVEL` straight into `do_load_level`
(`src/editor_system.c:236-254`, `:263-266`), which calls
`editor_cb_load_level` → `block_system_clear_all(ctx->block)` +
`level_system_load_file(ctx->level, ".../editor.data")`
(`src/game_callbacks.c:903-918`) — i.e. it **reloads the on-disk
scratch level file from scratch**, discarding whatever the player had
just drawn before pressing `P`. That second-order consequence of the
same over-broad guard is `xboing-bsz`'s root cause: it is not a
missing snapshot, it is an *unwanted* reload firing where the
original never reloads from `editor.data` at all during a play-test
round-trip (it reloads from its own just-saved `tempName`, never from
`editor.data`).

`do_load_level` also independently sets `ctx->modified = 0`
(`:253`) — a second, redundant zeroing point for `xboing-nl4` beyond
`editor_system_reset`'s own `ctx->modified = 0`. Both need to stop
firing on a play-test return; fixing the guard that gates
`editor_system_reset` from running fixes both, since the reload path
is only reached *through* `editor_system_reset` setting `state =
EDITOR_STATE_LEVEL`.

**Gotcha found during this design pass, not obvious from a static
read: the canvas-widen call lives in the same guarded block.**
`mode_edit_exit` (`src/game_modes.c:1406-1436`) unconditionally
narrows the canvas (`sdl2_renderer_set_logical_width(ctx->renderer,
SDL2R_LOGICAL_WIDTH)`) whenever `SDL2ST_EDIT` is exited for a reason
other than a dialogue push — including the `EDIT → GAME` play-test
transition. If the fix simply adds `&& !ctx->play_test_active` to
the *whole* existing `if` in `mode_edit_enter`, the canvas will never
re-widen on return from play-test (since the entry-side widen call
would now be skipped too), leaving the tool palette permanently
clipped after the first play-test round-trip. **The widen call and
the reset/init-palette calls need independent guards** — see §3.4.

## 3. Recommended mechanism

### 3.1 Play-test detection: a dedicated `ctx->play_test_active` flag

**Recommendation: add `bool play_test_active` to `game_ctx_t`
(`include/game_context.h`), set `true` in
`editor_cb_on_playtest_start` before the `SDL2ST_GAME` transition,
cleared `false` in `editor_cb_on_playtest_end` after the `SDL2ST_EDIT`
transition returns.** Reject inferring play-test from
`sdl2_state_previous()` / `sdl2_state_saved_mode()`.

**Why not infer from `previous`/`saved_mode`.** One existing call
site already does this today, and it is exactly the fragile pattern
to retire, not extend: `game_input.c:314-327`'s Escape handler
(inside `if (mode == SDL2ST_GAME)`) uses
`sdl2_state_previous(ctx->state) == SDL2ST_EDIT` as its own ad hoc
"are we play-testing" check, to route Escape straight back to
`SDL2ST_EDIT` instead of opening the abort-game confirm dialogue.
This happens to be correct *today* only because `EDIT → GAME` has
exactly one call site (`editor_cb_on_playtest_start`), so `previous
== SDL2ST_EDIT` while current is `GAME` is currently synonymous with
"play-testing." But `previous` is a single-slot value that gets
overwritten by the *next* transition regardless of semantic
relevance — `mode_edit_enter`'s own bug (§2.3) is a direct
demonstration of the same class of fragility one hop later
(`DIALOGUE` push/pop overwrites it, requiring a bespoke exclusion,
and the play-test hop needed a second one). Any future mode inserted
between `GAME` and `EDIT` (e.g. a pause/pop sequence that updates
`previous` without the player ever really leaving play-test) silently
breaks both call sites again, and there would then be **two
independently-decaying heuristics** for the same fact. Per this
project's own principle ("single source of truth wins — when two
places encode the same fact, drive one from the other,"
`CLAUDE.md`), both call sites should read one flag instead.

**Why a dedicated `bool` cannot leak into a real game.** The flag is
set in exactly one function (`editor_cb_on_playtest_start`) and
cleared in exactly one function (`editor_cb_on_playtest_end`), both
private to `game_callbacks.c` and reachable **only** through
`editor_system`'s `EDITOR_KEY_PLAYTEST` handling
(`src/editor_system.c:1010-1015`, `:1046-1055`), which in turn is
reachable only from `SDL2ST_EDIT`'s own key dispatch
(`src/game_modes.c:1547-1548`). There is no code path that starts a
*real* game (`start_new_game`, `mode_game_enter`'s attract-cycle /
game-over / bonus branches, savegame load/restore) that sets this
flag, and a real game's `mode_game_enter` entry never runs through
`prev == SDL2ST_EDIT` unless it came from a play-test start (the only
`EDIT → GAME` transition in the codebase — confirmed by grepping
every `sdl2_state_transition(..., SDL2ST_GAME)` call site). The flag
defaults to `false` (the context is `calloc`'d,
`src/game_init.c:231`) and is cleared unconditionally at the end of
every play-test session, so there is no window where a subsequent
*real* game session could start with it still `true` — the only two
writers bracket the entire play-test lifetime and nothing else
touches the field.

**Consistency cleanup bundled into the same stage.** Once the flag
exists, `game_input.c:317`'s Escape check and (optionally,
low-risk, purely for single-source-of-truth hygiene since it is
already provably equivalent) `mode_game_enter`'s `prev ==
SDL2ST_EDIT` branch condition (`game_modes.c:222`) should read
`ctx->play_test_active` instead of re-deriving the same fact from
`sdl2_state_previous()`. `mode_game_enter`'s check is unambiguous
either way (there's genuinely only one path into `GAME` from `EDIT`),
so switching it is optional polish, not a correctness fix — but
leaving one of the two call sites on the old heuristic after
introducing the canonical flag would itself violate the same
"drive one from the other" principle this stage exists to satisfy.

### 3.2 `xboing-hay` (lives / game-over)

```c
/* src/game_rules.c — game_rules_ball_died, illustrative diff */
void game_rules_ball_died(game_ctx_t *ctx)
{
    if (ball_system_get_active_count(ctx->ball) > 0)
        return;

    /* Play-test: lives never deplete, matching DecExtraLife's
     * `if (mode != MODE_EDIT) livesLeft--;` no-op (original/level.c:
     * 349-350).  The original never needed a dedicated flag for this
     * because `mode` stays MODE_EDIT for the whole editor session,
     * play-test included (original/main.c:680, editor.c:386) -- so
     * DeadBall's `livesLeft <= 0` game-over check (original/level.c:
     * 482-483) can never trip.  The modern port re-enters a
     * genuinely distinct SDL2ST_GAME mode for play-test, so it needs
     * ctx->play_test_active to recover the same fact. */
    if (!ctx->play_test_active)
    {
        ctx->lives_left--;

        if (ctx->lives_left <= 0)
        {
            if (ctx->audio)
                sdl2_audio_play_at_percent(ctx->audio, "game_over", 99);
            message_system_set(ctx->message, "GAME OVER", 0, 0);
            sdl2_state_transition(ctx->state, SDL2ST_HIGHSCORE);
            return;
        }
    }

    /* Still have lives (or play-testing) -- reset ball on paddle. */
    if (ctx->audio)
        sdl2_audio_play_at_percent(ctx->audio, "balllost", 99);
    if (!gun_system_get_unlimited(ctx->gun))
    {
        gun_system_add_ammo(ctx->gun);
        gun_system_add_ammo(ctx->gun);
    }
    paddle_system_set_reverse(ctx->paddle, 0);
    ball_system_env_t env = game_callbacks_ball_env(ctx);
    ball_system_reset_start(ctx->ball, &env);
}
```

Only the decrement + game-over branch is guarded. The "reset ball
and keep playing" tail is **not** duplicated or special-cased — in
the original, `DeadBall`'s ammo/paddle/ball-reset side effects are
not gated by `mode` either (they live in the same unconditional
`else` branch that the guarded `DecExtraLife` call sits inside), so
play-test and real gameplay should keep sharing that exact code path.
This keeps the diff to one `if` around the two lines that must not
fire during play-test, rather than forking the whole function.

**Same fix, second call site: `game_rules_check`'s level-complete
transition.** Folded into this stage per jck's round 2 review — see
§2.2's addendum:

```c
/* src/game_rules.c — game_rules_check, illustrative diff */
void game_rules_check(game_ctx_t *ctx)
{
    /* Level completion: no required blocks remain -> go to bonus
     * screen.  Guarded the same way as game_rules_ball_died's
     * game-over transition (this function): clearing the board
     * during play-test must not reach the real bonus sequence,
     * matching CheckGameRules's mode==MODE_GAME-only call site
     * (original/main.c:1140-1141) -- see §2.2. */
    if (!block_system_still_active(ctx->block) && !ctx->play_test_active)
    {
        special_system_turn_off(ctx->special);
        if (ctx->audio)
            sdl2_audio_play_at_percent(ctx->audio, "applause", 70);
        sdl2_state_transition(ctx->state, SDL2ST_BONUS);
        ...
    }
    ...
}
```

Same flag, same file, same mechanism as the ball-death guard above —
one stage, one `git blame`-able change to `game_rules.c`'s play-test
posture, not two.

### 3.3 `xboing-hay` (cursor)

No new code. Per §2.2, this is a symptom of the game-over hijack, not
an independent gap — once §3.2 stops play-test from ever reaching
`SDL2ST_HIGHSCORE`, every play-test session ends through
`editor_cb_on_playtest_end` → `sdl2_state_transition(ctx->state,
SDL2ST_EDIT)`, and `mode_edit_enter`'s existing unconditional
`sdl2_cursor_set(ctx->cursor, SDL2CUR_PLUS)` (`game_modes.c:1360`)
restores it. The test plan (§5) includes a characterization test
that would have caught this without any cursor-specific code change,
to prove the claim rather than assert it.

### 3.4 `xboing-nl4` (modified flag) and the canvas-widen split

Split `mode_edit_enter`'s single guard into two independent
conditions — this is the fix for both `xboing-nl4` and the canvas
gotcha noted in §2.3:

```c
static void mode_edit_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    if (ctx->cursor)
        sdl2_cursor_set(ctx->cursor, SDL2CUR_PLUS);

    special_system_turn_off(ctx->special);

    bool from_dialogue = sdl2_state_previous(ctx->state) == SDL2ST_DIALOGUE;

    /* Canvas width: re-widen whenever we're not resuming from a
     * dialogue (mode_edit_exit's matching guard never narrowed it
     * for a dialogue push -- see game_modes.c:1422-1423).  This must
     * run on a play-test return too: the EDIT->GAME transition at
     * play-test start narrowed it via mode_edit_exit, and nothing
     * else re-widens it. */
    if (!from_dialogue)
        sdl2_renderer_set_logical_width(ctx->renderer,
                                        SDL2R_LOGICAL_WIDTH + EDITOR_TOOL_WIDTH);

    /* Reset/reload: skip for BOTH a dialogue resume (existing guard,
     * docs/specs/2026-07-11-editor-parity.md S1.5(a)) AND a play-test
     * return (new) -- see docs/specs/2026-07-12-playtest-fidelity.md
     * S2.3/S3.4.  editor_system_reset() zeroes modified/level_number
     * and forces EDITOR_STATE_LEVEL, whose next tick reloads
     * editor.data from disk (src/editor_system.c do_load_level) --
     * exactly the two effects a play-test round-trip must NOT have
     * (xboing-nl4, xboing-bsz). */
    if (!from_dialogue && !ctx->play_test_active)
    {
        editor_system_reset(ctx->editor);
        editor_system_init_palette(ctx->editor, MAX_STATIC_BLOCKS);
        editor_system_set_level_title(ctx->editor, level_system_get_title(ctx->level));
    }
}
```

`ctx->play_test_active` must still read `true` at this point —
`editor_cb_on_playtest_end` clears it only *after*
`sdl2_state_transition` returns (§3.5), and `sdl2_state_transition`
calls `on_enter` synchronously before returning
(`src/sdl2_state.c:126-164`), so the ordering is safe by construction,
not by timing luck.

### 3.5 `xboing-bsz` (board round-trip)

**Recommendation: in-memory capture/restore via the existing pure
`savegame_system_capture` / `savegame_system_restore` functions
(`include/savegame_system.h`), not a temp-file save/reload mirroring
`SetupPlayTest`/`FinishPlayTest`.**

**Two options considered:**

1. **In-memory snapshot (recommended).** Capture
   `ctx->play_test_snapshot_info` / `ctx->play_test_snapshot_level`
   (new `savegame_data_t` / `savegame_level_t` fields on
   `game_ctx_t`, embedded by value — mirrors the existing precedent
   of embedding `highscore_table_t`/`paths_config_t` by value,
   `include/game_context.h:19-20`) via `savegame_system_capture` at
   play-test start, and apply them back via `savegame_system_restore`
   at play-test end. Both functions are already documented as "pure:
   no file I/O" (`include/savegame_system.h:9-13`), already exercised
   by the savegame test-fixture pattern (`docs/TESTING.md`), and
   already know how to round-trip everything a play-test session can
   mutate: `occupied`/`type`/`counter_slide`/`random`/`hit_points`
   and `BLACK_BLK`'s `next_frame_offset`
   (`include/savegame_system.h:29-33`).
2. **Temp-file save/reload, mirroring the original exactly.**
   Rejected. `tempnam()` is a documented TOCTOU/symlink race and is
   flagged by modern toolchains (`-Wdeprecated-declarations` on
   glibc); reproducing it would be re-introducing a known-insecure
   pattern this codebase's own compiler-warnings policy (`-Wall
   -Wextra -Wpedantic -Werror`, `docs/BUILDING.md`) actively guards
   against. More fundamentally, the original needed a **file**
   round-trip because `SetupPlayTest`/`FinishPlayTest` and the real
   game loop lived in the same process but reinitialized global state
   (`blocks[][]`, `livesLeft`, etc.) through the same load path the
   game already used — the file *was* the hand-off mechanism between
   "editor edits" and "game state." The modern port keeps a single
   `game_ctx_t` (and a single `block_system_t` inside it) alive across
   the entire `EDIT ⇄ GAME` transition — `mode_game_enter`'s
   `prev == SDL2ST_EDIT` branch already says as much in its own
   comment ("use existing blocks, just place ball"). There is no
   process boundary or global-reinitialization step to bridge, so
   there is nothing a file round-trip would buy that an in-memory
   struct copy doesn't already provide, with no disk I/O, no cleanup-
   on-crash concern, and no temp-path collision risk under parallel
   test runs.

**Exact hook points**, both in `src/game_callbacks.c` (only file that
currently defines these two callbacks):

```c
static void editor_cb_on_playtest_start(void *ud)
{
    game_ctx_t *ctx = ud;

    /* Snapshot the board (+ incidental session state) before
     * mode_game_enter's EDIT branch runs.  In-memory equivalent of
     * SetupPlayTest's save-to-tempName (original/editor.c:591-597) --
     * see docs/specs/2026-07-12-playtest-fidelity.md S3.5 for why no
     * file round-trip is needed here. */
    ctx->play_test_active = true;
    savegame_system_capture(ctx, &ctx->play_test_snapshot_info,
                            &ctx->play_test_snapshot_level);

    /* Faithful port: original zeroes score for the test session
     * (original/editor.c:603, SetTheScore(0L)) rather than carrying
     * over whatever the editor's incidental score was. */
    score_system_set(ctx->score, 0);

    sdl2_state_transition(ctx->state, SDL2ST_GAME);
}

static void editor_cb_on_playtest_end(void *ud)
{
    game_ctx_t *ctx = ud;

    /* Undo any block damage from the test session and restore the
     * pre-test board -- matches FinishPlayTest's reload from
     * tempName (original/editor.c:634-636), NOT a reload from
     * editor.data (which would discard unsaved edits, xboing-bsz).
     * Must run before the transition below: mode_edit_enter's
     * play_test_active guard (game_modes.c) still needs to read
     * true. */
    savegame_system_restore(ctx, &ctx->play_test_snapshot_info,
                            &ctx->play_test_snapshot_level);
    score_system_set(ctx->score, 0); /* original/editor.c:629-630 */

    sdl2_state_transition(ctx->state, SDL2ST_EDIT);

    /* Clear only after the transition returns -- see S3.4. */
    ctx->play_test_active = false;
}
```

**Interaction with the level_system grid the editor renders.**
`savegame_system_restore`'s level half writes through
`block_system` (`ctx->block`), the same instance the editor's own
renderer and `editor_cb_query_cell`/`editor_cb_add_block` read and
write (`src/game_callbacks.c`). There is no separate "editor grid" —
`SDL2ST_EDIT` and `SDL2ST_GAME` already share one `block_system_t` by
construction (confirmed by `mode_game_enter`'s "use existing blocks"
branch never calling `block_system_clear_all` or reloading a level).
So restoring the snapshot into `ctx->block` is immediately visible to
the editor's own render/query/save paths with no bridging code
needed — the snapshot mechanism only has to get the *timing* right
(capture before `SDL2ST_GAME` mutates anything, restore before
`mode_edit_enter`'s guard would otherwise force a reload), not
reconcile two separate copies of the grid.

**Restoring `info` alongside `level` is intentionally not narrowed
to "board only."** `savegame_system_restore` requires a non-NULL
`info` argument (only `level` is documented nullable,
`include/savegame_system.h:45-47`), and restoring the captured
pre-test `info` (paddle position/size, gun ammo, ball state, tilts)
is harmless: none of that is rendered while `EDITOR_STATE_TEST` is
not active (`src/game_render.c`'s paddle/ball suppression outside
`EDITOR_STATE_TEST`, per `docs/specs/2026-07-11-editor-parity.md`
§3.4). The explicit `score_system_set(..., 0)` calls above are the one
place this needs a manual override, because the original forces
score to a **literal** `0` on both ends of play-test rather than
restoring a captured value, and skipping that would otherwise leave
a stale nonzero score visible in the editor's own HUD (added by
`docs/specs/2026-07-11-editor-parity.md` §3.3) once every play-test
session leaves a residual score behind.

## 4. Implementation stages

### Stage 1 — flag, snapshot fields, capture/restore wiring

- **Write-set:** `include/game_context.h` (new `bool
  play_test_active`, new `savegame_data_t
  play_test_snapshot_info`, new `savegame_level_t
  play_test_snapshot_level`; add `#include "savegame_io.h"` —
  verified no circular dependency, `savegame_io.h` does not include
  `game_context.h`), `src/game_callbacks.c`
  (`editor_cb_on_playtest_start` / `editor_cb_on_playtest_end`: flag
  set/clear, capture/restore calls, `score_system_set(..., 0)` calls).
- **Worker:** `jdc`. **jck approval: yes** — introduces the
  mechanism the other two stages depend on; must verify the
  capture/restore timing against a level containing `COUNTER_BLK` and
  `BLACK_BLK` cells (animated/cooldown state) as well as a plain
  static block, not just an empty grid.
- **Depends on:** nothing.

### Stage 2 — `xboing-hay` (lives, game-over, cursor, level-complete)

- **Write-set:** `src/game_rules.c` (`game_rules_ball_died` guard,
  §3.2; `game_rules_check`'s level-complete → `SDL2ST_BONUS` guard,
  §3.2 addendum — folded in per jck round 2 review, same file, same
  flag, one stage).
- **Worker:** `jdc`. **jck approval: yes** — gameplay rule change
  (infinite lives during test, no bonus-screen hijack on a cleared
  test board), and the lives fix indirectly resolves the cursor half
  of the bug; must verify no regression to real-game lives/game-over
  *and* real-game level-complete behavior (both guards are `if
  (!ctx->play_test_active)`, so real games are provably unaffected,
  but jck should confirm against a real 3-life game-over sequence
  and a real level clear).
- **Depends on:** Stage 1 (needs the flag).

### Stage 3 — `xboing-nl4` + `xboing-bsz` (editor guard split)

- **Write-set:** `src/game_modes.c` (`mode_edit_enter`'s guard split,
  §3.4), `src/game_input.c` (Escape-shortcut consolidation onto
  `ctx->play_test_active`, §3.1's consistency cleanup), optionally
  `src/game_modes.c`'s `mode_game_enter` (`prev == SDL2ST_EDIT` →
  `ctx->play_test_active`, same optional cleanup).
- **Worker:** `jdc`. **jck approval: yes** — editor state-machine
  change; must verify (a) modified/level_number/level_title survive a
  play-test round-trip, (b) the tool-palette canvas re-widens
  correctly after play-test (the split-guard gotcha, §2.3), and (c) a
  genuine fresh editor entry (from `INTRO` via `E`, and from a real
  game via `E`, per `docs/specs/2026-07-11-editor-parity.md` §3.3
  point 3) still resets/reloads exactly as before.
- **Depends on:** Stage 1 (needs the flag). Independent of Stage 2 —
  can run in parallel.

### Stage 4 — tests

- **Write-set:** `tests/test_game_rules.c` (new play-test-guard
  cases for both `game_rules_ball_died` and `game_rules_check`),
  `tests/test_integration_editor.c` (extend the existing
  `test_editor_playtest_transition`, §5).
- **Worker:** `gjm`. **jck approval: no** (test-only, no production
  behavior change — the review gate for the tests themselves is a
  `review` mission per `docs/WORKFLOW.md`, evaluator `jdc`).
- **Depends on:** Stages 1-3 (tests exercise the shipped fix, not a
  stub).

## 5. Test plan

Extend the existing scaffolding rather than build new fixtures —
`tests/test_integration_editor.c` already drives `EDITOR_KEY_PLAYTEST`
through the real callback wiring (`test_editor_playtest_transition`,
`:338-364`), and `tests/test_game_rules.c` already calls
`game_rules_ball_died(ctx)` directly as a characterization pattern
(`test_ball_died_clears_reverse`, `:99-118`).

1. **`xboing-hay` (lives/game-over), `tests/test_game_rules.c`.**
   `setup_game` fixture, drive to `SDL2ST_GAME` with `prev ==
   SDL2ST_EDIT` semantics by setting `ctx->play_test_active = true`
   directly (unit-level, no need to drive real editor input), force
   `ball_system_clear_all` (no active balls), call
   `game_rules_ball_died(ctx)` up to 5 times. Assert `ctx->lives_left`
   never changes from its play-test-entry value and
   `sdl2_state_current(ctx->state)` never becomes `SDL2ST_HIGHSCORE`.
   A second case with `play_test_active = false` asserts the existing
   real-game decrement/game-over behavior is unchanged (regression
   guard on the guard itself).
2. **`xboing-hay` (level-complete hijack), `tests/test_game_rules.c`.**
   Sibling to case 1, same fixture: set `ctx->play_test_active =
   true`, clear every required block (`block_system_clear_all` or
   equivalent so `block_system_still_active` reads false), call
   `game_rules_check(ctx)`. Assert `sdl2_state_current(ctx->state)`
   stays `SDL2ST_GAME` (no transition to `SDL2ST_BONUS`). A second
   sub-case with `play_test_active = false` asserts the existing
   real-game level-complete → `SDL2ST_BONUS` transition still fires
   (regression guard on the guard itself, mirroring case 1's pair).
3. **`xboing-hay` (cursor), `tests/test_integration_editor.c`.**
   Extend `test_editor_playtest_transition`: after entering play-test,
   assert `sdl2_cursor_current(ctx->cursor) == SDL2CUR_NONE` (matches
   `mode_game_enter`'s hide); drive enough `game_rules_ball_died`
   calls during the test tick loop to exhaust 3 simulated lives (or
   call it directly, same as case 1, interleaved with the existing
   `tick_frames`); after exiting play-test, assert
   `sdl2_state_current(ctx->state) == SDL2ST_EDIT` (not
   `SDL2ST_HIGHSCORE`) and `sdl2_cursor_current(ctx->cursor) ==
   SDL2CUR_PLUS`.
4. **`xboing-nl4`, `tests/test_integration_editor.c`.** In the same
   extended test: record `editor_system_is_modified(ctx->editor)` and
   `editor_system_get_level_number(ctx->editor)` immediately before
   `EDITOR_KEY_PLAYTEST` (after drawing a block, so `modified` is
   `true` and `level_number` is whatever the fixture loaded), assert
   both are unchanged immediately after the play-test round-trip
   completes (a few `tick_frames` after the second `P`).
5. **`xboing-bsz`, `tests/test_integration_editor.c`.** In the same
   test: after drawing a block at `(2, 3)` (matching the existing
   `test_editor_clear_grid` pattern, `:322-336`) and entering
   play-test, assert `block_system_is_occupied(ctx->block, 2, 3)` is
   still `true` throughout the test tick loop (proves play-test uses
   the drawn board, not a stale reload) and still `true` after
   exiting (proves the round-trip preserves it — this is the
   characterization test for the bug itself, since the shipped-today
   behavior wipes it back to whatever `editor.data` last had on
   disk). A second sub-case draws a `COUNTER_BLK` with a nonzero
   slide, lets a ball damage it during the test tick loop (or calls
   the block-hit path directly), and asserts the *pre-damage* slide
   value is restored after play-test ends — this is what proves the
   snapshot/restore (not just "don't reload from disk") is doing real
   work, matching `FinishPlayTest`'s undo-test-damage behavior
   (§1.1), not merely `xboing-bsz`'s narrower "edits survive"
   framing.
6. **Canvas-width gotcha (§2.3/§3.4), `tests/test_integration_editor.c`
   or `tests/test_sdl2_regions.c`.** Assert
   `sdl2_renderer_get_logical_width` (or the equivalent accessor) is
   back at `SDL2R_LOGICAL_WIDTH + EDITOR_TOOL_WIDTH` after a
   play-test round-trip, not stuck at `SDL2R_LOGICAL_WIDTH` — this is
   the regression this design explicitly calls out as easy to get
   wrong with a single merged guard.
7. **Regression: dialogue round-trip still unaffected.** Re-run (or
   confirm still green) the existing dialogue-resume assertions from
   `docs/specs/2026-07-11-editor-parity.md`'s test plan — the
   guard split in §3.4 must not change behavior for the
   `SDL2ST_DIALOGUE` case, only add a second, independent condition
   for play-test.

All new/extended tests run under both `make test` and `make
asan-test` per standard policy — no new I/O, threads, or global state
is introduced, so no new sanitizer surface is expected, but the
guard's stack of three booleans (dialogue / play-test / neither) is
exactly the kind of branch logic worth exercising under UBSan for
uninitialized-read confidence on `ctx->play_test_active` in contexts
that don't go through `game_create`'s `calloc`.

## 6. Required ADRs

Two, both for `docs/DESIGN.md`:

1. **Editor play-test detection: dedicated flag over previous-mode
   inference.** Records §3.1's decision and rejection of extending
   `sdl2_state_previous()`/`saved_mode()` inference, citing the
   existing `game_input.c:317` heuristic as the cautionary precedent
   this design retires rather than imitates.
2. **Editor play-test board round-trip: in-memory
   `savegame_system_capture`/`restore` over a temp-file mirror of
   `SetupPlayTest`/`FinishPlayTest`.** Records §3.5's decision, the
   `tempnam()` security rejection, and the architectural constraint
   (single shared `game_ctx_t`/`block_system_t` across `EDIT ⇄ GAME`)
   that makes the original's file hand-off unnecessary in the modern
   port.

## 7. Found during design, explicitly out of scope for this mission

**Round 2 update (jck review):** the level-complete → `SDL2ST_BONUS`
hijack originally listed here has been moved into scope — see §2.2's
addendum and Stage 2. It is the fourth manifestation of the same
defect as `xboing-hay`'s game-over hijack (an editor-session action
reaching a real-game terminal screen via the same missing
`play_test_active` guard), not an independent, separately-schedulable
gap, so leaving it out would have been an incomplete application of
the mechanism this design introduces.

One observation remains genuinely out of scope — it shares no root
cause with any of the three charter bugs and should not be folded
into these stages without separate jck sign-off and a new bead:

- **Paddle not re-expanded to huge on ball respawn (real game or
  play-test).** The original's `DeadBall` calls `ChangePaddleSize(...,
  PAD_EXPAND_BLK)` twice (`original/level.c:496-497`) in the
  "still have lives" branch, unconditional on mode. The modern
  `game_rules_ball_died`'s equivalent branch does not resize the
  paddle at all. This is a general gameplay-fidelity gap that fires
  identically on every real-game respawn, not just during play-test —
  it has no `play_test_active`-shaped fix and needs its own bead.
