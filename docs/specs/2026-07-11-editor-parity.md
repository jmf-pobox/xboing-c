# Level Editor Parity — Implementation Blueprint

Input: `docs/audits/2026-07-11-editor-parity.md` (committed `b8392a0`).
This spec turns the audit's 9 blockers + 6 majors + 4 minors into a
concrete, stage-ordered implementation plan. Read the audit first —
this document does not repeat the gap descriptions, only the fix
design.

Mission: `m-2026-07-11-028` (bead `xboing-di8`), worker `jdc`,
evaluator `jck`.

## 1. The dialogue mechanism (blockers 3, 4)

### 1.1 Why the current stubs exist

`editor_cb_yes_no` and `editor_cb_input_dialogue`
(`src/game_callbacks.c:907-913`, `:1012-1023`) are stubs because the
original's equivalents are **blocking** calls. `YesNoDialogue` and
`UserInputDialogueMessage` (`original/editor.c:845-846`, `:900-901`,
`:942-943`, `:974-975`) run a nested `XNextEvent` loop inside Xlib —
the call does not return until the player answers. A mechanical port
of that signature (`return int` / `return const char *`) only works
if the modern callback can also block. It can't: `mode_edit_update`
(`src/game_modes.c:1368-1499`) runs once per fixed-timestep tick and
must return every frame so rendering and input polling continue. This
is the reason the mission calls this "the hard one" — it isn't a
wiring gap, it's a synchronous-into-asynchronous control-flow
mismatch, and `editor_system.c`'s own state machine (`do_load`,
`do_save`, `do_set_time`, `do_set_name`,
`handle_editor_key`'s `EDITOR_KEY_QUIT`/`CLEAR` cases) is written
assuming the dialogue callback returns the final answer in the same
call. All of them need restructuring, not just the two callback
bodies.

### 1.2 The existing async pattern to follow

`SDL2ST_DIALOGUE` already works exactly this way for three flows:
abort-game confirm, quit confirm, and set-starting-level
(`src/game_input.c:311-370`). The shape:

1. The initiating code calls `sdl2_state_push_dialogue(ctx->state)`,
   and on success calls `dialogue_system_open(ctx->dialogue, message,
   icon, validation)` and sets a static pending flag
   (`quit_pending` / `abort_pending` / `level_pending`,
   `src/game_modes.c:66-89`).
2. `mode_dialogue_update` drives `dialogue_system_update` every tick
   and calls `sdl2_state_pop_dialogue` once
   `dialogue_system_is_finished` (`src/game_modes.c:1257-1275`).
3. `sdl2_state_pop_dialogue` calls `mode_dialogue_exit` (the
   dialogue's `on_exit`) **then** the restored mode's `on_enter`
   (`src/sdl2_state.c:194-220`). `mode_dialogue_exit` reads
   `dialogue_system_was_cancelled` / `dialogue_system_get_input` and
   consumes exactly one pending flag (`src/game_modes.c:1277-1330`).

This is the pattern to extend for the editor, not a new mechanism.

### 1.3 Recommended design: pending-action state machine inside `editor_system`

**Callback interface change** (`include/editor_system.h`). Replace
the two synchronous callbacks with fire-and-forget *requests* that
return success/failure (so the editor can refuse to enter a dialogue
state if the integration layer couldn't open one):

```c
/* Returns nonzero if the dialogue was opened. */
int (*on_request_yes_no_dialogue)(const char *message, void *ud);
int (*on_request_input_dialogue)(const char *message, int numeric_only,
                                 void *ud);
```

**New state** `EDITOR_STATE_DIALOGUE` added to `editor_state_t`.
`editor_system_mouse_button` / `editor_system_mouse_motion` gain the
same early-return guard they already have for `EDITOR_STATE_TEST`
(`src/editor_system.c:351-352`) so clicks don't leak through while a
dialogue is open.

**New internal enum** (private to `editor_system.c`, not exposed):

```c
typedef enum
{
    EDITOR_PENDING_NONE,
    EDITOR_PENDING_QUIT_CONFIRM,
    EDITOR_PENDING_LOAD_CONFIRM, /* only used when modified */
    EDITOR_PENDING_LOAD_INPUT,
    EDITOR_PENDING_SAVE_INPUT,
    EDITOR_PENDING_TIME_INPUT,
    EDITOR_PENDING_NAME_INPUT,
    EDITOR_PENDING_CLEAR_CONFIRM
} editor_pending_action_t;
```

plus a `pending_action` field on `struct editor_system`. Only `LOAD`
is a genuine two-step chain: `handle_editor_key` for
`EDITOR_KEY_LOAD` asks "Unsaved work, continue load?" **first** when
`modified`, and only opens the level-number input dialogue if the
player answers yes (`original/editor.c:1052-1061`, mirrored today at
`src/editor_system.c:870-882`). Every other flow (`SAVE`, `TIME`,
`NAME`) is a single dialogue; `QUIT` and `CLEAR` are single yes/no
confirms with no follow-up input.

**New public resume entry point:**

```c
/* Called by the integration layer once a requested dialogue
 * resolves. cancelled: nonzero if Escape was pressed. input: the
 * entered text (a single 'y'/'Y'/'n'/'N' char for yes/no dialogues).
 * No-op unless editor_system_get_state() == EDITOR_STATE_DIALOGUE. */
void editor_system_dialogue_result(editor_system_t *ctx, int cancelled,
                                   const char *input);
```

Internally, `do_load`/`do_save`/`do_set_time`/`do_set_name` and the
`QUIT`/`CLEAR` branches of `handle_editor_key` split into a
`begin_*` half (build the message, call the `on_request_*`
callback, and on success set `pending_action` + `state =
EDITOR_STATE_DIALOGUE`; on failure, behave exactly as today when a
callback is `NULL` — silently skip) and the parse/act half, which
moves into `editor_system_dialogue_result`'s switch on
`pending_action`. `LOAD_CONFIRM`'s "yes" branch calls the same
`begin_load_input` helper the un-modified path already calls
directly — the only place the chain actually chains.

### 1.4 Integration layer wiring

`src/game_callbacks.c`: rename `editor_cb_yes_no` /
`editor_cb_input_dialogue` to `editor_cb_request_yes_no` /
`editor_cb_request_input`. Both call `sdl2_state_push_dialogue`,
`dialogue_system_open` (icon `DIALOGUE_ICON_TEXT` throughout — the
original uses `TEXT_ICON` for every editor prompt,
`original/editor.c:845`, `:900`, `:942`, `:974`, `:1039` etc. — there
is no `DIALOGUE_ICON_DISK` use in any editor flow), and a new pending
flag setter, mirroring `src/game_input.c:321-326`.

`include/game_modes.h` / `src/game_modes.c`: add a fourth pending
flag, `editor_dialogue_pending` (alongside `quit_pending` /
`abort_pending` / `level_pending`, `src/game_modes.c:66-89`) and
`game_modes_set_editor_dialogue_pending(void)`. Consume it in
`mode_dialogue_exit` (`src/game_modes.c:1277-1330`), the same site
that already drives `quit_pending`/`level_pending`, since the
destination after every editor dialogue is always `SDL2ST_EDIT` (no
per-destination logic is needed the way `abort_pending` needs
`mode_game_enter`):

```c
if (editor_dialogue_pending)
{
    editor_dialogue_pending = 0;
    editor_system_dialogue_result(ctx->editor,
        dialogue_system_was_cancelled(ctx->dialogue),
        dialogue_system_get_input(ctx->dialogue));
}
```

### 1.5 Two integration gotchas this design must close

**(a) `mode_edit_enter` re-initializes on every dialogue resume.**
`sdl2_state_pop_dialogue` calls the dialogue's `on_exit`
(`mode_dialogue_exit`) and then the *restored* mode's `on_enter`
(`src/sdl2_state.c:207-217`) — and the restored mode for every
editor-originated dialogue is `SDL2ST_EDIT`, so `mode_edit_enter`
(`src/game_modes.c:1336-1355`) fires on every single dialogue
close. Today it unconditionally calls `editor_system_reset` and
`editor_system_init_palette`, which would wipe `modified`,
`level_number`, `level_title`, and the palette selection the instant
any editor dialogue (including a plain Save) completes. Fix: guard
the reset/init/canvas-widen block with `if
(sdl2_state_previous(ctx->state) != SDL2ST_DIALOGUE)`. This was not
visible to the audit (a static code read) — it only surfaces when
tracing the actual pop call sequence, which is why it's called out
here rather than in the audit.

**(b) The `mode_edit_update` QUIT fallback would fire mid-dialogue.**
`src/game_modes.c:1409-1417` force-transitions to `SDL2ST_INTRO` if,
after calling `editor_system_key_input(EDITOR_KEY_QUIT)`, the editor
state isn't `EDITOR_STATE_FINISH`. Under the new design, a
successful quit request leaves the state at `EDITOR_STATE_DIALOGUE`
(not `FINISH`) — the existing condition would immediately kill the
dialogue that was just opened. Fix: also require that the *state
machine's* current mode is still `SDL2ST_EDIT`:

```c
if (sdl2_state_current(ctx->state) == SDL2ST_EDIT &&
    editor_system_get_state(ctx->editor) != EDITOR_STATE_FINISH)
    sdl2_state_transition(ctx->state, SDL2ST_INTRO);
```

This preserves the fallback's actual intent (force-exit when no
dialogue callback is wired / the push failed) without clobbering a
legitimately open dialogue, whose presence is signaled by
`sdl2_state_current` having already changed to `SDL2ST_DIALOGUE`.

### 1.6 Dialogue box re-centering (ADR-057 follow-up)

`game_render_dialogue` hardcodes `bx = 92` (`src/game_render.c:1002`,
matching the `575`-wide canvas: `(575 - 381) / 2 ≈ 96`, off by a few
px from `SDL2RGN_DIALOGUE`'s `x=97` — the two were never in sync,
also out of scope here). Against the editor's widened `695`-wide
canvas, the correct center is `(695 - 381) / 2 = 157`; a fixed value
is wrong in one of the two modes no matter what it's set to. Fix:
compute it at render time from the renderer's live logical size
(`sdl2_renderer_get_logical_size`, already used elsewhere,
`include/sdl2_renderer.h:101`):

```c
int logical_w = 0, logical_h = 0;
sdl2_renderer_get_logical_size(ctx->renderer, &logical_w, &logical_h);
int bx = (logical_w - DIALOGUE_WIDTH) / 2;
```

`by` stays fixed — window height never changes between modes.

## 2. Save-path data fixes (blockers 6, 7, 8; major 5)

These do not depend on section 1 and should ship first — they are
live data-loss bugs today, independent of whether the dialogue
mechanism exists yet (per jdc's correction to the audit's dependency
order: blocker 6 has zero dependency on real dialogue input, since
it only changes what's read at save time, not what's typed).

### 2.1 Hardcoded time bonus (blocker 6) — ships alone, first

`editor_cb_save_level` (`src/game_callbacks.c:990`) writes
`fprintf(fp, "120\n")` unconditionally. Replace with
`level_system_get_time_bonus(ctx->level)` — the accessor already
exists (`include/level_system.h:127`). No callback change, no
dialogue dependency: this alone stops every re-save from corrupting
the level's time limit, even before Set Time can be edited.

### 2.2 Wiring `on_set_time` (blocker 5) — depends on section 1

`do_set_time` (`src/editor_system.c:797-820`) already calls
`ctx->cb.on_set_time(num, ctx->user_data)` once a valid number
arrives; the callback is simply never assigned in
`game_callbacks_editor()` (`src/game_callbacks.c:1043-1061`). Add
`editor_cb_on_set_time`, wire it to a new setter added the same way
as the existing getter:

```c
/* include/level_system.h, mirroring level_system_get_time_bonus at :127 */
void level_system_set_time_bonus(level_system_t *ctx, int seconds);
```

(`src/level_system.c:314-320` shows the getter reading
`ctx->time_bonus`; the setter is the same three lines in reverse.)
This fix is listed under section 1's umbrella because `do_set_time`
only becomes meaningful once `T` actually reaches the player through
a real input dialogue — wiring it before section 1 lands has no
observable effect.

### 2.3 Counter-block slide round-trip (blocker 7, major 5)

No new accessor is needed — `block_system_get_render_info`
(`include/block_system.h:369`) already fills `counter_slide` on its
output struct (`include/block_system.h:96`), and is already used
elsewhere for exactly this kind of read (`gun_cb_check_block_hit`,
`src/game_callbacks.c:383-415`). Fix `editor_cb_query_cell`
(`src/game_callbacks.c:875-884`) to call it instead of the
`occupied`/`get_type` pair it uses today, and stop hardcoding
`counter_slide = 0`:

```c
static int editor_cb_query_cell(int row, int col, editor_cell_t *cell,
                                void *ud)
{
    const game_ctx_t *ctx = ud;
    block_system_render_info_t info;
    if (block_system_get_render_info(ctx->block, row, col, &info) !=
            BLOCK_SYS_OK ||
        !info.occupied)
        return 0;
    cell->occupied = 1;
    cell->block_type = info.random ? RANDOM_BLK : info.block_type;
    cell->counter_slide = info.counter_slide;
    return 1;
}
```

This single change fixes counter-slide loss across every board
transform (`editor_system_flip_horizontal/vertical`,
`scroll_horizontal/vertical`, `src/editor_system.c:460-689`) because
they all read cells through `query_cell` already
(`ctx->cb.query_cell(r, c, &left_cells[r], ...)` etc.) and write
them back through `place_block`, which already forwards
`counter_slide` (`src/editor_system.c:113-118` →
`on_add_block(row, col, block_type, counter_slide, visible, ud)` →
`editor_cb_add_block` → `block_system_add(..., counter_slide, 0)`,
`src/game_callbacks.c:855-861`). The plumbing already exists
end-to-end; only the read side was lying about the value.

**Save-path encoding.** `block_type_to_char` must take
`counter_slide` and, for `COUNTER_BLK`, return `'0' + counter_slide`
(clamped `[0,5]`) instead of the hardcoded `'0'`
(`src/game_callbacks.c:919-976`), matching the format ground truth
(`original/file.c:418-440`: `'0'`→slide 0, `'1'`→slide 1 … `'5'`→slide
5). `editor_cb_save_level`'s per-cell loop
(`src/game_callbacks.c:993-1001`) changes from calling
`block_system_get_type` directly to reading through the same
random-aware, counter-slide-aware path as `query_cell` (see below —
one query, two call sites).

### 2.4 `RANDOM_BLK` round-trip (blocker 8)

`block_system_add` already sets the runtime `.random` flag correctly
the moment a `RANDOM_BLK` is placed
(`src/block_system.c:455-459`: `bp->random = 1; bp->block_type =
RED_BLK;` — it resolves to a concrete color immediately and a
one-frame-later animation step takes over color-cycling). The gap is
entirely on the *read* side: `editor_cb_query_cell` and
`editor_cb_save_level` both read the resolved color, never checking
`block_system_get_random`. The fix in 2.3 above already folds this
in — `cell->block_type = info.random ? RANDOM_BLK : info.block_type;`
— because `block_system_render_info_t` carries both `counter_slide`
and `random` in a single call.

**Save loop.** Change `editor_cb_save_level`'s per-cell resolution
to perform the same random-check before calling
`block_type_to_char`, e.g. by extracting a small static helper
`resolve_save_char(ctx, row, col)` that calls
`block_system_get_render_info` once and applies the same
`info.random ? RANDOM_BLK : info.block_type` substitution before
formatting the char.

**This replicates the original's own save-path check — it is not a
deviation.** `SaveLevelDataFile` (`original/file.c:562-609`) already
checks `blockP->random` at *write* time for every color-cycling
block type and writes `'?'` instead of the resolved color's letter —
exactly the substitution `resolve_save_char` performs above. The
separate whole-grid walk, `HandleRandomBlocks`
(`original/editor.c:812-824`), invoked after `FinishPlayTest`, after
every board transform, and after loading a level (`:639`, `:807`,
`:867`), was never load-bearing for *save* — it exists purely to
un-resolve the in-memory grid so the editor's own display shows
`RANDOM_BLK` (the "- R -" swatch) instead of the momentarily-resolved
color. That display-side responsibility is what stage 8 below (the
render-side `RANDOM_BLK` composite fix) covers, using the same
`.random`-aware read pattern rather than a mutation pass. This
design makes every *read* of a cell (`query_cell`, the save loop, and
stage 8's render read) random-aware, so nothing ever needs to be
walked and un-resolved after the fact; `block_type` storage always
holds the resolved color, and display/save/transform reads restore
the `RANDOM_BLK` view on demand — functionally equivalent to
`SaveLevelDataFile`'s own check, one fewer pass than
`HandleRandomBlocks`, no separate normalization function to keep in
sync. `editor_system.c`'s existing `normalize_random_blocks` no-op
(`src/editor_system.c:94-111`) can stay a no-op permanently under
this design; its doc comment should be updated to say so rather than
"deferred to the integration layer." jck sign-off applies to stage 8
(the display-side change), already flagged there — the save-path
check itself needs no separate sign-off since it is a direct port of
`SaveLevelDataFile`'s existing logic, not a new design choice.

**Render-side consequence found during design, not in the audit.**
`render_block_composite`'s `RANDOM_BLK` case
(`src/game_render.c:186-205`, shared by `game_render_blocks` and
`game_render_editor_palette`) keys off `block_type == RANDOM_BLK`
literally — which, per `block_system_add` above, is never true after
the first frame (`block_type` is always the resolved color). This
means the "- R -" overlay for a `RANDOM_BLK` cell is dead code today,
in both live gameplay and the editor grid, once the editor grid also
starts reading `.random`-aware cells for its own rendering (it
currently doesn't — `game_render_blocks` reads `ctx->block` directly,
not through `query_cell`). Recommend switching the composite's
condition from `block_type == RANDOM_BLK` to a `random` flag added to
`render_block_composite`'s parameters, since both call sites already
have `info.random` available (`block_system_render_info_t.random`,
`editor_palette_entry_t` doesn't carry a runtime flag — the palette
swatch is a static preview, not a live cell, so it correctly keeps
showing the `RANDOM_BLK` sprite for its dedicated palette entry and
does not need this fix). This is a one-line condition change with a
one-parameter signature change, low risk, but it is a gameplay-visible
rendering change beyond pure editor scope — hence its own stage
below rather than folding it into 2.4 silently.

## 3. Render clusters (blockers 1, 2, 9; majors 1, 2, 3, 6; minor 1)

Designed here; implemented by `sjl` in a later stage since it's
render code.

### 3.1 Two-column palette (major 3, blocker 9)

Replace the single-column loop in `game_render_editor_palette`
(`src/game_render.c:728-781`, capped `i < 20`) with the original's
two-column geometry (`original/editor.c:329-369`,
`SetupBlockWindow`): column 1 holds indices
`0..min(MAX_ROW, MAX_STATIC_BLOCKS)-1` = `0..17` (`MAX_ROW=18`,
`MAX_STATIC_BLOCKS=25`, `include/block_types.h:49,52`); column 2
continues with the remaining static types (`18..24`, 7 entries) then
appends the 5 counter-block variants (`25..29`) — 12 entries in
column 2, matching `editor_system_init_palette`'s existing fill
order (`src/editor_system.c:282-309`: static types first, then
`COUNTER_BLK` slides 1-5). Column geometry, citing
`original/editor.c:332,338,345,351,362`:

```text
editorRowHeight = PLAY_AREA_H / MAX_ROW        (580 / 18 ~ 32)
col1_x          = PALETTE_X + EDITOR_TOOL_WIDTH / 4 - sprite_w / 2
col2_x          = PALETTE_X + EDITOR_TOOL_WIDTH / 2
                    + EDITOR_TOOL_WIDTH / 4 - sprite_w / 2
entry_y(row)    = PALETTE_Y + row * editorRowHeight
```

Also fix `PALETTE_ENTRY_H = 25` (`src/game_render.c:725`) to
`PLAY_AREA_H / MAX_ROW` (~32) — the original computes row pitch this
way (`original/editor.c:332`) and the current hardcoded 25 doesn't
match, an independent minor drift found during this design pass, not
in the audit; low risk, bundle with this stage since it's touching
the same constants.

Removing the `i < 20` cap (a direct consequence of moving to two
columns, per the audit's "root cause" framing, major 3) resolves
blocker 9 as a side effect of this geometry change, not a separate
fix.

### 3.2 Active: indicator and in-list highlight (majors 1, 2)

Add rendering to `SDL2RGN_EDITOR_TYPE` (`{545, 650, 120, 35}`,
`src/sdl2_regions.c:44`, matching `original/stage.c:277-279`
exactly): draw the "Active:" label plus a full-size preview of
`editor_system_get_palette_entry(ctx->editor,
editor_system_get_selected_palette(ctx->editor))`, mirroring
`SetCurrentSymbol` (`original/editor.c:256-268`).

**Recommendation on the in-list highlight: remove it.** The original
has no equivalent — the only 1996 selection feedback is the Active:
preview (audit major 1/2 both make this point). The current
highlight (`src/game_render.c:743-748`) is an invented addition with
an internal magic-number mismatch (`PALETTE_W=100` vs the `120`px
region, ADR-057's own trigger). Once the Active: indicator exists,
keeping a second, non-original selection cue adds visual noise this
port hasn't earned anywhere else and duplicates information. Remove
it rather than "fix" its width — jck to confirm at review since this
is a UI-fidelity call, not just a bug fix.

### 3.3 HUD in editor mode (blocker 1)

Add `SDL2ST_EDIT` to the gate at `src/game_render.c:1228-1230`. This
single addition also fixes HUD visibility while an editor dialogue is
open: `effective = sdl2_state_saved_mode(ctx->state)` when
`mode == SDL2ST_DIALOGUE` (`src/game_render.c:1224-1226`) already
resolves to `SDL2ST_EDIT` for editor-originated dialogues, so adding
`SDL2ST_EDIT` to the `effective ==` list covers both the plain editor
screen and the editor-dialogue overlay in one line. Matches
`MapAllWindows` always mapping `scoreWindow`/`levelWindow`/
`messWindow`/`specialWindow`/`timeWindow`
(`original/stage.c:388-399`).

**Showing the HUD is not enough — its content must also be
synced.** The original repopulates HUD *content* on every editor
level load, not just at window-map time: `DoLoadLevel`
(`original/editor.c:196-201`, the initial `editor.data` load on
entering the editor) calls `SetCurrentMessage`, `DisplayLevelInfo(
levelWindow, 1)` (level number hardcoded to 1 for the scratch
`editor.data` buffer), `TurnSpecialsOff`, `DrawSpecials`, in that
order. `LoadALevel` (`original/editor.c:826-885`, the `L`-key
reload), on successful load, calls `SetCurrentMessage` and
`DisplayLevelInfo(levelWindow, num)` (`:870`, `:874`) — but, verified
by reading the full function body, it does **not** call
`TurnSpecialsOff`/`DrawSpecials`; those two appear only in
`DoLoadLevel` and separately in `SetupPlayTest`/`FinishPlayTest`
(`original/editor.c:616-620`, `:627-632`), never in `LoadALevel`.
The modern gap has three independent parts, and each has a different
correct fix, not one blanket "sync everything on load":

1. **Level number.** `game_render_lives` reads `ctx->level_number`
   (`game_ctx_t`) but the editor tracks its own level number
   internally (`editor_system_get_level_number`, already public at
   `include/editor_system.h:288`, backed by
   `src/editor_system.c:51,183,275,730,779`); nothing bridges the
   two, so the HUD shows a stale/zero gameplay level while editing.
   Fix at *read* time, not write time — the same substitution
   principle section 2.4 above uses for `RANDOM_BLK`: inside
   `game_render_lives` (`src/game_render.c:536`), resolve the
   effective mode the same way the outer HUD block already does
   (`sdl2_state_current(ctx->state)`, then
   `sdl2_state_saved_mode(ctx->state)` if that's
   `SDL2ST_DIALOGUE` — mirroring the existing `effective` computation
   at `src/game_render.c:1224-1226`), and when that resolves to
   `SDL2ST_EDIT`, source the displayed level number from
   `editor_system_get_level_number(ctx->editor)` instead of
   `ctx->level_number`. The existing `if (level <= 0) level = 1;`
   clamp (`src/game_render.c:564-565`) already reproduces
   `DoLoadLevel`'s hardcoded "1" for the as-yet-unnumbered
   `editor.data` buffer — no separate special-case needed for that.
   This is a pure read, no new state, `src/game_render.c`-only.
2. **Timer.** `game_render_timer` reads `ctx->time_bonus_total`/
   `ctx->time_remaining`, set only in gameplay's `mode_game_enter`
   (`src/game_modes.c:157-159`) and decremented only by the gameplay
   tick — never touched by the editor, so it is stale or absent (0)
   while editing, even though `timeWindow` is always mapped. The
   editor never runs the gameplay tick, so there is nothing to count
   down; the faithful display is a *static* value: the currently
   loaded level's full time bonus. Same read-time fix, same
   function: when the resolved mode is `SDL2ST_EDIT`, source the
   displayed seconds from `level_system_get_time_bonus(ctx->level)`
   (existing accessor, `include/level_system.h:127`, already used in
   section 2.1) instead of `ctx->time_remaining`, reusing the
   existing MM:SS formatting and color thresholds against that static
   value. `src/game_render.c`-only, no new state.
3. **Specials leak.** `E` is reachable mid-game
   (`src/game_input.c:332-334`), so a player with Killer/Reverse/x2
   active can enter the editor with those specials still set —
   nothing today resets them, unlike `DoLoadLevel`'s `TurnSpecialsOff`
   call. This *is* a mutation, not a read, so it cannot be fixed
   inside `game_render.c`. Add one call,
   `special_system_turn_off(ctx->special)` (existing accessor,
   `include/special_system.h:129`), at the top of `mode_edit_enter`
   (`src/game_modes.c:1336-1355`) — matching `DoLoadLevel`'s
   `TurnSpecialsOff(display)` (`original/editor.c:200`), called only
   once, at editor entry. Per the verified read of `LoadALevel`
   above, `TurnSpecialsOff` is **not** part of the `L`-key reload
   path, so this call belongs in `mode_edit_enter` only, not in the
   load-level flow. Place it unconditionally, independent of the
   dialogue-resume guard section 1.5(a) adds around the
   reset/init/canvas-widen block — `special_system_turn_off` is
   idempotent when specials are already off (verified:
   `src/special_system.c:126-150` only fires its one callback,
   `on_wall_state_changed`, when walls were actually on), so refiring
   it on every dialogue-close-back-to-editor is harmless, unlike the
   `editor_system_reset`/`init_palette` calls that gotcha 1.5(a)
   guards. This keeps stage 7 independent of stage 3's landing order.

Net effect: this stage's write-set is `src/game_render.c` (level
number + timer, read-time, no new state) plus `src/game_modes.c`
(one `special_system_turn_off` call in `mode_edit_enter`, a genuine
state mutation). Because it now mutates `game_ctx_t` state shared
with `SDL2ST_GAME` (specials), this stage needs jck approval — see
the updated stage 7 entry in section 5.

### 3.4 Suppress paddle/ball outside play-test (blocker 2)

Gate `game_render_paddle` / `game_render_balls`
(`src/game_render.c:408,411`, inside `game_render_playfield`) behind
`editor_system_get_state(ctx->editor) == EDITOR_STATE_TEST`, passed
in from the `SDL2ST_EDIT` dispatch branch. `game_render_playfield` is
shared with `SDL2ST_GAME`, so this needs a parameter (or a
`ctx`-visible editor-state check inside the function, since
`ctx->editor` is always available) rather than two separate
functions — cite `RedrawEditorArea` never drawing paddle/ball outside
test (`original/editor.c:206-216`) vs `SetupPlayTest` explicitly
resetting/placing them (`original/editor.c:587-621`).

### 3.5 Red grid lines (major 6, xboing-c40)

Already tracked. Port `DrawEditorGrid` (`original/editor.c:139-153`):
vertical lines at `x = xinc, 2*xinc, ..., PLAY_AREA_W` down to
`PLAY_AREA_H - 4 - 3*yinc` (the paddle-reserved rows are excluded),
horizontal lines at `y = yinc, ..., PLAY_AREA_H - 3*yinc`, both with
`xinc = PLAY_AREA_W / MAX_COL`, `yinc = PLAY_AREA_H / MAX_ROW`, color
`reds[4]`. Render only when not in `EDITOR_STATE_TEST`
(`original/editor.c:210-211`), called from wherever the `SDL2ST_EDIT`
dispatch branch calls `game_render_playfield` today. No dependency on
this spec's other fixes — sequence per xboing-c40's own schedule.

### 3.6 Editor background tile (minor 1)

Verify-on-live per the audit; no design decision needed until a
screenshot confirms the visual delta. If it is fixed: `DoLoadLevel`
and `RedrawEditorArea` hardcode background 3
(`original/editor.c:175-177`, `:208`); the modern port reads the
level's own background (`level_system_get_background`,
`src/game_render.c:426`). The faithful fix is a `SDL2ST_EDIT`-only
override in `game_render_background`'s caller: pass a fixed
background index `3` instead of the level's when
`sdl2_state_current(ctx->state) == SDL2ST_EDIT` (or the saved mode
resolves to it under a dialogue). Deferred pending the Verify step.

## 4. Smaller fixes

### 4.1 Right-click inspect vs erase (major 4)

`mode_edit_update` maps both button 2 and button 3 to erase
(`src/game_modes.c:1388-1389`:
`editor_system_mouse_button(ctx->editor, play_x, play_y, 2, 1)` for
either). Split them: button 3 (right) should call a **new**,
read-only path — not `editor_system_mouse_button` with a
destructive-button code — that queries the clicked cell's hit points
and surfaces them via `on_message` (or a new dedicated callback,
`on_inspect_cell`, if a distinct display slot beyond the message bar
is wanted), matching `Button3`'s `DisplayScore(display, scoreWindow,
blockP->hitPoints)` / `DisplayScore(..., 0L)`
(`original/editor.c:547-557`) — a **separate window**
(`scoreWindow`) from the message bar in the original, so the
faithful modern target is `game_render_score`'s existing score
display, not `message_system`. This needs a design call: either (a)
add `editor_system_mouse_button`'s button-3 case
(`src/editor_system.c:390-392`, already correctly a no-op internally)
a way to report the queried hit-points back out through a new
callback, or (b) have the integration layer bypass
`editor_system_mouse_button` entirely for button 3 and query
`block_system_get_hit_points` directly. Recommend (b): button 3 is
read-only and doesn't need to flow through the editor's draw-action
state machine at all, so `mode_edit_update` calling
`block_system_get_hit_points(ctx->block, row, col)` directly is the
smallest change with the correct query behavior.

**The result needs a persistence mechanism — routing it into
`game_render_score` alone doesn't survive the modern render loop.**
`game_render_score` (`src/game_render.c:648`) runs every frame — once
for `SDL2ST_GAME` (`:1199`) and, once stage 7 above adds `SDL2ST_EDIT`
to the HUD gate, again for the editor (`:1232`) — and today always
draws `score_system_get(ctx->score)`. A one-shot write to that same
value on right-click would be overwritten on the very next frame.
The original's `DisplayScore` (`original/score.c:193`, called from
`original/editor.c:552`/`554`) persists under X11's expose model
because nothing repaints `scoreWindow` on its own — it holds the
last-drawn value until something explicitly redraws it. Verified by
reading the full `Button3` case (`original/editor.c:547-557`) and the
button-up handling immediately after (`:577-579`): **only** the
`Button3`-down case ever touches `scoreWindow` in the editor;
`Button1`/`Button2` (draw/erase) never do. There is no code path in
`original/editor.c` that reverts `scoreWindow` to the real score —
each right-click simply overwrites the previous inspected value
(occupied → `hitPoints`, unoccupied → `0`), and that value is what
stays visible until the next right-click.

The modern equivalent: add two fields to `game_ctx_t`
(`include/game_context.h`), following the exact precedent already
established by `attract_level_display`
(`include/game_context.h:162`, `/* 0 = use real level_number */`):

```c
int editor_inspect_active;            /* 0 = show real score */
unsigned long editor_inspect_value;   /* last-queried hit points */
```

`mode_edit_update`'s new button-3 handler sets both — `1` /
`block_system_get_hit_points(ctx->block, row, col)` if occupied, `1`
/ `0` if not (matching `Button3`'s two `DisplayScore` calls exactly)
— on every right-click; nothing else in `mode_edit_update` touches
these fields, matching the verified original behavior above.
`game_render_score` consumes the override, gated to editor mode only
so it can never leak into the real gameplay HUD even if the flag is
never cleared:

```c
sdl2_state_mode_t mode = sdl2_state_current(ctx->state);
if (mode == SDL2ST_DIALOGUE)
    mode = sdl2_state_saved_mode(ctx->state);
unsigned long score_val = (mode == SDL2ST_EDIT && ctx->editor_inspect_active)
    ? ctx->editor_inspect_value
    : score_system_get(ctx->score);
```

(mirrors the existing `effective`-mode pattern at
`src/game_render.c:1224-1226`, reused for the same purpose in stage 7
above). As a hygiene measure beyond what the original needs (X11
windows are naturally isolated per mode; the modern port re-renders
every frame from shared `game_ctx_t` state), also reset
`ctx->editor_inspect_active = 0` in `mode_edit_exit`
(`src/game_modes.c:1357-1366`) so the override can never persist into
a later editor session. Write-set: `include/game_context.h` (two
fields), `src/game_modes.c` (`mode_edit_update` button-3 handler,
`mode_edit_exit` reset), `src/game_render.c` (`game_render_score`
mode-gated read).

### 4.2 `on_error` wiring (minor 2)

`game_callbacks_editor()` never assigns `.on_error`
(`src/game_callbacks.c:1043-1061`), so `do_load`/`do_save`'s failure
paths (`src/editor_system.c:736-739`, `:769-771`, `:785-787`)
silently do nothing. Add `editor_cb_on_error` →
`message_system_set(ctx->message, message, 0, frame)` (non-sticky,
matching the transient nature of the original's `ErrorMessage`
dialogs, `original/editor.c:163`, `:193`, `:382`, `:864`, `:918`).
Trivial; bundle with section 1's `game_callbacks_editor()` edits
since both touch the same struct literal.

### 4.3 ADR for digit-key/arrow palette selectors (minor 3)

Documentation only — no code change. Record in `docs/DESIGN.md` that
`SDL2I_SPEED_1`..`_9` and `SDL2I_LEFT`/`_RIGHT` palette selection
(`src/game_modes.c:1461-1484`) is an additive UX improvement with no
1996 counterpart (`original/editor.c` has zero digit-keysym
handlers; the only 1996 selection path is mouse click on
`blockWindow`, `HandleEditorToolBar`, `original/editor.c:270-305`).
Write this ADR once the two-column palette (3.1) ships, so it
describes final index ranges (`1-9` reaching only column-1 entries
`0-8`; arrow-cycling reaches all 30).

## 5. Implementation stages

Ordered by dependency. Each is sized for one `implement` + `test`
mission pair.

1. **Hardcoded save-time fix** (blocker 6). Independent of
   everything else — ships first.
   - Write-set: `src/game_callbacks.c`
     (`editor_cb_save_level`).
   - Worker: `jdc`. jck approval: **yes** (saved level-data
     correctness).
   - Depends on: nothing.

2. **Counter-slide + random-flag round-trip** (blockers 7, 8, major
   5). Both fixes share the `block_system_get_render_info` read
   path — do together.
   - Write-set: `src/game_callbacks.c`
     (`editor_cb_query_cell`, `block_type_to_char`,
     `editor_cb_save_level`), `src/editor_system.c`
     (`normalize_random_blocks` doc-comment update only, no logic
     change).
   - Worker: `jdc`. jck approval: **yes** (level-data fidelity
     across all 80 canonical levels; must verify round-trip on a
     level containing both `COUNTER_BLK` and `RANDOM_BLK` cells).
   - Depends on: nothing (independent of stage 1 and the dialogue
     mechanism).

3. **Async dialogue mechanism** (blockers 3, 4, 5) + `on_error`
   wiring (minor 2). The large stage — new editor state, new
   pending-action enum, new callback signatures, the
   `mode_edit_enter`/`mode_edit_update` integration fixes in
   section 1.5, and the `level_system_set_time_bonus` setter.
   - Write-set: `include/editor_system.h`, `src/editor_system.c`,
     `src/game_callbacks.c`, `include/game_modes.h`,
     `src/game_modes.c`, `include/level_system.h`,
     `src/level_system.c`.
   - Worker: `jdc`. jck approval: **yes** (changes editor
     quit/save/load/clear UX from always-silently-confirmed to a
     real safety net — must verify no unsaved-work data loss
     regressions and that prompts match the original's wording).
   - Depends on: nothing structurally, but should land after stages
     1-2 so the save-path fixes aren't re-tested against a moving
     dialogue layer.

4. **Dialogue box re-centering** (section 1.6).
   - Write-set: `src/game_render.c` (`game_render_dialogue`).
   - Worker: `sjl`. jck approval: no (pure geometry fix, no
     gameplay/data effect).
   - Depends on: stage 3 (only exercised once editor dialogues can
     actually open).

5. **Two-column palette geometry** (major 3, blocker 9).
   - Write-set: `src/game_render.c`
     (`game_render_editor_palette` and its `PALETTE_*` constants).
   - Worker: `sjl`. jck approval: no (rendering-only, no gameplay
     effect — palette *reachability* via keys/arrows already works
     today per the audit's Verify note).
   - Depends on: nothing from stages 1-4; sequence before stage 6.

6. **Active: indicator + remove in-list highlight** (majors 1, 2).
   - Write-set: `src/game_render.c`.
   - Worker: `sjl`. jck approval: **yes** (removing the invented
     highlight is a UI-fidelity call, not a pure bug fix).
   - Depends on: stage 5 (geometry must be final first, per the
     audit's own ordering rationale).

7. **HUD in editor mode + suppress paddle/ball outside play-test**
   (blockers 1, 2), plus the HUD content-sync (section 3.3): level
   number and timer read-time redirects, and one
   `special_system_turn_off` call on editor entry.
   - Write-set: `src/game_render.c` (HUD gate, paddle/ball
     suppression, `game_render_lives`/`game_render_timer` read-time
     redirects), `src/game_modes.c` (`mode_edit_enter` specials-off
     call).
   - Worker: `sjl`. jck approval: **yes** — the HUD-visibility and
     paddle/ball-suppression parts alone are unambiguous fidelity
     restorations with a direct original citation, but the
     content-sync addition mutates `game_ctx_t` fields
     (`level_number`-adjacent read path, specials) shared with
     `SDL2ST_GAME`, so the stage as a whole needs sign-off.
   - Depends on: nothing; the `special_system_turn_off` call is
     placed unconditionally (idempotent when specials are already
     off, section 3.3 point 3), so this stage does not need to wait
     on stage 3's dialogue-resume guard and can still run in parallel
     with stages 3-6.

8. **Render-side RANDOM_BLK composite fix** (found during design,
   section 2.4's addendum).
   - Write-set: `src/game_render.c` (`render_block_composite`
     signature + call sites).
   - Worker: `sjl`. jck approval: **yes** (visible during live
     gameplay, not just the editor — a gameplay-rendering change).
   - Depends on: stage 2 (needs the random-aware read pattern
     established there as the reference).

9. **Red grid lines** (major 6, xboing-c40). Already tracked
   separately; no dependency on this spec's stages.

10. **Right-click inspect vs erase** (major 4), including the
    score-display persistence mechanism (section 4.1).
    - Write-set: `include/game_context.h` (two new fields),
      `src/game_modes.c` (`mode_edit_update` button-3 handler,
      `mode_edit_exit` reset), `src/game_render.c`
      (`game_render_score` mode-gated read).
    - Worker: `jdc`. jck approval: **yes** — separating button 2/3
      is an unambiguous restoration of read-only original behavior,
      but the persistence mechanism adds new `game_ctx_t` state and
      changes what `game_render_score` displays during editor mode,
      which needs sign-off.
    - Depends on: nothing.

11. **Editor background tile** (minor 1) — pending the audit's
    Verify step; only scheduled if the live screenshot shows a
    jarring difference.
    - Write-set: `src/game_render.c`.
    - Worker: `sjl`. jck approval: yes if implemented (visual
      fidelity call).
    - Depends on: the Verify screenshot.

12. **ADR: digit-key/arrow palette selectors** (minor 3).
    Documentation only.
    - Write-set: `docs/DESIGN.md`.
    - Worker: `jdc` (or whoever closes stage 5, so index ranges are
      final). jck approval: **yes** (records a deliberate deviation
      from original input model).
    - Depends on: stage 5.

## 6. z-spec decision: no

A formal Z spec is not warranted for the editor's
`EDIT` ↔ `DIALOGUE` push/resume state machine. The outer screen
state machine (all 16 `SDL2ST_*` modes) already has a z-spec because
its invariants are global and load-bearing across every screen —
transition legality, frame-counter behavior, and dialogue save/
restore all interact across the whole mode set, and getting that
wrong breaks every screen simultaneously. The editor's pending-action
sub-machine is different in kind: it is a small, purely local
extension confined to one mode (`SDL2ST_EDIT` plus the shared
`SDL2ST_DIALOGUE`), with 7 pending-action states, a linear resume
path per action, and exactly one two-step chain (`LOAD_CONFIRM` →
`LOAD_INPUT`). Its correctness is fully covered by the two integration
gotchas already identified by hand in section 1.5 (both concrete,
both fixable with one-line guards) plus characterization tests per
`docs/TESTING.md` Layer 1/2 (inject the six key commands, assert
`pending_action`/state transitions, assert `mode_edit_enter` does not
reset `modified`/`level_title` across a dialogue round-trip). A Z
spec would formalize a state space smaller than what the existing
`editor_state_t` enum plus one new field already documents in code —
net negative: more artifacts to keep in sync, no invariant it would
catch that a test can't.

## 7. Visual-verification plan

Reuse the existing pipeline only — no new capture tooling.

1. `make golden-screen SCREEN=editor INTERVAL=200` (or, if a
   dedicated original editor golden doesn't exist yet under
   `tests/golden/original/editor/`, capture one the same way every
   other screen was captured — the four-place wiring in
   `docs/TESTING.md`'s "Adding a new screen" section:
   `src/game_init.c` name function + `vc_check` branch,
   `src/sdl2_cli.c` `-visual-capture` mapping, `Makefile`
   `visual-check` screen list, then `make golden-screen
   SCREEN=editor`). `screenshots/level-editor.gif` (695×720) is the
   fallback reference if a live capture isn't feasible for a given
   sub-state.
2. `make modern-screen SCREEN=editor INTERVAL=200` after each
   render-affecting stage (5, 6, 7, 8, 9, 11).
3. `make visual-check` (or targeted `llm_compare.py --screen
   editor`) comparing modern vs. golden.
4. For the dialogue-overlay states (stage 3, 4): capture via the
   savegame-fixture + `-load` pattern is not applicable here (that
   pattern is for reaching deep *gameplay* state, not editor
   dialogues) — instead use the documented `SDL2ST_EDIT` entry path
   (`E` key from attract) plus `ydotool`/`xdotool` per session type
   (`docs/TESTING.md`'s "Capturing dialogue / pause overlays"
   section) to drive the `S`/`L`/`T`/`N`/`C`/`Q` keys and capture
   each dialogue substate. No new script — this is the same
   key-injection path already used for the abort-game dialogue.
5. User verification (Gate 6): open old vs. new side-by-side in
   `eog`, per `docs/TESTING.md`.

## 8. Required ADRs

Three:

1. **Editor async dialogue mechanism** (stage 3) — records the
   pending-action design (section 1.3), the two integration gotchas
   (section 1.5) and their fixes, and the callback signature change.
   This is a genuine architectural decision (new state, new callback
   contract) and belongs in `docs/DESIGN.md` alongside ADR-057.
2. **In-list highlight removal** (stage 6) — records the decision to
   drop the invented highlight rather than fix its width, citing the
   audit's major 1/2 findings and the original's lack of an
   equivalent.
3. **Digit-key/arrow palette selectors** (stage 12, already listed
   in section 4.3) — records the additive input-model deviation.

The `HandleRandomBlocks`-vs-read-time-normalization deviation
(section 2.4) and the `RANDOM_BLK` composite condition fix (section
2.4 addendum) are implementation details of stage 2/8, not
architectural decisions on the scale of the three above — call them
out in commit messages with `original/` citations instead of a
standalone ADR, per the existing convention for smaller faithful-port
substitutions elsewhere in this codebase.
