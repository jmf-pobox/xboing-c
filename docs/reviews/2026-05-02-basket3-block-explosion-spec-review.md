# Spec Review: Basket 3 — Block Explosion Lifecycle

**Spec under review:** `.tmp/missions/basket3-block-explosion-implement.yaml`
**Ground truth:** `docs/research/2026-05-02-block-explosion.md`
**Reviewer:** gjm (Glenford J. Myers, test-expert)
**Date:** 2026-05-02

---

## Verdict: PASS WITH REVISIONS

Three blocking findings must be resolved before jdc starts. The rest
are non-blocking but actionable.

---

## Blocking Findings

**B-1 [original-source-alignment] Stage 4 is not a clear-only frame via
sprite_block_explode_key — it always returns a non-NULL key.**

The spec says stage 4 is a "clear-only frame (render path skips sprite
when slide==4 by selecting NONE)". The skip mechanism in
`src/game_render.c:109` is `if (!key) continue`. But
`sprite_block_explode_key` (`src/sprite_catalog.h:466-538`) has no
code path that returns NULL — the default case returns red explosion
sprites. So slide==4 will render a sprite, not clear. The spec's skip
mechanism is structurally wrong.

Additionally, `explode_slide` in the spec starts at 1 and the render
call at `src/game_render.c:94` passes it raw to
`sprite_block_explode_key(info.block_type, info.explode_slide)`. That
function uses `frame % 3` as the array index. With slide values 1, 2,
3, 4 the sequence is `k[1], k[2], k[0], k[1]` — not `k[0], k[1],
k[2]` as the original delivers (`original/blocks.c:1511-1526` passes
slide 0, 1, 2 to `ExplodeBlockType`).

Two defects in the render path need to be explicit in the spec:

1. The render call must offset by 1: `sprite_block_explode_key(type,
   explode_slide - 1)` so stages 1-3 map to frames 0-2.
2. Stage 4 (the `XClearArea` stage) must be handled by an explicit
   `if (info.explode_slide == 4)` branch in `game_render.c` that skips
   drawing, rather than relying on `sprite_block_explode_key` returning
   NULL. The spec must either add `src/game_render.c` to the write-set
   and specify this change, or describe how the NONE signal is produced.

**Fix:** Add `src/game_render.c` to the write-set. Specify that the
render path passes `explode_slide - 1` to `sprite_block_explode_key`
for stages 1-3, and that `explode_slide == 4` is a skip-draw branch
checked before the key lookup. Update the success criteria with a
verifiable statement about the sprite index mapping.

---

**B-2 [test-plan] The finalize callback receives saved hit_points, but
`clear_entry` zeroes `hit_points` before the callback fires — the spec
does not make this ordering invariant testable.**

The spec (step 2 under `block_system_update_explosions`) says: "Save
`block_type`, `hit_points` to local vars (clear_entry wipes them). ...
If `cb != NULL`, call `cb(..., saved_hit_points, ud)`." This is correct
mechanically — local variable copy before `clear_entry`.

However, test case `test_update_invokes_finalize_callback_with_saved_state`
is described as verifying "saved values match what was set at trigger
time even though clear_entry already ran." That test verifies the
callback receives the right values, but it cannot verify the ordering
guarantee (that the callback fires AFTER clear_entry). A test is needed
that confirms `block_system_is_occupied(ctx, row, col)` returns 0
inside the callback — i.e., the cell is already unoccupied when the
callback fires.

This matters because `game_callbacks_on_block_finalize` for BOMB_BLK
calls `block_system_explode` on neighbors from within the callback. If
the source cell is still occupied during that call, and the source is
its own neighbor after wrap-around (impossible in a 18x9 grid but a
concern for the reader), the guard could misbehave. More practically,
the spec text for step 1 of the callback says "Fires AFTER occupancy is
cleared" — that claim has no corresponding test case.

**Fix:** Add a 13th test case:
`test_finalize_callback_sees_cell_unoccupied`: place a stub callback
that calls `block_system_is_occupied(ctx, row, col)` on the source cell
and records the result; after `block_system_update_explosions`, assert
the stub saw occupied==0. Update the test count in success_criteria to
13.

---

**B-3 [test-plan] No test covers BOMB_BLK chain-into-HYPERSPACE_BLK
neighbor.**

The spec checks HYPERSPACE immunity in the trigger path
(`test_explode_returns_err_for_hyperspace`), but the BOMB chain fires
from within `game_callbacks_on_block_finalize`, not directly from
`block_system_explode`. The test plan has no case that:

1. Arms a BOMB_BLK with a HYPERSPACE_BLK as an 8-neighbor.
2. Runs the animation to finalize.
3. Verifies the HYPERSPACE_BLK cell remains occupied and unexploding.

Without this test, the HYPERSPACE immunity inside `block_system_explode`
is exercised in isolation only, not in the BOMB chain context. The
chain path in the finalize callback calls `block_system_explode` for
each of 8 neighbors — the HYPERSPACE guard fires inside that call.
There is no test at the integration point.

**Fix:** Add a 14th test:
`test_bomb_chain_skips_hyperspace_neighbor`. Arm a BOMB_BLK at (1,1)
and a HYPERSPACE_BLK at (1,2). Run explosions to finalize. Assert
`block_system_is_occupied(ctx, 1, 2)` is nonzero and
`block_system_get_type(ctx, 1, 2) == HYPERSPACE_BLK`. Update test count
to 14. Note: this test exercises the combined BOMB-chain + HYPERSPACE
path, not just `block_system_explode` in isolation.

---

## Non-Blocking Findings

**N-1 [original-source-alignment] Re-entry guard returns
BLOCK_ERR_INVALID_ARG where the original silently no-ops.**

The spec notes this divergence but calls it "harmless (caller ignores
it)". For the BOMB chain path, `game_callbacks_on_block_finalize` calls
`block_system_explode` for 8 neighbors, and two overlapping BOMB_BLK
explosions may try to re-arm an already-exploding neighbor. The error
return is indeed harmless if the caller does not log it as an error or
propagate it.

Recommendation: the spec should explicitly say the return value is
ignored by `game_callbacks_on_block_finalize` for BOMB chain calls.
As written, the finalize callback pseudocode has no error check on
`block_system_explode` calls — that matches. Confirm this is intentional
in the spec comment so jdc doesn't add error logging.

**Action:** Add one sentence to the step 4 BOMB_BLK pseudocode: "The
return value of `block_system_explode` is intentionally discarded in
the chain reaction context (matching original/blocks.c:1825 silent
skip)."

---

**N-2 [test-plan] Stage 1 fires at the same tick as trigger (F=0, not
F+10). The spec's test verifies progression starting at F=10 — this
is correct but could be clearer.**

The test `test_update_advances_one_stage_per_tick_at_delay` describes
"slide progression (1→2 at F=10, 2→3 at F=20, 3→4 at F=30, 4→5 at
F=40)". This implies stage 1 fires at F=0 and stage 2 at F=10. That
matches `original/blocks.c:1502` (`explodeNextFrame == frame` equality
check, NOT >=) and `original/blocks.c:1832` (`explodeNextFrame = frame`
at trigger).

However, the test description does not state that a call to
`block_system_update_explosions(ctx, F=0, ...)` immediately after
`block_system_explode(ctx, ..., F=0)` advances slide from 1 to 2.
If the test only verifies F=10 as the first transition, the F=0
same-tick stage-1 behavior is untested.

**Fix:** The test should call `block_system_update_explosions` at F=0
immediately after arming and assert that `explode_slide` is still 1
after F=0 (stage 1 renders at F=0 but slide increments to 2 — actually
verify what happens: the original increments slide AFTER firing stage
1, so at end of F=0 the slide is 2, next_frame is F=10). Clarify the
expected post-update value of `explode_slide` at F=0 in the test
description.

---

**N-3 [write-set] `src/game_render.c` is missing from the write-set.**

From B-1: the render path needs a change (stage-4 skip branch and
slide-1 offset). The current write-set does not include
`src/game_render.c`. This will cause jdc to discover the gap during
implementation and edit a file outside the declared write-set.

**Fix:** Add `src/game_render.c` to `write_set`.

---

**N-4 [API gaps] BULLET_BLK and MAXAMMO_BLK finalize handlers — modern
API exists in gun_system.h; spec's "look up" instruction is superfluous.**

The spec says: "If `gun_system_add_bullets` does not exist with that
signature, use the equivalent gun-system API (look up in
include/gun_system.h)."

The modern API at `include/gun_system.h` provides:

- `gun_system_add_ammo(ctx)` — adds one ammo, clamped at `GUN_MAX_AMMO`
- `gun_system_set_unlimited(ctx, 1)` — enables unlimited
- `gun_system_set_ammo(ctx, GUN_MAX_AMMO + 1)` — matches original
  `SetNumberBullets(MAX_BULLETS + 1)` sentinel

There is no `gun_system_add_bullets` — the spec's function name is
hypothetical. The correct implementation is `BLOCK_NUMBER_OF_BULLETS_NEW_LEVEL`
(=4) calls to `gun_system_add_ammo`. This exact pattern already exists
in `gun_cb_on_block_hit` at `src/game_callbacks.c:317-319` for bullet
hits. The finalize handler should follow the same pattern verbatim.

The spec should specify the exact API calls, not ask jdc to look them
up. Replace the vague instruction with: "Call `gun_system_add_ammo`
four times (matching `gun_cb_on_block_hit:317-319`)."

**Fix (non-blocking):** Tighten the BULLET_BLK finalize handler
description with the concrete function names.

---

**N-5 [API gaps] BONUS_BLK — no modern equivalent of IncNumberBonus /
ToggleKiller exists; spec's "file a bd follow-up" instruction conflicts
with the "no deferring obvious work" rule.**

The spec says: "If no modern equivalent exists, file a bd follow-up
bead and DOCUMENT the gap." `include/special_system.h` provides
`special_system_set(ctx, SPECIAL_KILLER, 1)` for the killer toggle.
There is no bonus counter (equivalent of `IncNumberBonus`) or
bonus count accessor in any header visible in the inputs.

The "no deferring obvious work" rule applies if the BONUS_BLK counter
logic can be implemented in <30 LOC. Adding a `bonus_count` field to
`game_ctx_t` and a threshold check is approximately 10 LOC. The spec
should prefer option (a): extend in this basket rather than defer.

However, this is the implementer's judgment call given what exists.
The spec correctly leaves this to jdc's discretion. The rating here is
non-blocking: the spec text is acceptable but should be more explicit
that option (a) is preferred per project rules.

**Fix:** Change "file a bd follow-up" to "prefer extending game_ctx_t
with a bonus_count field in this basket (<10 LOC); file a bd only if
the implementation reveals a dependency on a not-yet-built subsystem."

---

**N-6 [success-criteria] "matches original" phrasing in several
criteria is vague without a citation.**

Success criterion: "Score deferred from hit time (game_callbacks.c:76-83
removed) to finalize callback (added)." This is measurable — specific
line numbers cited, observable behavior (score changes when block
disappears not when ball hits).

Success criterion: "BOMB_BLK 8-neighbor chain replaced with 8
block_system_explode calls at frame + BLOCK_EXPLODE_DELAY (research
section 5 BOMB_BLK)." This is measurable — 8 specific calls, frame
arithmetic is explicit.

Both criteria are adequately cited. No vague "matches original" language
appears. This check: PASS.

---

**N-7 [test-plan] Frame-counter wraparound with negative `frame`
argument is not covered.**

`block_system_explode` and `block_system_update_explosions` accept
`int frame`. The original used unsigned frame counters; the modern
uses `(int)sdl2_state_frame(ctx->state)`. Negative frame values cannot
occur at game start (frame starts at 0), but an adversarial test with
frame=-1 should verify the equality guard `explode_next_frame == frame`
does not produce undefined behavior or spurious stage advances.

This is a low-priority gap (negative frames are not reachable in
normal play) but easy to add.

**Fix (optional):** Add a case
`test_explode_with_negative_frame_clamps_correctly` passing frame=-1
to `block_system_explode` and verifying the function returns
`BLOCK_ERR_INVALID_ARG` or, if accepted, that `block_system_update_explosions`
at frame=-1 does nothing and at frame=9 does nothing (still waiting
for explode_next_frame=-1). Useful for UBSan coverage.

---

## Issue-Specific Verdicts (Contract Checklist)

**1. Test plan completeness.**
Partially deficient. Missing: (a) BOMB chain into HYPERSPACE neighbor
[blocking, B-3]; (b) finalize callback sees cell unoccupied [blocking,
B-2]; (c) same-tick stage-1 behavior [non-blocking, N-2]; (d)
frame-wraparound edge case [optional, N-7].

**2. Success criteria measurability.**
Adequate. All criteria cite specific line numbers or observable state.
No vague "matches original" without a citation was found.

**3. Write-set adequacy.**
Deficient: `src/game_render.c` is missing. Required for stage-4 skip
fix [blocking, B-1].

**4. Hit-time / finalize-time split.**
Correct. Cross-checked against research section 5 and
`original/ball.c` call sites at lines 896, 915, 930, 943, 959, 973.
All physical state changes (DEATH, REVERSE, PAD_SHRINK, PAD_EXPAND,
STICKY, MULTIBALL, EXTRABALL, WALLOFF, MGUN, HYPERSPACE) remain at
hit time. Score, BOMB chain, BONUSX2/X4, TIMER, BULLET, MAXAMMO,
BONUS move to finalize. One note: BONUSX2_BLK at `src/game_callbacks.c:159`
sets only X2 without disabling X4 — the spec correctly identifies this
and fixes it in the finalize handler with mutual exclusion matching
`original/blocks.c:1619, 1628`. PASS.

**5. Re-entry guard semantics.**
Divergence from original is documented but implementation choice is
incomplete. BLOCK_ERR_INVALID_ARG on re-entry is louder than the
original silent no-op. Addressed in N-1 above: non-blocking, requires
clarifying comment in the spec pseudocode.

**6. Modern API gaps (BONUS_BLK / BULLET_BLK / MAXAMMO_BLK).**
BULLET_BLK and MAXAMMO_BLK: modern API exists and is precise. Spec
instruction is vague — fix per N-4. BONUS_BLK: no bonus counter
exists; spec correctly flags this. Preference should be to extend
in-basket per N-5. Overall: non-blocking with the clarifications noted.

**7. Stage-4 sprite key off-by-one.**
Confirmed blocking defect. `sprite_block_explode_key` uses 0-based
indexing; spec sets `explode_slide` starting at 1. Render path passes
`explode_slide` raw. Result: wrong sprite frame sequence (1→2→0→1
instead of 0→1→2). Stage-4 "skip" mechanism also broken (function
never returns NULL). Full analysis in B-1.

**8. Score timing observability.**
No existing test in `tests/test_block_system.c` asserts that score
increments at hit time (confirmed by reading the file list — the
current test suite covers block grid geometry and collision, not
`game_callbacks.c` scoring). No test will break. The 40-tick deferral
is visible to the player (score lag after block disappears).
The spec's eyeball acceptance criterion covers this. PASS — no regressions
to update, behavior change documented in the spec.

**9. BOMB chain frame arithmetic.**
The spec uses `frame + BLOCK_EXPLODE_DELAY` where `frame` is read from
`sdl2_state_frame(ctx->state)` at finalize time. At finalize: the
source block started at F_start and finalized at F_start+40. The
neighbor starts at F_start+40+10 = F_start+50. Original arithmetic:
source starts at frame F, neighbors start at F+EXPLODE_DELAY (=F+10,
not F+50). The research section 5 says "frame + EXPLODE_DELAY" where
`frame` is the argument to `SetBlockUpForExplosion` in the finalize
handler — which is called with the *current* game frame, not the
source start frame. So at finalize the current frame IS F_start+40.
Neighbor starts at F_start+40+10 = 10 ticks after source finalize.
This matches the original. PASS.

---

## Summary of Required Changes to the Spec

| Priority | Change |
|---|---|
| Blocking | Add `src/game_render.c` to write-set; specify stage-4 skip-draw branch and slide-1 offset correction in render path (B-1) |
| Blocking | Add test 13: `test_finalize_callback_sees_cell_unoccupied` (B-2) |
| Blocking | Add test 14: `test_bomb_chain_skips_hyperspace_neighbor` (B-3) |
| Non-blocking | Add "return value discarded intentionally" comment to BOMB chain pseudocode (N-1) |
| Non-blocking | Clarify that stage 1 fires at F=0 and test post-F=0 slide value (N-2) |
| Non-blocking | Replace vague gun API lookup with concrete `gun_system_add_ammo` x4 (N-4) |
| Non-blocking | Prefer in-basket bonus_count field over follow-up bead for BONUS_BLK (N-5) |
| Optional | Add frame=-1 UBSan edge case test (N-7) |
