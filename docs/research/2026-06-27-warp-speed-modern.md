# Warp Speed Modern Analysis

**Bead:** xboing-c-tks  
**Date:** 2026-06-27  
**Scope:** Modern port only. No code changes.

---

## 1. Code Path

### 1a. Key handler (attract-only)

`src/game_input.c:158-173` — `game_input_global()`, `is_attract` block.
Keys 1-9 map to `SDL2I_SPEED_1..SDL2I_SPEED_9` actions. On press:

```c
sdl2_loop_set_speed(ctx->loop, s)   // src/game_input.c:166
```

**Critical:** speed keys are gated behind `is_attract`
(`src/game_input.c:136-138, 159`). `SDL2ST_GAME` is NOT in `is_attract`.
Speed keys are silently ignored during gameplay. This matches
`original/main.c:685` where `handleSpeedKeys` is called from
`handleIntroMode` only.

### 1b. Loop tick-interval computation

`src/sdl2_loop.c:45-48`:

```c
static uint64_t compute_tick_interval(int speed_level)
{
    return (uint64_t)SDL2L_TICK_UNIT_US * (uint64_t)(10 - speed_level);
}
```

Constants: `include/sdl2_loop.h:35-39`

```c
#define SDL2L_DEFAULT_SPEED 5
#define SDL2L_TICK_UNIT_US  1500
```

### 1c. Ball velocity normalization

`src/ball_math.c:153-193` — `ball_math_normalize_speed()`, called per-tick
per-active-ball from `src/ball_system.c:588-589`.

Speed level plumbed via: `src/game_callbacks.c:575`:

```c
.speed_level = sdl2_loop_get_speed(ctx->loop),
```

into `ball_system_env_t.speed_level` (`include/ball_system.h:63`).

### 1d. Velocity constants

`include/ball_types.h:28-31`:

```c
#define MAX_X_VEL   14
#define MAX_Y_VEL   14
#define MIN_DY_BALL  2
#define MIN_DX_BALL  2
```

---

## 2. Frame Clock Per Speed Level

Formula (`src/sdl2_loop.c:47`, `include/sdl2_loop.h:17-21`):

```text
tick_interval_us = 1500 * (10 - speed_level)
```

| Speed | tick_us | ticks/sec |
|-------|---------|-----------|
| 1     | 13500   | 74        |
| 2     | 12000   | 83        |
| 3     | 10500   | 95        |
| 4     | 9000    | 111       |
| 5     | 7500    | 133       |
| 6     | 6000    | 167       |
| 7     | 4500    | 222       |
| 8     | 3000    | 333       |
| 9     | 1500    | 667       |

Direction: speed 1 = slowest (9× longer tick than speed 9), speed 9 =
fastest. This matches the original: higher warp = faster.

---

## 3. Ball Velocity Per Speed Level

`src/ball_math.c:166-169`:

```c
alpha = sqrt(MAX_X_VEL^2 + MAX_Y_VEL^2);  // sqrt(14^2 + 14^2) = sqrt(392) = 19.799
alpha /= 9.0f;                              // per speed level unit = 2.200
alpha *= (float)speed_level;               // target vector magnitude
```

Result: `alpha = 2.200 * speed_level` (px/tick, as a vector magnitude).

The function then scales the current (dx, dy) vector to this magnitude
(`src/ball_math.c:173-186`). Clamps applied post-scale:

- `*dy == 0` → set to `MIN_DY_BALL = 2` (`src/ball_math.c:188-190`)
- `*dx == 0` → set to `MIN_DX_BALL = 2` (`src/ball_math.c:191-192`)

No explicit MAX_X_VEL/MAX_Y_VEL clamp after normalization. At speed 9,
alpha = 19.80, a 45-degree vector gives dx=dy=14 (exactly MAX), so the
clamp isn't triggered at speed 9 on a diagonal. Pure vertical/horizontal
is capped by MIN_DX/DY but not MAX — the unclamped diagonal can slightly
exceed 14 per component at speed 9 for non-45-degree angles.

Ball velocity (alpha = vector magnitude, px/tick):

| Speed | alpha (px/tick) |
|-------|-----------------|
| 1     | 2.20            |
| 2     | 4.40            |
| 3     | 6.60            |
| 4     | 8.80            |
| 5     | 11.00           |
| 6     | 13.20           |
| 7     | 15.40           |
| 8     | 17.60           |
| 9     | 19.80           |

---

## 4. Wall-Clock Characterization

Play area: `GAME_PLAY_HEIGHT = 580 px` (`include/game_context.h:63`).

Pixels per second = alpha (px/tick) × ticks/sec:

| Speed | alpha | ticks/sec | px/sec | 580 px crossing (sec) |
|-------|-------|-----------|--------|-----------------------|
| 1     | 2.20  | 74        | 163    | 3.56                  |
| 2     | 4.40  | 83        | 365    | 1.59                  |
| 3     | 6.60  | 95        | 627    | 0.93                  |
| 4     | 8.80  | 111       | 977    | 0.59                  |
| 5     | 11.00 | 133       | 1463   | 0.40                  |
| 6     | 13.20 | 167       | 2204   | 0.26                  |
| 7     | 15.40 | 222       | 3419   | 0.17                  |
| 8     | 17.60 | 333       | 5861   | 0.099                 |
| 9     | 19.80 | 667       | 13207  | 0.044                 |

At speed 1, a straight-down launch (alpha = 2.20 px/tick, 74 ticks/sec)
takes **3.56 seconds** to cross the 580 px play area. That is glacial —
at speed 5 the same crossing takes 0.40 s.

---

## 5. The 5× Slower Tick Problem

### Original timing (from original/misc.c:102-108, original/main.c:115-140)

`sleepSync(display, ms)` calls `usleep(ms * 300)`.  
`ms = speed = userDelay * (10 - N)`. At `userDelay = 1` (default):

| Speed N | sleep (µs) | frames/sec |
|---------|------------|------------|
| 1       | 9 × 300 = 2700 | 370    |
| 5       | 5 × 300 = 1500 | 667    |
| 9       | 1 × 300 = 300  | 3333   |

### Comparison at speed 5 (default)

| System  | Period/frame | Rate       |
|---------|-------------|------------|
| Original | 1500 µs    | 667 fps    |
| Modern   | 7500 µs    | 133 fps    |

Modern is **5× slower** at speed 5. The tick interval formula
(`SDL2L_TICK_UNIT_US = 1500`) is matched to the original's speed-5
sleep (1500 µs), but the multiplier `(10 - speed)` means:

- Speed 5: `1500 * 5 = 7500 µs` (original was `1500 * 1 = 1500 µs`)

The `SDL2L_TICK_UNIT_US` constant was named for the original speed-9
floor (300 µs × 5 ≈ 1500 µs is not quite right either; the original
speed-9 sleep is 300 µs, not 1500 µs). The net effect: at every speed
level, the modern tick rate is 5× slower than the original's frame rate.

Ball velocity per tick (`ball_math_normalize_speed`) is identical in
both systems (same formula, same constants). So:

```text
modern px/sec = original px/sec / 5   (at same speed level)
```

### Speed 1 specifically

| System   | px/sec | crossing (580 px) |
|----------|--------|-------------------|
| Original | 814    | 0.71 s            |
| Modern   | 163    | 3.56 s            |

Modern speed 1 is **5× slower** than original speed 1 in wall-clock.
The "glacial" report is correct and quantified.

---

## 6. Tick-Rate Scaling Direction

Both modern and original: **higher number = faster**. No sign flip.

Original: speed N → sleep = `(10-N) * 300 µs` → faster at N=9.  
Modern: speed N → tick = `1500 * (10-N) µs` → faster at N=9.

Both scale the same direction. The problem is the 5× base offset.

The modern formula produces tick intervals that are uniformly 5× the
original's at every level:

```text
modern_tick_us(N)    = 1500 * (10-N)
original_sleep_us(N) =  300 * (10-N)

ratio = 1500/300 = 5  (constant across all N)
```

This is because `SDL2L_TICK_UNIT_US = 1500` where it should be `300` to
match original frame rate. The comment in `include/sdl2_loop.h:17-21`
documents the modern values (`Warp 5 = 7500 µs = ~133 ticks/sec`)
without comparing to the original.

---

## 7. Root Cause Summary

There are three compounding factors. All three contribute. Root cause is
(b) — the 5× base offset is the dominant and universal factor.

**(a) Normalization formula — NOT the root cause at speed 1.**
`alpha = 2.20 * speed_level` is identical to the original. The formula
is correct. At speed 1, alpha = 2.20 px/tick in both systems.

**(b) Tick base rate — root cause.**
`SDL2L_TICK_UNIT_US = 1500` produces a 5× slower tick rate than the
original at every speed level. Fix: change to `SDL2L_TICK_UNIT_US = 300`
OR apply `5×` to `alpha` inside `ball_math_normalize_speed` as a
compensation factor. The former is cleaner — it brings the loop rate
back to the original's base.

**(c) Compound at speed 1.**
Speed 1 is additionally penalized by a 5× longer tick (13500 vs 7500 µs
at speed 5), while speed 1 also gets 5× lower pixel velocity per tick
(2.20 vs 11.00). Both combine multiplicatively: speed 1 is
`(11.0/2.2) × (7500/13500) = 5 × 0.56 = 2.8×` slower in px/sec than
speed 5. This is the "glacial" experience.

---

## Key Citations

| Fact | Citation |
|------|----------|
| `SDL2L_TICK_UNIT_US = 1500` | `include/sdl2_loop.h:39` |
| `SDL2L_DEFAULT_SPEED = 5` | `include/sdl2_loop.h:35` |
| `compute_tick_interval` formula | `src/sdl2_loop.c:45-47` |
| Speed keys attract-only gate | `src/game_input.c:159` |
| Speed key dispatch loop | `src/game_input.c:161-172` |
| `ball_math_normalize_speed` formula | `src/ball_math.c:153-193` |
| `alpha` computation | `src/ball_math.c:166-169` |
| `MAX_X_VEL = 14, MAX_Y_VEL = 14` | `include/ball_types.h:28-29` |
| `MIN_DY_BALL = 2, MIN_DX_BALL = 2` | `include/ball_types.h:30-31` |
| `speed_level` sourced from loop | `src/game_callbacks.c:575` |
| `ball_math_normalize_speed` call site | `src/ball_system.c:589` |
| `GAME_PLAY_HEIGHT = 580` | `include/game_context.h:63` |
| Original `sleepSync` implementation | `original/misc.c:102-108` |
| Original `SetUserSpeed` | `original/main.c:132-141` |
| Original `FAST_SPEED = 5` | `original/include/main.h:81` |
| Original `handleSpeedKeys` | `original/main.c:741-810` |
| Original speed global init | `original/main.c:115, 120` |
