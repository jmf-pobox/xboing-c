# Animation Speed and State-Machine Cadence Parity Audit

**Date:** 2026-07-14
**Author:** jck (evaluator: jdc)
**Mission:** m-2026-07-14-005

## Scope

This audit compares the **rate** at which animated things advance —
block sprite cycling, block/roamer/drop movement, ball sprite/guide
flicker, SFX visual effects, the ambient border glow, the EyeDude
walk cycle, paddle animation, the bonus tally screen, and the
attract-mode frame multiplier — between `original/` (1996 Xlib) and
`src/` (modern SDL2 port). It does **not** revisit the mid-play
special-spawn throttle audited under ADR-069/PR#184, and it does not
re-derive ball launch speed or per-tick ball displacement, which
`docs/audits/2026-07-13-gameplay-logic-parity.md` already
characterized as an exact match. This audit cites and builds on that
prior work rather than re-litigating it.

## Method

Every constant below is traced to its `#define` in both trees and
converted to real-world milliseconds using the tick anchor
established in `docs/audits/2026-07-13-gameplay-logic-parity.md`
and `docs/research/2026-06-27-warp-speed-original.md`:

- **Gameplay tick (`MODE_GAME` / `MODE_BALL_WAIT`):**
  `sleepSync(display, speed)` at default `speed=25` →
  `usleep(25*300) = 7500 µs` per iteration
  (`original/main.c:1876-1877`, `original/misc.c:102-108`). The
  modern fixed-timestep loop reproduces this exactly:
  `tick_interval_us = 1500 * (10 - speed)` at `speed=5` → `7500 µs`
  (`src/sdl2_loop.c:45-47`, ADR-013). The global `frame` counter
  (original) and `sdl2_state_frame(ctx->state)` (modern) both
  advance by exactly 1 per tick in this mode.
- **Attract-screen tick (every non-`MODE_GAME`/`MODE_BALL_WAIT`
  mode, including `MODE_BONUS`):** `sleepSync(display, 3)` →
  `usleep(900 µs)` plus `XSync` round-trip
  (`original/main.c:1746-1749, 1876-1879`). The modern port
  compensates for its slower ~7.5 ms fixed tick by calling each
  attract module's `update()` `ATTRACT_FRAME_MULTIPLIER=6` times per
  tick (`src/game_modes.c:390`), advancing a local
  `attract_frame_counter` once per sub-call.

Both anchors were verified by reading the dispatch code directly,
not assumed.

## 1. Block animations

`original/blocks.c:1258-1478` (`HandlePendingAnimations`), called
once per gameplay tick from `original/main.c:1135`. Modern equivalent
`block_system_advance_animations()` (`src/block_system.c:604-646`),
called once per tick from `src/game_modes.c:298`. Both run at the
gameplay tick rate (7.5 ms/tick at default speed) — the calling
cadence is correct in isolation. The *implementation* of what
happens on each call differs by block type:

### 1a. ROAMER_BLK / DROP_BLK — movement is entirely absent (HIGH)

`original/blocks.c:1364-1421` moves a `ROAMER_BLK` to an adjacent
cell by calling `AddNewBlock()` at the new position and
`ClearBlock()` at the old one, on a **randomized** schedule: the eye
"tell" frame is chosen with `rand() % 5` and re-rolled every
`(rand() % ROAM_EYES_DELAY) + 50` ticks (50–349 ticks,
`original/include/blocks.h:81`), and the block attempts to move
using that same value (`d = bonusSlide + 1`) after
`(rand() % ROAM_DELAY) + 300` ticks (300–1299 ticks,
`original/include/blocks.h:82`) — or waits longer if the target cell
is occupied (`original/blocks.c:1416-1419`). `DROP_BLK` moves down
one row on an identical `AddNewBlock`/`ClearBlock` pattern
(`original/blocks.c:1447-1474`), gated by
`(rand() % DROP_DELAY) + 200` ticks (`original/include/blocks.h:79`).

The modern `block_system_advance_animations()`
(`src/block_system.c:604-646`) only cycles the **sprite frame** for
`ROAMER_BLK` (a deterministic `(frame / BLOCK_ROAM_EYES_DELAY) % 5`
look-direction rotation) and has **no case for `DROP_BLK` at all**.
Grepping the entire `src/` tree for `block_system_add(` call sites
(`src/block_system.c:426`, `src/game_callbacks.c:862`,
`src/game_rules.c:91-143`, `src/game_init.c:1213`,
`src/savegame_system.c:392`) turns up only level-load, savegame
restore, and the special/bonus spawner (already audited under
ADR-069) — **no call site ever relocates an existing `ROAMER_BLK` or
`DROP_BLK` on the grid.** Real-world cadence: original moves every
~2.25–9.75 s (roamer) or ~1.5–9 s (drop) at default speed; modern
moves **never**. This is not a cadence mismatch, it is a missing
mechanic — a `ROAMER_BLK` sits still forever cycling a deterministic
look-direction and a `DROP_BLK` never drops.

**ADR check:** no ADR in `docs/DESIGN.md` scopes movement out.
ADR-016 (block collision) explicitly scopes to collision geometry
only and says nothing about the animation/movement subsystem. This
is an undocumented gap, not an approved design decision.

### 1b. DEATH_BLK — wink rhythm and period are both wrong (MEDIUM)

`original/blocks.c:1313-1334` traced tick-by-tick from spawn
(`nextFrame = frame + DEATH_DELAY2`, `original/blocks.c:2395-2398`):

| Real tick offset | `bonusSlide` shown | Visible duration |
|---|---|---|
| 0 (spawn) | 0 | 700 ticks (`DEATH_DELAY2`) |
| +700 | 1 | 100 ticks (`DEATH_DELAY1`) |
| +800 | 2 | 100 ticks |
| +900 | 3 | 100 ticks |
| +1000 | 4 | **0 — drawn then instantly overwritten by the reset-to-0 draw in the same iteration** (`original/blocks.c:1324-1330`) |
| +1000 | 0 (repeat) | 700 ticks |

Total period **1000 ticks** (7.5 s at default speed): a long
"idle/closed-eye" hold followed by a fast 3-frame blink, with the
4th frame (`bonusSlide == 4`) never actually visible to the player.

Modern: `bp->bonus_slide = (frame / BLOCK_DEATH_DELAY1) % 5;`
(`src/block_system.c:626-629`) is a **uniform** 5-step rotation, 100
ticks per step, 500-tick period (3.75 s) — exactly **half** the
original period, with **no idle hold** and with the previously
invisible 4th frame now visible for 100 ticks every cycle. A 1996
player would notice the pirate-face block now "winks" twice as often
with no pause between blinks and an extra frame the original never
showed.

**ADR check:** no ADR documents this. Undocumented deviation.

### 1c. BONUS_BLK / BONUSX2_BLK / BONUSX4_BLK — correct rate, reversed direction (LOW)

`original/blocks.c:1188-1218` (`HandlePendingBonuses`) decrements
`bonusSlide` from 3 → 0 → 3 every `BONUS_DELAY=150` ticks (600-tick
period). Modern: `bp->bonus_slide = (frame / BLOCK_BONUS_DELAY) % 4;`
(`src/block_system.c:619-624`) counts **up** 0 → 3 every 150 ticks
(also a 600-tick period). Rate matches exactly; rotation direction
is reversed — a cosmetic mirror-image spin, not a timing bug.

### 1d. EXTRABALL_BLK — MATCH

`original/blocks.c:1336-1352`: flip 0/1 every `EXTRABALL_DELAY=300`
ticks (600-tick period). Modern:
`(frame / BLOCK_EXTRABALL_DELAY) % 2` (`src/block_system.c:631-634`)
— identical rate and identical sequence (a 2-state flip has no
direction to reverse).

### 1e. EXPLODE_DELAY — MATCH

`original/blocks.c:1480-1646` (`ExplodeBlocksPending`) advances one
explosion stage every `EXPLODE_DELAY=10` ticks, 4 stages plus
finalize. Modern `block_system_update_explosions()`
(`src/block_system.c:558-602`) uses the identical
`BLOCK_EXPLODE_DELAY=10` gate and stage count, called at the same
per-tick cadence (`src/game_modes.c:304`).

## 2. Ball animation — MATCH

Already fully characterized in
`docs/audits/2026-07-13-gameplay-logic-parity.md` ("Ball Physics"
section): `BALL_FRAME_RATE=5` gates real ball movement
(`original/ball.c:2051,2088` vs `src/ball_system.c:377,442`),
`BALL_ANIM_RATE=15` gates the ball sprite slide
(`original/ball.c:417` vs `src/ball_system.c:395,606`),
`BIRTH_FRAME_RATE=5` gates the birth/pop sprite animation
(`original/ball.c:1828,1873` vs `src/ball_system.c:143,236,848,893`),
and `BALL_AUTO_ACTIVE_DELAY=3000` gates the ready-ball auto-launch
timeout (`original/ball.c:1154` vs `src/ball_system.c:569,910`). All
four constants are identical, and the launch-guide flicker gate
`frame % (BALL_FRAME_RATE * 8)` (`original/ball.c:456`) has an exact
modern counterpart noted in `src/game_render.c:287-295`. **No new
findings.**

## 3. SFX visual effects and ambient border glow

### 3a. SHAKE / FADE / BLIND / SHATTER / STATIC — MATCH

All five constants are identical between trees:
`SHAKE_DELAY=5` (`original/sfx.c:75` / `SFX_SHAKE_DELAY`,
`include/sfx_system.h:22`), fade grid stride 12
(`original/sfx.c:251,255` / `SFX_FADE_STRIDE=12`,
`include/sfx_system.h:24`), and the 50-frame STATIC placeholder
duration (`original/sfx.c:157` / `SFX_STATIC_DURATION=50`,
`include/sfx_system.h:26`). Both trees dispatch these unconditionally
once per gameplay tick — `original/main.c:1347-1377` (top of
`handleGameStates`, runs every iteration regardless of mode) vs
`sfx_system_update()` at `src/game_modes.c:314`, called once per
tick from `mode_game_update`. Cadence and constants both match.

### 3b. BorderGlow now runs during live gameplay — never did in the original (HIGH)

`BorderGlow()` (`original/sfx.c:324-359`, 40-frame color-step
interval) is called from exactly seven places in `original/`:
`keysedit.c:286`, `demo.c:291`, `highscore.c:530`, `intro.c:406`,
`inst.c:239`, `keys.c:329`, `preview.c:201` — **every one of them an
attract/menu screen.** `original/main.c:926-1478`
(`handleGameMode` and the rest of `handleGameStates`) contains
**zero** calls to `BorderGlow()`. During `MODE_GAME` the play-window
border is set once (red) and never animated by this function.

The modern port calls `sfx_system_update_glow()` unconditionally
every tick inside `mode_game_update()` (`src/game_modes.c:315`), and
`game_render_border_glow()` is called from the `SDL2ST_GAME` render
branch (`src/game_render.c:1426-1433`, comment: "Animated border
glow (overwrites static red border)") as well as the equivalent
`SDL2ST_DIALOGUE`-over-`SDL2ST_GAME` branch
(`src/game_render.c:1386-1393`). **The pulsing red/green ambient
border glow now animates continuously during actual gameplay** — an
effect a 1996 player would only ever have seen on the title,
instructions, keys, high-score, demo, or preview screens, never
while playing. `SFX_GLOW_FRAME_INTERVAL=40` and `SFX_GLOW_STEPS=7`
are numerically correct (`include/sfx_system.h:33-34`); the defect
is scope, not rate.

**ADR check:** ADR-023 (`docs/DESIGN.md:1386-1445`) documents
BorderGlow as "an independent animation" with correct constants but
never states or approves that it should run during `MODE_GAME`. This
is an undocumented scope expansion, not a logged design decision.

## 4. EyeDude cadence — MATCH

`EYEDUDE_FRAME_RATE=30` gates both the walk-step advance and the
sprite-slide increment in `original/eyedude.c:308` (`HandleEyeDudeWalk`,
called from `original/main.c:1137` inside `handleGameMode`, i.e. once
per gameplay tick). The modern `eyedude_system_update()` uses the
identical `EYEDUDE_FRAME_RATE` gate (`src/eyedude_system.c:193`) and
is called once per tick from `src/game_modes.c:311` with the same
`sdl2_state_frame()` value used for every other gameplay-tick system.
Rate and calling cadence both match.

## 5. Paddle animation cadence

The original has **no size-change (shrink/expand) transition
animation** — `ChangePaddleSize()` (`original/paddle.c:318-353`)
reassigns `currentPaddleSize` and the next redraw simply shows the
new size; there is no intermediate frame. The modern port matches
this (instant size change, no interpolation) — **N/A, correctly
absent in both.**

Paddle *movement* cadence (not requested for this audit but adjacent
enough to flag by cross-reference) was already found to deviate 2×
in `docs/audits/2026-07-13-gameplay-logic-parity.md` section 9:
original gates `handlePaddleMoving()` behind
`PADDLE_ANIMATE_DELAY=5` (`original/main.c:962-963`), giving
`10 px / 37.5 ms ≈ 267 px/s`; the modern `paddle_system_update()` has
no equivalent gate and moves `4 px / 7.5 ms ≈ 533 px/s` — exactly
2× faster. That finding is not re-derived here; it is carried
forward as a known, already-logged, still-open deviation and is
**not** double-counted in this audit's ranked table below.

## 6. Bonus screen line cadence — derivation rests on a false tick-rate premise (HIGH, needs runtime verification)

`include/bonus_system.h:30-65` derives `BONUS_LINE_DELAY`,
`BONUS_INIT_DELAY`, and `BONUS_STEP_DELAY` from the claim that the
original bonus screen runs at the **gameplay** tick
(`SLOW_SPEED=30 ms`, set via `SetGameSpeed(SLOW_SPEED)` at the head
of every `DoX` function in `original/bonus.c:258,267,276,297,...`).
Tracing the mode-dispatch code shows this premise is false:

- `mode = MODE_BONUS` is set at `original/level.c:416`.
  `MODE_BONUS` (`original/include/main.h:70`, value 7) is neither
  `MODE_GAME` (3) nor `MODE_BALL_WAIT` (5).
- The main loop's tick-pacing branch
  (`original/main.c:1746-1749` and the live-loop copy at
  `1876-1879`) is: `if (mode == MODE_GAME || mode == MODE_BALL_WAIT)
  sleepSync(display, speed); else sleepSync(display, 3);` — so while
  `mode == MODE_BONUS`, every iteration sleeps `usleep(900 µs)`
  (the same fast attract-screen rate used by intro/demo/highscore),
  **not** whatever `speed` was last set to.
- The `speed` global (written by `SetGameSpeed`/`SetUserSpeed`) is
  read in exactly two places in the entire `original/` tree — both
  inside that same `if (mode == MODE_GAME || mode == MODE_BALL_WAIT)`
  branch (`original/main.c:1747, 1877`; confirmed by grep, no other
  `.c` file declares `speed` extern). `SetGameSpeed(SLOW_SPEED)`
  calls throughout `bonus.c` therefore have **no effect on the
  bonus screen's own pacing** — they set a variable that is dead
  until the bonus sequence finishes and calls
  `SetGameSpeed(FAST_SPEED)` (`original/bonus.c:614`) just before
  returning to `MODE_GAME`, where `speed` becomes live again.
  This `SetGameSpeed(SLOW_SPEED)`-then-`FAST_SPEED` idiom is used
  identically and just as inertly in `presents.c:687`,
  `original/highscore.c:494`, `original/editor.c:396`, and `original/preview.c:170` — none of
  which are `MODE_GAME` either. It is a codebase-wide pattern, not
  something special to `bonus.c`.
- `DoBonus()` (`original/main.c:1401-1403`) is dispatched from the
  same unconditional per-iteration `switch (mode)` used for every
  other attract screen (`Introduction()`, `Presents()`, etc.) — no
  extra internal throttle.

**Consequence:** `LINE_DELAY=100` ticks (`original/bonus.c:88`)
elapses in `100 * ~0.9-1.2 ms ≈ 90-120 ms` of real time, not the
"~3 seconds" `include/bonus_system.h:30-37` claims. The per-coin/
per-bullet loop (`original/bonus.c:355-373, 464-489`) draws one item
per tick with **no per-item delay at all** beyond the tick itself —
at ~1 ms/tick a full row of coins renders in single-digit
milliseconds. The current modern constants
(`BONUS_LINE_DELAY=2000`, `BONUS_INIT_DELAY=100`,
`BONUS_STEP_DELAY=20`, each already discounted 20% from the
originally-derived 2400/120/24 per `include/bonus_system.h`
comments) are built on the `SLOW_SPEED=30 ms` assumption and are
therefore likely on the order of **20-30× slower** than the true
original wall-clock cadence for this screen.

**Calibration:** this is a static-analysis finding, not a
runtime measurement — I have not captured a frame-timestamped
video of `original/xboing`'s bonus screen to confirm the ~90-120 ms
figure directly. The code path proving `speed` is inert during
`MODE_BONUS` is unambiguous and independently verified by grep; the
resulting real-world duration follows deductively from it. Treat
the direction and rough magnitude of the deviation as high
confidence, and the exact "90-120 ms" figure as an estimate pending
an empirical capture.

**Also note:** `docs/specs/2026-06-06-bonus-renderer-rewrite.md`
records that the maintainer found the original (un-multiplied)
100-sub-frame value "flashed by in ~2 seconds, unreadable" and
lengthened it for readability. Given the analysis above, that
"unreadable, too fast" observation is consistent with an
authentically fast original — the fix may have traded fidelity for
readability without recognizing it as a deliberate redesign. Per
the applicable design-change checklist, that trade needs an explicit
ADR entry (it is currently absent) if it is to stand, or the values
need re-deriving against the true `~0.9-1.2 ms` tick.

## 7. Attract-mode frame multiplier (`ATTRACT_FRAME_MULTIPLIER=6`)

`original/main.c:1746-1749, 1876-1879` uses `sleepSync(display, 3)`
uniformly for **every** non-`MODE_GAME`/`MODE_BALL_WAIT` mode —
confirmed this includes `MODE_PRESENTS`, `MODE_INTRO`,
`MODE_INSTRUCT`, `MODE_DEMO`, `MODE_PREVIEW`, `MODE_KEYS`,
`MODE_KEYSEDIT`, `MODE_HIGHSCORE`, and `MODE_BONUS` — so a single
multiplier applied uniformly across all attract screens
(`src/game_modes.c:390`, used consistently at every
`sfx_system_update_glow(ctx->sfx, attract_frame_counter)` call site)
is structurally the right shape of fix.

The magnitude is an approximation, not a benchmark. `usleep(900 µs)`
is the *sleep* component only; `sleepSync` also calls `XSync(display,
False)` first (`original/misc.c:105`), whose round-trip cost on 1996
X11 hardware is unmeasured here. The `src/game_modes.c:381-387`
comment estimates "~1.2 ms per frame = ~833 fps," giving a true
ratio of `7500 / 1200 ≈ 6.25×` against the modern 7.5 ms tick — close
to, but not exactly, the chosen round number 6. Using the bare
`usleep` figure without `XSync` overhead gives `7500 / 900 ≈ 8.33×`.
No document in `docs/` or `docs/research/` derives 6 from a measured
benchmark of the actual original binary; it is a reasoned estimate.
At `6×` vs a true ratio anywhere in the `6.25-8.33×` range, attract
animations (glow, presents credit stages, demo block cycling) run
roughly **4-28% slower** than the original — a small, likely
imperceptible discrepancy relative to the two HIGH findings above,
but worth resolving with an actual timing capture rather than a
guess, since a bench for this already exists in the golden-screenshot
capture pipeline (`docs/TESTING.md` Layer 4).

## Ranked deviation table

| # | Finding | Severity | Original citation | Modern citation | Player-visible feel impact |
|---|---|---|---|---|---|
| 1 | `ROAMER_BLK`/`DROP_BLK` never move — sprite-only animation, no relocation | HIGH | `original/blocks.c:1364-1474` | `src/block_system.c:604-646` (no movement call site anywhere in `src/`) | Roamer/drop blocks are inert set-dressing instead of a moving threat/obstacle. Levels using these types play materially easier and look wrong. |
| 2 | ~~Bonus-screen line/step delay constants derived from a false premise~~ **RETRACTED — see Maintainer correction below** | ~~HIGH~~ NOT A BUG | `original/main.c:1746-1749`, `original/level.c:416` | `include/bonus_system.h:30-65` | Retracted: the modern pacing is *intentional* hand-tuning for readability (`docs/specs/2026-06-06-bonus-renderer-rewrite.md`), not an unintended deviation. Bead xboing-725 closed as not-a-bug. |
| 3 | BorderGlow ambient animation now runs during live `MODE_GAME`/`SDL2ST_GAME`, exclusively an attract-screen effect in the original | HIGH | `original/sfx.c:324-359` + 7 attract-only call sites, zero calls in `original/main.c:926-1478` | `src/game_modes.c:315`, `src/game_render.c:1426-1433` | A pulsing red/green border now decorates actual gameplay that a 1996 player never saw. Always-on, highly visible. |
| 4 | `DEATH_BLK` wink rhythm: uniform 500-tick 5-step cycle replaces a 1000-tick idle-then-blink rhythm; the 4th frame becomes visible | MEDIUM | `original/blocks.c:1313-1334,2395-2398` | `src/block_system.c:626-629` | The pirate-face block blinks twice as often, with no pause, and shows a frame the original never revealed. |
| 5 | `BONUS_BLK`/`BONUSX2_BLK`/`BONUSX4_BLK` coin-spin direction reversed (rate is correct) | LOW | `original/blocks.c:1204-1207` | `src/block_system.c:619-624` | Cosmetic mirror-image spin; same period, same readability. |
| 6 | `ATTRACT_FRAME_MULTIPLIER=6` is a reasoned estimate, not a measured value (true ratio ≈6.25-8.33×) | LOW | `original/misc.c:102-108`, `original/main.c:1746-1749` | `src/game_modes.c:390` | Attract-screen animations run a few to ~25% slower than the true original; not gameplay-affecting. |

**Cross-referenced, not double-counted:** paddle movement 2× too
fast (`docs/audits/2026-07-13-gameplay-logic-parity.md` section 9,
`original/main.c:962-963` vs `src/paddle_system.c`) is an
already-logged open finding from a prior audit and is not re-scored
here.

**Confirmed matches, no action needed:** ball movement/animation
frame rates (§2), EyeDude walk cadence (§4), paddle size-change
animation (§5, correctly absent in both), SHAKE/FADE/BLIND/SHATTER/
STATIC SFX constants and calling cadence (§3a), `EXTRABALL_BLK` and
block-explosion cadence (§1d, §1e).

## Recommended follow-up beads

1. **Implement `ROAMER_BLK`/`DROP_BLK` grid movement** in
   `block_system_advance_animations()` (or a new
   `block_system_advance_movement()`), porting the randomized
   timing and the eye-frame/movement-direction coupling from
   `original/blocks.c:1364-1474` verbatim. This is the highest-value
   fix — it restores a missing mechanic, not just a cadence.
2. ~~**Re-derive the bonus-screen pacing constants**~~ **RETRACTED.**
   The maintainer confirmed the bonus-screen pacing is intentional
   hand-tuning for readability (`docs/specs/2026-06-06-bonus-renderer-
   rewrite.md`), not an unintended deviation — see the Maintainer
   correction below. No action; bead xboing-725 closed as not-a-bug.
3. **Scope `BorderGlow` back to attract/menu modes only.** Either
   gate the `sfx_system_update_glow()` call and
   `game_render_border_glow()` draw to non-`SDL2ST_GAME` modes, or
   add an ADR explicitly approving the always-on gameplay glow as a
   deliberate visual enhancement (the DESIGN.md principle in
   `CLAUDE.md` requires this be a named, approved decision, not a
   silent scope creep).
4. **Fix `DEATH_BLK` cadence** to reproduce the 700-tick idle hold +
   3-tick blink rhythm and the invisible 4th frame, rather than a
   uniform 5-step rotation.
5. **(Optional, low priority) Reverse the `BONUS_BLK` family's spin
   direction** to match the original's countdown, and/or measure the
   true `ATTRACT_FRAME_MULTIPLIER` ratio with a timing capture
   instead of the current round-number estimate.

## Addendum: COUNTER_BLK slide is event-driven (N/A for cadence)

For completeness (the mission named "counter-block slide" as a cadence
item): `COUNTER_BLK`'s `counterSlide` is decremented purely on-hit in
both trees — `original/gun.c:328-362` and `src/block_system.c:1079-1106`
— with no timer / `nextFrame` gating in either. It is event-based, not
time-based, so there is no animation cadence to compare. No deviation.

## Maintainer correction (2026-07-14): bonus-screen finding RETRACTED

The "bonus screen ~20-30× too slow" finding above is a **false
positive** and is retracted. The maintainer confirms the bonus-screen
tally pacing was **hand-tuned to be usable/readable** — the current
cadence (slower than the 1996 fast attract-tick rate) is an *intentional*
readability trade-off, documented in
`docs/specs/2026-06-06-bonus-renderer-rewrite.md`, not an unintended
deviation. Restoring the original's fast rate would undo that tuning and
reproduce the "too fast, unreadable" problem already solved. The audit
mis-read the rewrite spec's "too fast" note as evidence the original
should be matched verbatim; the correct reading is that the deviation is
a deliberate, accepted design choice. Bead xboing-725 closed as not-a-bug.
