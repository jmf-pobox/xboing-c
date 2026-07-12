# Editor Window Width — Design Spec

- **Status:** Draft — ready for implementation
- **Worker:** Sam J. Lantinga (sjl), mission `m-2026-07-11-016`
- **Evaluator:** John D. Carmack (jdc)
- **Motivating bead:** `xboing-di8` — editor tool palette clipped at the
  right edge of the 575px-wide logical canvas.

## Problem

`SDL2R_LOGICAL_WIDTH` (`include/sdl2_renderer.h:21`) is a fixed 575 in
every mode, including `SDL2ST_EDIT`. The editor tool palette is drawn
at:

```c
#define PALETTE_X (PLAY_AREA_X + PLAY_AREA_W + 15)  /* 35+495+15 = 545 */
#define PALETTE_W 100
```

(`src/game_render.c:712-715`), and its selection-highlight rect spans
`PALETTE_X - 2` to `PALETTE_X + PALETTE_W + 2` = 545 to 649
(`src/game_render.c:735`). 649 < 575 is false — the palette overflows
the 575px canvas by ~74px and is clipped/cramped against the window
edge, confirmed by a user screenshot.

## What the original did

`original/editor.c:161-164` (`DoLoadLevel`, called on editor entry):

```c
ObtainWindowWidthHeight(display, mainWindow, &oldWidth, &oldHeight);
if (!ResizeMainWindow(display, mainWindow, oldWidth + EDITOR_TOOL_WIDTH, oldHeight))
    ErrorMessage("Cannot resize main window");
SetWindowSizeHints(display, oldWidth + EDITOR_TOOL_WIDTH, oldHeight);
```

`original/editor.c:381-383` (`DoFinish`, called on editor exit):

```c
if (!ResizeMainWindow(display, mainWindow, oldWidth, oldHeight))
    ErrorMessage("Cannot resize main window");
SetWindowSizeHints(display, oldWidth, oldHeight);
```

`EDITOR_TOOL_WIDTH` = 120 (`original/include/editor.h:59`). The
palette's own sub-window, `blockWindow`, is created at
`offsetX + PLAY_WIDTH + 15 = 35 + 495 + 15 = 545`, width
`EDITOR_TOOL_WIDTH` = 120 (`original/stage.c:273-274`) — an exact
match for the modern `PALETTE_X` = 545. The original never draws the
palette inside the normal 565px (`MAIN_WIDTH+PLAY_WIDTH`) window; it
grows the X11 window by 120px in Xlib's 1:1 pixel space, in place
(top-left anchored — `XResizeWindow` does not move a window), reveals
the two child windows, and shrinks back to the original width on
exit. There is no rescaling anywhere in this path — Xlib has no
concept of it. `DrawStageBackground` is also called on `blockWindow`
and `typeWindow` (`original/editor.c:175-177`) — the tiled space
background fills the added panel, not just the play area.

`EDITOR_TOOL_WIDTH` = 120 is already defined identically in the
modern port (`include/editor_system.h:27`) but never consumed for
sizing — confirming the constant was carried over in anticipation of
this fix and only the resize call was dropped.

## Mechanism

### The lever: widen both logical width and physical window width, in lockstep

The modern renderer has one concept the original didn't: a logical
canvas that SDL scales to the physical window
(`SDL_RenderSetLogicalSize`, `src/sdl2_renderer.c:155`). Changing
*only* `SDL_RenderSetLogicalSize`'s width (575 → 695) without touching
the physical window changes the logical:physical aspect ratio.
`SDL_RenderSetLogicalSize` computes a single uniform scale factor
(`min(physical_w/logical_w, physical_h/logical_h)`) and letterboxes
the shorter axis — so growing logical width alone, with the physical
window fixed, shrinks the effective scale and the play area would
render measurably smaller on screen. That breaks the stated
constraint: the play grid (9 cols × 495px) must keep both its
geometry **and** its on-screen pixel size; only a panel is added.

Changing *only* the physical window (`SDL_SetWindowSize`) without
touching logical width does nothing for the palette — it would just
add letterboxed dead space or (if `SDL_RenderSetLogicalSize` picks up
the new output size automatically, version-dependent) uniformly
rescale everything up, again resizing the play area.

**Decision: change both, together, sized so the physical:logical
scale factor is provably unchanged.** This is the SDL2 analogue of the
original's Xlib resize: grow the window to reveal new canvas space
without touching the existing content's pixel mapping.

Formula (implemented once, in `sdl2_renderer.c`, reused for widen and
restore):

```text
new_window_w = (new_logical_w * current_window_h) / current_logical_h
```

`current_logical_h` (720) and `current_window_h` are never touched by
this feature — only width changes. Because the multiply happens
before the divide, in the same operand order used at window-creation
time (`src/sdl2_renderer.c:100`: `fit_w = fit_h * logical_width /
logical_height`), restoring `new_logical_w` to 575 reproduces the
*exact* pre-edit physical width, including on displays where the
window was initially shrunk to fit (`SDL_GetDisplayUsableBounds` path,
`src/sdl2_renderer.c:91-110`) and the scale isn't a clean integer.
There is no need to snapshot/restore a separate `oldWidth` the way
`original/editor.c:118` does (`static int oldWidth, oldHeight`) — the
formula is self-inverting given `current_window_h` and
`current_logical_h` are stable.

Concretely, for the default config (scale=2, no display clamp):
`current_window_h` = 1440, `current_logical_h` = 720, scale = 2.
Widen: `new_window_w = 695 * 1440 / 720 = 1390` (was 1150 at
logical_w=575, scale exactly preserved, no rounding). Exit:
`new_window_w = 575 * 1440 / 720 = 1150` — back to the original value
exactly.

### New value: `EDITOR_TOOL_WIDTH` (120), reusing the existing constant

`new_logical_width = SDL2R_LOGICAL_WIDTH + EDITOR_TOOL_WIDTH = 575 +
120 = 695`. This is not an arbitrary tuning value — it's the same
arithmetic the original performs (`oldWidth + EDITOR_TOOL_WIDTH`), and
`EDITOR_TOOL_WIDTH` = 120 already exists at
`include/editor_system.h:27` for exactly this purpose. Reuse it; do
not introduce a second constant.

With `SDL2R_LOGICAL_WIDTH_EDIT` = 695: the palette highlight rect's
right edge (649, from `PALETTE_X + PALETTE_W + 2`) sits 46px inside
the new 695px boundary — comfortably clear, in the same ballpark as
the original's clearance (`blockWindow` right edge 665 inside the
695px-wide window = 30px). **`PALETTE_X` and `PALETTE_W` do not need
to change** — the fix is entirely in the canvas size, not the
palette's own layout, because `PALETTE_X` = 545 already matches the
original `blockWindow` x-origin exactly.

### New renderer primitive

Add one function to `include/sdl2_renderer.h` / `src/sdl2_renderer.c`
(general-purpose — this is a renderer-lifecycle concern, not
editor-specific policy, consistent with "one abstraction per platform
concern" — the editor must not reach into SDL types):

```c
/*
 * Change the renderer's logical width while preserving the on-screen
 * pixel size of existing logical content (height is never touched).
 *
 * Windowed mode: grows/shrinks the physical window width in lockstep,
 * derived from the window's current height, so the logical->physical
 * scale factor is unchanged.  Existing content (e.g. the play area)
 * does not resize or shift; only new horizontal logical space is
 * exposed (or reclaimed).  The window is not repositioned or
 * recentred — same top-left-anchored growth as the original's
 * ResizeMainWindow (original/editor.c:162-164).
 *
 * Fullscreen mode: the physical window already spans the display and
 * cannot grow, so only the logical width changes.  SDL's own uniform
 * letterbox scaling then shrinks the whole canvas to fit the new,
 * wider aspect ratio.  This is a deliberate, documented fidelity
 * trade-off unique to fullscreen editor use — see
 * docs/specs/2026-07-11-editor-window-width.md.
 *
 * No-op if new_logical_width already equals the current logical
 * width (idempotent — safe to call on every mode-enter).
 *
 * Returns 0 on success, -1 on NULL ctx or non-positive width.
 */
int sdl2_renderer_set_logical_width(sdl2_renderer_t *ctx, int new_logical_width);
```

Implementation sketch:

```c
int sdl2_renderer_set_logical_width(sdl2_renderer_t *ctx, int new_logical_width)
{
    if (ctx == NULL || new_logical_width <= 0)
        return -1;
    if (new_logical_width == ctx->logical_width)
        return 0;

    if (!ctx->fullscreen)
    {
        int win_w = 0, win_h = 0;
        SDL_GetWindowSize(ctx->window, &win_w, &win_h);
        int new_win_w = (new_logical_width * win_h) / ctx->logical_height;
        SDL_SetWindowSize(ctx->window, new_win_w, win_h);
    }

    ctx->logical_width = new_logical_width;
    SDL_RenderSetLogicalSize(ctx->renderer, ctx->logical_width, ctx->logical_height);
    return 0;
}
```

Order matters: resize the physical window *before* calling
`SDL_RenderSetLogicalSize`, so the logical-size call's internal scale
computation reads the already-updated output size, not a stale one.

## Aspect ratio / letterboxing implications

`SDL_RenderSetLogicalSize` maintains aspect ratio by picking
`scale = min(physical_w / logical_w, physical_h / logical_h)` and
letterboxing whichever axis has excess space. Because this design
grows `physical_w` in exact proportion to `logical_w` (both derived
from the same unchanged `physical_h` / `logical_h` pair), `scale_x`
and `scale_y` remain equal (up to integer-truncation, ≤1 physical
pixel) after the change — no letterbox bars are introduced by the
resize, and the play area's physical position/size is bit-identical
before and after, because the origin offset SDL computes for
letterboxing is `(physical_w - logical_w*scale)/2`, which stays 0 when
scale_x == scale_y exactly.

**Fullscreen is the one case this can't hold**: the physical window is
fixed at the display's resolution, so widening logical width there
necessarily shrinks the derived scale and the whole canvas (play
area included) renders slightly smaller — matching the new,
wider aspect ratio inside the same physical bounds. This is called
out explicitly above and in the function doc comment; it does not
regress the common windowed case this bead is about (the reported
screenshot), and the visual-fidelity capture pipeline
(`docs/TESTING.md` Layer 4) runs windowed, not fullscreen, so it will
not paper over the trade-off.

## Layout: does anything else move?

No. `PALETTE_X` = 545, `PALETTE_W` = 100
(`src/game_render.c:712-715`), and `SDL2RGN_EDITOR` = `{545, 60, 120,
580}` (`src/sdl2_regions.c:41`) are all already positioned to match
the original `blockWindow` geometry (`original/stage.c:273-274`).
The only thing wrong today is that the *canvas* clips at 575; growing
the canvas to 695 is sufficient. No palette constant changes.

One render bug the widen exposes and must be fixed in the same
change: `render_main_background` (`src/game_render.c:678-705`), which
tiles the space-background texture behind everything in every mode,
loops `for (tx = 0; tx < SDL2R_LOGICAL_WIDTH; ...)` — a compile-time
575, not the renderer's *current* logical width. With the canvas
widened to 695 during `SDL2ST_EDIT`, the new 120px panel would render
as bare black (from `sdl2_renderer_clear`, `src/sdl2_renderer.c:190`)
instead of the tiled background the original draws behind
`blockWindow`/`typeWindow` (`DrawStageBackground(display, blockWindow,
3, True)`, `original/editor.c:176-177`). Fix: change the loop bounds
to query `sdl2_renderer_get_logical_size(ctx->renderer, &w, &h)`
instead of the `SDL2R_LOGICAL_WIDTH`/`SDL2R_LOGICAL_HEIGHT` macros.
This is a one-line generalization (the function already takes `ctx`)
and correctly covers every mode, present and future, that changes
logical width — not just this one.

Besides `render_main_background`, grep for `SDL2R_LOGICAL_WIDTH` /
`SDL2R_LOGICAL_HEIGHT` turns up exactly two other call sites:
`src/game_render_ui.c:163,167` (presents-screen centering) and
`src/game_render_ui.c:967` (bonus-screen layout). Both are explicitly
out of scope for this change — neither is reachable from the
`SDL2ST_EDIT` render dispatch, which (`src/game_render.c:1198-1203`)
only calls the background, playfield, and editor-palette renderers.
The fix set for this bead is `render_main_background` alone; the
presents/bonus usages render correctly today because their modes
never adopt a widened logical width.

Two things checked and found *not* to need changes:

- **Mouse coordinate mapping** — `sdl2_input.c` reads `event->motion.x/y`
  directly from SDL mouse events (`src/sdl2_input.c:247-273`); it
  contains no reference to `SDL2R_LOGICAL_WIDTH` anywhere. SDL
  remaps these event coordinates to the renderer's *current* logical
  size internally, so widening at runtime requires no change here —
  clicks on the newly-revealed palette area already land at the
  correct logical x.
- **Dialogue centring** (`SDL2RGN_DIALOGUE` = `{97, 300, 381, 120}`,
  hardcoded against a 575-wide canvas, `src/sdl2_regions.c:46-47`) —
  irrelevant today because the editor's `on_input_dialogue` /
  `on_yes_no_dialogue` callbacks (`editor_cb_input_dialogue`,
  `editor_cb_yes_no`, `src/game_callbacks.c:1012-1023,1054-1055`) are
  stubs that do not call `sdl2_state_push_dialogue` — confirmed by
  grep, no call site exists. If/when a real modal dialogue is wired
  into the editor, `SDL2RGN_DIALOGUE`'s hardcoded 575 will under-centre
  against a 695-wide canvas by ~60px. Flagged for whoever does that
  work; out of scope here since the callback wiring doesn't exist yet.

## Restore-on-exit and rapid EDIT↔other transitions

`sdl2_state_transition` always calls `on_exit` of the old mode before
`on_enter` of the new one (`src/sdl2_state.c:144-161`), and is a no-op
if the destination equals the current mode
(`src/sdl2_state.c:138-141`). There are exactly two ways out of
`SDL2ST_EDIT` today, both via `sdl2_state_transition` (not the
dialogue push/pop path, which the editor doesn't use — see above):

- `editor_cb_on_finish` → `SDL2ST_INTRO` (`src/game_callbacks.c:1025-1029`)
- `editor_cb_on_playtest_start` → `SDL2ST_GAME`
  (`src/game_callbacks.c:1031-1035`), and back via
  `editor_cb_on_playtest_end` → `SDL2ST_EDIT`
  (`src/game_callbacks.c:1037-1041`)

**Add `mode_edit_exit`** (does not exist today — only `mode_edit_enter`
and `mode_edit_update` are registered, `src/game_modes.c:1592-1593`)
that calls `sdl2_renderer_set_logical_width(ctx->renderer,
SDL2R_LOGICAL_WIDTH)`. **`mode_edit_enter`** calls
`sdl2_renderer_set_logical_width(ctx->renderer, SDL2R_LOGICAL_WIDTH +
EDITOR_TOOL_WIDTH)`. Because every transition into or out of EDIT goes
through the same `on_exit`/`on_enter` pair, and the primitive is
idempotent (no-op if already at the target width), this needs no
special-casing for the playtest round trip: EDIT→GAME shrinks back to
575 (matching normal gameplay), GAME→EDIT (playtest end) widens back
to 695. No state leaks into INTRO/GAME/any other mode because every
non-EDIT mode always sees 575 on entry regardless of what happened
before. There is no window where the "widened" state could be left
active outside `SDL2ST_EDIT`.

This exact-restore guarantee assumes the window height is unchanged
between EDIT entry and exit — the formula derives the restored width
from the *current* window height at exit time. If the user manually
resizes the window's height during an EDIT session, `mode_edit_exit`
still produces a self-consistent width for that new height, but not
literally the pre-edit physical width. This is the same caveat any
modal-geometry assumption in the app already carries today; it is
disclosed here, not newly introduced or fixed.

## Verification plan

### Gate 4 — visual (mandatory)

1. Build, launch, transition to the editor (`E` from intro, or
   whatever the current entry key is).
2. Capture screenshot: `make modern-screen SCREEN=editor INTERVAL=200`
   (editor is not yet in the capture pipeline's screen list per
   `docs/TESTING.md`'s "adding a new screen" checklist — that's a
   prerequisite step for whoever implements this, using the existing
   `-visual-capture` / `vc_check` machinery in `src/game_init.c`).
3. Compare against `original/xboing`'s editor screen captured live
   (X11 + ImageMagick `import`, `docs/TESTING.md` Layer 4) — confirm
   the palette is fully visible, not clipped, and the play-area grid
   is pixel-identical in size/position to the non-edit game screen
   capture (overlay diff or side-by-side).

### Gate 5 — LLM judge

`llm_compare.py` on the new editor golden vs. modern screenshot, once
gate 4's captures exist.

### Gate 6 — user verification

Open both in `eog`, confirm side by side.

### Headless/unit assertions (fast, CI-run, no display needed)

1. **`tests/test_sdl2_renderer.c`** (`SDL_VIDEODRIVER=dummy`): direct
   unit tests for `sdl2_renderer_set_logical_width`:
   - Widen then verify `sdl2_renderer_get_logical_size` returns the
     new width, height unchanged.
   - Verify window width grew by the expected proportional amount
     (`sdl2_renderer_get_window_size`, aspect-ratio-preserved
     assertion in the same style as the existing `test_custom_scale` /
     `test_window_size_default_scale` tests, `tests/test_sdl2_renderer.c:88-112`).
   - Widen then restore to the original width; assert window width
     returns to the exact original value (exercises the self-inverting
     formula).
   - No-op call (same width twice) doesn't change anything — assert
     window size is byte-identical before/after.
   - Fullscreen: assert window physical size is unchanged after
     widening (only logical size changes).
2. **Integration test** using the existing `setup_editor` fixture
   (`SDL2ST_EDIT`, `docs/TESTING.md` / `tests/CLAUDE.md`): transition
   INTRO→EDIT, assert `sdl2_renderer_get_logical_size` == 695; then
   EDIT→INTRO (quit), assert it's back to 575. Also cover the
   playtest round trip: EDIT→GAME (playtest start key), assert 575;
   GAME→EDIT (playtest end), assert 695 again. This directly tests the
   "rapid EDIT↔other transitions" requirement without any keystroke
   injection (uses `sdl2_state_transition` / editor key-input helpers
   already used elsewhere in the test suite).

## Files to change (implement-mission write-set)

- `include/sdl2_renderer.h` — declare `sdl2_renderer_set_logical_width`.
- `src/sdl2_renderer.c` — implement it (struct already has
  `logical_width`/`logical_height`/`fullscreen`/`window` fields; no
  struct changes needed).
- `src/game_modes.c` — `mode_edit_enter` (widen call); add
  `mode_edit_exit` (restore call) and register it at
  `sdl2_state_register(ctx->state, SDL2ST_EDIT, &def)` (currently
  `src/game_modes.c:1592-1595`, missing `.on_exit`).
- `src/game_render.c` — `render_main_background` loop bounds: replace
  the `SDL2R_LOGICAL_WIDTH`/`SDL2R_LOGICAL_HEIGHT` macro references
  (`src/game_render.c:690-698`) with
  `sdl2_renderer_get_logical_size(ctx->renderer, &w, &h)` output.
- `tests/test_sdl2_renderer.c` — new unit tests per Verification Plan.
- `tests/test_game_modes.c` (or wherever EDIT-mode integration tests
  already live — grep for `setup_editor` to find the right file) —
  new integration test per Verification Plan.
- `src/game_init.c`, `Makefile` — capture-pipeline registration for an
  `editor` screen (`vc_check` branch + screen-name function +
  `visual-check` target's screen list), needed to run Gate 4/5
  reproducibly. This is plumbing, not core to the fix, but the design
  doc's own verification plan depends on it existing.
- New golden: `tests/golden/original/editor/*.png` (captured via
  `make golden-screen SCREEN=editor`, once the above plumbing exists).

No new constants are needed — `EDITOR_TOOL_WIDTH` = 120 already exists
at `include/editor_system.h:27` and is the correct, original-derived
value to add.

## Z-spec

Not warranted. The leader's assessment is correct: this is
deterministic, stateless geometry (one arithmetic formula, applied
symmetrically on mode enter/exit) with no concurrent state, no
invariant over a sequence of operations beyond "enter widens, exit
narrows, back to the same value" — which the existing
`sdl2_state_transition` machinery already guarantees structurally
(exactly one `on_exit`/`on_enter` pair per transition, no
interleaving, no re-entrancy). A Z spec earns its cost when there's a
non-trivial invariant over an unbounded interleaving of operations
(e.g. save/load state machines, block-grid mutation ordering); a
single idempotent resize function called from two fixed call sites
does not meet that bar. The unit + integration tests above cover the
actual risk (arithmetic correctness, round-trip exactness) more
cheaply than a formal spec would.
