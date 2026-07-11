# Resume — Editor window-width fix (xboing-di8)

**Branch:** `feat/editor-window-width` (from `master`). Design committed
`30c14ab`. Base master is at `9a24e65` (1.0.3 release).
**Bead:** `xboing-di8` (P2, `in_progress`, claimed). Follow-up split out:
`xboing-c40` (editor grid lines — separate, do NOT do here).
**Phase:** design DONE + approved by the user. **Next = implementation.**

## The bug

In level-editor mode the SDL2 port keeps a fixed render logical width
`SDL2R_LOGICAL_WIDTH=575` (`include/sdl2_renderer.h:21`). The tool palette is
drawn at `PALETTE_X = OFFSET_X(35) + GAME_PLAY_WIDTH(495) + 15 = 545`,
`PALETTE_W=100`, highlight rect right edge x=649 (`src/game_render.c:57-61,
712-735`). That overflows the 575 canvas by ~70px → palette clipped
(user screenshot confirmed). The original widened the window by
`EDITOR_TOOL_WIDTH=120` on editor entry and restored on exit
(`original/editor.c:161-164`, `:381-383`; `original/stage.c:232,238-240,
272-279`). Modern port never does this; `EDITOR_TOOL_WIDTH=120` already
exists unused at `include/editor_system.h:27`.

## Approved design — source of truth

`docs/specs/2026-07-11-editor-window-width.md` (committed, 398 lines,
markdownlint-clean). Read it before implementing — the summary below is a
pointer, the doc is authoritative.

**Mechanism:** on entering `SDL2ST_EDIT`, grow logical width AND physical
window width **in lockstep** so the logical→physical scale factor is
unchanged — 575 → 575+120 = **695**. Play grid keeps exact geometry and
on-screen pixel size; only a 120px palette panel is added on the right.
Restore to 575 on exit. `PALETTE_X/W` unchanged.

- **Restore formula (self-inverting):** `new_window_w = new_logical_w *
  current_window_h / current_logical_h` (mirrors the creation-time fit math
  at `sdl2_renderer.c:100`; exact restore given window height unchanged
  between EDIT entry/exit — manual height-resize mid-EDIT is the one
  disclosed edge case).
- **Secondary bug (must fix in same change):** `render_main_background`
  (`src/game_render.c:690-698`) tiles using compile-time
  `SDL2R_LOGICAL_WIDTH/HEIGHT` macros → the widened panel would render bare
  black. Fix: read current size via `sdl2_renderer_get_logical_size(...)`.
- **z-spec: NOT needed** (deterministic geometry; `sdl2_state_transition`
  already guarantees one `on_exit`→`on_enter` pair, no interleaving).

## Files to change (implement-mission write-set)

- `include/sdl2_renderer.h` — declare `sdl2_renderer_set_logical_width`.
- `src/sdl2_renderer.c` — implement it. Idempotent (no-op if already at
  target). Windowed: grow window width proportionally. Fullscreen:
  logical-only (disclosed trade-off). Struct already has
  `logical_width/logical_height/fullscreen/window` — no struct change.
- `src/game_modes.c` — `mode_edit_enter` widen call
  (`set_logical_width(..., 575+120)`); add **new** `mode_edit_exit` restore
  call (`set_logical_width(..., 575)`) and register `.on_exit` (currently
  missing at the `SDL2ST_EDIT` registration ~`game_modes.c:1589-1596`).
- `src/game_render.c` — `render_main_background` loop bounds → current
  logical size.
- `tests/test_sdl2_renderer.c` — unit tests: widen (logical grows, height
  same), window grew proportionally (style of `test_custom_scale`/
  `test_window_size_default_scale` at `:88-112`), widen→restore returns
  exact original window width, no-op idempotence, fullscreen physical
  unchanged.
- Integration test (grep `setup_editor` for the file, e.g.
  `tests/test_keybindings.c:91` fixture): INTRO→EDIT asserts
  `get_logical_size==695`; EDIT→INTRO asserts 575; playtest round trip
  EDIT→GAME==575, GAME→EDIT==695. Uses `sdl2_state_transition`, no keystroke
  injection needed.
- `src/game_init.c` + `Makefile` — register an `editor` screen in the
  visual-capture pipeline (4-place checklist in `docs/TESTING.md` "Adding a
  new screen": name fn + `vc_check` branch in `game_init.c`; `-visual-capture`
  map in `sdl2_cli.c`; `visual-check` screen list in `Makefile`). Needed for
  Gates 4/5.
- New golden: `tests/golden/original/editor/*.png` via
  `make golden-screen SCREEN=editor` (needs live X11 — see below).

## Process to follow (docs/WORKFLOW.md Phase 3+)

Delegation is a mission, not a solo act (`.claude/rules/delegation.md`).
Implementation is NOT done by the leader (`claude`) directly.

1. **implement mission** — worker `sjl` (SDL renderer/window is its domain;
   it authored the design), evaluator `jdc` (sjl→jdc pairing). Write-set =
   the files above minus tests. `mission create` → spawn sjl worker →
   `result` → spawn jdc evaluator → `reflect`/`advance` on findings →
   `close`. Note: `implement` archetype requires leader≠worker (rule I
   shipped as ethos#346; sjl≠claude so fine).
2. **test mission** — worker `gjm`, evaluator `jdc` — the unit +
   integration tests (can fold into the implement mission if tighter).
3. **jck faithfulness** — confirm the rendered editor matches the original
   (already APPROVE-on-design; re-confirm at visual gate).
4. **Gate 2** `make check` (format/cppcheck/markdownlint local unavailable
   on this Mac → run in CI; ctest + asan run locally). **Gate 3** the
   mission reviews above. **Gates 4-6** visual: capture editor screenshot vs
   original, `make visual-check`/`llm_compare.py`, user confirms in `eog`.
5. **Gate 8 PR** — per `docs/GIT.md`: push, open PR, drive the 2-min review
   loop (Copilot/Cursor/bugbot), address findings folding fixes in directly
   ("PRs need not be narrow" — user's standing preference), merge
   `--squash --delete-branch` when the gate holds, post-merge cleanup.

## Design review outcomes (already done)

- Design mission `m-2026-07-11-016` **closed**. `jdc` PASS
  (verified restore formula + `on_exit`-before-`on_enter` dispatch
  algebraically); `jck` APPROVE-WITH-NOTES (faithful to original; the two
  notes — arithmetic slip + out-of-scope `game_render_ui.c:163,167,967`
  macro usages + restore edge case — already folded into the doc round 2).

## Standing constraints / gotchas

- macOS dev box: `clang-format`/`cppcheck`/`markdownlint`/`deb-lint` are NOT
  installed → they run in CI. `ctest` + `make asan-test` run locally.
- **CMake static-lib dep gotcha** (`docs/TESTING.md`): after changing a `.c`
  compiled into a static lib, `touch src/<file>.c` or `cmake --preset debug`
  to force relink; `xboing`/test binaries may not pick up the change.
- Visual goldens need a **live X11 display** (`import`); this is a Mac —
  Gate 4 golden capture of `original/xboing`'s editor may need the Linux/X11
  box or be deferred to CI/user. The headless logical-width assertions (unit
  + integration) run fine on macOS under `SDL_VIDEODRIVER=dummy`.
- Never widen scope into `xboing-c40` (editor grid lines) — separate bead.

## Immediate next step

Open the **implement mission** (`sjl` worker, `jdc` evaluator) for the
renderer primitive + `mode_edit_enter/exit` + `render_main_background` fix,
per the write-set above. Then tests, visual verification, PR.
