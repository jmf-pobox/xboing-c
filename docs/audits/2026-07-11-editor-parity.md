# Level Editor Parity Audit (2026-07-11)

## Scope

This audit covers `SDL2ST_EDIT` (the level editor) end to end: grid
editing, the tool palette, the HUD/status windows, play-testing, and
save/load. It compares the modern implementation against the 1996
ground truth in `original/editor.c` and `original/stage.c`.

The audit was triggered by a visual comparison of a modern editor
screenshot against `original/level-editor.gif`, which surfaced about
8 obvious gaps. This document is the exhaustive follow-up: every
difference found while reading the original source end to end,
cited by line, with severity and tracking status. No design or code
changes are made here — this is the input to the design phase.

**Out of scope / already resolved:** the editor window width defect
(bead xboing-di8) — the logical canvas now widens by
`EDITOR_TOOL_WIDTH` (120px) on entering `SDL2ST_EDIT` and shrinks
back on exit, matching `original/editor.c:161-164` and
`original/editor.c:381-383`. Fixed on this branch, commit `c490555`,
documented as ADR-057 in `docs/DESIGN.md`. Referenced here only where
a later finding builds on it.

## Method

Read `original/editor.c` and `original/stage.c` in full. Cross
referenced every drawing/input routine against the modern
implementation: `src/editor_system.c`, `include/editor_system.h`,
`src/game_render.c` (editor render dispatch), `src/game_modes.c`
(`mode_edit_enter`/`mode_edit_update`/`mode_edit_exit`),
`src/game_callbacks.c` (`editor_cb_*`, `game_callbacks_editor`), and
`src/sdl2_regions.c` (`SDL2RGN_EDITOR`, `SDL2RGN_EDITOR_TYPE`).

## Gap list

Sorted blocker, then major, then minor. Each entry cites the
original behavior, the modern file/function responsible (or
"absent"), and tracking status.

### Blockers

1. **HUD is not rendered in editor mode.** The original always maps
   `scoreWindow`, `levelWindow`, `messWindow`, `specialWindow`, and
   `timeWindow` (`original/stage.c:388-399` `MapAllWindows`), and
   `DoLoadLevel` explicitly refreshes them on entering the editor
   (`original/editor.c:196-201`: `SetCurrentMessage`,
   `DisplayLevelInfo`, `TurnSpecialsOff`, `DrawSpecials`). The modern
   `SDL2ST_EDIT` render branch
   (`src/game_render.c:1209-1214`) calls only
   `game_render_background`, `game_render_playfield`, and
   `game_render_editor_palette` — no score, lives, level number,
   message bar, specials flags, or timer. The shared "attract-mode
   HUD" gate right after the dispatch switch
   (`src/game_render.c:1228-1230`) explicitly lists `SDL2ST_INTRO`,
   `INSTRUCT`, `DEMO`, `PREVIEW`, `KEYS`, `KEYSEDIT`, `HIGHSCORE`,
   `BONUS` — `SDL2ST_EDIT` is omitted. Status: new finding.

2. **Paddle and ball render during normal (non-play-test) editing.**
   `game_render_playfield` (`src/game_render.c:374-418`,
   used by the `SDL2ST_EDIT` case) unconditionally calls
   `game_render_paddle` (line 408) and `game_render_balls`
   (line 411). In the original, `RedrawEditorArea`
   (`original/editor.c:206-216`), the routine used for every
   non-test redraw, only calls `DrawStageBackground`,
   `DrawEditorGrid`, and `RedrawAllBlocks` — no paddle, no ball.
   Paddle/ball only appear once `SetupPlayTest`
   (`original/editor.c:587-621`) calls `ResetPaddleStart` /
   `ResetBallStart`, and the `Editor()` dispatcher
   (`original/editor.c:1133-1169`) only drives
   `HandleBallMode`/`HandleBulletMode`/etc. in the `EDIT_TEST` case.
   A player who enters the editor without play-testing sees a live
   paddle and ball that should not be there. Status: new finding.

3. **The yes/no confirmation dialogue is a stub that always answers
   "yes," with no dialogue box shown.** `editor_cb_yes_no`
   (`src/game_callbacks.c:907-913`) unconditionally `return 1;` with
   the comment "Always confirm — proper dialogue integration
   deferred to Phase 6." In the original, `YesNoDialogue` is a real
   modal box the player must answer with `y`/`n` before quitting
   with unsaved work, loading over unsaved work, or clearing the
   level (`original/editor.c:1039`, `:1042`, `:1056`, `:1086`).
   Today every one of those prompts is silently auto-confirmed —
   there is no safety net and no visible confirmation UI at all.
   Status: new finding.

4. **The text/number input dialogue is faked.**
   `editor_cb_input_dialogue` (`src/game_callbacks.c:1012-1023`)
   ignores both of its meaningful parameters (`message`,
   `numeric_only`) and fabricates a return value: the current editor
   level number as a string (or "80" if none is loaded). In the
   original, `UserInputDialogueMessage` is a real modal text-entry
   box used for four distinct prompts — load level number
   (`original/editor.c:845-846`), save level number
   (`:900-901`), time limit in seconds (`:942-943`), and level name
   (`:974-975`). None of those can actually be entered by the player
   today; every one of them silently receives "the current level
   number" instead of what the player typed. Status: new finding.

5. **Set Time (`T`) does nothing.** `editor_system.c`'s
   `do_set_time` (`src/editor_system.c:797-820`) guards the
   time-set side effect behind `if (ctx->cb.on_set_time != NULL)`.
   `game_callbacks_editor()` (`src/game_callbacks.c:1043-1061`)
   never assigns `.on_set_time`, so the callback is `NULL` and the
   guard silently skips the call — while `show_message(ctx, "Time
   limit adjusted", 1)` still fires, telling the player a change
   happened when nothing changed. Compare
   `original/editor.c:SetTimeForLevel` (`:934-962`), which calls
   `SetLevelTimeBonus` for real. Status: new finding.

6. **Saving a level always writes a hardcoded time bonus of 120
   seconds**, discarding the level's actual time limit (which, per
   finding 5, cannot even be edited).
   `editor_cb_save_level` (`src/game_callbacks.c:978-1005`) writes
   `fprintf(fp, "120\n");` unconditionally (line 990). The original
   `SaveLevelDataFile` writes the live value:
   `snprintf(str, ..., "%d\n", GetLevelTimeBonus())`
   (`original/file.c:544-546`). This silently corrupts the time
   limit of **any** existing level re-saved through the editor, not
   just newly authored ones. Status: new finding.

7. **Counter block hit-count is always saved as `0`.**
   `block_type_to_char` (`src/game_callbacks.c:919-976`) takes only
   `block_type`, never `counter_slide`, and its `COUNTER_BLK` case
   (line 937-938) unconditionally returns `'0'`. The original level
   file format encodes the counter's hit-count directly as digit
   characters `'0'`-`'5'` (`original/file.c:418-440`:
   `case '1': ... COUNTER_BLK, 1 ... case '5': ... COUNTER_BLK, 5`).
   `COUNTER_BLK` is one of the most common block types across the
   80 canonical levels — every one of them loses its hit-count the
   moment it is re-saved through the editor. Status: new finding.

8. **`RANDOM_BLK` cells resolved during play-testing are not restored
   before saving.** The original's `HandleRandomBlocks`
   (`original/editor.c:812-824`) walks the grid and forces any cell
   with the runtime `.random` flag set back to `RANDOM_BLK`, and is
   called after `FinishPlayTest` (`:634-641` — a `RANDOM_BLK` cell
   resolves to a concrete color during gameplay, and this call
   restores it to `'?'` before the level is editable/savable again).
   The modern `block_system` models the same flag
   (`include/block_system.h:98`
   `int random; /* RANDOM_BLK flag */`, plus
   `block_system_get_random`/`block_system_set_random`,
   lines 297-298) — the data model exists — but nothing in the
   editor integration layer reads or writes it.
   `editor_cb_query_cell` (`src/game_callbacks.c:875-884`) only
   copies `occupied`/`block_type`/`counter_slide`; the editor
   system's own `normalize_random_blocks`
   (`src/editor_system.c:94-111`) is an explicit no-op with a comment
   deferring the work to "the integration layer," and that layer
   never implements it. 26 of the 80 canonical levels contain `?`
   (`RANDOM_BLK`) cells (confirmed by grep against `levels/*.data`);
   play-testing any of them and then saving from the editor will
   bake the resolved color in, permanently losing the "random"
   behavior for that cell. Status: new finding.

9. **The tool palette only renders the first 20 of 30 entries**,
   permanently hiding 5 static block types and all 5 counter-block
   variants from the visible swatch list. `editor_system_init_palette`
   (`src/editor_system.c:282-309`) correctly builds all 30 entries
   (25 static block types, index 0-24, plus `COUNTER_BLK` slides
   1-5, index 25-29 — matches `MAX_STATIC_BLOCKS=25` in
   `original/include/block_types.h:49` and
   `original/editor.c:SetupBlockWindow` lines 329-369). But
   `game_render_editor_palette`
   (`src/game_render.c:728-781`) caps its render loop at
   `for (int i = 0; i < count && i < 20; i++)` (line 734). Because
   the enum ordering is `... MAXAMMO_BLK=21, ROAMER_BLK=22,
   TIMER_BLK=23, RANDOM_BLK=24` followed by the 5 counter entries,
   the swatches for `MAXAMMO_BLK`, `ROAMER_BLK`, `TIMER_BLK`,
   `RANDOM_BLK`, `DROP_BLK`, and every numbered counter block are
   never drawn. Keyboard palette selection (see minor 3 below) can
   still reach index 20-29 logically via arrow-cycling, but there is
   no visible swatch anywhere in the UI confirming what is selected
   at those indices — level authors have no discoverable way to
   place a timer, roamer, random, drop, or numbered counter block.
   Status: new finding.

### Majors

1. **No "Active:" current-selection indicator is rendered anywhere.**
   The original has a dedicated, always-visible `typeWindow` showing
   the label "Active:" plus a full-size preview of the selected
   block/slide (`SetCurrentSymbol`, `original/editor.c:256-268`),
   redrawn on entering the editor
   (`Editor()`, `:1143`), on every toolbar click (`:297`), and after
   every board redraw (`RedrawEditorArea`, `:215`). The modern port
   even defines the matching region,
   `SDL2RGN_EDITOR_TYPE = {545, 650, 120, 35}`
   (`src/sdl2_regions.c:44`, matching
   `original/stage.c:277-279` exactly), but nothing in
   `game_render.c` ever draws to it. The only "selection" feedback
   today is the in-list highlight described in the next finding.
   Status: new finding.

2. **The in-list selection highlight is an invented feature with a
   magic width that does not match its own region**, and is the
   likely source of the leader's "oversized yellow bar overflowing
   right" observation. `game_render_editor_palette`
   (`src/game_render.c:723-726`) hardcodes
   `#define PALETTE_W 100`, independent of both the actual
   `SDL2RGN_EDITOR` region width (120px,
   `src/sdl2_regions.c:41`) and the true pixel width of whatever
   block sprite is being highlighted (block sprite widths vary
   across the 25 static types). The highlight rect
   (`src/game_render.c:746-748`) is drawn at
   `{PALETTE_X - 2, ey - 2, PALETTE_W + 4, PALETTE_ENTRY_H}` — a
   fixed 104px-wide bar regardless of what it's next to. The
   original has **no equivalent highlight in the swatch list at
   all** — the only "what's selected" feedback in 1996 XBoing is
   the separate Active: preview (finding 1 above), not an
   in-list overlay. This is a bolted-on addition that both
   diverges from the original's UI model and has an internal
   magic-number mismatch (100 vs. the 120 the region and window
   width actually reserve). Status: new finding.

3. **Palette layout is a single column; the original uses two.**
   `game_render_editor_palette` lays every entry out top-to-bottom
   at `ey = PALETTE_Y + i * PALETTE_ENTRY_H`
   (`src/game_render.c:740`). `SetupBlockWindow`
   (`original/editor.c:329-369`) instead lays out two columns: the
   first 18 static types (limited by `MAX_ROW`) in column one
   (`:335-343`), the remaining static types continuing into column
   two (`:348-356`), and the 5 counter-block variants appended
   further down column two (`:359-368`). A single column at 25px
   per row needs 750px for 30 entries against a 580px-tall play
   area — the 20-entry cutoff in the blocker above is a direct,
   structural consequence of this layout choice, not an independent
   bug. Status: new finding, root cause of blocker 9.

4. **Right mouse button erases instead of inspecting.** In the
   original, `Button3` in `HandleEditorMouseButtons`
   (`original/editor.c:547-557`) is a **read-only** action:
   `DisplayScore(display, scoreWindow, blockP->hitPoints)` (or 0 if
   empty) shows the clicked block's hit points in the score window,
   and `drawAction = ED_NOP` — it never modifies the grid. The
   modern `mode_edit_update`
   (`src/game_modes.c:1388-1389`) maps *both* middle and right mouse
   buttons to erase:
   `if (sdl2_input_mouse_pressed(ctx->input, 2) ||
   sdl2_input_mouse_pressed(ctx->input, 3))
   editor_system_mouse_button(ctx->editor, play_x, play_y, 2, 1);`
   — right-click is destructive where the original was inspect-only,
   and the hit-points inspection feature is missing entirely.
   Status: new finding.

5. **Counter-block slide is discarded during board transforms**,
   independent of the save-time loss in blocker 7.
   `editor_cb_query_cell` (`src/game_callbacks.c:875-884`)
   hardcodes `cell->counter_slide = 0;` (line 882) with the comment
   "Simplified — full counter tracking deferred." Every board
   transform in `editor_system.c` —
   `editor_system_flip_horizontal` (`:460-510`),
   `editor_system_flip_vertical` (`:512-563`),
   `editor_system_scroll_horizontal` (`:565-626`), and
   `editor_system_scroll_vertical` (`:628-689`) — reads cells
   through this callback before rewriting them, so any counter
   block's hit-count is zeroed the instant the level is flipped or
   scrolled, even before a save. The original threads
   `counterSlide` through every one of the equivalent transforms
   (e.g. `original/editor.c:663-664` in `FlipBoardHorizontal`,
   `:696-719` in `ScrollBoardHorizontal`, `:745-746` in
   `FlipBoardVertical`, `:780-804` in `ScrollBoardVertical`).
   Status: new finding.

6. **Red editor grid lines are entirely absent from the modern
   renderer.** `DrawEditorGrid`
   (`original/editor.c:139-153`) draws a grid of red
   (`reds[4]`) lines over the 15 editable rows × 9 columns on every
   level load (`DoLoadLevel`, `:180`) and every non-play-test redraw
   (`RedrawEditorArea`, `:210-211`, gated off during `EDIT_TEST`).
   No equivalent call exists anywhere in `game_render.c` or
   `game_render_editor_palette`. Status: **already tracked**,
   bead xboing-c40.

### Minors

1. **Editor forces a fixed background (type 3); the modern port
   shows the level's own configured background instead.**
   `DoLoadLevel` and `RedrawEditorArea`
   (`original/editor.c:175-177`, `:208`) always call
   `DrawStageBackground(display, window, 3, True)` — a literal `3`
   (`back3Pixmap`), independent of whatever background number the
   loaded level file specifies. The modern
   `game_render_background` (`src/game_render.c:424-459`), called
   for `SDL2ST_EDIT` at `src/game_render.c:1211`, instead reads
   `level_system_get_background(ctx->level)` — the level's actual
   background tile. This is cosmetic only (no gameplay/data impact)
   but is a visible difference; see the Verify section — needs a
   live screenshot to confirm how noticeable the tile difference is
   in practice. Status: new finding.

2. **`on_error` is never wired.** `game_callbacks_editor()`
   (`src/game_callbacks.c:1043-1061`) does not assign `.on_error`,
   so load/save failure paths in `editor_system.c` (`do_load`
   `:736-739`, `do_save` `:769-771`, `:785-787`) silently do nothing
   on failure instead of surfacing feedback. The original calls
   `ErrorMessage`/`ShutDown` on the equivalent failures
   (`original/editor.c:163`, `:193`, `:382`, `:864`, `:918`).
   Status: new finding.

3. **Digit keys 1-9 and left/right-arrow palette cycling are new
   input methods with no original counterpart.** `mode_edit_update`
   (`src/game_modes.c:1461-1484`) adds keyboard palette selection —
   `SDL2I_SPEED_1`..`SDL2I_SPEED_9` select palette entries 0-8
   (`:1462-1470`), and `SDL2I_LEFT`/`SDL2I_RIGHT` cycle the
   selection (`:1472-1484`). Grepping `original/editor.c` for
   `XK_1` through `XK_9` and any digit keysym returns zero matches —
   the 1996 editor's **only** palette-selection mechanism is mouse
   click on a `blockWindow` swatch (`HandleEditorToolBar`,
   `:270-305`). This is an additive UX improvement, not a defect,
   but per the "modernization vs. redesign" test it is a genuine
   change to the input model the mission specifically asked about
   and should be recorded as a deliberate decision in
   `docs/DESIGN.md` (or removed) rather than left undocumented.
   Status: new finding — needs an ADR, not a code fix.

4. **Read/write level directory split (`levels_dir_readable` /
   `levels_dir_writable`) differs from the original's single
   writable directory model.** This is **not** a gap — it is a
   documented, approved deviation. `docs/DESIGN.md` (~line
   2124-2163) already records the ADR: the 1996 model
   (`original/editor.c:857-860`, `:887-931`, `:912-915`, plus
   `original/Imakefile:35-38, 208`'s `chmod a+rw` on installed data)
   assumed a single-user workstation where the install directory was
   writable; that assumption is incompatible with modern read-only
   `/usr/share`. Listed here only for completeness per the mission's
   coverage checklist. Status: **resolved / already documented**, no
   action needed.

## Recommended implementation order

Grouped by dependency, not by severity — some blockers share a root
cause and should be fixed together.

1. **Wire the real dialogue system into the editor callbacks**
   (blockers 3, 4). Everything else that depends on user text/number
   entry — Set Time, Save-as, Load, Set Name — is unblocked by this
   one fix, and it directly enables retiring the "Phase 6" stub
   comments in `game_callbacks.c`. `dialogue_system.c` already exists
   and is used elsewhere in the codebase (`SDL2ST_DIALOGUE`); this is
   wiring, not new subsystem design.
2. **Wire `on_set_time` and stop hardcoding the save-time value**
   (blockers 5, 6) — trivial once (1) provides real input, but track
   as a distinct fix since the hardcoded `"120\n"` is a live data-loss
   bug today independent of the dialogue work.
3. **Fix counter-block slide loss** (blocker 7, major 5) — both the
   save-path `block_type_to_char` and the transform-path
   `editor_cb_query_cell` need the real `counter_slide`; fix together
   since they share the same missing plumbing
   (`block_system_get_counter_slide` or equivalent accessor).
4. **Restore the `.random` flag round-trip** (blocker 8) — the data
   model (`block_system_get_random`/`set_random`) already exists;
   this is purely an integration-layer wiring fix in
   `editor_cb_query_cell`/`editor_cb_add_block`/`block_type_to_char`,
   parallel in shape to fix 3.
5. **Rebuild the palette as a real two-column layout** (major 3,
   blocker 9) before fixing the highlight or the Active: indicator —
   both of the latter are cosmetic on top of the palette geometry,
   and redoing the highlight math against a 20-entry single column
   would be wasted work.
6. **Add the Active: indicator and remove/rework the in-list
   highlight** (majors 1, 2) — once the two-column layout exists,
   decide whether to keep an in-list highlight at all (it has no
   original precedent) or replace it entirely with a faithful
   Active: swatch at `SDL2RGN_EDITOR_TYPE`.
7. **HUD in editor mode + suppress paddle/ball outside play-test**
   (blockers 1, 2) — independent of the palette work; can be done in
   parallel with steps 3-6. Straightforward: add `SDL2ST_EDIT` to the
   HUD-render gate (`src/game_render.c:1228-1230`), and gate
   `game_render_paddle`/`game_render_balls` in the `SDL2ST_EDIT`
   dispatch branch behind `editor_system_get_state(ctx->editor) ==
   EDITOR_STATE_TEST`.
8. **Grid lines** (major 6 / xboing-c40) — already tracked separately;
   sequence wherever that bead lands, no dependency on the above.
9. **Right-click inspect vs. erase** (major 4) — small, isolated fix
   in `mode_edit_update`'s mouse-button mapping; do any time, no
   dependency on the rest.
10. **`on_error` wiring** (minor 2) — trivial, do alongside (1)'s
    dialogue wiring since both touch `game_callbacks_editor()`.
11. **ADR for the digit-key/arrow palette selectors** (minor 3) —
    documentation only; do once the palette layout (step 5) is
    finalized so the ADR describes the shipped behavior, not an
    interim one.

## Verify

Items that need a live build + screenshot check, not just a code
read, before the design phase treats them as settled:

- **Editor background tile (minor 1).** Confirm how visually
  different the level's own background vs. the original's hardcoded
  `BACKGROUND_3` actually looks on a representative level (e.g. one
  using background index 1 or 5) — if the difference is negligible
  at a glance, this may not be worth fixing; if it's jarring
  (very different color/pattern), raise its severity.
- **Blind-selection reachability of palette indices 20-29 (blocker
  9).** Confirm via manual play that arrow-key cycling and/or
  clicking below the visible 20 rows can actually reach and place
  `TIMER_BLK`/`ROAMER_BLK`/`RANDOM_BLK`/`DROP_BLK`/`MAXAMMO_BLK`/
  counter variants today, even blind — this affects whether the fix
  is "make visible what already works" or "make work at all."
- **Real-world impact of the `.random` flag gap (blocker 8).**
  Confirm by loading a level with `?` cells in the editor, entering
  play-test, letting a `RANDOM_BLK` cell resolve to a concrete color,
  exiting play-test, and saving — verify the saved file's cell
  reverts to `?` or not. The code read strongly suggests it does not,
  but this needs a live round-trip to confirm severity and to use as
  a regression test once fixed.
- **Cursor and window-manager interaction during the widened
  editor canvas** — not a new finding, but worth re-checking
  alongside any editor render changes since ADR-057 already flagged
  `SDL2RGN_DIALOGUE`'s hardcoded 575-wide centering as needing
  re-centering once a modal dialogue is wired into the editor
  (directly relevant to recommendation 1 above).

## Summary of citations by module

- `original/editor.c` — state machine, mouse/keyboard handling,
  palette setup, board transforms, save/load prompts.
- `original/stage.c` — window creation/mapping (`CreateAllWindows`,
  `MapAllWindows`), confirming HUD windows are always visible.
- `original/file.c` — `SaveLevelDataFile`/`ReadNextLevel`, the level
  file format ground truth for time bonus and counter-block
  encoding.
- `original/include/block_types.h` — block type enum ordering and
  `MAX_STATIC_BLOCKS`/`MAX_BLOCKS` constants.
- `src/editor_system.c`, `include/editor_system.h` — pure-logic
  editor state machine (palette, transforms, key dispatch).
- `src/game_render.c` — editor render dispatch, palette rendering,
  HUD gate.
- `src/game_modes.c` — `mode_edit_enter`/`update`/`exit`, input
  wiring.
- `src/game_callbacks.c` — `editor_cb_*`, the integration layer
  where most of the blockers live.
- `src/sdl2_regions.c`, `include/sdl2_regions.h` — defined-but-unused
  `SDL2RGN_EDITOR_TYPE` region.
- `docs/DESIGN.md` — ADR-057 (width fix, resolved) and the
  levels-directory-split ADR (~line 2124, resolved/approved).
