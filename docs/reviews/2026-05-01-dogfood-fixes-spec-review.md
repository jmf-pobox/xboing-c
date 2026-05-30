# Vision-Keeper Review: dogfood-fixes-implement contract

**Reviewer:** jck (Justin C. Kibell)
**Mission:** m-2026-05-02-010 (worker: jck, evaluator: jmf-pobox)
**Spec:** `.tmp/missions/dogfood-fixes-implement.yaml`
**Date:** 2026-05-01

> Note: jck's role is read-only. The analytical work is jck's; the COO acted as scribe to file the artifact through the runtime. Same systemic gap as prior missions (xboing-c-xzt).

---

## Overall Verdict: APPROVE WITH ONE BLOCKING CONDITION (Fix 4)

Three of four fixes are clean. Fix 4 has a condition-direction error in the proposed guard that will reintroduce high-speed tunneling for one case. Worker must correct it before landing.

---

## Fix 1 — Gun direction-2 (gun_system_shoot unlimited guard)

**Verdict: APPROVED.**

The research correctly identifies the canonical behavior: `original/gun.c:491-513` guards on `numBullets > 0`, not on `fastGun`. The player can always fire as long as ammo is nonzero. Direction-1 (fires without MGUN pickup) is 1996 behavior, not a bug. The contract explicitly says "Direction-1 is canonical — NO fix needed." That is correct.

The proposed predicate change `if (gun_system_get_ammo(ctx) > 0 || ctx->unlimited)` is the minimal correct fix. The legacy equivalent is `original/blocks.c:1590-1591` where `SetUnlimitedBullets(True)` and `SetNumberBullets(MAX_BULLETS + 1)` coexist — the original kept ammo above zero when unlimited was on. The modern design separates the flag from the counter, making the explicit `|| ctx->unlimited` guard the correct translation.

Test matrix `(0,0)=no fire, (0,1)=fire, (4,0)=fire, (4,1)=fire` is complete and sufficient.

Game-feel: no player from 1995 would notice this fix. The failure mode was silent; the fix restores intended behavior after pickup.

---

## Fix 2 — Reverse persistence (paddle_system_set_reverse clear sites)

**Verdict: APPROVED. Test refinement required (non-blocking).**

Both canonical clear sites confirmed against original:

- `original/file.c:122` — `SetReverseOff()` inside `SetupStage()` (level-advance). Modern `game_rules_next_level` calls `paddle_system_reset` at line 215 with no reverse clear. GAP.
- `original/level.c:492` — `SetReverseOff()` inside `DeadBall()`, guarded by `GetAnActiveBall() == -1 && livesLeft > 0`. Modern `game_rules_ball_died` calls `ball_system_reset_start` at line 263 with no reverse clear. GAP.

Both proposed call insertions match the original's placement exactly. No new clear sites. Pure behavior restoration.

`TurnSpecialsOff()` correctly does not include reverse (`original/special.c:84-95`). Modern `special_system_turn_off` correctly excludes reverse. The fix goes in the callers, not in `special_system`. That understanding is correct.

**Test refinement (non-blocking):** the test must SET reverse_on = 1 before calling the transition function, then assert 0 after. Otherwise the test passes vacuously (default is already 0).

Game-feel: a 1995 player would immediately notice reverse persisting into the next level. This restores expected feel.

---

## Fix 3 — Skull collision (BALL_POP vs ClearBallNow)

**Verdict: APPROVED with acknowledged deviation. Test refinement required (non-blocking).**

Canonical behavior at `original/ball.c:847-861` calls `ClearBallNow(display, window, i)` — instant, synchronous, no animation, no pop. `ClearBallNow` (`original/ball.c:665-676`) erases the ball, zeroes the slot, calls `DeadBall`. No pop animation. The modern architecture has no synchronous equivalent; `ball_system_change_mode(BALL_POP)` is the closest available primitive.

**Deviation:** BALL_POP plays a brief expansion animation. The original DEATH_BLK death was instant. A 1995 player would notice: in the original, touching a skull produced an immediate vanish + block explosion. With BALL_POP, the ball has a brief pop animation before disappearing.

**My verdict:** accept this deviation as a modernization constraint. The alternative (a separate instant-clear path: `BALL_DIE` state or direct slot-free from the callback) would be cleaner but is a larger change. The functional requirement (ball dies, lives decrement, `BALL_EVT_DIED` fires) is met by BALL_POP. The visual difference is minor and the current behavior (ball is immortal after skull hit) is far worse than a brief pop.

Worker must remove `(void)ball_index;` at line 68 and build a `ball_system_env_t env = game_callbacks_ball_env(ctx)` before the switch (or lazily before DEATH_BLK) to pass to `ball_system_change_mode`. The function signature at `src/ball_system.c:215-216` confirms `ball_system_change_mode(ctx->ball, &env, ball_index, BALL_POP)`. No new API needed.

HYPERSPACE_BLK exclusion: correct. Whether hyperspace kills or teleports is a separate design call I will make.

**Test refinement (non-blocking):** the test must also assert (a) `block_system_is_occupied(block_ctx, row, col)` returns false after the hit, and (b) the ball at the CORRECT `ball_index` is transitioned (not a hard-coded index — use a non-zero ball_index in the test to verify).

---

## Fix 4 — Ball tunneling (adjacency-filter ball-position guard)

**Verdict: BLOCKING — guard direction is INVERTED.**

The research correctly identifies the root cause: when a ball is in the gap between two adjacent occupied blocks, the adjacency filter suppresses both: BLOCK_REGION_BOTTOM on the upper block, BLOCK_REGION_TOP on the lower block. Ball tunnels through both.

The fix intent is correct: suppress only when the ball is genuinely inside the neighbor's territory (phantom continuation), not when in the gap.

**The proposed guard direction is backwards.** Worked example:

For the reproducer (ball at y=192 with block 6 at bp->y=198):

- Ball is in the gap (y=187..197 between block 5 bottom y=186 and block 6 top y=198)
- Cross-product returns BLOCK_REGION_TOP (ball above block, hits top face)
- Adjacency check: row 5 above is occupied → suppress

The contract's proposed guard: `if (by < bp->y) return BLOCK_REGION_NONE` (suppress).
With ball at y=192 < bp->y=198: condition is true → SUPPRESS. **This is the case we WANT to fire.** The contract's guard suppresses the gap hit (wrong behavior).

The phantom case is when the ball has moved THROUGH the upper neighbor and is now inside the lower block (`by >= bp->y`). That is the continuation phantom we want to suppress.

**Correct guard direction:**

| Region | Phantom case (suppress) | Gap case (do NOT suppress) |
|--------|-------------------------|----------------------------|
| BLOCK_REGION_TOP | `by >= bp->y` (ball inside block from above) | `by < bp->y` (ball in gap above block) |
| BLOCK_REGION_BOTTOM | `by <= bp->y + bp->height` (ball inside from below) | `by > bp->y + bp->height` (ball in gap below) |
| BLOCK_REGION_LEFT | `bx >= bp->x` | `bx < bp->x` |
| BLOCK_REGION_RIGHT | `bx <= bp->x + bp->width` | `bx > bp->x + bp->width` |

**Verbatim revision text** (replace the contract's BLOCK_REGION_TOP/BOTTOM bullets):

```yaml
  - BLOCK_REGION_TOP: suppress only when `by >= bp->y` (ball at or
    inside the block's top edge — phantom continuation from upper
    neighbor). Do NOT suppress when `by < bp->y` (ball above block
    top, in the gap — genuine hit).
  - BLOCK_REGION_BOTTOM: suppress only when `by <= bp->y + bp->height`
    (ball at or inside block bottom). Do NOT suppress when ball below.
  - BLOCK_REGION_LEFT: suppress only when `bx >= bp->x`.
  - BLOCK_REGION_RIGHT: suppress only when `bx <= bp->x + bp->width`.
```

**High-speed regression check:** the ball-position guard does not reintroduce tunneling. At max ball speed (`MAX_X_VEL=MAX_Y_VEL=14`, block height=20), a ball still takes at least one ray-march step inside a 20px block. The guard only changes which suppression fires; it does not affect step count or AABB hit detection. Speed normalization (`ball_math_normalize_speed`) prevents the ball from exceeding block dimensions in one tick. Safe at canonical speeds.

The deterministic reproducer (blocks at (5,4) and (6,4), ball at x=247, y=192, dx=1, dy=-5) is well-specified and will catch this regression.

**Test refinement (non-blocking):** assert that at least one of the two blocks took a hit (`hit_points` decremented or `block_system_is_occupied` returns false), NOT just that dy changed sign. A block can be hit without dy reversing in every reproducer tick.

---

## Summary

| Fix | Verdict | Issue |
|-----|---------|-------|
| 1 — Gun unlimited | APPROVED | None |
| 2 — Reverse clear | APPROVED | Tests must pre-set reverse=1 (non-blocking) |
| 3 — Skull / BALL_POP | APPROVED (deviation acknowledged) | Tests need block-clear + correct ball_index assertions (non-blocking) |
| 4 — Tunneling guard | **BLOCKING** | Guard direction inverted; must be `by >= bp->y` for TOP, etc. |

Worker must correct Fix 4's guard direction before landing. All other fixes are clear to implement as specified, with the test refinements applied.
