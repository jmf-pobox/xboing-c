# Frame/Tick Calibration Investigation — GAME vs ATTRACT Cadence

**Date:** 2026-07-16
**Author:** jdc (evaluator: jck)
**Mission:** m-2026-07-16-014 (investigate)

## Scope

Investigate the maintainer's report that the ball auto-launch timeout
(`BALL_AUTO_ACTIVE_DELAY=3000`) fires at ~22-25 seconds in the modern
port, against a stated target of "~3 seconds (range 3-5s)". Determine
whether this is a porting defect (missing frame multiplier, wrong
tick rate, or both) or faithful behavior, enumerate every
frame-count-based gameplay timing constant, and recommend a
calibration strategy. No production code is changed by this
investigation.

## Headline finding — the anchor's premise does not hold

**The modern port's ~22.5-second auto-launch is the faithful 1996
behavior, not a porting bug.** Tracing the original's actual runtime
`speed` value (not the naive `speed=5` read of `sleepSync`'s
parameter name) shows the original ran its main loop at the exact
same 7.5 ms/iteration the modern port uses by default. At that
cadence, `BALL_AUTO_ACTIVE_DELAY=3000` un-gated main-loop iterations
elapse in `3000 * 7.5 ms = 22.5 s` in **both** trees. This is not a
new derivation — it is the same baseline two already-committed audits
(`docs/research/2026-06-27-warp-speed-modern.md` §5,
`docs/audits/2026-07-13-gameplay-logic-parity.md` §2/§9) independently
established and used for their own constant conversions
(e.g. the `ROAM_DELAY`/`DROP_DELAY` real-seconds figures in
`docs/audits/2026-07-14-animation-cadence-parity.md` §1a already
assume 7.5 ms/tick, not 1.5 ms).

This directly conflicts with the mission anchor's assumption that the
original's real-world auto-launch time is "~3-5s" and that the modern
~22.5s is a ~5x-stretched bug. It is not — see §1 below for the full
derivation. If the maintainer's actual goal is a **~3-second**
auto-launch regardless of what the 1996 original did, that is a
deliberate gameplay-feel redesign (shortening `BALL_AUTO_ACTIVE_DELAY`
by ~7.5x from its faithfully-ported value), not a calibration fix —
flagged for jck's ruling in §6.

## 1. Measured tick-rate ratio — corrected derivation

### 1a. Modern

`sdl2_loop`'s tick interval: `tick_interval_us = 1500 * (10 -
speed_level)` (`src/sdl2_loop.c:45-47`, `include/sdl2_loop.h:35-39`).
At `SDL2L_DEFAULT_SPEED=5`: `1500*(10-5) = 7500 us = 7.5 ms/tick` →
**133 ticks/sec**. `stub_tick()` (`src/sdl2_state.c:1176-1189`) calls
`sdl2_state_update()` exactly once per consumed tick, which
increments `ctx->frame` exactly once (`src/sdl2_state.c:233-238`)
unless paused or in a dialogue. `mode_game_update()`
(`src/game_modes.c:286-373`) calls `ball_system_update`,
`block_system_advance_animations`, `block_system_update_explosions`,
etc. **exactly once per call** — no loop, no multiplier — using
`(int)sdl2_state_frame(ctx->state)` directly as the frame argument.

### 1b. Original — the naive read is wrong

The mission anchor's original derivation used `speed=5` directly:
`usleep(5*300)=1500us`→~667 fps→`3000/667≈4.5s`. **This is the same
mistake an earlier draft of `docs/research/2026-06-27-warp-speed-modern.md`
made and explicitly retracted** ("the error was misreading the `ms`
argument to `sleepSync`"). Tracing the actual runtime value of the
global `speed` (`original/main.c:119`, `long speed`):

1. `InitialiseGame()` calls `InitialiseSettings()`
   (`original/init.c:542`), which calls `SetUserSpeed(5)`
   (`original/init.c:529`) — sets `userDelay=5`,
   `speedLevel=10-5=5` (`original/main.c:132-141`).
2. `InitialiseGame()` itself calls `SetUserSpeed(5)` again
   (`original/init.c:916`) — same effect, redundant but consistent.
3. `main()` then calls `SetGameSpeed(FAST_SPEED)`
   (`original/main.c:1897`, right after `InitialiseGame()` returns):
   `speed = (long)delay * (long)userDelay` (`original/main.c:155-160`)
   = `FAST_SPEED(5) * userDelay(5)` = **25**.
4. The main loop calls `sleepSync(display, speed)` for
   `MODE_GAME`/`MODE_BALL_WAIT` (`original/main.c:1876-1877`), and
   `sleepSync` does `usleep(ms * 300)` (`original/misc.c:102-108`):
   `usleep(25*300) = usleep(7500) = 7.5 ms/iteration` → **133
   iterations/sec**.

**The original's true default gameplay tick rate is 7.5 ms — identical
to the modern port's 7.5 ms at `SDL2L_DEFAULT_SPEED=5`.** This is not
a new finding; `docs/audits/2026-07-13-gameplay-logic-parity.md` §2
computed the same `speed=25`/`7500us` figure independently while
characterizing ball physics ("Real-world tick interval is
`usleep(speed*300)`... where `speed = FAST_SPEED(5) * userDelay`...
This gives `speed = 5*5 = 25` → `usleep(7500us)`").

### 1c. Resulting ratio

**Measured ratio at default speed: 1.0x — the tick rates match
exactly.** There is no 5x (or any) global stretch in `MODE_GAME` /
`SDL2ST_GAME` frame-count timers. `BALL_AUTO_ACTIVE_DELAY=3000` main-
loop iterations elapses in `3000 * 7.5ms = 22.5s` in **both** the
original and the modern port at default settings. The modern port's
observed ~22-25s (anchor) is consistent with the 22.5s theoretical
figure (the small variance is normal frame-accumulator jitter, not a
defect); it is not consistent with a "should be ~3-5s" original.

## 2. GAME vs ATTRACT per-tick multiplier — root cause of `ATTRACT_FRAME_MULTIPLIER`, and why GAME correctly has none

`ATTRACT_FRAME_MULTIPLIER=6` (`src/game_modes.c:422`) exists **only**
because the original used a **different, `speed`-independent** tick
rate for every non-`MODE_GAME`/`MODE_BALL_WAIT` mode:
`sleepSync(display, 3)` unconditionally (`original/main.c:1746-1749,
1876-1879`) → `usleep(900us)` plus an `XSync` round-trip, empirically
estimated at "~1.2ms/frame ≈ 833fps" by the comment at
`src/game_modes.c:413-417` (not a measured benchmark — see
`docs/audits/2026-07-14-animation-cadence-parity.md` §7, true ratio
estimated at 6.25-8.33x, not exactly 6x).

The modern port has **one** fixed-timestep loop shared by every mode,
always running at the `speed`-derived rate (133 ticks/sec at default).
For attract screens, that is slower than the original's fixed
~833fps, so `mode_presents_update`, `mode_intro_update`,
`mode_instruct_update`, `mode_demo_update`, `mode_keys_update`, etc.
(`src/game_modes.c:470,533,595,646,681,736,798,923,1270` — all 10
`for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)` sites) call their
system's `update()` 6 times per real tick against a **separate,
locally-tracked** `attract_frame_counter`
(`src/game_modes.c:424`) — never `sdl2_state_frame()` — to
approximate the original's faster, `speed`-independent cadence.

**`mode_game_update()` has no such loop, and needs none:** unlike
attract screens, the original's `MODE_GAME` tick rate is `speed`-
derived (`original/main.c:1876-1877`, the *other* branch of the same
`if`), and the modern port's tick rate is `speed`-derived identically
(§1). Both engines advance their real frame counter by exactly 1 per
main-loop iteration in gameplay, at the exact same real cadence. There
is nothing to compensate for.

**Answer to "(a) missing multiplier, (b) tick-rate choice, or (c)
both":** **neither.** The 6x attract multiplier is a compensation for
a rate mismatch that is specific to attract screens (original uses a
fixed fast, `speed`-independent rate there; modern uses the shared
`speed`-derived rate everywhere). `MODE_GAME` was never subject to
that mismatch — its rate already matched natively, in both trees,
before any modernization work. Adding a "gameplay frame multiplier"
would not fix a defect; it would **introduce** a ~6x (or whatever
factor chosen) real-time speed-up across every gameplay timer that
currently faithfully matches the original — see §6.

## 3. Frame-count timing constants — enumerated and classified

All values below assume default speed (`SDL2L_DEFAULT_SPEED=5` /
original `speedLevel=5`, `speed=25`), 7.5 ms/tick in **both** trees
per §1.

| Constant | Original value (cite) | Modern value (cite) | Real-time cadence (both trees, since tick rate matches) | Classification |
|---|---|---|---|---|
| `BALL_AUTO_ACTIVE_DELAY` | 3000 (`original/include/ball_types.h:41`, checked un-gated at `original/ball.c:2068`) | 3000 (`include/ball_types.h:41`, `src/ball_system.c:569,910`) | 22.5 s | Ported 1:1, faithful — **not stretched** |
| `BALL_FRAME_RATE` | 5 (`original/include/ball_types.h:36`, gate at `original/ball.c:2051,2088`) | 5 (`include/ball_types.h:36`, `src/ball_system.c:377,442`) | ball moves every 37.5 ms (293 px/s, confirmed MATCH by `docs/audits/2026-07-13-gameplay-logic-parity.md` §2) | Ported 1:1, faithful |
| `BIRTH_FRAME_RATE` | 5 (`original/include/ball_types.h:35`, `original/ball.c:1828,1873,1970`) | 5 (`include/ball_types.h:35`, `src/ball_system.c:143,236,848,893,1349`) | birth/pop sprite steps every 37.5 ms | Ported 1:1, faithful |
| `BONUS_SEED` | 2000 (`original/include/main.h:58`, `original/main.c:970-971`) | 2000 (`src/game_rules.c:34,102`) | random 0-15 s, avg 7.5 s until a bonus/special spawn roll | Ported 1:1, faithful |
| `BLOCK_ROAM_EYES_DELAY` (`ROAM_EYES_DELAY`) | 300 (`original/include/blocks.h:81`, `original/blocks.c:1364-1421`) | 300 (`include/block_system.h:65`, sprite-only, see caveat below) | eye "tell" reroll every 375 ms - 2.6 s (`(rand()%300)+50` ticks) | Ported 1:1 for the sprite gate; **actual roamer relocation is entirely unimplemented in modern** — separate, already-logged defect (`docs/audits/2026-07-14-animation-cadence-parity.md` §1a), not a cadence issue |
| `BLOCK_ROAM_DELAY` (`ROAM_DELAY`) | 1000 (`original/include/blocks.h:82`) | 1000 (`include/block_system.h:66`, unused — see above) | move attempt every 2.25-9.75 s | Same caveat: constant is ported, but never consumed (no move call site in `src/`) |
| `BLOCK_DROP_DELAY` (`DROP_DELAY`) | 1000 (`original/include/blocks.h:79`, `original/blocks.c:1447-1474`) | 1000 (`include/block_system.h:63`, unused) | drop every 1.5-9.0 s | Constant ported; `DROP_BLK` movement entirely missing in modern (same audit) |
| `BLOCK_RANDOM_DELAY` (`RANDOM_DELAY`) | 500 (`original/include/blocks.h:78`) | 500 (`include/block_system.h:62`) | — (morph cadence; confirmed fixed under PR #187 per commit `11d9e36`) | Ported 1:1, faithful |
| `BLOCK_BONUS_DELAY` (`BONUS_DELAY`) | 150 (`original/include/blocks.h:72`, `original/blocks.c:1188-1218`) | 150 (`include/block_system.h:57`, `src/block_system.c:619-624`) | 1.125 s/step, 4.5 s period | Ported 1:1, faithful (spin direction was reversed, separately fixed under PR #189 per commit `901d102`) |
| `BLOCK_EXTRABALL_DELAY` (`EXTRABALL_DELAY`) | 300 (`original/include/blocks.h:77`, `original/blocks.c:1336-1352`) | 300 (`include/block_system.h:61`, `src/block_system.c:631-634`) | 2.25 s/state, 4.5 s period | Ported 1:1, faithful |
| `BLOCK_EXPLODE_DELAY` (`EXPLODE_DELAY`) | 10 (`original/include/blocks.h:71`, `original/blocks.c:1480-1646`) | 10 (`include/block_system.h:56`, `src/block_system.c:558-602`) | 75 ms/stage, 300 ms total | Ported 1:1, faithful |
| `BLOCK_DEATH_DELAY1`/`DELAY2` (`DEATH_DELAY1`/`2`) | 100 / 700 (`original/include/blocks.h:75-76`, `original/blocks.c:1313-1334,2395-2398`) | 100 / 700 (`include/block_system.h:59-60`, `src/block_system.c:642-645`, PR #192) | 6.0 s idle hold + 3×0.75 s blink steps, 8.25 s period (`hold0=800` ticks, `period=1100` ticks) | Ported 1:1, faithful in ticks **and** real seconds (see §4) |
| `PADDLE_VELOCITY` (`PADDLE_VEL`) | 10 px, gated every `PADDLE_ANIMATE_DELAY=5` iterations (`original/include/paddle.h:69`, `original/include/main.h:57`, `original/main.c:962-963`) | 4 px, applied every tick, **no gate** (`include/paddle_system.h:49`, `src/paddle_system.c:157-179`) | Original: `10px/37.5ms ≈ 267 px/s`. Modern: `4px/7.5ms ≈ 533 px/s` | **Genuine, already-logged, unresolved deviation — modern paddle is 2.0x too FAST** (not a tick-rate artifact; the `PADDLE_ANIMATE_DELAY` gate was dropped and velocity only halved instead of quartered). `docs/audits/2026-07-13-gameplay-logic-parity.md` finding #1, HIGH severity, open |
| Ball speed (`SPEED_ALPHA[5]`) | `alpha=2.2*speedLevel=11.0 px/tick`, gated by `BALL_FRAME_RATE` (`original/ball.c:1172-1196`) | `SPEED_ALPHA[5]=11.00` (`src/ball_math.c`, ADR-045) | 293 px/s both trees | Confirmed MATCH, deliberately anchored (ADR-045) |
| `ATTRACT_FRAME_MULTIPLIER` (context only, not a GAME-mode constant) | N/A — original attract rate is fixed `usleep(900us)+XSync`, `speed`-independent | 6 (`src/game_modes.c:422`) | Estimated ratio 6.25-8.33x; 6 is a reasoned round number, not measured (`docs/audits/2026-07-14-animation-cadence-parity.md` §7) | Attract-only; unrelated to the GAME-mode question, included for completeness per the mission brief |

## 4. Anchors that must not break

### 4a. Bonus-screen line pacing — intentional, hand-tuned, do not touch

`include/bonus_system.h` constants (`BONUS_LINE_DELAY=2000`,
`BONUS_INIT_DELAY=100`, `BONUS_STEP_DELAY=20`) are **not** derived
from the gameplay tick at all — `MODE_BONUS` runs at the
`speed`-independent attract rate in the original
(`original/main.c:1746-1749`, confirmed by
`docs/audits/2026-07-14-animation-cadence-parity.md` §6), and the
maintainer confirmed the current modern pacing is deliberate
readability tuning, not a fidelity target
(`docs/specs/2026-06-06-bonus-renderer-rewrite.md`, bead xboing-725
closed not-a-bug). **This is orthogonal to the GAME-mode
BALL_AUTO_ACTIVE_DELAY question** — bonus pacing was never governed by
the same tick math being investigated here, and no strategy in §6
touches it.

### 4b. DEATH_BLK wink (PR #192, `ad6efa2`) — faithful in ticks AND real seconds; no tension

The mission brief asks whether the wink fix (800/1100-tick asymmetric
rhythm), ruled faithful *in ticks*, might still be *stretched in real
seconds* if gameplay ticks run ~5x slow. **They do not (§1) — so there
is no tension.** `hold0=BLOCK_DEATH_DELAY2+BLOCK_DEATH_DELAY1=800`
ticks and `period=BLOCK_DEATH_DELAY2+4*BLOCK_DEATH_DELAY1=1100` ticks
(`src/block_system.c:642-645`) elapse in `800*7.5ms=6.0s` and
`1100*7.5ms=8.25s` in the modern port. The original's un-gated
main-loop iteration counter runs at the identical 7.5ms/iteration
(§1b), so the same tick counts elapse in the identical wall-clock time
there too. **The wink fix is correct as shipped and needs no revisit
as a side effect of this investigation.**

## 5. The anchor conflict, restated plainly

The mission brief states the target is "~3 seconds (revised down from
~5s), treat as ground truth" for the auto-launch delay, and frames the
current ~22-25s as a bug to calibrate away. The evidence says
otherwise: **~22.5s is what `BALL_AUTO_ACTIVE_DELAY=3000` faithfully
produces in the 1996 original itself**, once the runtime `speed=25`
(not the naively-read `speed=5`) is traced through `SetUserSpeed` +
`SetGameSpeed`. Two already-committed, independently-authored audits
used this exact 7.5ms/25-speed baseline before this investigation
started. This is not a close call or a matter of interpretation — it
is directly traceable, cited code.

This means "get to ~3s" is not a calibration/porting-fidelity task. It
is a request to make the modern game's ball auto-launch **faster than
the original ever was**, by design, for player-experience reasons.
That is squarely a gameplay-feel decision — the same category CLAUDE.md
reserves for jck ("Ball physics math... ARE the game feel", "Approve
or reject changes that affect gameplay mechanics"), and it needs the
same explicit, documented, playtest-backed treatment ADR-045 gave the
ball-speed compression (`docs/DESIGN.md:2708-2775`), not a silent
constant edit.

## 6. Recommended strategy

**Do not implement a gameplay frame multiplier (option i) or a global
rescale of ported-1:1 constants (option ii).** Both presuppose a
systemic ~5x (or similar) stretch that §1-§3 show does not exist. Applying
either would:

- Speed up every faithfully-matching gameplay timer in §3
  (ball movement, block explosions, bonus/special spawn odds, the
  just-shipped DEATH_BLK wink, RANDOM/BONUS/EXTRABALL cadences) by
  the chosen factor, turning currently-correct behavior into new,
  undocumented deviations.
- Directly contradict ADR-013 (fixed-timestep loop reproduces the
  original's main-loop-iteration interval exactly,
  `docs/DESIGN.md:745-804`) and ADR-045 (ball speed deliberately
  anchored to the original's real px/s, `docs/DESIGN.md:2708-2775`),
  both of which this codebase has already invested real playtest and
  audit effort into getting right.
- Re-open findings §4b (DEATH_BLK) and the block-delay cadences in §3
  that two prior audits confirmed as MATCH, for no fidelity gain.

**Option iii (per-constant), scoped to a single, deliberately-flagged
value, is the only defensible path if the ~3s target survives jck's
review:** change `BALL_AUTO_ACTIVE_DELAY` alone. At 133 ticks/sec,
3 seconds ≈ 400 ticks (a ~7.5x reduction from 3000); the anchor's
"3-5s" range spans roughly 400-667 ticks. This is a single `#define`
change (`include/ball_types.h:41`) plus the two call sites that copy
it (`src/ball_system.c:569,910`) and their `original/`-derived
comments. It:

- Does not touch tick rate, the loop, or any other constant.
- Does not regress any of the MATCH findings in §3/§4.
- Is small in scope (one constant, ~3 call sites, characterization
  test update) but is **not** a bug fix — it is a named deviation
  from the 1996 original's actual behavior and must be logged as such
  (an ADR alongside ADR-045, with before/after playtest, same as the
  paddle-velocity fix already recommended in
  `docs/audits/2026-07-13-gameplay-logic-parity.md` bead #1).

**Separately, and regardless of the auto-launch question:** the
already-logged, unresolved `PADDLE_VELOCITY` 2x-too-fast deviation
(§3) is a genuine porting defect (not a design choice) with an
existing recommended fix (`PADDLE_VELOCITY` 4→2,
`include/paddle_system.h:49`) blocked only on a jck-approved playtest.
It is unrelated to this mission's auto-launch question but was
explicitly in the requested constant enumeration and remains open.

**Needs jck's ruling:**

1. Is "~3s auto-launch" truly the desired target, understanding it
   requires diverging from the original's own ~22.5s behavior by
   design (not fixing a porting bug)?
2. If yes, what real-world figure — 3s, 4s, 5s — and does it get an
   ADR alongside ADR-045?
3. Does the PADDLE_VELOCITY 2x-too-fast finding get scheduled
   independently of this decision?

## Evidence index

| Fact | Citation |
|---|---|
| Modern tick formula, default 7.5ms/tick | `src/sdl2_loop.c:45-47`, `include/sdl2_loop.h:35-39` |
| `sdl2_state_update` increments frame once/tick | `src/sdl2_state.c:233-238` |
| `stub_tick` calls `sdl2_state_update` once per consumed tick | `src/sdl2_state.c:1176-1189` |
| `mode_game_update` — no multiplier loop, calls systems once/tick | `src/game_modes.c:286-373` |
| `ATTRACT_FRAME_MULTIPLIER=6`, 10 call sites | `src/game_modes.c:422,470,533,595,646,681,736,798,923,1270` |
| Original `speed`/`userDelay`/`speedLevel` globals | `original/main.c:113-141` |
| `SetUserSpeed`/`SetGameSpeed` formulas | `original/main.c:132-160` |
| `InitialiseSettings` calls `SetUserSpeed(5)` | `original/init.c:506-529,542` |
| `InitialiseGame` calls `SetUserSpeed(5)` again | `original/init.c:909-916` |
| `main()` calls `SetGameSpeed(FAST_SPEED)` after init | `original/main.c:1890-1899` |
| Original main-loop tick dispatch (GAME vs attract) | `original/main.c:1746-1749,1876-1879` |
| `sleepSync` implementation | `original/misc.c:102-108` |
| `BALL_AUTO_ACTIVE_DELAY` un-gated check | `original/ball.c:2068`, `src/ball_system.c` (frame equality/threshold at set sites 569,910) |
| `BALL_AUTO_ACTIVE_DELAY=3000` both trees | `original/include/ball_types.h:41`, `include/ball_types.h:41` |
| DEATH_BLK wink fix (PR #192) | `src/block_system.c:642-645`, commit `ad6efa2` |
| Paddle velocity 2x finding (open) | `docs/audits/2026-07-13-gameplay-logic-parity.md` §9, finding #1 |
| Prior corrected tick-rate derivation | `docs/research/2026-06-27-warp-speed-modern.md` §5 |
| Prior independent 7.5ms/speed=25 derivation | `docs/audits/2026-07-13-gameplay-logic-parity.md` §2 |
| Attract-mode 6x multiplier rationale/estimate | `docs/audits/2026-07-14-animation-cadence-parity.md` §7 |
| Bonus-screen pacing ruled intentional | `docs/specs/2026-06-06-bonus-renderer-rewrite.md`, bead xboing-725 |
| ADR-013 (fixed-timestep loop) | `docs/DESIGN.md:745-804` |
| ADR-045 (ball speed compression) | `docs/DESIGN.md:2708-2775` |
