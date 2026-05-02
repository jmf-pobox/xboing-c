# Visual-Fidelity Audit — Pass 2: Areas Missed by Pass 1

**Mission:** m-2026-05-02-019 (worker: jck, evaluator: jmf-pobox)
**Date:** 2026-05-02
**Status:** Complete — covers 13 pass-2 areas (KILL_BLK explosion, level-info layout, specials column verification, paddle, eyedude, particles, bonus, game over, attract, window geometry, TIMER_BLK overlay, score multiplier, BLACKHIT_BLK).

> Note: jck's role is read-only. Analytical work is jck's; the COO acted as scribe to file the artifact through the runtime.

---

## Summary

13 areas examined; 6 confirmed gaps (in addition to pass 1's 10) + 11 confirmed no-gap items. Combined priority list at end re-ranks all 16 visible-element gaps from both passes.

---

## Area 1 — Block Explosion (KILL_BLK / explode state machine)

**Original** (`original/blocks.c ExplodeBlocksPending` + `DrawBlock(KILL_BLK)`): blocks transition through KILL_BLK frames before being cleared. The explode sequence runs over multiple ticks, advancing through animation frames at a fixed delay before the cell is freed.

**Modern** (`src/block_system.c:32-35`, `include/block_system.h:56`, `src/game_callbacks.c:90`): explosion state-machine fields (`exploding`, `explode_start_frame`, `explode_next_frame`, `explode_slide`) are scaffolded but **never written after initialization**. `BLOCK_EXPLODE_DELAY=10` is defined but unused. `game_callbacks.c:90` calls `block_system_clear()` directly on hit, bypassing any explosion. `ExplodeBlocksPending()` has no equivalent.

**Gap (P1, structural — addresses xboing-c-tmr):** Implement explosion lifecycle: on block hit, set `exploding=1`, advance `explode_slide` per `BLOCK_EXPLODE_DELAY`, render KILL_BLK frame from slide, finally clear when slide reaches end.

---

## Area 2 — Level Info Panel Layout (Lives + Level Number)

**Original** (`original/level.c:210, 224`): lives display is right-anchored at x=175 within levelWindow with stride=30. Level number digits are right-anchored at x=260 within levelWindow (absolute x=544 in mainWindow).

**Modern** (`src/game_render.c:435, 450-458`): lives are left-anchored at x=289+i*18 with stride=18. Level number at x=289 or x=311.

**Gaps (P4, P5):** Lives anchor + stride wrong (right→left, 30→18). Level number position wrong.

---

## Area 3 — Specials Panel Column Layout

**Original** (`original/special.c DrawSpecials`): 4 columns at x={5, 55, 110, 155} within specialWindow at absolute (292, 655).

**Modern** (`src/game_render.c game_render_specials` post-PR #97): same column offsets via `special_system_get_labels` returning `col_x ∈ {5, 55, 110, 155}`. All 8 labels render correctly.

**No gap.** Pass 1's x4-cropping hypothesis was implausible (25px clearance in 180px panel) and is dismissed. Specials panel is correct.

---

## Area 4 — Paddle Rendering

**Original**: paddle sizes 40/50/70 px via three sprite variants. Sticky and reverse modes have no separate visual indicator on the paddle itself — those states show in the specials panel only.

**Modern**: matching size sprites in sprite_catalog. No sticky/reverse paddle-specific overlay.

**No gap.** Both rely on the specials panel for state indication.

---

## Area 5 — Eyedude Animation (Walk + Death)

**Original** (`original/eyedude.c`): walk animation has standard frame count and tick rate. Death sequence cycles death frames.

**Modern** (`src/eyedude_system.c`): matches frame counts and rates exactly.

**No gap.**

---

## Area 6 — Particle Effects (Tinks)

**Original** (`original/gun.c:215`): `DrawTheTink(display, window, xpos, 2)` where `window` is `playWindow`. Tinks render at y=2 within play area.

**Modern** (`src/game_render.c:393`): tinks at `PLAY_AREA_Y + 2 = 62`, matching original position.

**No gap.** Tink lifetime, position, dimensions all match.

---

## Area 7 — Bonus Screen Animation

**Original** (`original/bonus.c:270-602`): state machine drives a sequence with `LINE_DELAY=100` between states (BONUS_SCORE → BONUS_BONUS → BONUS_LEVEL → BONUS_BULLET → BONUS_TIME → BONUS_HSCORE → BONUS_END_TEXT → BONUS_FINISH). `DoBonuses()` (`bonus.c:280-389`) draws one BONUS_BLK sprite per call at incrementally wider positions, decrementing `numBonus`. `DoBullets()` (`bonus.c:431-490`) draws one bullet sprite per call. Each animated state takes `count + 6` × `LINE_DELAY` frames minimum.

**Modern** (`src/game_render_ui.c:510-568`): `game_render_bonus()` renders all states visible simultaneously based on `state >= BONUS_STATE_*` thresholds. "Bonus Coins" shows a count string `"Bonus Coins: N x 3000 = M"` — no animated coin sprites. Bullet bonus and time bonus are placeholder text strings. Each state advances on time, not coin-by-coin.

**Gaps (P6, P10):**

1. Line-by-line reveal absent. Lines above current state appear simultaneously rather than at LINE_DELAY=100 intervals.
2. Coin-by-coin BONUS_BLK animation absent.
3. Bullet-by-bullet depletion animation absent (likely subsumed by P10).

---

## Area 8 — Game Over Sequence

**Original** (`original/level.c:452-471 EndTheGame`): sets message, plays sound, calls `UpdateHighScores()`, transitions to MODE_HIGHSCORE. No animated game-over screen — direct transition.

**Modern** (`src/game_rules.c:249-258`): sets `game_active=false`, plays "game_over" sound, sets "GAME OVER" message, transitions to SDL2ST_HIGHSCORE.

**No gap.** Both transition directly.

---

## Area 9 — Window Geometry

**Original** (`original/stage.c:CreateAllWindows`) vs **modern** (`src/sdl2_regions.c`): all six sub-window positions match exactly.

**No gap.**

---

## Area 10 — TIMER_BLK Time-Remaining Overlay

**Original** (`original/blocks.c:1668-1670`): `case TIMER_BLK: RenderShape(...timeblock...)` — renders one clock sprite. **No text overlay**, no time-remaining digit drawn on the block face.

**Modern** (`src/sprite_catalog.h:344-345`): `TIMER_BLK → SPR_BLOCK_CLOCK`. One sprite, no composite.

**No gap.** gjm's hypothesis about time-remaining overlay was incorrect.

---

## Area 11 — Score Multiplier Display

**Original** (`original/score.c:193-203 DisplayScore`): no multiplier indicator in score window. x2/x4 state shown only via specials panel labels.

**Modern** (`src/game_render.c:520-543`): renders score digits only. Multiplier in specials panel.

**No gap.**

---

## Area 12 — BLACK_BLK Hit Flash (BLACKHIT_BLK Transition)

**Original** (`original/ball.c:986-1004`): the BLACKHIT_BLK branch is unreachable in normal play. After the first hit, `nextFrame = frame + INFINITE_DELAY` (per `original/blocks.c:2304`), so `frame <= nextFrame` is always True and the block goes to KILL_BLK explosion path. The BLACKHIT branch can never fire on the first hit, and after the first hit `exploding=True` causes the outer check at `original/ball.c:804` to skip the whole switch. **BLACKHIT_BLK is effectively dead code.**

**Modern** (`src/game_callbacks.c:170-173`): default case calls `block_system_clear()` immediately. BLACK_BLK cleared instantly on hit.

**No gap.** Modern matches the effective original behavior (dead code in original = no BLACKHIT_BLK display).

---

## Area 13 — Eyedude Deveyes (Devil-Eyes SFX)

**Original** (`original/stage.c:126`): `blinkslides[]` 26-step sequence for the devil-eyes blind-effect transition between levels. Triggered by SFX_BLIND/FADE area transitions, not bullet misses.

**Modern** (`src/sfx_system.c:549-556`): `sfx_system_start_deveyes()` correctly wired to the 26-step sequence.

**No gap.** Devil-eyes is a level-transition effect, not a fire-reaction state. Correctly implemented.

---

## Area 14 — Eyedude Bullet Collision (Newly Surfaced)

**Original** (`original/gun.c` + eyedude logic): bullet hitting eyedude triggers EYEDUDE_DIE state, awards 10000 points.

**Modern** (`src/game_callbacks.c:369-381`): `gun_cb_check_eyedude_hit` and `gun_cb_on_eyedude_hit` are stubs — return 0 / no-op.

**Gap (P11, NEW):** Eyedude cannot be killed by bullets. This is xboing-m0y from the original bead queue.

---

## Combined Priority Gap List (Pass 1 + Pass 2)

Re-ranked by player visibility × frequency.

| # | Gap | Source | Bead |
|---|-----|--------|------|
| **P1** | Block explosion animation absent | `src/game_callbacks.c:90`, `src/block_system.c:32-35` | xboing-c-tmr (existing — needs update) |
| **P2** | BONUSX2/X4/BONUS animations frozen | `src/sprite_catalog.h:350-355` | xboing-c-ejn |
| **P3** | DROP_BLK red instead of green + missing digit | `src/sprite_catalog.h:338-339` | xboing-c-t96 |
| **P4** | Lives display anchor + stride wrong | `src/game_render.c:435` | NEW |
| **P5** | Level number position wrong | `src/game_render.c:450-458` | NEW |
| **P6** | Bonus screen simultaneous vs line-by-line reveal | `src/game_render_ui.c:532-567` | NEW |
| **P7** | Guide oscillation 2.67× too fast | `src/ball_system.c:918` | xboing-c-xny |
| **P8** | RANDOM_BLK wrong sprite + missing "- R -" | `src/sprite_catalog.h:346-347` | xboing-c-nk9 |
| **P9** | DEATH_BLK never animates | `src/sprite_catalog.h:318-319` | xboing-c-qe2 |
| **P10** | Bonus coin-by-coin animation absent | `src/game_render_ui.c:538-542` | NEW |
| **P11** | Eyedude bullet collision stubbed | `src/game_callbacks.c:369-381` | xboing-m0y (existing — needs update) |
| **P12** | BULLET_BLK shows MAXAMMO sprite | `src/sprite_catalog.h:310-311` | xboing-c-oz1 |
| **P13** | EXTRABALL_BLK 2-frame animation absent | `src/sprite_catalog.h:84` | xboing-c-ax9 |
| **P14** | NoWalls border doesn't turn green | `src/game_render.c:268-282` | xboing-c-7ow |
| **P15** | ROAMER_BLK eye direction absent | `src/sprite_catalog.h:102-106` | xboing-c-agi |
| **P16** | Presents "XBOING" letter spacing wrong | `src/game_render_ui.c:141` | xboing-c-clv |

xboing-c-fuz (bullet alignment) is subsumed — the bullet strip's position is downstream of the level-info panel layout (P4, P5). Fix those, the bullets land correctly.

---

## Confirmed No-Gap Items (Pass 2)

| Item | Verification |
|------|--------------|
| Specials panel column layout | All 8 labels at x={5, 55, 110, 155}, both rows |
| Paddle sizes / sticky / reverse overlay | No paddle-specific overlay in either |
| TIMER_BLK time overlay | No overlay in original |
| Score multiplier display | Specials panel only in both |
| BLACKHIT_BLK | Dead code in original |
| Eyedude walk/death animation | Frame counts and rates match |
| Devil-eyes SFX trigger | Level-transition effect; correctly wired |
| Bullet tink lifetime + position | Match exactly |
| Score digit layout (30×40 / stride 32) | Match |
| Game over sequence | Direct transition in both |
| Window geometry (all 6 sub-windows) | Match exactly |
