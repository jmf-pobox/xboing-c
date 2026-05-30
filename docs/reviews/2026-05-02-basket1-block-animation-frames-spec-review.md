# Spec Review: Basket 1 — Block Animation Frame Selection

**Mission:** m-2026-05-02-020 (reviewer: sjl, evaluator: jdc)
**Date:** 2026-05-02
**Contract under review:** `.tmp/missions/basket1-block-animation-frames.yaml`
**Verdict: ACCEPT WITH CHANGES**

Two non-blocking findings require verbatim text changes before jdc starts.
No blocking findings. The contract is structurally sound: scope is well-bounded,
write-set is correct, and the 4-bead / 2-round budget is realistic.

---

## Finding 1 — Sprite key constants: ALL PRESENT (no gap)

**Severity:** Informational (no action required)
**Contract field:** context, lines 29, 31-38

The contract states at line 29: "src/sprite_catalog.h:350-355 returns frame-1 keys only."
It then says the worker must add frame-2/3/4 sprite keys *if not yet defined*.

**Verified from `src/sprite_catalog.h`:**

- `SPR_BLOCK_X2_1` through `SPR_BLOCK_X2_4` — defined at lines 88-91
- `SPR_BLOCK_X4_1` through `SPR_BLOCK_X4_4` — defined at lines 92-95
- `SPR_BLOCK_BONUS_1` through `SPR_BLOCK_BONUS_4` — defined at lines 96-99
- `SPR_BLOCK_DEATH_1` through `SPR_BLOCK_DEATH_5` — defined at lines 69-73
- `SPR_BLOCK_EXTRABALL` and `SPR_BLOCK_EXTRABALL_2` — defined at lines 84-85
- `SPR_BLOCK_ROAMER`, `_ROAMER_L`, `_ROAMER_R`, `_ROAMER_U`, `_ROAMER_D` — defined at lines 102-106

Every sprite key the contract references already exists. The worker does not
need to add any `#define` entries. The "ADD them" instruction in the contract
context (line 47) is dead weight — it will not trigger and can be removed to
avoid false alarm during round-1 review.

**There is no sprite_catalog.c.** The entire catalog is inline functions in
`src/sprite_catalog.h`. The contract's reference to `src/sprite_catalog.c` in
both `inputs.references` (line 12 of YAML) and `write_set` (line 87) is
stale. `sprite_block_animated_key` should be added as an inline static function
in `src/sprite_catalog.h`, same pattern as `sprite_guide_key`,
`sprite_ball_key`, `sprite_counter_slide_key` etc.

**Verbatim revision — context block, replace lines 47-52:**

```text
OLD:
  For BONUSX2/X4/BONUS frame-2/3/4 sprite keys: if not yet defined in
  `sprite_catalog.h`, ADD them. If the underlying PNG assets are
  missing from `bitmaps/blocks/`, file a follow-up bead and skip
  those types in this basket (we'll come back when assets land). The
  implement worker should verify with `ls bitmaps/blocks/ | grep -i bonus`
  early.

NEW:
  All BONUSX2/X4/BONUS, DEATH, EXTRABALL, and ROAMER sprite key constants
  (frames 2-4 for bonus types, frames 1-5 for DEATH, frame 2 for EXTRABALL,
  L/R/U/D for ROAMER) are already defined in `src/sprite_catalog.h` (lines
  84-106). The worker does not add new #define entries. Verify PNG assets
  exist under `assets/images/blocks/` (not `bitmaps/blocks/`) before
  coding — see Finding 2 of the sjl spec review.
```

**Verbatim revision — write_set, remove stale entry:**

```text
OLD:
  - src/sprite_catalog.c

NEW:
  (remove this line — the file does not exist; all sprite catalog code
  lives in src/sprite_catalog.h as inline functions)
```

**Verbatim revision — inputs.references, remove stale entry:**

```text
OLD:
  - src/sprite_catalog.c

NEW:
  (remove this line)
```

---

## Finding 2 — Asset path: `assets/images/blocks/`, not `bitmaps/blocks/`

**Severity:** Non-blocking (must correct before round 1 to avoid worker confusion)
**Contract field:** context, line 52 (`ls bitmaps/blocks/ | grep -i bonus`)

The contract directs the worker to check `bitmaps/blocks/`. That directory
does not exist. PNG assets live under `assets/images/blocks/`.

**Assets confirmed present** (verified by find on `assets/images/blocks/`):

| Contract key | File | Present? |
|---|---|---|
| `blocks/x2bonus1` | `assets/images/blocks/x2bonus1.png` | YES |
| `blocks/x2bonus2` | `assets/images/blocks/x2bonus2.png` | YES |
| `blocks/x2bonus3` | `assets/images/blocks/x2bonus3.png` | YES |
| `blocks/x2bonus4` | `assets/images/blocks/x2bonus4.png` | YES |
| `blocks/x4bonus1` | `assets/images/blocks/x4bonus1.png` | YES |
| `blocks/x4bonus2` | `assets/images/blocks/x4bonus2.png` | YES |
| `blocks/x4bonus3` | `assets/images/blocks/x4bonus3.png` | YES |
| `blocks/x4bonus4` | `assets/images/blocks/x4bonus4.png` | YES |
| `blocks/bonus1`   | `assets/images/blocks/bonus1.png`   | YES |
| `blocks/bonus2`   | `assets/images/blocks/bonus2.png`   | YES |
| `blocks/bonus3`   | `assets/images/blocks/bonus3.png`   | YES |
| `blocks/bonus4`   | `assets/images/blocks/bonus4.png`   | YES |
| `blocks/death1..5` | `assets/images/blocks/death1-5.png` | YES |
| `blocks/xtrabal` / `xtrabal2` | present | YES |
| `blocks/roamer`, `roamerL/R/U/D` | present | YES |

All assets are present. The "follow-up bead if assets missing" carve-out will
not fire. The scope is not reduced.

**Verbatim revision — context, line 52:**

```text
OLD:
  implement worker should verify with `ls bitmaps/blocks/ | grep -i bonus`
  early.

NEW:
  implement worker should verify with `find assets/images/blocks -name "*bonus*"`
  early. Note: assets live under `assets/images/blocks/`, not `bitmaps/blocks/`.
```

---

## Finding 3 — API design: add sibling, do not extend existing `sprite_block_key`

**Severity:** Non-blocking (recommendation to tighten the contract's "or" language)
**Contract field:** context line 45, success_criteria line 92

The contract offers two options: "extend sprite_block_key (or add a sibling
helper)." This ambiguity will cause jdc to re-derive the decision at round 1.
Pre-decide now.

Extending `sprite_block_key` to accept a second `slide` parameter breaks
every existing call site (there are at least 3: game_render_blocks at line 100,
plus any tests that call it with one argument). The existing function is a
lookup-by-type helper with a clean single-parameter interface — all existing
callers pass only `block_type`.

The correct pattern is a sibling function, consistent with how the catalog
already handles counter blocks (`sprite_counter_slide_key`) and ball birth
(`sprite_ball_birth_key`). Name: `sprite_block_animated_key(int block_type,
int slide)`. Returns `NULL` for non-animated types (same contract as
`sprite_block_key` for unknown types), falls back to frame-1 key for animated
types when slide is out of range.

`game_render_blocks` at `src/game_render.c:98-100` would then change from:

```c
/* Normal static block sprite */
key = sprite_block_key(info.block_type);
```

to:

```c
/* Animated block sprite if block type supports it, else static */
key = sprite_block_animated_key(info.block_type, info.bonus_slide);
if (!key)
    key = sprite_block_key(info.block_type);
```

This preserves the existing `sprite_block_key` contract for all non-animated
callers and keeps the call site at `src/game_render.c:100` readable.

**Verbatim revision — context, line 45:**

```text
OLD:
  Implementation approach: extend `sprite_block_key` (or add a sibling
  helper `sprite_block_key_animated(block_type, bonus_slide)`) that

NEW:
  Implementation approach: add a sibling helper
  `sprite_block_animated_key(int block_type, int slide)` to
  `src/sprite_catalog.h` as a static inline function (same pattern as
  `sprite_counter_slide_key`). Do NOT modify the existing `sprite_block_key`
  signature — it has call sites that must not change. The new helper
```

**Verbatim revision — success_criteria, line 92:**

```text
OLD:
  sprite_block_key_animated (or extended sprite_block_key) returns the correct
  frame key for each (block_type, bonus_slide) combination

NEW:
  sprite_block_animated_key(int block_type, int slide) in src/sprite_catalog.h
  returns the correct frame key for each (block_type, slide) combination.
  sprite_block_key signature is unchanged; existing call sites compile without
  modification.
```

---

## Finding 4 — Verifiability: missing edge cases

**Severity:** Non-blocking (add to success_criteria or test notes)
**Contract field:** verification block (lines 74-79), success_criteria

The pure-logic test coverage in the contract covers the happy path per type
but misses three edge cases that the existing catalog functions all handle
explicitly:

1. **Out-of-range slide**: `sprite_ball_key` uses `slide % 4`;
   `sprite_ball_birth_key` returns `SPR_BALL_1` for out-of-range. The new
   function must define its behavior for `slide < 0` or `slide >= frame_count`.
   Recommend: modulo for cycling types (BONUSX2/X4/BONUS/EXTRABALL), clamp for
   directional types (ROAMER), clamp for counted types (DEATH). Tests must
   assert the exact out-of-range behavior, not just leave it undefined.

2. **Non-animated block type passed to animated helper**: calling
   `sprite_block_animated_key(RED_BLK, 0)` should return `NULL` (not crash,
   not silently return frame-1 key). The contract does not specify this.
   `game_render_blocks` at line 99 uses the pattern `if (!key) continue;`,
   so `NULL` is the correct sentinel. Test must assert `NULL` for a
   non-animated type.

3. **NONE_BLK / KILL_BLK / unknown types**: the existing `sprite_block_key`
   returns `NULL` for these. The animated sibling must do the same. Add one
   test case each.

**Verbatim addition — success_criteria, insert after line 98:**

```text
  - sprite_block_animated_key(RED_BLK, 0) returns NULL (non-animated type)
  - sprite_block_animated_key(NONE_BLK, 0) returns NULL
  - sprite_block_animated_key(BONUSX2_BLK, -1) and (BONUSX2_BLK, 99) do not
    crash and return a defined key (modulo behavior — tests assert which key)
  - sprite_block_animated_key(ROAMER_BLK, 5) returns SPR_BLOCK_ROAMER (clamp
    to index 0 for out-of-range directional slide — matches original rand() % 5
    which never exceeds 4)
```

---

## Finding 5 — Scope sizing: 4 beads / 2 rounds is realistic

**Severity:** Informational
**Contract field:** budget (rounds: 2)

All sprite key constants exist (`src/sprite_catalog.h` lines 84-106).
All PNG assets exist under `assets/images/blocks/`. No asset conversion
work is needed. The implementation is:

1. Add `sprite_block_animated_key` static inline to `src/sprite_catalog.h`
   (a switch statement over 6 block types, ~50 lines)
2. Update `game_render_blocks` at `src/game_render.c:98-100` (3-line change)
3. Add `tests/test_sprite_catalog.c` with ~30 test cases
4. Add `tests/test_game_render.c` if it does not exist (stubs render calls,
   verifies key selection logic — may be harder if SDL2 is not mockable)
5. Wire new test file into `tests/CMakeLists.txt`

Round 1: items 1-3, make check passes. Round 2: address review findings.
Two rounds is appropriate. No split needed.

Note: `tests/test_game_render.c` does not exist yet. If `game_render_blocks`
mixes SDL2 calls too tightly to unit-test the key-selection path without
a renderer, jdc should extract the key-selection logic into a testable pure
function rather than trying to mock SDL2. The success criteria at line 93
("game_render_blocks at src/game_render.c:100 passes info.bonus_slide")
can be verified by code review of the call site rather than a unit test
if SDL2 mocking proves impractical.

---

## Summary of Required Revisions

| # | Severity | Field | Change |
|---|---|---|---|
| 1a | Non-blocking | context lines 47-52 | Remove "ADD them if not defined" + correct asset path |
| 1b | Non-blocking | write_set | Remove `src/sprite_catalog.c` (does not exist) |
| 1c | Non-blocking | inputs.references | Remove `src/sprite_catalog.c` |
| 2 | Non-blocking | context line 52 | Correct path `bitmaps/` to `assets/images/` |
| 3a | Non-blocking | context line 45 | Pre-decide: sibling function, not signature extension |
| 3b | Non-blocking | success_criteria line 92 | Name the function, assert no existing call sites break |
| 4 | Non-blocking | success_criteria | Add 4 edge-case assertions |

No blocking findings. All assets present. All sprite keys present. The
"follow-up bead if assets missing" carve-out does not fire. All 4 beads
(xboing-c-ejn, qe2, ax9, agi) proceed in a single basket.
