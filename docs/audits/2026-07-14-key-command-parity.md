# Key-Command / Input-Binding Parity Audit

Date: 2026-07-14
Auditor: jck (Justin C. Kibell)
Mission: m-2026-07-14-008 (report, CONFIRM pass)

## Scope

Every key/mouse/CLI binding in the 1996 original that a player or
level designer can press, mapped mode-by-mode to its modern
equivalent: paddle movement, ball launch, bullet fire, pause, tilt,
quit, sound/SFX toggles, control-mode toggle, speed warp, level
select, editor entry, the attract-cycle advance key, high-score
screens, editor tool keys (draw/erase/inspect, save/load/clear/
set-time/set-name, flip/scroll, palette select), dialogue keys
(Return/Escape/Backspace/digit-entry), and the `-grab` pointer-confine
CLI flag. This is a **confirm pass**: the maintainer already ran
dedicated work on this surface (ADR-051 through ADR-059), so the
expectation going in was mostly MATCH with residual gaps surfaced as
candidates, not a fix queue.

## Method

Read `original/main.c` (`handleGameKeys`, `handleIntroKeys`,
`handleMiscKeys`, `handleSpeedKeys`, `handleQuitKeys`,
`handleExitKeys`, `handlePresentsKeys`, `handleMouseButtons`,
`handleKeyPress` mode dispatch), `original/editor.c`
(`handleAllEditorKeys`, `HandleEditorMouseButtons`,
`HandleEditorToolBar`), and `original/dialogue.c`
(`handleDialogueKeys`, `validateDialogueKeys`) to build the ground-
truth key→action→mode table. Mapped each row to
`src/game_input.c` (`game_input_global` for edge-triggered keys,
`game_input_update`/`input_update_paddle` for held keys),
`src/sdl2_input.c` (binding table), `src/game_modes.c`
(`mode_edit_update` editor keys/mouse), `src/dialogue_system.c` +
`src/game_main.c` (dialogue key routing), and `src/sdl2_cli.c` /
`src/game_init.c` (CLI flags). Every row below cites both sides.

## Key → Action Comparison Table

### Global / gameplay (`main.c` `handleGameKeys` / `handleMiscKeys` / `handleSpeedKeys`)

| Key | Original action | Original mode(s) | Modern key | Modern action | Modern mode(s) | Verdict |
|-----|------------------|-------------------|------------|----------------|-----------------|---------|
| Left / j / J | paddle left (held) | GAME, WAIT, BALL_WAIT, PAUSE — `original/main.c:485-490` | Left / J | `input_update_paddle`, `PADDLE_DIR_LEFT` — `src/game_input.c:51-52` | GAME | MATCH (cadence: per-tick, correct — held key in `game_input_update`) |
| Right / l / L | paddle right (held) | same — `main.c:499-504` | Right / L | `PADDLE_DIR_RIGHT` — `src/game_input.c:53-54` | GAME | MATCH (see paddle-velocity note below) |
| k / K | shoot: activate waiting ball, else fire bullet | GAME — `main.c:492-497` | K | `SDL2I_SHOOT`, same activate→shoot fallback | GAME | MATCH (`src/game_input.c:263-271`) |
| space | launch ball (via `ActivateWaitingBall` on mouse-button path too) | GAME — `main.c:539-552` (also attract cascade, see below) | Space | `SDL2I_START` in-game path | GAME | **MATCH with a cadence caveat** — binding correct, but `input_launch_ball` reads `just_pressed` in `game_input_update` (per-tick, `src/game_input.c:70-79`), so it can multi-fire at high warp with a sticky paddle. See the Cadence Review and peer-review addendum (candidate deviation). |
| Button1/2/3 (mouse) | activate waiting ball else shoot | GAME — `main.c:360-369` | Left/Middle/Right click | same dual-use | GAME | MATCH (`src/game_input.c:274-284`) |
| t / T | tilt board, `MAX_TILTS=3` cap, message | GAME — `main.c:451-473` | T | `SDL2I_TILT`, `GAME_MAX_TILTS` cap, same message | GAME | MATCH (`src/game_input.c:297-319`) |
| d / D | pop the active ball | GAME — `main.c:475-483` | D | `SDL2I_KILL_BALL` | GAME | MATCH (`src/game_input.c:286-295`) |
| p / P | toggle pause | WAIT, BALL_WAIT, PAUSE, GAME — `main.c:524-527`, `ToggleGamePaused` `main.c:232-252` | P | `SDL2I_PAUSE`, GAME↔PAUSE toggle | GAME | MATCH, cadence correct (edge-triggered in `game_input_global`, not per-tick — `src/game_input.c:145-164`) |
| Escape | "Abort current game? [y/n]" dialogue → intro | GAME — `main.c:506-509` | Escape | `SDL2I_ABORT`, same confirm dialogue | GAME | MATCH (`src/game_input.c:339-360`) |
| `=` (XK_equal) | **debug-only** cheat: skip to next level, gated on `debug==True` | GAME — `main.c:511-522` | — | none | — | **MISSING** (see Deviation #1) |
| z / Z | save game (only if `saving==True`) | GAME — `main.c:438-444` | Z | `SDL2I_SAVE`, `savegame_system_save` | GAME | MATCH — modernized: save always available (savegame v2), not original's `saving` gate; documented in ADR-038 |
| x / X | load saved game | GAME — `main.c:446-449` | X | `SDL2I_LOAD`, `savegame_system_load` | GAME | MATCH |
| a / A | toggle audio on/off, message | ALL modes (falls to `handleMiscKeys` default case) — `main.c:396-422, 848-851` | A | `SDL2I_TOGGLE_AUDIO`, same messages | ALL modes | MATCH (`src/game_input.c:197-210`) |
| g / G | toggle keys/mouse paddle control, "toggle" sound | ALL modes — `main.c:377-394, 859-862` | G | `SDL2I_TOGGLE_CONTROL` | ALL modes | MATCH (`src/game_input.c:250-257`) |
| i / I | `XIconifyWindow` (minimize) | ALL modes — `main.c:853-857` | I | fullscreen toggle (`sdl2_renderer_toggle_fullscreen`) | ALL modes | **REMAPPED, deviation undocumented** — code comment claims an ADR exists; none found (see Deviation #2) |
| + / KP_Add | volume up, message | ALL modes — `main.c:822-833` | = / KP_Add | `SDL2I_VOLUME_UP` | ALL modes | MATCH (`src/game_input.c:227-233`) |
| - / KP_Subtract | volume down, message | ALL modes — `main.c:835-846` | - / KP_Minus | `SDL2I_VOLUME_DOWN` | ALL modes | MATCH |
| q / Q | "Exit XBoing you wimp? [y/n]" → shutdown | ALL modes (default case) — `main.c:864-868` | Q | `SDL2I_QUIT`, same confirm text | ALL modes except EDIT/DIALOGUE/play-test | MATCH — the EDIT/play-test exclusions are deliberate and correctly mirror the original's `EDIT_TEST` switch swallowing Q entirely (`original/editor.c:992-1030`) and the editor having its own Q handler |
| 1-9 | set warp speed 1-9, per-speed tone volume | attract modes only (`handleIntroKeys` default → `handleSpeedKeys`) — `main.c:683-686, 741-811` | 1-9 | `SDL2I_SPEED_1..9` | attract modes only (`is_attract` allowlist) | MATCH (`src/game_input.c:174-193`) |

### Attract / intro-family (`main.c` `handleIntroKeys`)

| Key | Original action | Original mode(s) | Modern key | Modern action | Verdict |
|-----|------------------|-------------------|------------|----------------|---------|
| space (title) | → MODE_GAME directly | INTRO — `main.c:539-548` | Space | → INSTRUCT, not GAME | **DELIBERATE REDESIGN, documented** — ADR-053, user-requested change of the title flow |
| space (other attract) | → MODE_GAME | INSTRUCT/DEMO/KEYS/KEYSEDIT/HIGHSCORE/PREVIEW — `main.c:540-548` | Space | → GAME (HIGHSCORE: → INTRO if `game_active`, else GAME) | MATCH, cadence fixed by ADR-053 (was multi-fire bug, now `game_input_global`, once/frame) |
| space (BONUS) | `SetBonusWait(BONUS_FINISH, frame)` — fast-forward | BONUS — `main.c:550-551` | Space | `bonus_system_skip` | MATCH, correctly kept per-tick (idempotent, ADR-053 rationale) |
| c / C | cycle attract screens INTRO→INSTRUCT→DEMO→KEYS→KEYSEDIT→HIGHSCORE→PREVIEW→INTRO | attract modes — `main.c:554-606` | C | `SDL2I_CYCLE`, `game_callbacks_attract_next` table (attract) | MATCH — same cycle order (`src/game_input.c:411-419`) |
| H (shift) | personal high scores | attract modes — `main.c:608-622` | Shift+H | `HIGHSCORE_TYPE_PERSONAL` (attract) | MATCH (`src/game_input.c:214-222`, `sdl2_input_shift_held`) |
| h | global high scores | attract modes — `main.c:624-637` | H | `HIGHSCORE_TYPE_GLOBAL` (attract) | MATCH |
| s / S | toggle visual SFX, message | attract modes — `main.c:639-669` | S | `SDL2I_TOGGLE_SFX` (attract) | MATCH (`src/game_input.c:167-172`) |
| w / W | change starting level (numeric dialogue) | attract modes — `main.c:671-673` | W | `SDL2I_SET_LEVEL`, numeric dialogue (attract) | MATCH (`src/game_input.c:371-388`) |
| e / E | enter editor | attract only — `main.c:676-681` (no E case in `handleGameKeys`) | E | `SDL2I_ENTER_EDITOR`, reachable from attract **and** `SDL2ST_GAME` | **DEVIATION (undocumented)** — modern widens E to gameplay; original binds it only in `handleIntroKeys` (attract). See Deviation #3 |

### Presents screen (`main.c` `handlePresentsKeys`)

| Key | Original action | Modern key | Modern action | Verdict |
|-----|------------------|------------|----------------|---------|
| space | `QuickFinish` — skip splash | Space | `presents_system_skip` | MATCH |
| q / Q | shutdown | Q | quit dialogue path — presents is not in the `is_attract` allowlist for the Q exclusion list (mode != EDIT/DIALOGUE/play-test), so Q reaches the same confirm-quit as elsewhere | MATCH in effect (both exit the game on Q); minor procedural difference — original shuts down immediately with no confirmation from PRESENTS specifically, modern always confirms. Low severity, not flagged as a ranked deviation (a confirm dialogue before quitting is strictly safer, not a feel change a 1996 player would object to) |

### Dialogue keys (`original/dialogue.c` `handleDialogueKeys` / `validateDialogueKeys`)

| Key | Original action | Modern key | Modern action | Verdict |
|-----|------------------|------------|----------------|---------|
| Return | accept, unmap dialogue | Return (`SDLK_RETURN`) | `DIALOGUE_KEY_RETURN` | MATCH (`src/game_main.c:127-129`) |
| Escape | cancel, unmap dialogue | Escape | `DIALOGUE_KEY_ESCAPE` | MATCH |
| BackSpace | delete last char, "key" sound | Backspace (`SDLK_BACKSPACE`) | `DIALOGUE_KEY_BACKSPACE` | MATCH |
| Delete | **also** delete last char (aliased with BackSpace) — `dialogue.c:327-328` | — | not wired (only `SDLK_BACKSPACE` handled, `src/game_main.c:130-132`) | **MINOR GAP** (see Deviation #4) |
| any char, TEXT_ENTRY_ONLY | accept `XK_space..XK_z` (0x20-0x7a) | `SDL_TEXTINPUT` | `DIALOGUE_VALIDATION_TEXT`, `ch >= ' ' && ch <= 'z'` | MATCH — identical byte range (`src/dialogue_system.c:42-44`) |
| any char, NUMERIC_ENTRY_ONLY | `XK_0..XK_9` | `SDL_TEXTINPUT` | `DIALOGUE_VALIDATION_NUMERIC`, `'0'..'9'` | MATCH |
| any char, YES_NO_ENTRY | `y/Y/n/N` only, one char max | `SDL_TEXTINPUT` | `DIALOGUE_VALIDATION_YES_NO`, same chars, `input_len>0` guard | MATCH |
| any char, ALL_ENTRY | `XK_space..XK_asciitilde` (0x20-0x7e) | `SDL_TEXTINPUT` | `DIALOGUE_VALIDATION_ALL`, `' '..'~'` | MATCH |

### Editor keys (`original/editor.c` `handleAllEditorKeys` / mouse handlers)

| Key/input | Original action | Modern key/input | Modern action | Verdict |
|-----------|------------------|-------------------|----------------|---------|
| Button1 (left) down | erase-then-draw at cell, `drawAction=ED_DRAW` | Left-click | `editor_system_mouse_button(...,1,1)` | MATCH (`src/game_modes.c:1509-1528`) |
| Button2 (middle) down | erase at cell, skull cursor, `drawAction=ED_ERASE` | Middle-click **and** Shift+Left-click | `editor_system_mouse_button(...,2,1)` | MATCH + **documented enhancement** — ADR-058 adds Shift+Left-click as a second erase path for hardware without a middle button; middle-click retained unchanged |
| Button3 (right) down | read-only inspect: show `hitPoints` or 0 in score window | Right-click | `editor_inspect_active`/`editor_inspect_value` = `block_system_get_hit_points` | MATCH (`src/game_modes.c:1533-1557`) |
| toolbar click (any button) | select palette entry (`curBlockType`) | Left-click on palette | `editor_system_select_palette` via `game_render_editor_palette_index_at` hit-test | MATCH (`src/game_modes.c:1666-1675`) |
| — (no keyboard palette select in original) | — | 1-9, Left/Right arrows | cycle/select palette entry | **ADDED, not a deviation from removed behavior** — original has zero keyboard palette binding (mouse-only, `original/editor.c:270-305`); modern adds keys 1-9 and arrow-cycle as a convenience. Does not conflict with any original binding (arrows are unbound in normal EDIT state in the original — only bound during the `EDIT_TEST` sub-state). Flagged for maintainer awareness only, not a fidelity gap |
| Q / q | confirm-if-modified, then quit to intro | Q | `SDL2I_QUIT`/`SDL2I_ABORT` → `EDITOR_KEY_QUIT` | MATCH (`src/game_modes.c:1579-1597`) |
| R / r | redraw + "Redraw" message (cosmetic) | R | `EDITOR_KEY_REDRAW`, same message | MATCH (`src/editor_system.c:987-989`) |
| l / L | confirm-if-modified, then load-level dialogue | L | `EDITOR_KEY_LOAD` | MATCH (`src/game_modes.c:1621`) |
| s / S | save-level dialogue | S | `EDITOR_KEY_SAVE` | MATCH (`src/game_modes.c:1620`) |
| t / T | set-time dialogue, range [1-3600] | T | `EDITOR_KEY_TIME` | MATCH (`src/game_modes.c:1623`) |
| n / N | set-name dialogue, 25-char max | N | `EDITOR_KEY_NAME` | MATCH (`src/game_modes.c:1624`) |
| p / P | enter play-test | P | `SDL2I_PAUSE` → `EDITOR_KEY_PLAYTEST` | MATCH (`src/game_modes.c:1604-1605`) |
| c / C | confirm, then clear level | C | `EDITOR_KEY_CLEAR` | MATCH (`src/game_modes.c:1622`) |
| h (lowercase) | flip board horizontal | h (no Shift) | `EDITOR_KEY_FLIP_H` | MATCH (`src/game_modes.c:1627-1629`) |
| H (shift) | scroll board horizontal | Shift+H | `EDITOR_KEY_SCROLL_H` | MATCH (`src/game_modes.c:1634`) |
| v (lowercase) | flip board vertical | v (no Shift) | `EDITOR_KEY_FLIP_V` | MATCH (`src/game_modes.c:1630`) |
| V (shift) | scroll board vertical | Shift+V | `EDITOR_KEY_SCROLL_V` | MATCH (`src/game_modes.c:1635`) |
| **EDIT_TEST sub-state:** Left/j/J, k/K, l/L/Right, P | paddle left, shoot, paddle right, end-test | same physical keys, but routed through normal GAME-mode paddle/shoot handling since play-test runs under `SDL2ST_GAME` with `play_test_active=true` | same effective behavior | MATCH — architecturally different (original keeps a dedicated `EDIT_TEST` sub-switch inside `handleEditorKeys`; modern reuses the GAME-mode input path), but produces the same key→action mapping. No held/edge cadence issue: paddle stays per-tick, P stays edge-triggered via the same `game_input_global` gate used for normal pause (`src/game_input.c:145-159`) |

### CLI flags

| Flag | Original | Modern | Verdict |
|------|----------|--------|---------|
| `-grab` | Confine pointer to window via `XGrabPointer` | `-grab`, confines via SDL — `src/game_init.c:360-361`, `src/sdl2_cli.c:192` | MATCH |
| `-debug` | Enables debug mode; only observable effect found is gating the `=` cheat-skip-level key | `-debug` parsed, stored in `ctx->debug_mode` | **PARSED BUT INERT** — see Deviation #1; the flag exists but nothing reads `ctx->debug_mode` after assignment (`src/game_init.c:322`) |

## Cadence Correctness Review

Checked every edge-triggered (fire-once) binding against the
`docs/TESTING.md` rule ("edge keys → `game_input_global` once per
frame; held keys → `game_input_update` per tick; a `just_pressed`
handler in per-tick code multi-fires").

- **All edge-triggered global keys** (P, S, A, G, H/h, +/-, I, Q, C,
  W, 1-9 speed, and the *attract*-screen Space) live in
  `game_input_global` (`src/game_input.c:130-462`), called exactly once
  per visual frame from `game_main.c:150`. No multi-fire risk in these.
  **Exception:** the *in-game* ball-launch Space (`SDL2I_START`,
  `input_launch_ball`, `src/game_input.c:70-79`) is read in
  `game_input_update` (per-tick), not `game_input_global`, so it can
  multi-fire at high warp with a sticky paddle — see the peer-review
  addendum below (Deviation, candidate). The "no multi-fire" conclusion
  applies to the global keys, not to the in-game launch.
- **Held paddle-direction keys** (Left/J, Right/L) live in
  `input_update_paddle`, called from `game_input_update`
  (`src/game_input.c:88-97`), itself called from `mode_game_update`
  once per fixed-timestep tick (`src/game_modes.c:287`) — correct
  side of the split, no sluggishness from being stuck in a
  once-per-frame handler.
- **Editor letter keys** (S/L/C/T/N/R, h/H/v/V) use a bespoke
  300ms-debounce raw-scancode poll (`ED_KEY` macro,
  `src/game_modes.c:1607-1639`) rather than the `just_pressed` action
  system, because several of those letters double-book onto game
  actions (L=paddle-right, S=SFX-toggle) in the binding table and
  would otherwise fire the wrong action while in the editor. This is
  a different mechanism from the documented `just_pressed` pattern
  but achieves the same "fire once per press" property (debounce
  window > one frame at any reasonable frame rate) — **not flagged
  as a cadence bug**, but noted as an architectural exception a future
  maintainer should know about before assuming all edge keys go
  through `sdl2_input_just_pressed`.
- **No held-key handler was found misplaced in `game_input_global`,
  and no edge-triggered handler was found misplaced in
  `game_input_update`.** Cadence placement is correct everywhere
  audited.

## Ranked Deviations

| # | Finding | Severity | Original cite | Modern cite | ADR status | Player impact |
|---|---------|----------|----------------|--------------|-------------|----------------|
| 1 | `=` (XK_equal) debug cheat-skip-level key is entirely absent. `-debug` CLI flag is parsed into `ctx->debug_mode` but nothing reads that field anywhere in `src/`. | Low | `original/main.c:511-522` (gated `if (debug == True)`) | `src/game_init.c:322` (dead field) | **UNDOCUMENTED** — no ADR | None for normal play (original also gated this behind a rarely-used debug flag); affects only maintainers/testers who relied on `-debug` + `=` to skip levels during QA |
| 2 | `I` key remapped from "iconify/minimize" to "toggle fullscreen." Code comment at `src/game_input.c:243-245` says "See ADR in docs/DESIGN.md" — **no such ADR exists.** | Medium | `original/main.c:853-857` (`XIconifyWindow`) | `src/game_input.c:246-247` (`sdl2_renderer_toggle_fullscreen`) | **UNDOCUMENTED — false citation.** A reasonable, defensible modernization (SDL2 has no iconify-via-key idiom as clean as X11's; fullscreen toggle is the natural analog), but the code claims a design record that isn't there. | Low direct gameplay impact (I is a rarely-pressed utility key), but the false citation is a process defect — a reader who checks the ADR to understand *why* finds nothing |
| 3 | `E` (enter editor) is reachable from `SDL2ST_GAME` in the modern port; the original's `handleGameKeys` switch has no `E` case at all — editor entry is only reachable from the attract-mode `handleIntroKeys` switch in 1996. | Low | `original/main.c:430-532` (no `XK_e`/`XK_E` case), `main.c:676-681` (attract-only) | `src/game_input.c:365-367` (`mode == SDL2ST_GAME \|\| is_attract`) | **UNDOCUMENTED** | Adds a capability the original didn't have (abandon an in-progress game straight into the editor); low risk since it's a deliberate widening of access, not a broken original behavior, but not logged as an approved deviation |
| 4 | `Delete` key does not trigger dialogue backspace; only `Backspace` is wired. Original aliases `XK_Delete` and `XK_BackSpace` to the same action. | Low | `original/dialogue.c:327-328` | `src/game_main.c:130-132` (only `SDLK_BACKSPACE`) | **UNDOCUMENTED** | Negligible — Backspace is the standard modern text-deletion key; Delete-as-backspace was a period X11/Sun-keyboard convenience unlikely to be muscle memory for a modern player |
| 5 (cross-reference, not re-derived here) | Paddle keyboard speed is 2.0x the original's real px/s (533 vs 267 px/s at default game speed) — `PADDLE_ANIMATE_DELAY` gate dropped, but `PADDLE_VELOCITY` only halved (10→4) instead of quartered (10→2.5→2). Directly relevant to this audit's "paddle move L/R (held)" row. | High (already tracked) | `original/paddle.c:180-193`, `original/main.c:962-963` | `include/paddle_system.h:49`, `src/game_input.c:43-64` | **UNDOCUMENTED** — already found and ranked in `docs/audits/2026-07-13-gameplay-logic-parity.md` finding #1; still unresolved as of this audit (`PADDLE_VELOCITY` is still `4`) | Confirmed open; not re-litigated here, just cross-referenced since it falls squarely inside this audit's "paddle move L/R held" scope |

No other deviations found in the comparison tables above (aside from
the additional candidate the peer review surfaced — the in-game
Space/ball-launch multi-fire at high warp, documented in the
peer-review addendum below). Every other row is a confirmed MATCH or a
documented, approved deviation (ADR-053 title-space redesign, ADR-058
shift-click erase).

## Confirmed Matching (summary)

The bulk of the KEY-COMMAND surface is a **confirmed MATCH**,
consistent with the mission's expectation given prior work
(ADR-051..059):

- All gameplay edge keys: shoot (K/space/mouse), tilt (T), kill-ball
  (D), pause (P), abort (Escape), save/load (Z/X)
- All held gameplay keys: paddle left/right (Left/J, Right/L) — binding
  and cadence correct; velocity magnitude is the separate, already-
  tracked deviation #5 above
- All global toggles: audio (A), control mode (G), volume (+/-),
  quit (Q)
- All attract-mode keys: cycle (C), space-cascade (per ADR-053),
  high-scores (H/h), SFX toggle (S), set-level (W), enter-editor (E)
- Presents screen: space-skip, quit
- All dialogue keys: Return, Escape, Backspace, and all four
  validation classes (text/numeric/yes-no/all)
- All editor keys: Q, R, L, S, T, N, P, C, h/H, v/V, mouse
  draw/erase/inspect (plus the Shift-click erase addition, ADR-058),
  palette selection
- `-grab` CLI flag
- Cadence placement: every edge-triggered binding audited is in
  `game_input_global` (once/frame); every held binding audited is in
  `game_input_update` (per tick). No multi-fire or sluggish-response
  bugs found in this pass.

## Recommended Follow-Up Beads

These are candidates for the maintainer to triage, not a mandated fix
list:

1. Either wire `ctx->debug_mode` to a skip-level cheat key (restoring
   the original's `=`/debug-gated behavior) or remove the dead
   `-debug` flag/field if it's not meant to do anything yet. Low
   priority — QA/dev convenience only.
2. Add the missing ADR for the `I` iconify→fullscreen remap (Deviation
   #2), or delete the false citation in `src/game_input.c:245` if a
   documented decision was never actually meant to be required for this
   one. Either resolves the "cites a record that doesn't exist" defect.
3. Decide and log whether `E`-from-`SDL2ST_GAME` (Deviation #3) is an
   intentional widening of access; if intentional, log it as an ADR;
   if not, gate it to attract-only to match the original exactly.
4. Optional: wire `SDLK_DELETE` alongside `SDLK_BACKSPACE` in
   `src/game_main.c` dialogue routing for full original parity
   (Deviation #4). Cosmetic, very low priority.
5. Not new to this audit, but in-scope and still open: resolve
   `docs/audits/2026-07-13-gameplay-logic-parity.md` finding #1 (paddle
   velocity 2x) — **jck must approve** any `PADDLE_VELOCITY` change per
   that audit's own recommendation, since it alters paddle feel.

## Peer-review addendum (jdc, 2026-07-14)

The peer review CONFIRMED findings 1-4 against the code and added two items:

- **New candidate — space/ball-launch can multi-fire at high warp.**
  `input_launch_ball` (`src/game_input.c:70-79`) reads
  `sdl2_input_just_pressed(SDL2I_START)` inside `game_input_update`
  (per-tick, `mode_game_update` → up to `SDL2L_MAX_TICKS_PER_UPDATE=10`
  ticks per frame at high warp). `ball_system_activate_waiting`
  activates one `BALL_READY` ball per call, but sticky-bat can hold 2+
  balls in `BALL_READY` (`ball_system.c:565-576`), so at high warp one
  space press could launch more than one stuck ball in a single frame
  vs the original's one-per-keypress. Same `just_pressed`-in-per-tick
  class ADR-053 fixed for attract-screen space; not extended here.
  CANDIDATE — verify at high game speed with a sticky paddle.

- **Row 77 (E) code-comment note.** `src/game_input.c:363-364`'s comment
  claims the original handles E in `handleGameKeys`; it does not (E is
  only in `handleIntroKeys`). Same class as the `I`-key false-ADR
  citation (Deviation #2) — a stale/incorrect provenance comment.

Two report defects the review caught were fixed in this revision: the
attract-family table column count (MD056) and row 77's verdict cell
(was `MATCH`, now correctly `DEVIATION`).
