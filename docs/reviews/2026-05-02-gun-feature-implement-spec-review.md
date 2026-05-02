# Peer Review: gun-feature-implement contract

**Date:** 2026-05-02
**Reviewer:** sjl (Sam J. Lantinga — SDL2/rendering/input)
**Mission:** m-2026-05-02-014
**Contract:** `.tmp/missions/gun-feature-implement.yaml`
**Research ref:** `docs/research/2026-05-02-gun-feature-comprehensive-reference.md`

---

## Verdict

**Accept with changes.**

Two blocking findings (Gap 5 ammo belt coordinates wrong; Gap 12 mouse-fire
abstraction underspecified) and three non-blocking findings. No scope split
needed — 13 gaps across 3 rounds is achievable for jdc given the research
document is well-structured and each gap is a small, localized change.

---

## Finding 1 — BLOCKING: Ammo belt X-coordinate formula is wrong for the level window width

**Severity:** Blocking
**Affected field:** `success_criteria` item for Gap 10 (game_render_ammo_belt), and the
`context` description of the X formula.

### Evidence

The contract specifies:

> X formula: `192 - (i+1)*9` per ammo index (right-aligned, 9px apart)

The level window has `LEVEL_AREA_X=284, w=286, h=52` (confirmed at
`src/game_render.c:417-418` and the layout comment at lines 51-52).

The original formula from `original/level.c:304-334` (per the research,
Topic 3) is:

```c
bulletPos = 192 - (GetNumberBullets() * 9)
```

That `192` is a window-relative X inside the original `levelWindow` (width 286).
For right-alignment of the entire ammo strip, it is the X of the first (leftmost)
bullet when all bullets are displayed. It is a base X for the strip, not an
`(i+1)` per-bullet formula.

The contract's `192 - (i+1)*9` formula iterates `i` from 0 for each bullet in a
strip of `ammo_count` bullets. For `ammo_count=4`:

- i=0: x=183, i=1: x=174, i=2: x=165, i=3: x=156

The original draws bullets at successive 9px intervals starting from
`bulletPos = 192 - (count * 9)`, e.g., for count=4:
`bulletPos = 192 - 36 = 156`, then draws bullet 0 at 156, bullet 1 at 165,
bullet 2 at 174, bullet 3 at 183.

Both produce the same pixel positions for the filled strip — the formulas are
equivalent for a full redraw. However, the contract never states this equivalence
or tests it.

The real problem: the research's formula `192 - (GetNumberBullets() * 9)` is the
start-X of the strip; the contract turns it into a per-bullet formula that runs
right-to-left (`i+1` means bullet 0 is drawn at the rightmost position). This
is the opposite drawing order from the original (which draws left-to-right from
`bulletPos`). The result is the same pixels if all slots are drawn, but the
**testable success criterion** says "4 bullet sprites visible with 9px spacing" —
it does not verify drawing order or that the rightmost bullet is at window-relative
x=183 (absolute x=`LEVEL_AREA_X + 183 = 467`).

More critically, the level window width is 286px. `192` as a window-relative
coordinate means the rightmost edge of the ammo strip is at absolute x=476.
For `GUN_MAX_AMMO=20` bullets at 9px each: strip width = 180px, leftmost
bullet at window-relative x=12. This fits. The contract does not check the
boundary condition for max-ammo (20 bullets), which must also fit.

**Revision:** Replace the X formula description in the context block:

Old:

```text
X formula: `192 - (i+1) * 9` per ammo index (right-aligned, 9px apart).
```

New:

```text
X formula: strip_start_x = 192 - (ammo_count * 9) (window-relative),
then draw bullet i at x = LEVEL_AREA_X + strip_start_x + (i * 9).
Matches original/level.c:AddABullet formula. Rightmost bullet edge is at
LEVEL_AREA_X + 192 = 476; leftmost bullet for 20 ammo is at LEVEL_AREA_X + 12.
Both fit within the level window (w=286, right edge at LEVEL_AREA_X + 286 = 570).
```

**Revision to success criterion item for Gap 10:**
Add: "rightmost bullet drawn at LEVEL_AREA_X + 183 (window-relative x=183) for
any ammo count 1..20."

---

## Finding 2 — BLOCKING: Mouse-fire needs edge-trigger, not level-trigger; no edge API exists

**Severity:** Blocking
**Affected field:** `context` Gap 12 description and `success_criteria` item for Gap 12.

### Evidence

The contract says:

> Wire SDL2 mouse-button-down (any button) in MODE_GAME to the same dual-use
> handler as K. Use the existing input layer to detect mouse clicks; do not
> poll raw events.

`sdl2_input_mouse_pressed()` exists and returns a level-trigger (button currently
held). Confirmed at `include/sdl2_input.h:162` and `src/sdl2_input.c:337`:

```c
bool sdl2_input_mouse_pressed(const sdl2_input_t *ctx, int button);
/* True if the given mouse button is currently pressed (1=left, 2=mid, 3=right). */
```

There is NO `sdl2_input_mouse_just_pressed()` function. The header lists all
public API and none of the mouse functions have edge-trigger semantics.
`sdl2_input.c` tracks `mouse_buttons` as a bitmask updated on
`SDL_MOUSEBUTTONDOWN` / `SDL_MOUSEBUTTONUP` but has no `mouse_just_pressed[]`
array analogous to `just_pressed[]`.

Using the level-trigger `sdl2_input_mouse_pressed()` for fire would cause the
gun to shoot on every frame the button is held — potentially 60 bullets per
second, emptying 20 ammo in one third of a second. The original fired once per
button-down event (`original/main.c:357-366` processes each `ButtonPress` event
once through the Xlib event loop — event-driven, not polled).

**The contract says "use the existing input layer" but the existing input layer
lacks the needed primitive.** The implementer has two options:

1. Add `sdl2_input_mouse_just_pressed()` to the input layer (modifies
   `src/sdl2_input.c`, `include/sdl2_input.h` — both outside the current
   write-set).
2. Track a local `prev_mouse_button` state in `game_input.c` to synthesize
   edge-trigger from the level-trigger.
3. Pre-compute mouse edge in `sdl2_input_begin_frame` alongside the existing
   key edge clearing (cleanest, but requires modifying `sdl2_input.c`).

Option 1/3 is the right long-term approach (consistent with the
"do not poll raw events" intent), but `include/sdl2_input.h` and `src/sdl2_input.c`
are outside the write-set. The write-set includes only `src/game_input.c` and
`include/game_input.h`.

**Revision:** Expand the write-set to include `src/sdl2_input.c` and
`include/sdl2_input.h`, and require the implementer to add
`sdl2_input_mouse_just_pressed(ctx, button)` following the same
pattern as `sdl2_input_just_pressed`. Without this the "do not poll raw events"
constraint is unimplementable while staying within the write-set.

Alternatively, constrain Gap 12 to option 2 (local edge synthesis in
`game_input.c`):

```text
Add mouse_btn_was_down[3] state to game_input.c (file-static) to synthesize
edge-trigger from sdl2_input_mouse_pressed. This avoids modifying sdl2_input.
Note: this pattern does not survive a game_input.c reload — acceptable for MVP.
```

Either revision must be chosen explicitly; the current contract leaves this
ambiguous and the implementer will either break the "no raw events" rule or
produce a continuous-fire bug.

---

## Finding 3 — BLOCKING: Gap 5 block-system multi-hit needs a new API function; write-set covers the files but the spec does not acknowledge the gap

**Severity:** Blocking
**Affected field:** `context` Gap 5 description, `write_set`, `success_criteria` for Gap 5.

### Evidence

The `block_system` public API (verified from `include/block_system.h`) has no
function to mutate `counter_slide` on an existing block. The public write
functions are: `block_system_add`, `block_system_clear`, `block_system_clear_all`.
There is no `block_system_decrement_counter_slide` or `block_system_set_counter_slide`.

The contract says:

> extend block_system to expose a hit_points or counter field per block
> (likely already present given the slide parameter at level load)

`counter_slide` IS already stored per-block (confirmed at `src/block_system.c:48`
and loaded via `block_system_add(..., counter_slide, frame)` at line 427). It is
also exposed read-only via `block_system_get_render_info` (line 812). But there
is no setter. The contract's phrase "likely already present" understates the work:
the field exists but cannot be mutated through the public API.

The implementer cannot write the multi-hit decrement logic in
`gun_cb_on_block_hit` (`src/game_callbacks.c`) without either:
a) calling `block_system_get_render_info` to read `counter_slide`, then
   having no way to write the decremented value back, or
b) adding a new function `block_system_decrement_gun_hit(ctx, row, col)`
   or `block_system_set_counter_slide(ctx, row, col, value)` to
   `src/block_system.c` and `include/block_system.h`.

Both `src/block_system.c` and `include/block_system.h` are already in the
write-set. The API extension is in-scope. The contract must acknowledge
this explicitly so the implementer knows it is required, not optional.

**Revision:** Add to Gap 5 description:

```text
No public API exists to decrement counter_slide on an existing block.
Add `block_system_decrement_gun_hit(block_system_t *ctx, int row, int col)`
to src/block_system.c / include/block_system.h. It should:
  - if block_type is in the SHOTS_TO_KILL_SPECIAL set AND counter_slide > 0:
      decrement counter_slide; return 1 (bullet consumed, do not clear yet)
  - if counter_slide reaches 0: call block_system_clear internally; return 0
    (block cleared, no further action needed in gun_cb_on_block_hit)
  - HYPERSPACE_BLK / BLACK_BLK: return 1 immediately (absorbed, never cleared)
  - All other block types: return 0 (clear normally via block_system_clear)
This function encapsulates the "who decides to clear" logic inside block_system
rather than leaking block-type switch logic into the callback layer.
```

Add `tests/test_block_system.c` to the test expansion list if not already there
(it is already in the write-set — confirmed).

---

## Finding 4 — NON-BLOCKING: Gun cb_on_ball_hit needs game_callbacks_ball_env, not a new accessor

**Severity:** Non-blocking
**Affected field:** `context` Gap 7 description.

### Evidence

The contract says:

> build a `ball_system_env_t` and call
> `ball_system_change_mode(ctx->ball, &env, ball_index, BALL_POP)`

`game_callbacks_ball_env(ctx)` already exists at
`src/game_callbacks.c:398-415` and constructs a `ball_system_env_t` from
`ctx`. It is already called in `gun_cb_check_ball_hit`'s sibling callback
`gun_cb_on_block_hit` path. The implementer can call it directly — no new
accessor needed.

The contract implies "build a ball_system_env_t" as if it were novel work.
It isn't: `game_callbacks_ball_env(ctx)` is the existing factory. The wording
may lead jdc to write a second construction instead of calling the factory.

**Revision (informational):** Amend Gap 7 description to:

```text
Call `game_callbacks_ball_env(ctx)` (already exists in game_callbacks.c)
to build the env, then call ball_system_change_mode. Do not construct env inline.
```

No write-set or success-criteria change needed; the factory is available
in-file. Blocking only if the implementer writes a second inline construction
that misses a field (e.g., no_walls, killer).

---

## Finding 5 — NON-BLOCKING: Scope gap — bullet-vs-eyedude scoring not specified

**Severity:** Non-blocking
**Affected field:** `context` — hypotheses-missed section (not present in contract).

### Evidence

The contract handles `gun_cb_on_ball_hit` but does not specify what happens
when a bullet kills the eyedude. `gun_cb_on_eyedude_hit` is a stub at
`src/game_callbacks.c:336-339`. Per the research document's eyedude section,
the eyedude has a score callback (`eyedude_cb_on_score`). No research topic
covers what bullet-kills-eyedude should score or do.

`original/gun.c` was cited for ball-kill behavior (Topic 10). The eyedude
kill pathway (`original/gun.c check_eyedude_hit` → `on_eyedude_hit`) is out
of scope for this mission (bead 4.2 per the `gun_cb_check_eyedude_hit` stub
comment). This is correct — do not add it here. The review confirms the
omission is intentional, not accidental.

No revision needed. Confirming the boundary is correct.

---

## Finding 6 — NON-BLOCKING: Scope sizing — 13 gaps in 3 rounds is achievable

**Severity:** Non-blocking
**Affected field:** `budget.rounds`.

Three rounds at jdc's pace is realistic. Counting by implementation sites:

- `gun_cb_on_block_hit`: 4 new cases (Gaps 1, 2, 3+4 combined, 10+11+5 per-block logic)
- `ball_cb_on_block_hit`: 1 line removed (Gap 6)
- `gun_cb_on_ball_hit`: 3 lines added (Gap 7)
- `game_rules_next_level`: 1 line (Gap 8)
- `game_rules_ball_died`: 2 lines (Gap 9)
- `game_render_lives`: new helper ~15 lines (Gap 10)
- `game_input.c SDL2I_SHOOT`: 5 lines (Gap 11)
- `game_input.c` mouse fire: 5-10 lines (Gap 12, after Finding 2 resolution)
- `block_system.c` new function: ~20 lines (Gap 5 API, per Finding 3)
- Tests: ~60-80 new test assertions

Round 1 (implementation) → Round 2 (tests + make check) → Round 3 (address
reviewer findings) is a tight but plausible budget. The blocking findings in
this review (Findings 1, 2, 3) must be resolved in the contract before
spawning jdc, or they will surface as round-1 implementation ambiguities
that eat budget.

---

## Summary Table

| # | Severity | Field | Short description |
|---|----------|-------|-------------------|
| F1 | Blocking | context Gap 10 / success_criteria Gap 10 | Ammo belt X formula misstated; drawing order ambiguous; max-ammo boundary not tested |
| F2 | Blocking | context Gap 12 / write_set | Mouse edge-trigger not in input API; write-set must expand or option chosen explicitly |
| F3 | Blocking | context Gap 5 / block_system API | No public setter for counter_slide; must add `block_system_decrement_gun_hit` |
| F4 | Non-blocking | context Gap 7 | `game_callbacks_ball_env` is the right factory; doc it explicitly |
| F5 | Non-blocking | (hypotheses missed) | bullet-vs-eyedude scope boundary confirmed correct |
| F6 | Non-blocking | budget | 13 gaps / 3 rounds achievable; blocking findings must be resolved before spawning |
