# Visual-Fidelity Audit — Pass 1: SDL2 Modern Build vs. 1996 Original

**Mission:** m-2026-05-02-017 (worker: jck, evaluator: jmf-pobox)
**Date:** 2026-05-02
**Status:** **PARTIAL** — covered ~7 of the contract's 15 areas. Missing items called out at the end. Follow-up audit pass needed.

> Note: jck's role is read-only (Read/Grep/Glob/WebFetch). Analytical work is jck's; the COO acted as scribe to file the artifact through the runtime. Same systemic gap as prior missions (xboing-c-xzt).

---

## Summary

15 distinct areas examined; 10 confirmed gaps surfaced. 4 items confirmed already correct in modern. Several contract areas were not addressed (see "Missed Areas" at end).

---

## Area 1 — DROP_BLK Appearance

**Original** (`original/blocks.c:1727-1735`): renders the green block pixmap (`greenblock`), then overlays a centered text string of the decimal hit-point count using `dataFont` in black.

**Modern** (`src/sprite_catalog.h:338-339`): `sprite_block_key(DROP_BLK)` returns `SPR_BLOCK_RED`. No text overlay applied. `game_render_blocks()` (`src/game_render.c:70-120`) draws one texture from the catalog and stops; no per-block composite path.

**Gap (confirmed):** TWO defects.

1. Wrong base sprite: green should be used, not red.
2. Missing hit-count digit overlay. Modern renderer has no compositing mechanism.

---

## Area 2 — Ball Animation Rate (Guide Direction Indicator)

**Original** (`original/ball.c:456`): guide advances every `frame % (BALL_FRAME_RATE * 8)` = 40 frames.

**Modern** (`src/ball_system.c:918`): guide advances every `frame % (BALL_FRAME_RATE * 3)` = 15 frames. **2.67x faster than original.**

**Ball spin rate** matches: `BALL_ANIM_RATE = 15` in both `original` and `include/ball_types.h:34`.

**Gap (confirmed):** Guide oscillates wrong. A waiting player sees the guide sweeping too fast — harder to aim. Constant should be `BALL_FRAME_RATE * 8`.

---

## Area 3 — Animated Cycling Blocks (BONUSX2, BONUSX4, BONUS_BLK, RANDOM_BLK)

**Original** (`original/blocks.c:1763-1773`): BONUSX2/BONUSX4/BONUS_BLK each call `RenderShape` with `bonusSlide` index into a 4-frame array. `bonusSlide` cycles 3→2→1→0 via `HandlePendingBonuses()`. RANDOM_BLK (`original/blocks.c:1700-1708`): red block + centered text `"- R -"`.

**Modern** (`src/sprite_catalog.h:350-355`): returns frame-1 keys only (`SPR_BLOCK_X2_1`, `SPR_BLOCK_X4_1`, `SPR_BLOCK_BONUS_1`). `game_render_blocks()` calls `sprite_block_key(info.block_type)` with no animation frame — all three block types permanently show frame 1. RANDOM_BLK maps to `SPR_BLOCK_BONUS_1`. The `bonus_slide` field is exposed in render-info but ignored for these types.

**Gap (confirmed, gjm pre-located):** BONUSX2/X4/BONUS never animate. RANDOM_BLK shows wrong sprite + missing "- R -" text.

---

## Area 4 — BULLET_BLK Composite Rendering

**Original** (`original/blocks.c:1680-1686`): yellow block + 4 bullet sprites composited at offsets (6,10), (15,10), (24,10), (33,10).

**Modern** (`src/sprite_catalog.h:310-311`): `sprite_block_key(BULLET_BLK)` returns `SPR_BLOCK_LOTSAMMO` — the `lotsammo.xpm` pixmap, intended for MAXAMMO_BLK only. `game_render_blocks()` draws as single texture; no composite path.

**Gap (confirmed):** BULLET_BLK and MAXAMMO_BLK are visually distinct in original. Modern conflates them. Fix: either composite path, or dedicated pre-composited asset.

---

## Area 5 — Bullet Sprite: In-Flight vs. Ammo Belt

**Original** (`original/gun.c:56`): loads `bullet.xpm` once into `bulletPixmap`. Same pixmap used for in-flight bullets, ammo belt, and bullet overlay on BULLET_BLK.

**Modern** (`src/game_render.c:355,493`): both in-flight and ammo belt use `SPR_BULLET`. **Same sprite key for both.**

**No gap.** Confirms gjm finding #4.

---

## Area 6 — Pause Overlay

**Original** (`original/main.c:1230-1231`): MODE_PAUSE case in `handleGameStates()` is empty (`break`). Game freezes — no overlay drawn. `SetGamePaused()` sets the message strip to "- Game paused -".

**Modern** (`src/game_render.c:955-976`): `SDL2ST_PAUSE` falls through `SDL2ST_GAME` case, rendering everything normally each frame. `mode_pause_update` only checks for unpause; message "- Game paused -" set on enter.

**No gap.** Both freeze physics; rendering live elements without motion is functionally equivalent.

---

## Area 7 — Instructions Screen

**Original** (`original/inst.c:213-255`): 4-state sequence (TITLE, TEXT, SPARKLE, FINISH). Text content at `original/inst.c:112-132` (20 lines). Footer at `original/inst.c:163` renders "Insert coin to start the game" at `y = PLAY_HEIGHT - 40` in `tann` color using `textFont`. Easter-egg message "Save the rainforests" at `original/inst.c:139`.

**Modern** (`src/game_render_ui.c:269-330`, `src/game_modes.c:337-360`): rendering path exists. Text driven by `intro_system.c`.

**Partial gap:** rendering path exists but text content vs original needs verification. Footer "Insert coin..." and easter-egg "Save the rainforests" likely missing.

---

## Area 8 — COUNTER_BLK Animation

**Original** (`original/blocks.c:1758-1761`): renders from `counterblock[slide]` (6 frames: base + 5 digits).

**Modern** (`src/game_render.c:92-96`): explicitly handles COUNTER_BLK using `info.counter_slide`. Maps to `SPR_BLOCK_COUNTER_1` through `SPR_BLOCK_COUNTER_5`.

**No gap.** Correctly implemented.

---

## Area 9 — DEATH_BLK Animation

**Original** (`original/blocks.c:1779-1781`): renders `death[slide]` where slide is `bonusSlide` (0-4) — 5-frame winking-pirate cycle via `HandlePendingAnimations`.

**Modern** (`src/sprite_catalog.h:69-73`, `:318-319`): defines SPR_BLOCK_DEATH_1 through 5 but `sprite_block_key(DEATH_BLK)` returns only `SPR_BLOCK_DEATH_1`. `game_render_blocks()` ignores `bonus_slide` for DEATH_BLK.

**Gap:** DEATH_BLK never animates. Winking pirate frozen at frame 1.

---

## Area 10 — EXTRABALL_BLK Animation

**Original** (`original/blocks.c:1787-1789`): renders `extraball[slide]` (2-frame flip).

**Modern** (`src/sprite_catalog.h:84-85`, `:312-313`): defines `SPR_BLOCK_EXTRABALL` and `SPR_BLOCK_EXTRABALL_2` but `sprite_block_key(EXTRABALL_BLK)` returns only frame 1.

**Gap:** EXTRABALL_BLK 2-frame animation absent.

---

## Area 11 — ROAMER_BLK Animation

**Original** (`original/blocks.c:1754-1756`): renders `roamer[slide]` (5 frames: neutral + 4 directional). `bonusSlide` set randomly via `HandlePendingAnimations`.

**Modern** (`src/sprite_catalog.h:102-106`): defines `SPR_BLOCK_ROAMER_L/R/U/D` but `sprite_block_key(ROAMER_BLK)` returns only `SPR_BLOCK_ROAMER` (neutral facing).

**Gap:** Eye-direction animation absent. Always neutral facing.

---

## Area 12 — Ball Birth / Pop Composite Rendering

**Original**: birth uses `ballBirthPixmap[0-7]` (8 frames). Pop uses same frames in reverse.

**Modern** (`src/game_render.c:142-163`): BALL_CREATE shows birth frames 1-8 via `sprite_ball_birth_key(info.slide)`. BALL_POP also uses birth frames.

**No gap.**

---

## Area 13 — Playfield Border Color (NoWalls Mode)

**Original** (`original/special.c:138-148`): `ToggleWallsOn()` changes `playWindow` border color — green for no-walls mode, red for walls-on via `XSetWindowBorder()`.

**Modern** (`src/game_render.c:268-301`): draws static red border (200, 0, 0). `game_render_border_glow()` (`:689-715`) animates glow but doesn't check `noWalls`.

**Gap:** Border doesn't turn green in walls-off mode. Visual confirmation of mode change is lost. `special_system_get_no_walls()` exists but isn't queried.

---

## Area 14 — Presents Screen Letter Positioning

**Original** (`original/presents.c:319-354`): `dists[]` = `{71, 73, 83, 41, 85, 88}`. Letters start at `x=40, y=220`, then `x += 10 + dists[i]` per letter. "II" variant draws two "I" at `y += 110`.

**Modern** (`src/game_render_ui.c:134-147`): letters at `lx = PLAY_AREA_X + 10 + i * 80` — fixed 80px stride. `y=250` (30px lower than original).

**Gap:** Wrong fixed stride; XBOING title letters will be misaligned.

---

## Area 15 — Score Digit Rendering Alignment

**Original** (`original/score.c:155-167`): `DrawOutNumber()` recursive right-to-left, drawing each digit at `x - 32` from right-anchored base. Digit size 30×40 with 32px stride.

**Modern** (`src/game_render.c:520-543`): uses `score_system_get_digit_layout()` for `x_positions[]`. Digit width = `SCORE_DIGIT_WIDTH`, height = `SCORE_WINDOW_HEIGHT`.

**Status: needs verification of `SCORE_DIGIT_WIDTH` constant. If it isn't 30 (with 32px stride), alignment is wrong.**

---

## TOP-N Prioritized Gap List

Priority = player visibility × frequency.

| # | Gap | Site | Fix scope |
|---|-----|------|-----------|
| **P1** | BONUSX2/X4/BONUS never animate (4-frame spin) | `src/game_render.c:100`, `src/sprite_catalog.h:350-355` | Add animation frame selection in `game_render_blocks()` for these types using `info.bonus_slide` |
| **P2** | DROP_BLK: red instead of green + missing hit-count digit | `src/sprite_catalog.h:338-339`, `src/game_render.c` | Return SPR_BLOCK_GREEN for DROP_BLK; add composite text rendering |
| **P3** | Guide oscillation 2.67x too fast | `src/ball_system.c:918` | Change `BALL_FRAME_RATE * 3` → `BALL_FRAME_RATE * 8` |
| **P4** | RANDOM_BLK: wrong sprite + missing "- R -" text | `src/sprite_catalog.h:346-347` | Return SPR_BLOCK_RED; add text overlay |
| **P5** | DEATH_BLK never animates (5-frame winking pirate) | `src/sprite_catalog.h:318-319`, `src/game_render.c` | Map `info.bonus_slide` (0-4) to death animation frames |
| **P6** | BULLET_BLK shows MAXAMMO sprite | `src/sprite_catalog.h:310-311` | Composite path or dedicated asset for yellow + 4 bullets |
| **P7** | EXTRABALL_BLK 2-frame animation absent | `src/sprite_catalog.h:84` | Map `info.bonus_slide % 2` to frames |
| **P8** | NoWalls border doesn't turn green | `src/game_render.c:268-282` | Check `special_system_get_no_walls()` in border render |
| **P9** | ROAMER_BLK eye direction absent | `src/sprite_catalog.h:102` | Map `info.bonus_slide` to L/R/U/D sprites |
| **P10** | Presents "XBOING" letter spacing wrong | `src/game_render_ui.c:141` | Use original `dists[]` widths and y=220 |

---

## MISSED AREAS (Audit Pass 1 did not cover)

This audit pass did not address ~8 of the contract's 15 areas. A second audit pass is needed to complete the comprehensive picture.

### Missed Areas

1. **Block explosion (KILL_BLK) animation lifecycle** — the maintainer's xboing-c-tmr report. Needs `original/blocks.c` lookup of KILL_BLK frame count + tick rate + when it plays.
2. **Level info panel layout (top of play area)** — bombs row, bullets row, level number digits. Bullet alignment from xboing-c-fuz lives here.
3. **Specials panel verification** — confirm all 8 labels render correctly per current `game_render_specials`.
4. **Paddle rendering** — size sprites (40/50/70), sticky-mode highlight, reverse-mode visual.
5. **Eyedude animation** — frame count, tick rate, fire-reaction state, death.
6. **Particle effects** — bullet tinks lifetime, screen-shake duration, paddle hit sparkle.
7. **Bonus screen** — layout, animation, score breakdown.
8. **Game over** — sequence, animation, score display.
9. **Most attract screens** — intro, demo, keys, highscore (only instructions + presents partially covered).
10. **Window geometry** — main window dims, sub-window positions, aspect.

These should be filed as a follow-up audit mission.
