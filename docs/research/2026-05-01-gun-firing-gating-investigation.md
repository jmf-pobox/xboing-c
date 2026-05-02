# Gun Firing Gating Investigation

**Date:** 2026-05-01
**Worker:** jdc
**Ticket:** xboing-jm8
**Mission:** m-2026-05-02-006

---

## 1. Observed Symptoms (from ticket xboing-jm8)

- **Direction-1:** K fires bullets without picking up an MGUN block.
- **Direction-2:** After picking up an MGUN block, K still does not fire.

---

## 2. Legacy Gun Firing Flow (original/)

### 2.1 Ammo initialization

`original/gun.c:168` — `InitialiseBullet()` calls `SetNumberBullets(4)`.

`original/file.c:117` — `LoadNextLevel()` calls
`SetNumberBullets(NUMBER_OF_BULLETS_NEW_LEVEL)`, where
`NUMBER_OF_BULLETS_NEW_LEVEL` is 4 (defined in
`original/include/blocks.h:74`).

`original/ball.c:1804-1805` — `ResetBallStart()` calls `AddABullet()`
twice, granting 2 bullets every ball death or creation.

Combined effect: at game start and on every level load the player has
4 ammo. On each ball death or respawn they gain 2 more. **Ammo is
never gated on MGUN pickup.**

### 2.2 shootBullet() guard (direction-1 verdict)

`original/gun.c:491-513` — `shootBullet()`:

```c
void shootBullet(Display *display, Window window)
{
    if ((GetNumberBullets() > 0) && (IsBallWaiting() == False))
    {
        if (ResetBulletStart(display, window) == True)
        {
            DeleteABullet(display);
            if (noSound == False)
                playSoundFile("shotgun", 50);
        }
    }
    else if (GetNumberBullets() == 0)
    {
        if (noSound == False)
            playSoundFile("click", 99);
    }
}
```

Guard is `numBullets > 0`, NOT `fastGun == True`. The player can
always fire as long as ammo is nonzero. **Firing without an MGUN
pickup is ORIGINAL behavior.** MGUN (fastGun) is only checked inside
`ResetBulletStart()` to decide single-fire vs dual-fire.

### 2.3 MGUN\_BLK ball hit — what the pickup actually does

`original/ball.c:831-845` — ball-block collision handler for
`MGUN_BLK`:

```c
case MGUN_BLK:
    ToggleFastGun(display, True);
    DrawSpecials(display);
    SetCurrentMessage(display, messWindow, "Machine Gun", True);
    DrawBlock(display, window, row, col, KILL_BLK);
    balls[i].lastPaddleHitFrame = frame + PADDLE_BALL_FRAME_TILT;
    if (Killer == True)
        return True;
    break;
```

`original/special.c:127-130` — `ToggleFastGun()` sets `fastGun` flag:

```c
void ToggleFastGun(Display *display, int state)
{
    fastGun = state;
    ...
}
```

`original/gun.c:618-631` — `ResetBulletStart()` reads `fastGun` to
dispatch single vs dual fire:

```c
static int ResetBulletStart(Display *display, Window window)
{
    if (fastGun == True)
    {
        (void)StartABullet(display, window, paddlePos - (size / 3));
        status = StartABullet(display, window, paddlePos + (size / 3));
    }
    else
        status = StartABullet(display, window, paddlePos);
    return status;
}
```

Summary of original behavior:

- No MGUN pickup: fires 1 bullet per shot from paddle center.
- With MGUN pickup: fires 2 bullets per shot (dual fire, +-paddle\_size/3).
- Both modes require `numBullets > 0` and `IsBallWaiting() == False`.

---

## 3. Modern Flow Trace (src/)

### 3.1 K-key to gun\_system\_shoot()

`src/game_input.c:297-302`:

```c
if (sdl2_input_just_pressed(ctx->input, SDL2I_SHOOT))
{
    gun_system_env_t genv = game_callbacks_gun_env(ctx);
    gun_system_shoot(ctx->gun, &genv);
}
```

`src/game_callbacks.c:381-390` — `game_callbacks_gun_env()`:

```c
gun_system_env_t game_callbacks_gun_env(const game_ctx_t *ctx)
{
    gun_system_env_t env = {
        .frame = ...,
        .paddle_pos = paddle_system_get_pos(ctx->paddle),
        .paddle_size = paddle_system_get_size(ctx->paddle),
        .fast_gun = special_system_is_active(ctx->special, SPECIAL_FAST_GUN),
    };
    return env;
}
```

`src/gun_system.c:298-352` — `gun_system_shoot()` guards:

- Guard 1: `is_ball_waiting` callback.
- Guard 2: `gun_system_get_ammo(ctx) > 0`.
- If `env->fast_gun`, dual fire; else single fire.
- On success: `gun_system_use_ammo()`.

### 3.2 Ammo at level start

`src/game_rules.c:222`:

```c
gun_system_set_ammo(ctx->gun, GUN_AMMO_PER_LEVEL);
```

`include/gun_system.h:34`: `GUN_AMMO_PER_LEVEL 4`.

`game_rules_next_level()` is called unconditionally at every level
transition (including level 1 start). So the player always starts
with 4 ammo. This matches `original/file.c:117`.

**Direction-1 verdict (modern):** Same as original. Player starts with
4 ammo and can fire K immediately. This is correct and intentional.
No bug exists here.

### 3.3 Modern MGUN\_BLK hit handler

`src/game_callbacks.c:133-137` — `ball_cb_on_block_hit()`:

```c
case MGUN_BLK:
    block_system_clear(ctx->block, row, col);
    special_system_set(ctx->special, SPECIAL_FAST_GUN, 1);
    gun_system_set_unlimited(ctx->gun, 1);
    return 0;
```

Two things happen:

1. `SPECIAL_FAST_GUN` is set to 1 in `special_system`.
2. `gun_system_set_unlimited()` is called with 1 (unlimited mode).

### 3.4 Direction-2: Why K does not fire after MGUN pickup

After MGUN pickup, `SPECIAL_FAST_GUN=1` and `ctx->gun->unlimited=1`.

`gun_system_shoot()` (`src/gun_system.c:314`):

```c
if (gun_system_get_ammo(ctx) > 0)
```

`gun_system_get_ammo()` (`src/gun_system.c:401-408`):

```c
int gun_system_get_ammo(const gun_system_t *ctx)
{
    if (ctx == NULL) return 0;
    return ctx->ammo;
}
```

`gun_system_set_unlimited()` sets `ctx->unlimited = 1` but does NOT
change `ctx->ammo`. `gun_system_shoot()` checks `ammo > 0`, not
`unlimited`. Therefore:

- If `ammo` has already been depleted to 0 before picking up MGUN,
  `gun_system_shoot()` hits the `else` branch (click sound) and never
  fires — even though `unlimited` is now on.
- If `ammo > 0`, it fires and `gun_system_use_ammo()` is called, which
  no-ops (due to `unlimited`), so ammo never decrements. That case works.

**Root cause of direction-2:** `gun_system_shoot()` gates on
`gun_system_get_ammo() > 0`, and `gun_system_set_unlimited()` does not
replenish `ammo`. When the player has fired all 4 starting rounds
before hitting an MGUN block, `ammo == 0` and unlimited mode is
silently ignored.

The fix must ensure that when unlimited mode is active, the ammo
check passes regardless of `ctx->ammo`.

---

## 4. Verdict Summary

| Symptom | Classification | Evidence |
|---------|---------------|----------|
| K fires without MGUN pickup | **Original behavior** | `original/gun.c:494` guards on `numBullets > 0`, not `fastGun`. Level start grants 4 bullets: `original/file.c:117`. |
| K does not fire WITH MGUN pickup | **Modern regression** | `gun_system_shoot()` in `src/gun_system.c:314` checks `ammo > 0` unconditionally, ignoring `ctx->unlimited`. |

---

## 5. Proposed Minimal Fix Scope

**File:** `src/gun_system.c`

**Function:** `gun_system_shoot()`

The fix is a one-line condition change. Replace:

```c
if (gun_system_get_ammo(ctx) > 0)
```

with:

```c
if (gun_system_get_ammo(ctx) > 0 || ctx->unlimited)
```

This makes `unlimited` mode actually bypass the ammo gate, matching
legacy behavior where `SetUnlimitedBullets(True)` + `SetNumberBullets(MAX_BULLETS + 1)` both coexist (`original/blocks.c:1590-1591`), and the `DecNumberBullets()` early-out in `original/gun.c:472-473`.

No other files need to change. The `gun_system_use_ammo()` no-op for
unlimited is already correct (`src/gun_system.c:389-391`).

Implement mission should target:

- File: `src/gun_system.c`
- Function: `gun_system_shoot()` (line 298)
- Change: condition at line 314 to also pass when `ctx->unlimited` is nonzero
- Tests: `tests/test_gun_system.c` — add cases for:

1. ammo=0, unlimited=0: shoot returns 0 (click).
2. ammo=0, unlimited=1: shoot returns 1 (fires).
3. ammo=4, unlimited=0: fires and decrements.
4. ammo=0, unlimited=1: fires and ammo stays 0.

---

## 6. Citations

| Claim | Source |
|-------|--------|
| Legacy fires without pickup (numBullets > 0 guard) | `original/gun.c:491-513` |
| Ball creation grants 2 ammo | `original/ball.c:1803-1805` |
| Level start grants 4 ammo | `original/file.c:117`, `original/include/blocks.h:74` |
| MGUN sets fastGun only (not gating) | `original/ball.c:831-845`, `original/special.c:127-130` |
| fastGun affects dual-fire path only | `original/gun.c:618-631` |
| Modern ammo-at-level-start | `src/game_rules.c:222`, `include/gun_system.h:34` |
| MGUN pickup sets unlimited | `src/game_callbacks.c:133-137` |
| shoot() checks ammo > 0, not unlimited | `src/gun_system.c:314` |
| unlimited does not set ammo | `src/gun_system.c:410-417` |
