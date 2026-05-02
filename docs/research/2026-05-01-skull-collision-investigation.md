# Skull (Death Block) Collision Bug Investigation

**Mission:** m-2026-05-02-008
**Worker:** jdc (John D. Carmack)
**Date:** 2026-05-01

---

## Summary

The modern `DEATH_BLK` case in `ball_cb_on_block_hit` clears the block and
returns 1.  Returning 1 causes `ball_system.c` to `return` immediately from
`update_a_ball` — but that `return` exits the physics step without ever
changing the ball's state.  The ball continues alive.  `ball_system_change_mode`
is never called with `BALL_POP`, so no pop animation runs and no `BALL_EVT_DIED`
event is emitted.

The dogfood symptom (ball touches skull block twice, nothing happens) matches
this exactly.

---

## 1. Which Levels Contain Skull Blocks

Level 01 has **zero** `D` characters (verified: `grep -c "D" levels/level01.data`
returns 0).  The maintainer's dogfood observation was not on level 1.

Levels that contain skull blocks (the 'D' character appears in a grid row line):

| Level file | Example row(s) |
|---|---|
| `levels/level17.data` | `DwwwtwwwD`, `DDwwpwwDD`, etc. — skull border |
| `levels/level34.data` | `dwDwwwDwd`, `?gwDwDwg?`, etc. — skull grid |
| `levels/level75.data` | `wXwwDwwXw` — row 8 |
| `levels/level77.data` | `DDDDDDDDD`, `DwDwDwDwD` — heavy skull level |

Character-to-block-type parsing: `src/level_system.c:127-130`.

---

## 2. Legacy DEATH\_BLK Collision Path

Source: `original/ball.c`.

### 2a. Collision Detection Calls HandleTheBlocks

`original/ball.c:1244`:

```c
if (HandleTheBlocks(display, window, row, col, i) == True)
    return;
```

If `HandleTheBlocks` returns `True`, `UpdateABall` returns immediately (no
bounce applied).  If it returns `False`, bounce math executes below the call
(`original/ball.c:1247-1308`).

### 2b. HandleTheBlocks — DEATH\_BLK Case

`original/ball.c:847-861`:

```c
case DEATH_BLK:
    /* Kill the ball now */
    ClearBallNow(display, window, i);

    /* Not a wall so explode the block */
    DrawBlock(display, window, row, col, KILL_BLK);
    balls[i].lastPaddleHitFrame = frame + PADDLE_BALL_FRAME_TILT;

    /* If in killer mode then don't bounce off block */
    if (Killer == True)
        return True;

    break;
```

Key observations:

1. `ClearBallNow` is called **immediately**, before the `break`.  This
   terminates the ball synchronously (erase → `ClearBall` → `DeadBall`).
2. The `return True` is only reached when `Killer == True`.  In the normal
   (non-killer) case, the code **breaks**, and `HandleTheBlocks` returns
   `False`.
3. Because `HandleTheBlocks` returns `False`, `UpdateABall` does NOT return
   early — it proceeds to apply bounce math to a ball that has already been
   cleared.  This is intentional: in the original, the ball was cleared by
   side-effect before the function returned.

### 2c. ClearBallNow

`original/ball.c:665-676`:

```c
void ClearBallNow(Display *display, Window window, int i)
{
    EraseTheBall(display, window, balls[i].oldx, balls[i].oldy);
    ClearBall(i);
    DeadBall(display, window);
}
```

`ClearBall` zeros the ball slot (`active=0`, resets all fields).
`DeadBall` decrements lives, handles game-over.  Both are synchronous.

The legacy death is a direct state mutation inside the physics loop, not a
callback or event.

---

## 3. Modern Equivalent — Traced Path

### 3a. Ball Physics Loop — on\_block\_hit Return Value Consumption

`src/ball_system.c:659-664`:

```c
if (ctx->callbacks.on_block_hit(row, col, i, ctx->user_data) != 0)
{
    /* Callback says ball should NOT bounce (killer, teleport, etc.) */
    return;
}
```

When `on_block_hit` returns nonzero, `update_a_ball` returns immediately.
This skips bounce math.  It does **not** change ball state and does **not**
emit any event.

### 3b. ball\_cb\_on\_block\_hit — DEATH\_BLK Case

`src/game_callbacks.c:89-91`:

```c
case DEATH_BLK:
    block_system_clear(ctx->block, row, col);
    return 1; /* Kill the ball */
```

`ball_index` is cast to void at `src/game_callbacks.c:68`:

```c
(void)ball_index;
```

So the ball index is permanently discarded.  No `ball_system_change_mode` call
is made.  The ball's `ballState` remains `BALL_ACTIVE`.

### 3c. What Return 1 Actually Does

`update_a_ball` returns.  On the next `ball_system_update` tick, the ball with
`ballState == BALL_ACTIVE` is processed again at `src/ball_system.c:374-378`:

```c
case BALL_ACTIVE:
    if ((env->frame % BALL_FRAME_RATE) == 0)
    {
        update_a_ball(ctx, env, i);
    }
    break;
```

The ball continues physics updates as if nothing happened.  No death animation.
No `BALL_EVT_DIED` event.  No life decrement.

---

## 4. The Gap

| | Legacy (`original/ball.c`) | Modern (`src/`) |
|---|---|---|
| Ball death trigger | `ClearBallNow()` called synchronously at `ball.c:851` | Not called — `ball_index` voided at `game_callbacks.c:68` |
| Block cleared | Yes (`DrawBlock` + `ClearBall`) | Yes (`block_system_clear`) |
| Ball state after | `active=0`, ball erased | `ballState == BALL_ACTIVE`, ball lives on |
| Lives decremented | Yes (via `DeadBall`) | No |
| Bounce applied | Yes — legacy breaks, not returns True; bounce runs on a now-cleared ball (harmless side-effect) | Return 1 prevents bounce — correct — but ball is not dead |
| Pop animation | Not used in legacy for DEATH\_BLK (ClearBallNow is instant) | Not triggered |

The root gap: `ball_index` is discarded at `src/game_callbacks.c:68`, making it
impossible to call `ball_system_change_mode(ctx->ball, env, ball_index, BALL_POP)`.
The callback signature provides `ball_index` precisely for cases like this, but
the implementation ignores it.

---

## 5. Verdict: Is `return 1` Sufficient to Kill the Ball?

No.  Returning 1 from `on_block_hit` prevents the bounce in the current tick.
It does not change `ballState`.  On the next tick the ball is processed as
`BALL_ACTIVE` again.  The ball is functionally immortal after touching a skull
block.

Killing the ball requires an explicit `ball_system_change_mode` call with
`BALL_POP` (or transitioning directly to `BALL_DIE`).  The legacy code used
`BALL_POP` only via `KillBallNow` (`original/ball.c:684`) — but for `DEATH_BLK`
it used `ClearBallNow` (instant clear, no animation).  The modern equivalent of
`ClearBallNow` is `ball_system_change_mode(ctx->ball, env, ball_index, BALL_POP)`
followed by allowing the pop animation to emit `BALL_EVT_DIED`.

The original did not play a pop animation for death blocks — `ClearBallNow` is
instant.  Transitioning to `BALL_POP` is a minor visual deviation acceptable
given the modern architecture.  A separate instant-clear path is also viable.
Either achieves the functional goal of killing the ball.

---

## 6. Proposed Minimal Fix Scope

**File:** `src/game_callbacks.c`

**Function:** `ball_cb_on_block_hit` (lines 66-167)

**Nature of change:**

1. Remove `(void)ball_index;` at line 68.  The parameter must be live.
2. In the `DEATH_BLK` case: before returning 1, call
   `ball_system_change_mode(ctx->ball, &env, ball_index, BALL_POP)`.  This
   requires building `ball_system_env_t env = game_callbacks_ball_env(ctx)` at
   the top of the function (or lazily before the call).
3. `ball_system_change_mode` is declared in `include/ball_system.h` and
   implemented at `src/ball_system.c:215-240`.  No new API is needed.

The `HYPERSPACE_BLK` case also returns 1 without acting on `ball_index`.
Whether hyperspace should kill or teleport the ball is a design question for
vision-keeper (jck), not part of this investigation.

No changes needed in `src/ball_system.c` or `include/ball_system.h`.

---

## 7. Level File Parsing Path Confirmation

'D' to `DEATH_BLK` mapping: `src/level_system.c:127-130`.

Level files use 9-character grid rows (lines 3-17 of each `.data` file).  The
parser reads them character by character via `level_system_char_to_block()`.
`LEVEL_SHOTS_TO_KILL` is set as the block's `slide` field at parse time
(`src/level_system.c:129`), which controls counter behavior — irrelevant to the
collision dispatch bug.

---

## References

| Symbol | Location |
|---|---|
| Legacy `DEATH_BLK` handling | `original/ball.c:847-861` |
| Legacy `ClearBallNow` | `original/ball.c:665-676` |
| Legacy collision dispatch | `original/ball.c:1241-1245` |
| Modern `on_block_hit` callback | `src/game_callbacks.c:66-167` |
| `ball_index` voided | `src/game_callbacks.c:68` |
| `DEATH_BLK` case (modern) | `src/game_callbacks.c:89-91` |
| Return value consumption | `src/ball_system.c:659-664` |
| `ball_system_change_mode` | `src/ball_system.c:215-240` |
| BALL\_POP setup | `src/ball_system.c:229-237` |
| 'D' to DEATH\_BLK mapping | `src/level_system.c:127-130` |
