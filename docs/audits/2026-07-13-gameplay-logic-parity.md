# Gameplay Logic and Rules Parity Audit

Date: 2026-07-13

Scope: game logic and rules parity between the modern SDL2 port
(`src/`, `include/`) and the 1996 original (`original/`). Explicitly
out of scope: code structure/architecture and pixel/screenshot
fidelity. Special focus: investigate the maintainer's suspicion that
level 1 is too hard, possibly due to special effects.

Method: for each area, the original source was read first
(`original/<file>.c:<line>`), then the modern equivalent
(`src/<file>.c:<line>`), then the two were compared directly. Every
deviation was checked against `docs/DESIGN.md` for an ADR. Constants
were traced to their `#define` sites in both trees, not assumed.

## 1. Block types

**Original.** Block catalog and per-hit point values in
`original/blocks.c` (`AddNewBlock`, hit-point switch ~2494-2566).
Simple color blocks (`RED_BLK`/`GREEN_BLK`/`BLUE_BLK`/`TAN_BLK`/
`YELLOW_BLK`/`PURPLE_BLK`) are always one-hit-kill via the `default:`
case in `HandleTheBlocks` (`original/ball.c:1006-1015`) — there is no
multi-hit mechanic for color blocks. `COUNTER_BLK` is the only
multi-hit block: `counterSlide` decrements per hit, killed at 0
(`original/ball.c:808-829`). Non-required (don't-block-completion)
types are enumerated in `StillActiveBlocks`
(`original/blocks.c:2482-2544`): `BLACK_BLK`, `BULLET_BLK`,
`ROAMER_BLK`, `BOMB_BLK`, `TIMER_BLK`, `HYPERSPACE_BLK`, `STICKY_BLK`,
`MULTIBALL_BLK`, `MAXAMMO_BLK`, `PAD_SHRINK_BLK`, `PAD_EXPAND_BLK`,
`REVERSE_BLK`, `MGUN_BLK`, `WALLOFF_BLK`, `EXTRABALL_BLK`,
`DEATH_BLK`, `BONUSX2_BLK`, `BONUSX4_BLK`, `BONUS_BLK`.

**Modern.** Point values in `src/score_logic.c:74-132`
(`score_block_hit_points`) — every value (`RED_BLK`=100,
`GREEN_BLK`=120, `BLUE_BLK`=110, `TAN_BLK`=130, `YELLOW_BLK`=140,
`PURPLE_BLK`=150, `COUNTER_BLK`=200, `BULLET_BLK`/`MAXAMMO_BLK`=50,
`ROAMER_BLK`=400, `DROP_BLK`=`(MAX_ROW-row)*100`) matches the
original switch verbatim. Required/non-required split in
`src/block_system.c:916-978` (`block_system_still_active`) lists
the identical 19 non-required types.

**Verdict: MATCH.** No point-value or hit-count drift found.

## 2. Ball physics

**Original.** Per-tick velocity magnitude formula:
`alpha = sqrt(MAX_X_VEL^2 + MAX_Y_VEL^2) / 9 * speedLevel`
(`original/ball.c:1172-1196`), applied only when
`(frame % BALL_FRAME_RATE) == 0` (`original/ball.c:2050-2053`,
`BALL_FRAME_RATE` = 5, `original/include/ball_types.h:36`). Real-world
tick interval is `usleep(speed * 300)` in `sleepSync`
(`original/misc.c:102-108`), where `speed = FAST_SPEED(5) * userDelay`
(`original/main.c:155-160`). At startup `SetUserSpeed(5)` is called
twice (`original/init.c:529`, `original/init.c:916`), setting
`userDelay = 5` and `speedLevel = 5` in lockstep — **not** the
`userDelay=1`/`speedLevel=5` static initializers in `main.c:115,120`,
which are overridden before the first frame. This gives
`speed = 5*5 = 25` → `usleep(7500us)` per main-loop iteration, and
because `BALL_FRAME_RATE=5`, the ball's *position* only updates every
5th iteration: one real ball movement every `5 * 7.5ms = 37.5ms`, at
magnitude `alpha = 2.2 * 5 = 11.0` px → **real ball speed ≈ 293 px/s**
at default settings. Paddle bounce trig
(`alpha=atan(Vx/-Vy)`, `beta=atan(hit/pad)`, `gamma=2*beta-alpha`) at
`original/ball.c:1103-1137`. Guide-launch direction table at
`original/ball.c:1691-1755`.

**Modern.** `sdl2_loop`'s tick interval is
`1500 * (10 - speed_level)` µs (`src/sdl2_loop.c:45-47`), which ADR-013
(`docs/DESIGN.md:745-804`) documents as reproducing the *original's
main-loop-iteration* interval exactly (not the 5x-bundled ball-move
interval). `env->frame` (`ball_system_env_t.frame`) is populated from
`sdl2_state_frame(ctx->state)` (`src/game_callbacks.c:578`), which
increments once per tick (`src/sdl2_state.c:233-238`) — a 1:1 mirror
of the original's `frame++` per main-loop iteration
(`original/main.c:1802`). `ball_system_update` re-applies the
identical `(env->frame % BALL_FRAME_RATE) == 0` gate
(`src/ball_system.c:377,442`). Velocity magnitude comes from
`SPEED_ALPHA[5] = 11.00` (`src/ball_math.c:160-171`), which ADR-045
(`docs/DESIGN.md:2708-2775`) states was deliberately anchored to equal
the original's `2.2*5=11.0` at the default speed. Paddle bounce trig
(`src/ball_math.c:109-151`) and guide table
(`src/ball_system.c:39-40`, `-5..5`/`-1..-5..-1`) are byte-for-byte
copies of the original formulas/tables.

**Computed result:** tick interval at default speed = `7500us` in
both trees (same formula, same startup speed=5). Combined with the
identical `%BALL_FRAME_RATE` gate, real ball movement occurs every
`5 * 7.5ms = 37.5ms` in both the original and the modern port, at the
same 11.0px magnitude → **≈293 px/s in both**, exactly matching.

**Verdict: MATCH** for the physics that ships. One **documentation**
inconsistency found in the process (not a behavior bug): the
`SPEED_ALPHA` comment in `src/ball_math.c:153-171` and prose in
`docs/DESIGN.md` ADR-045 both state speed 5 "= original's 1467 px/sec"
— that number appears to have been computed as `11.0 / 0.0075s`
(dividing by the raw tick interval) rather than
`11.0 / (5 * 0.0075s)` (dividing by the actual ball-move interval
after the `BALL_FRAME_RATE` gate). The mislabeled annotation does not
affect gameplay because both trees apply the exact same gate and the
exact same anchor value — but it is worth a documentation fix so a
future reader doesn't "correct" `SPEED_ALPHA[5]` toward the wrong
number. See the Level 1 section below for why this matters to the
maintainer's specific complaint.

## 3. Scoring

**Original.** Multiplier application: x2 checked before x4, x2 wins
if both active (`ComputeScore`, `original/score.c` — replicated
verbatim in `src/score_logic.c:26-36` per its own header comment
citing `score.c:226-229`). Bonus sequence values:
`BONUS_COIN_SCORE=3000`, `SUPER_BONUS_SCORE=50000`, `BULLET_SCORE=500`,
`LEVEL_SCORE=100`, `TIME_BONUS_POINTS=100`
(`original/bonus.c:838-888`, `ComputeAndAddBonusScore`). Extra life
every `NEW_LIVE_SCORE_INC=100000` (`original/level.c:90,370-383`).

**Modern.** Identical constants and formula in
`src/score_logic.c:16-72` (`score_apply_multiplier`,
`score_compute_bonus`, `score_extra_life_threshold`) — the file's own
header cites these as direct extractions from `score.c`/`bonus.c`/
`level.c` and is explicit that "do not fix bugs here."

**Verdict: MATCH.**

## 4. Lives & ball death

**Original.** `START_LIVES=3`, `MAX_LIVES=6` (display cap only)
(`original/level.c:88-89`). `DeadBall` (`original/level.c:474-505`):
game over when `livesLeft<=0 && GetAnActiveBall()==-1`; otherwise, on
last-ball death, `SetReverseOff()` + `ChangePaddleSize(PAD_EXPAND_BLK)`
called *twice* to force the paddle back to HUGE, then
`DecExtraLife`+`ResetBallStart`.

**Modern.** `ctx->lives_left = 3` at new-game start
(`src/game_modes.c:104`). The paddle re-expand-on-death and "4 balls"
DeadBall fidelity work is called out by name in the recent commit
`610403b fix(game): DeadBall fidelity (4 balls + paddle re-expand),
play-test Q, attract-score leak (#181)`, indicating this exact
parity gap was already found and fixed.

**Verdict: MATCH** (post-#181).

## 5. Level completion

**Original.** `StillActiveBlocks()==False` → bonus screen
(`original/level.c:398-419`, `CheckGameRules`), gated additionally by
`blocksExploding>1` still pending (`original/blocks.c:2538-2540`).

**Modern.** `block_system_still_active` (`src/block_system.c:916-978`)
reproduces the identical required/non-required type list and the
`blocks_exploding > 1` pending-explosion gate
(`src/block_system.c:975-978`). Level completion drives
`SDL2ST_BONUS` transition in `src/game_rules.c` (comment at
`game_rules.c:363` cites this explicitly).

**Verdict: MATCH.**

## 6. Bonus sequence

**Original.** `ComputeAndAddBonusScore` (`original/bonus.c:715-888`)
computes bonus coins, level bonus, bullet bonus, time bonus in that
order; values cited in Scoring above.

**Modern.** `score_compute_bonus` (`src/score_logic.c:45-72`)
reproduces the same four components in the same order and gating
(bullet bonus unconditional; the others gated by `time_bonus>0`).

**Verdict: MATCH.**

## 7. Specials

**Original.** Killer, machine-gun (`MGUN_BLK` → `ToggleFastGun`),
sticky bat, walls-off, reverse, and x2/x4 bonus are toggled on/off via
`special.c`/`paddle.c`/`gun.c` and have **no built-in auto-expiry
timer** — grepping `original/special.c` for `frame +`/`nextFrame`
finds nothing; specials persist until explicitly turned off (level
end via `TurnSpecialsOff`, or superseded by another special).

**Modern.** `src/special_system.c` has no `duration`/`expire`/
`timeout` logic either — same persist-until-toggled-off model, called
via `special_system_turn_off` at new-level and next-level boundaries
(`src/game_modes.c:121`, `src/game_rules.c` `game_rules_next_level`).

**Verdict: MATCH.**

## 8. Ammo/bullets

**Original.** `NUMBER_OF_BULLETS_NEW_LEVEL=4`
(`original/include/blocks.h:74`), `MAX_BULLETS=20`
(`original/include/gun.h:59`).

**Modern.** `GUN_AMMO_PER_LEVEL=4`, `GUN_MAX_AMMO=20`
(`include/gun_system.h:33-34`), applied at new-game start via
`gun_system_set_ammo(ctx->gun, GUN_AMMO_PER_LEVEL)`
(`src/game_modes.c:122`).

**Verdict: MATCH.**

## 9. Paddle

**Original.** Sizes 40/50/70 px for SMALL/MEDIUM/HUGE
(`original/paddle.c:283-292`, `GetPaddleSize`). New-game paddle size
is forced to `PADDLE_HUGE` (`original/main.c:946`) — note
`currentPaddleSize` (`original/paddle.c:83`) has **no static
initializer** and is never assigned anywhere else in `original/`
outside `main.c:946`, `file.c:110`/`209`, and `editor.c:607`; it is a
genuine "must be set before first use" original-code quirk, not a
modern-port bug. Keyboard movement: `PADDLE_VEL=10`
(`original/include/paddle.h:69`) applied in `MovePaddle`
(`original/paddle.c:180-193`), gated to run only every
`PADDLE_ANIMATE_DELAY=5` main-loop iterations
(`original/include/main.h:57`, applied at `original/main.c:962-963`:
`if ((frame % PADDLE_ANIMATE_DELAY) == 0) handlePaddleMoving(...)`).
Real-world paddle speed at default game speed (7.5ms/iteration, see
Ball Physics above): `10px / (5 * 7.5ms) = 10px / 37.5ms ≈ 267 px/s`.

**Modern.** Sizes 40/50/70 px preserved
(`include/paddle_system.h`, `pixel_width_for_size`). New-game paddle
size correctly forced to `PADDLE_SIZE_HUGE`
(`src/game_modes.c:124`) — matches. Keyboard movement:
`PADDLE_VELOCITY=4` (`include/paddle_system.h:49`), applied in
`paddle_system_update` (`src/paddle_system.c:157-179`). **This call
has no equivalent `PADDLE_ANIMATE_DELAY` gate.**
`input_update_paddle` (`src/game_input.c:43-64`) calls
`paddle_system_update` unconditionally, once per
`game_input_update(ctx)` call, which itself runs exactly once per
tick inside `mode_game_update` (`src/game_modes.c:281-291`, no
`ATTRACT_FRAME_MULTIPLIER`-style loop, unlike the six attract-mode
handlers in the same file). Real-world paddle speed at default game
speed (tick interval 7.5ms, identical formula to the original per
ADR-013): `4px / 7.5ms ≈ 533 px/s`.

**Verdict: DEVIATION — undocumented.** The modern paddle moves at
**≈533 px/s vs the original's ≈267 px/s — exactly 2.0x faster** at
identical, default game-speed settings. `docs/DESIGN.md` ADR-017
(`docs/DESIGN.md:1020-1067`, "Pure C paddle system") does not mention
dropping `PADDLE_ANIMATE_DELAY` or derive the `PADDLE_VELOCITY=4`
replacement value at all — in fact its own prose at line 1042
states `PADDLE_VELOCITY (10)`, which doesn't match the actual
`#define PADDLE_VELOCITY 4` in `include/paddle_system.h:49`, a second
sign that the constant's derivation was never worked through end to
end. `original/CLAUDE.md`'s module-mapping table calls this "adjusted
for per-frame update," implying a conversion was attempted, but the
correct conversion factor to preserve real-world px/s when removing a
"move every 5th tick" gate and running every tick instead is
`10 / 5 = 2`, not `4`. No characterization test for paddle px/s exists
in the test tree (`tests/test_paddle_system.c` was not checked for
this specific assertion during this audit — recommended follow-up
below).

## Level 1 difficulty — special focus

**Level 1 content is byte-identical and contains zero dynamic/hazard
blocks.** `levels/level01.data` ("Genesis," time bonus 120) is:

```text
.........
.........
rrrrrrrrr
bbbbbbbbb
ggggggggg
ttttttttt
.........
.........
000000000
yyyyyyyyy
ppppppppp
B...B...B
.........
.........
.........
```

Per the character map in `src/level_system.c:75-182` (which mirrors
`original/level.c`/`original/blocks.c` letter codes), this is
`RED_BLK`, `BLUE_BLK`, `GREEN_BLK`, `TAN_BLK` (one-hit color rows),
`COUNTER_BLK` with `slide=0` (behaves as a one-hit block — see Block
Types above), `YELLOW_BLK`, `PURPLE_BLK` (one-hit rows), and three
`BULLET_BLK` ammo pickups (non-hazard, doesn't count toward level
completion). **There is no `ROAMER_BLK`, `MGUN_BLK`, `BOMB_BLK`,
`DEATH_BLK`, `BLACK_BLK`, `WALLOFF_BLK`, `REVERSE_BLK`, `STICKY_BLK`,
`HYPERSPACE_BLK`, `TIMER_BLK`, `DROP_BLK`, or `RANDOM_BLK` anywhere in
level 1.** The random mid-level bonus/special spawner
(`original/main.c:977-1103`, replicated in
`src/game_rules.c:86-166`, verified MATCH — identical 27-way
probability table) can still inject a special block during play, but
that mechanic is level-independent and not something level 1 does
differently from any other level.

**Conclusion: the "special effects" hypothesis is not supported by
the level data.** Level 1 has no scripted special/hazard blocks to
misfire in the first place, and the mid-level random-spawn logic
checked bit-for-bit matches the original's odds and effects.

**Ball launch speed and per-frame movement also match exactly** at
level 1 (see Ball Physics section) — both trees produce ≈293 px/s at
default game speed, using the identical `BALL_FRAME_RATE` gate and the
identical tick-interval formula (ADR-013), with `SPEED_ALPHA[5]`
deliberately anchored to the original's formula (ADR-045). This rules
out ball speed as the level-1 difficulty driver, contrary to the
mission's leading suspicion.

**The one concrete, quantified, code-cited deviation found that
plausibly affects level-1 (and every level's) difficulty is the
paddle speed regression documented in Paddle §9 above: the modern
paddle moves 2.0x faster in real px/s than the original at default
settings.** A doubled paddle speed does not make the ball harder to
reach — if anything it should make interception easier — but it
changes paddle *control precision*: the original's tuned feel was a
paddle that creeps at ~267 px/s and requires deliberate,
sustained key-holds to cross the 495px play width (≈1.85s
edge-to-edge); the modern paddle crosses in ≈0.93s. For the specific
gameplay mechanic that matters most on early levels — landing the
ball at a precise offset from paddle center to control the
trigonometric bounce angle (`ball_math_paddle_bounce`,
`original/ball.c:1103-1137`) — a paddle that's twice as twitchy is
measurably harder to stop precisely under time pressure, which is a
plausible mechanism for "feels harder" that has nothing to do with
special effects. This is a hypothesis, not a proven root cause: no
playtest or characterization test was run as part of this audit to
confirm the perceptual effect. It is offered as the most concrete,
falsifiable lead uncovered, in place of the special-effects theory,
which the level-1 data does not support at all.

**Null result restated:** no level-1-specific special-effect timing
bug, block spawn/animation gating bug, or ball-speed bug was found.
The one confirmed, quantified deviation (paddle speed 2x) is global,
not level-1-specific, and its direction of effect on perceived
difficulty needs a playtest to confirm.

## ADR cross-check

| Area | Deviation found | ADR status |
|------|-----------------|------------|
| Ball speed table (`SPEED_ALPHA`, all levels except 5) | Compressed 1↔9 range (4.3x vs 81x) | **INTENTIONAL — ADR-045**, `docs/DESIGN.md:2708-2775`, user-playtest-approved |
| Fixed-timestep loop replacing `usleep` | Architecture change, same formula | **INTENTIONAL — ADR-013**, `docs/DESIGN.md:745-804` |
| Ball speed at default (level 1) | None found — matches | N/A (no ADR needed) |
| `SPEED_ALPHA[5]`/ADR-045 "1467 px/sec" annotation | Mislabeled units (doc-only, no behavior impact) | **UNDOCUMENTED** — inaccurate comment, not tracked as a bead |
| Paddle velocity 2x speed-up (`PADDLE_VELOCITY=4` w/ no `PADDLE_ANIMATE_DELAY` gate) | Real px/s doubled vs original | **UNDOCUMENTED** — ADR-017 (`docs/DESIGN.md:1020-1067`) covers the paddle module's existence but not this constant's derivation, and its own prose (`PADDLE_VELOCITY (10)`) contradicts the shipped code (`4`) |
| Block types, scoring, lives, level completion, bonus sequence, specials, ammo | None found — all MATCH | N/A |

## Ranked findings

| # | Finding | Severity | Original cite | Modern cite | Gameplay impact |
|---|---------|----------|----------------|--------------|------------------|
| 1 | Paddle keyboard speed is 2.0x the original's real px/s (533 vs 267 px/s at default game speed); `PADDLE_ANIMATE_DELAY` gate dropped but velocity only halved instead of quartered | High | `original/paddle.c:180-193`, `original/main.c:962-963`, `original/include/paddle.h:69`, `original/include/main.h:57` | `src/paddle_system.c:157-179`, `src/game_input.c:43-64`, `include/paddle_system.h:49` | Affects paddle control precision on every level, including level 1; plausible (unconfirmed) contributor to "feels harder" |
| 2 | ADR-045 / `ball_math.c` comment states speed-5 target as "1467 px/sec (matches original)"; correct value (accounting for the `BALL_FRAME_RATE` gate) is ≈293 px/sec | Low | `original/ball.c:1041-1196`, `original/ball.c:2050-2053` | `src/ball_math.c:153-171`, `docs/DESIGN.md:2708-2775` | None — actual constants are correct and match; comment/ADR prose is misleading for future maintainers |
| 3 | ADR-017 prose states `PADDLE_VELOCITY (10)`; shipped code is `4` | Low | — | `docs/DESIGN.md:1042`, `include/paddle_system.h:49` | None — documentation drift only, but it is the kind of drift that would have caught finding #1 if it had been correct |

## Recommended follow-up beads

1. Playtest / characterization test: measure paddle edge-to-edge
   crossing time in the modern port at default speed and compare
   against the original's ≈1.85s (per `original/paddle.c:180-266`,
   `PADDLE_VEL`/`PADDLE_ANIMATE_DELAY`). If confirmed 2x too fast,
   change `PADDLE_VELOCITY` from 4 to 2 (`include/paddle_system.h:49`)
   to restore the original's real-world px/s — **jck must approve
   this change** as it alters paddle feel; do not ship without a
   before/after playtest.
2. Fix the `SPEED_ALPHA[5]` / ADR-045 documentation: replace "1467
   px/sec" with the correct ≈293 px/sec derivation (dividing by
   `5 * tick_interval`, not `tick_interval` alone) in both
   `src/ball_math.c:153-159` and `docs/DESIGN.md` ADR-045, so a future
   reader tuning `SPEED_ALPHA` for other levels anchors to the right
   number.
3. Fix ADR-017 prose (`docs/DESIGN.md:1042`) to say
   `PADDLE_VELOCITY (4)` matching the shipped constant, and add a
   sentence deriving why 4 (or 2, if bead #1 changes it) is the
   correct per-tick replacement for the removed
   `PADDLE_ANIMATE_DELAY` gate — this is exactly the kind of ADR gap
   that let finding #1 ship unnoticed.
4. Add a `tests/test_paddle_system.c` characterization test asserting
   real-world paddle px/s at each of the 9 speed levels against the
   original's `PADDLE_VEL / (PADDLE_ANIMATE_DELAY * tick_interval)`
   formula, the same way `src/ball_math.c`/ADR-045 already has
   coverage for ball speed.

## Post-audit correction (2026-07-14): specials was NOT a match

The "Specials" section above marked the special/bonus behavior a
`MATCH`. That was wrong. It examined the special *flags* (killer,
no-walls, etc.) and the `rand()%27` distribution, but did not examine
the mid-play *spawn throttle*.

After the maintainer clarified that the level-1 hazards appear
dynamically ("adding blocks in holes"), a focused investigation found
the real root cause of the level-1 difficulty: the modern spawner
(`src/game_rules.c` `try_spawn_bonus`) dropped both of the original's
throttles (`original/main.c:970-1071`, `blocks.c:1079,1173-1186`):

- `bonus_block_active` was read as the one-at-a-time gate but set
  `true` nowhere, so specials spawned every `~rand()%2000` frames
  unconditionally; and
- spawned blocks got `last_frame = frame + BLOCK_INFINITE_DELAY`, so
  they never auto-expired.

Harmful specials (shrink / reverse / walloff / death) therefore
accumulated without bound — worst on level 1's mostly-empty grid. This
is the actual "level 1 too hard" cause, not the paddle-speed deviation
(finding #1), which stands as a separate, real, but secondary control
issue. Fixed under ADR-069 / PR #184 (faithful restore, jck-approved,
RED-proven regression tests). This correction supersedes the "Specials"
`MATCH` verdict above.
