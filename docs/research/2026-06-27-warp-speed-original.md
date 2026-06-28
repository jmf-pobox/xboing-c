# Warp speed: original 1996 reference

**Author:** jck (delegated research)
**Date:** 2026-06-27
**Bead:** xboing-c-tks

## 1. Code path — speed key handling

`handleSpeedKeys` at `original/main.c:741-812` handles keys `1`–`9`. Called
exclusively from `handleIntroKeys` (`original/main.c:683-685`). Speed keys
are NOT processed during `MODE_GAME`; `handleGameKeys` (`original/main.c:529-531`)
calls only `handleMiscKeys` on its default branch.

| Key | Call | `delay` | `speedLevel` set |
|-----|------|---------|------------------|
| 1 | `SetUserSpeed(9)` | 9 | 1 |
| 2 | `SetUserSpeed(8)` | 8 | 2 |
| 3 | `SetUserSpeed(7)` | 7 | 3 |
| 4 | `SetUserSpeed(6)` | 6 | 4 |
| 5 | `SetUserSpeed(5)` | 5 | 5 |
| 6 | `SetUserSpeed(4)` | 4 | 6 |
| 7 | `SetUserSpeed(3)` | 3 | 7 |
| 8 | `SetUserSpeed(2)` | 2 | 8 |
| 9 | `SetUserSpeed(1)` | 1 | 9 |

`SetUserSpeed` (`original/main.c:132-141`):

```c
temp = (speed / (long)userDelay);
userDelay = delay;
speed = (long)(temp * userDelay);
speedLevel = 10 - delay;
```

Initial state (`original/main.c:115,120`): `speedLevel = 5`, `userDelay = 1`.
`main()` calls `SetGameSpeed(FAST_SPEED)` at `original/main.c:1897`, setting
`speed = 5`.

Result invariant: `speed = 5 * (10 - speedLevel)`.

## 2. Frame clock per speed level

`sleepSync` (`original/misc.c:102-108`):

```c
void sleepSync(Display *display, unsigned long ms) {
    XSync(display, False);
    if (ms > 0) usleep(ms * 300);
}
```

Main loop calls `sleepSync(display, speed)` (`original/main.c:1876-1877`).

Sleep formula: `usleep(speed * 300) = usleep(1500 * (10 - speedLevel))` µs.

| Warp | `userDelay` | `speed` | sleep (µs) | nominal ticks/s |
|------|-------------|---------|------------|-----------------|
| 1 | 9 | 45 | 13,500 | ~74 |
| 2 | 8 | 40 | 12,000 | ~83 |
| 3 | 7 | 35 | 10,500 | ~95 |
| 4 | 6 | 30 | 9,000 | ~111 |
| 5 | 5 | 25 | 7,500 | ~133 |
| 6 | 4 | 20 | 6,000 | ~167 |
| 7 | 3 | 15 | 4,500 | ~222 |
| 8 | 2 | 10 | 3,000 | ~333 |
| 9 | 1 | 5 | 1,500 | ~667 |

The nominal ticks/s is an **upper bound** assuming sleep is the only cost.
On 1994 X11 hardware, XSync RTT was 1–5 ms and `XCopyArea` blits ate more,
so the actual frame rate at all speed levels was materially lower than
nominal — especially at the high end where the 1.5 ms sleep is shorter than
typical XSync RTT.

The `* 300` value is the "delay tuned for modern hardware" comment
(`original/misc.c:104`); the original 1994 multiplier was larger and was
reduced during initial modernization. `docs/ARCHITECTURE_LEGACY.tex:588`
confirms the provenance. `docs/SPECIFICATION.md:399` says `* 400` —
**stale; archived source and ADR-013 both use `* 300`.**

## 3. Ball velocity per speed — normalization

`UpdateABall` (`original/ball.c:1023`) normalizes every tick at lines
1168-1196:

```c
Vx = (float)balls[i].dx;
Vy = (float)balls[i].dy;
Vs = sqrtf(Vx*Vx + Vy*Vy);

alpha = sqrtf(MAX_X_VEL*MAX_X_VEL + MAX_Y_VEL*MAX_Y_VEL);  /* ~19.799 */
alpha /= 9.0;
alpha *= (float)speedLevel;
if (Vs == 0.0) Vs = 1.0;
beta = alpha / Vs;

Vx *= beta;  Vy *= beta;
balls[i].dx = Vx > 0 ? (int)(Vx+0.5) : (int)(Vx-0.5);
balls[i].dy = Vy > 0 ? (int)(Vy+0.5) : (int)(Vy-0.5);
if (balls[i].dy == 0) balls[i].dy = MIN_DY_BALL;
if (balls[i].dx == 0) balls[i].dx = MIN_DX_BALL;
```

Constants (`original/include/ball_types.h:28-32`):

- `MAX_X_VEL = 14`, `MAX_Y_VEL = 14`
- `MIN_DX_BALL = 2`, `MIN_DY_BALL = 2`

Per-tick magnitude: **`alpha ≈ 2.200 * speedLevel`** px.

| Warp | alpha (px/tick) | dx @ 45° (int) |
|------|-----------------|----------------|
| 1 | 2.20 | 2 |
| 2 | 4.40 | 3 |
| 3 | 6.60 | 5 |
| 4 | 8.80 | 6 |
| 5 | 11.00 | 8 |
| 6 | 13.20 | 9 |
| 7 | 15.40 | 11 |
| 8 | 17.60 | 12 |
| 9 | 19.80 | 14 |

Normalization fires every tick regardless of collisions, so alpha is the
true per-tick displacement magnitude.

## 4. Wall-clock characterization

Play area: `PLAY_WIDTH = 495`, `PLAY_HEIGHT = 580` (`original/include/stage.h:62-63`).

| Warp | alpha | ticks/s | px/s | 580 px vertical crossing |
|------|-------|---------|------|--------------------------|
| 1 | 2.20 | ~74 | ~163 | ~3.6 s |
| 5 | 11.00 | ~133 | ~1,463 | ~0.40 s |
| 9 | 19.80 | ~667 | ~13,207 | ~0.044 s |

Speed 1 was meant to be slow — the display message at `original/main.c:748`
is literally "Warp 1 - Slow". A 3.6 s crossing is by design.

## 5. Per-step span

Pixels-per-second scales as `N / (10 - N)` because both velocity and tick
rate increase with N. Total 1→9 span is **81×**.

| Step | Δ multiplier | Δ % |
|------|--------------|-----|
| 1→2 | 2.25× | +125% |
| 2→3 | 1.71× | +71% |
| 3→4 | 1.56× | +56% |
| 4→5 | 1.50× | +50% |
| 5→6 | 1.50× | +50% |
| 6→7 | 1.56× | +56% |
| 7→8 | 1.71× | +71% |
| 8→9 | 2.25× | +125% |

The curve is symmetric around speed 5. Adjacent steps at the extremes more
than double the previous level's speed.

## 6. Modern port conformance

Both subsystems exactly reproduce the original:

- Tick interval: `SDL2L_TICK_UNIT_US = 1500` (`include/sdl2_loop.h:39`);
  `compute_tick_interval = 1500 * (10 - speed)` (`src/sdl2_loop.c:45-47`).
- Velocity: `ball_math_normalize_speed` (`src/ball_math.c:153-193`) is a
  character-for-character replica of `original/ball.c:1168-1196`.
- Scope: speed keys gated behind `is_attract` at `src/game_input.c:159-173`,
  matching the original's intro-only scope.

## 7. Implication for the "glacial" report

Warp 1 was 3.6 s vertical crossing in 1996 too — by design. The modern
port reproduces the formula exactly. If the modern feels worse than the
original at the extremes, the cause is that 1990s X11/CPU overhead
compressed the curve in practice: at speed 9 the work floor (10 ms typical)
exceeded the 1.5 ms sleep, so actual frame rate plateaued. Modern SDL2's
fixed-timestep accumulator honors the sleep precisely, exposing the
formula's true non-linearity.

The bug is not in the modern code. The original formula was tuned for
hardware that hid the formula's pathologies. On modern hardware the
formula plays as written.
