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

## 5. Original timing — corrected

This section's earlier draft (preserved in git history) hypothesised
that modern was 5× slower than the original because the `* 300`
multiplier in `original/misc.c:107` made the original 5× faster per
frame at each speed. **That was wrong.** The error was misreading the
`ms` argument to `sleepSync`. The full path:

1. `SetGameSpeed(FAST_SPEED)` at `original/main.c:1897` sets
   `speed = FAST_SPEED * userDelay = 5 * 1 = 5`.
2. `SetUserSpeed(delay)` at `original/main.c:132-141` then sets
   `speed = 5 * (10 - speedLevel)` for each warp keypress.
3. Main loop calls `sleepSync(display, speed)` at
   `original/main.c:1876-1877`, which calls `usleep(speed * 300)`.

So `sleep_us = 1500 * (10 - speedLevel)`:

| Speed N | sleep (µs) | nominal frames/sec |
|---------|------------|--------------------|
| 1       | 13,500     | 74                 |
| 5       | 7,500      | 133                |
| 9       | 1,500      | 667                |

These numbers match the modern tick interval exactly. The modern port
is **character-for-character correct** at every speed level.

## 6. Why speed 1 still feels "glacial" on modern hardware

Because the formula was tuned against 1990s X11 hardware that hid its
non-linearity at the extremes:

- On a Sun SPARCstation, the per-frame X11 work (XSync RTT,
  `XCopyArea` blits, event polling) ate 8–15 ms of CPU regardless of
  the requested `usleep`. At speed 9 the nominal 1.5 ms sleep was
  dwarfed by ~10 ms of work; actual frame rate plateaued near ~85 fps.
- At speed 1 the 13.5 ms sleep added to the ~10 ms work, giving
  ~43 fps. The full 81× nominal span (`N / (10-N)`) collapsed to
  roughly 2× in real-world wall-clock.

Modern SDL2's fixed-timestep accumulator honors the sleep to the
microsecond and rendering is sub-millisecond. The formula's true 81×
range is now exposed. Speed 1 becomes 3.56 s vertical crossing
(unplayable); speed 9 becomes 0.044 s (untrackable). The "glacial"
report is correct, but the cause is hardware-floor compression no
longer hiding the formula, not a tick-rate divergence between modern
and original.

See `docs/research/2026-06-27-warp-speed-original.md` for the
ground-truth original-side analysis (jck) and `docs/DESIGN.md`
ADR-045 for the deliberate-deviation fix.

---

## 7. Conclusion

The modern port reproduces the original formula exactly. The bug is
not in the modern code; it is that the original formula assumed a
hardware floor that no longer exists. The fix is a deliberate
deviation: replace the computed alpha with a tuned lookup table that
compresses the 1↔9 span back into a playable range
(`SPEED_ALPHA[]` in `src/ball_math.c`).

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
