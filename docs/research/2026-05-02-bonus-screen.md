# Bonus Screen Original Behavior: Research Notes

**Date:** 2026-05-02
**Author:** jck (research mission m-2026-05-03-001)
**Scope:** Basket 5 visual-fidelity audit — vf8 (line-by-line reveal) + tp4 (coin/bullet animation)
**Sources:** `original/bonus.c`, `original/include/bonus.h`, `include/bonus_system.h`, `src/bonus_system.c`, `src/game_render_ui.c:510-568`

---

## 1. Constants

All constants are defined in `original/bonus.c:86-100` as internal macros (not in the header):

| Constant | Value | Definition site |
|---|---|---|
| `GAP` | 30 | `original/bonus.c:86` |
| `LINE_DELAY` | 100 | `original/bonus.c:88` |
| `SAVE_LEVEL` | 5 | `original/bonus.c:89` |
| `BONUS_COIN_SCORE` | 3000 | `original/bonus.c:91` |
| `SUPER_BONUS_SCORE` | 50000 | `original/bonus.c:92` |
| `BULLET_SCORE` | 500 | `original/bonus.c:93` |
| `LEVEL_SCORE` | 100 | `original/bonus.c:94` |
| `TIME_BONUS` | 100 | `original/bonus.c:95` |

`LINE_DELAY = 100` frames is the universal inter-state wait interval. Every state transition (except the initial BONUS_TEXT→BONUS_SCORE wait of 5 frames, and the BONUS_END_TEXT→BONUS_FINISH wait of 200 frames) uses exactly `frame + LINE_DELAY` as the wait target (`original/bonus.c:275, 300, 322, 347, 378, 426, 455, 485, 525, 585`).

`MAX_BONUS = 8` is defined in `original/include/bonus.h:65`. When `numBonus > MAX_BONUS`, the super bonus path fires instead of per-coin animation (`original/bonus.c:330`).

Sound effect names used in the bonus sequence:

- `"bonus"` — played once per coin drawn (`original/bonus.c:366`)
- `"key"` — played once per bullet drawn (`original/bonus.c:473`)
- `"supbons"` — super bonus trigger (`original/bonus.c:334`)
- `"Doh1"` — no coins collected (`original/bonus.c:315`)
- `"Doh2"` — no level bonus (timer ran out) (`original/bonus.c:421`)
- `"Doh3"` — no bullet bonus (`original/bonus.c:450`)
- `"Doh4"` — timer ran out for coins (`original/bonus.c:293`), also for time bonus (`original/bonus.c:520`)
- `"applause"` — end text state (`original/bonus.c:600`)

---

## 2. State Machine

The full state sequence is defined by the `BonusStates` enum at `original/include/bonus.h:71-83`:

```text
BONUS_TEXT → BONUS_SCORE → BONUS_BONUS → BONUS_LEVEL →
BONUS_BULLET → BONUS_TIME → BONUS_HSCORE → BONUS_END_TEXT →
[BONUS_WAIT as intermediary] → BONUS_FINISH
```

`BONUS_WAIT` is not a visible content state; it is an interstitial timer state that transitions to `waitMode` when `frame == waitingFrame` (`original/bonus.c:637`).

Per-state behavior as dispatched by `DoBonus()` at `original/bonus.c:640-697`:

**BONUS_TEXT** (`DrawTitleText`, `original/bonus.c:231-261`):

- Clears message window, sets "- Bonus Tally -"
- Draws "- Level N -" centered using `titleFont` at `ypos` (initialized to 180 in `ResetBonus()`, `original/bonus.c:773`)
- Draws "Press space for next level" at `PLAY_HEIGHT - 12` using `textFont` in `tann` color
- Conditionally draws floppy-disk save indicator every `SAVE_LEVEL` levels
- Advances `ypos += titleFont->ascent + GAP`
- Sets wait: `BONUS_SCORE` at `frame + 5`

**BONUS_SCORE** (`DoScore`, `original/bonus.c:263-278`):

- Draws "Congratulations on finishing this level." centered in `white` using `textFont`
- Advances `ypos += 35 + GAP`
- Sets wait: `BONUS_BONUS` at `frame + LINE_DELAY`

**BONUS_BONUS** (`DoBonuses`, `original/bonus.c:280-389`):

- Called repeatedly each frame while coins remain; does NOT set a LINE_DELAY wait until coins are drained
- Draws one `BONUS_BLK` sprite per call, then decrements `numBonus`
- Transitions to `BONUS_LEVEL` after the last coin is drawn
- See Section 3 for full animation semantics

**BONUS_LEVEL** (`DoLevel`, `original/bonus.c:391-429`):

- Instant one-shot: draws level bonus text and sets wait `BONUS_BULLET` at `frame + LINE_DELAY`
- Text: `"Level bonus - level N x 100 = M points"` in `yellow`
- Adds `level_adjusted * LEVEL_SCORE` to `bonusScore`

**BONUS_BULLET** (`DoBullets`, `original/bonus.c:431-490`):

- Called repeatedly each frame while bullets remain (same per-tick pattern as coins)
- Draws one bullet sprite per call, then decrements bullet count via `DeleteABullet()`
- See Section 4 for full animation semantics

**BONUS_TIME** (`DoTimeBonus`, `original/bonus.c:492-526`):

- Instant one-shot: draws time bonus text and sets wait `BONUS_HSCORE` at `frame + LINE_DELAY`
- See Section 5 for details

**BONUS_HSCORE** (`DoHighScore`, `original/bonus.c:528-586`):

- Instant one-shot: draws ranking text and sets wait `BONUS_END_TEXT` at `frame + LINE_DELAY`
- See Section 6 for details

**BONUS_END_TEXT** (`DoEndText`, `original/bonus.c:588-603`):

- Instant one-shot: draws "Prepare for level N+1", plays `"applause"`, sets wait `BONUS_FINISH` at `frame + LINE_DELAY * 2` (200 frames)

**BONUS_FINISH** (`DoFinish`, `original/bonus.c:605-624`):

- Increments `level`, sets up stage, maps play window, returns to `MODE_GAME`
- Resets `BonusState = BONUS_TEXT`

The `ypos` accumulator begins at 180 (`original/bonus.c:773`) and grows downward as each state appends its line. Each state uses `textFont->ascent + GAP` or `textFont->ascent + GAP * 1.5` or `textFont->ascent + GAP / 2` as the increment. Font used: `titleFont` for BONUS_TEXT level header; `textFont` for all other states.

Colors by state: `red` for level header, `white` for congratulations, `blue` for coin/bullet/timer-void messages, `yellow` for level/time bonus lines, `red` for high score ranking, `yellow` for end text.

---

## 3. DoBonuses() Per-Coin Animation

Source: `original/bonus.c:280-389`.

The function is called once per frame tick while `BonusState == BONUS_BONUS`. Each call performs at most one coin draw. The state machine does NOT arm a LINE_DELAY timer between coins; instead it stays in `BONUS_BONUS` (via `BONUS_WAIT` only at the very end) and processes one coin per invocation.

**Timer-void path** (`original/bonus.c:288-303`): if `GetLevelTimeBonus() == 0`, draws "Bonus coins void - Timer ran out!" in `blue`, plays `"Doh4"`, advances `ypos`, and immediately arms `BONUS_LEVEL` wait (`frame + LINE_DELAY`). No coin sprites drawn.

**No-coins path** (`original/bonus.c:312-328`): if `numBonus == 0` on first entry, draws "Sorry, no bonus coins collected." in `blue`, plays `"Doh1"`, arms `BONUS_LEVEL` wait. No coin sprites drawn.

**Super-bonus path** (`original/bonus.c:330-351`): if `numBonus > MAX_BONUS` on first entry, draws "Super Bonus - N" in `blue` using `titleFont`, adds `SUPER_BONUS_SCORE` to `bonusScore`, plays `"supbons"`, arms `BONUS_LEVEL` wait. No individual coin sprites.

**Normal path** (the per-coin animation loop body, `original/bonus.c:353-388`):

On `firstTime`, computes `maxLen = (numBonus * 27) + (10 * numBonus) + 5`. This is the full width of the coin row in pixels. Each coin sprite slot is `27 + 10 = 37` pixels wide.

Each frame thereafter:

1. Computes remaining row width: `plen = (numBonus * 27) + (10 * numBonus)` (`original/bonus.c:358`)
2. Computes x: `x = ((PLAY_WIDTH + MAIN_WIDTH) / 2 + maxLen / 2) - plen` (`original/bonus.c:359`)
3. Draws `BONUS_BLK` sprite at `(x, ypos)` via `DrawTheBlock()` (`original/bonus.c:362`)
4. Plays `"bonus"` sound at volume 50 (`original/bonus.c:365-367`)
5. Adds `ComputeScore(BONUS_COIN_SCORE)` (i.e., 3000 × multiplier) to `bonusScore` and refreshes display (`original/bonus.c:369-370`)
6. Calls `DecNumberBonus()` to decrement `numBonus` (`original/bonus.c:373`)
7. If `numBonus <= 0`: arms `BONUS_LEVEL` wait at `frame + LINE_DELAY`, resets `numBonus`, advances `ypos += textFont->ascent + GAP * 1.5`, resets `firstTime` (`original/bonus.c:375-388`)

The x-position formula places coins left-to-right: because `plen` shrinks as `numBonus` decrements, x grows rightward each call. Effectively the coins fill in from left to right across a centred row.

The sprite drawn is `BONUS_BLK` — the bonus coin block type. y-position is `ypos` at the time `DoBonuses()` first fires (set by `DoScore` advancing ypos before transitioning). The y does not change during the coin animation; all coins share one y row.

Effective coin stride: each coin occupies `37 pixels` (`27 + 10`), and each successive call places the next coin 37 pixels to the right of the previous.

Score increment per coin: `ComputeScore(3000)`. `ComputeScore` applies the current multiplier (×1, ×2, ×4 depending on remaining blocks). So at ×2 multiplier the display increments 6000 per coin.

---

## 4. DoBullets() Per-Bullet Animation

Source: `original/bonus.c:431-490`.

Same structural pattern as `DoBonuses()`. Called once per frame tick while `BonusState == BONUS_BULLET`.

**No-bullets path** (`original/bonus.c:442-459`): if `GetNumberBullets() == 0` on first entry, draws "You have used all your bullets. No bonus!" in `blue`, plays `"Doh3"`, arms `BONUS_TIME` wait. No bullet sprites.

**Normal path** (per-bullet loop body, `original/bonus.c:461-489`):

On `firstTime`: computes `maxLen = (GetNumberBullets() * 7) + (3 * GetNumberBullets())`. Each bullet slot is `7 + 3 = 10` pixels wide.

Each frame:

1. Computes remaining row width: `plen = (GetNumberBullets() * 7) + (3 * GetNumberBullets())` (`original/bonus.c:466`)
2. Computes x: `x = ((PLAY_WIDTH + MAIN_WIDTH) / 2 + maxLen / 2) - plen` (`original/bonus.c:467`)
3. Draws bullet sprite at `(x, ypos)` via `DrawTheBullet()` (`original/bonus.c:469`)
4. Plays `"key"` sound at volume 50 (`original/bonus.c:472-474`)
5. Adds `ComputeScore(BULLET_SCORE)` (500 × multiplier) to `bonusScore` and refreshes display (`original/bonus.c:476-477`)
6. Calls `DeleteABullet()` to decrement bullet count (`original/bonus.c:480`)
7. If `GetNumberBullets() == 0`: arms `BONUS_TIME` wait at `frame + LINE_DELAY`, advances `ypos += textFont->ascent + GAP / 2`, resets `firstTime` (`original/bonus.c:483-489`)

Bullet sprite: drawn by `DrawTheBullet()` — the gun ammo sprite. Bullet sprite slot width is 10 pixels (stride `7 + 3 = 10`). Coins are much wider (37 pixels each); bullets are compact.

Sound name: `"key"` (not `"ammo"` — it reuses the keyboard-press sound). Volume 50.

Score per bullet: `ComputeScore(500)`.

---

## 5. Time Bonus Animation

Source: `original/bonus.c:492-526`.

`DoTimeBonus()` is a single-shot function, not a per-tick loop. It fires once when `BonusState == BONUS_TIME`, computes the entire time bonus in one call, and transitions immediately.

If `secs > 0`:

- Draws `"Time bonus - N seconds x 100 = M points"` in `yellow` using `textFont` (`original/bonus.c:504-506`)
- Adds `ComputeScore(TIME_BONUS * secs)` to `bonusScore` (`original/bonus.c:509-510`)
- Refreshes score display

If `secs == 0`:

- Draws `"No time bonus - not quick enough!"` in `yellow`
- Plays `"Doh4"` (`original/bonus.c:519-521`)

Advances `ypos += textFont->ascent + GAP / 2` and arms `BONUS_HSCORE` wait at `frame + LINE_DELAY` (`original/bonus.c:524-525`).

The time bonus has no per-second animation. All `secs * 100` points are awarded in a single frame. This contrasts with coin and bullet animations where one item is processed per frame.

---

## 6. High Score and End Text States

**BONUS_HSCORE** (`original/bonus.c:528-586`):

Calls `GetHighScoreRanking(score)` to obtain the player's current rank (`original/bonus.c:536`). Single-shot rendering:

- If `myrank == 1`: "You are ranked 1st. Well done!"
- If `myrank == 2..10`: constructs ordinal suffix (nd/rd/th) and prints "You are currently ranked Nth."
- If `myrank == 0` (unranked): "Keep on trying!"

Text color: `red`. Font: `textFont`. Y: `ypos` at time of call (`original/bonus.c:582`).

Advances `ypos += textFont->ascent + GAP / 2`, arms `BONUS_END_TEXT` wait at `frame + LINE_DELAY` (`original/bonus.c:583-585`).

**BONUS_END_TEXT** (`original/bonus.c:588-603`):

Draws "Prepare for level N+1" in `yellow` using `textFont` at `ypos` (`original/bonus.c:595-597`). Plays `"applause"` sound (`original/bonus.c:599-601`). Arms `BONUS_FINISH` wait at `frame + LINE_DELAY * 2` (200-frame hold, the longest pause in the sequence) (`original/bonus.c:602`). Single-shot; no animation.

---

## 7. Modern Gap Reproduction

### game_render_ui.c:510-568 — threshold rendering

The modern `game_render_bonus()` function uses `state >= BONUS_STATE_X` comparisons to decide which lines to render (`src/game_render_ui.c:532,536,545,553,557,561`). This means: once the state machine has reached state X, line X (and all prior lines) are rendered on every frame. All reached lines are simultaneously visible at full opacity from the moment their threshold is crossed.

The original has no such re-draw loop. Each state function calls `DrawShadowCentredText()` exactly once, writing to the X11 window's backing store. The text persists because X11 backing store retains it; the game never redraws previous lines. The visual result is equivalent (lines accumulate top-to-bottom), but the mechanism differs.

### BONUS_STATE_BONUS — static coin string (line 540)

`src/game_render_ui.c:536-543`:

```c
if (state >= BONUS_STATE_BONUS)
{
    int coins = bonus_system_get_coins(ctx->bonus);
    char buf[64];
    snprintf(buf, sizeof(buf), "Bonus Coins: %d x 3000 = %d", coins, coins * 3000);
    sdl2_font_draw_shadow_centred(..., buf, ...);
}
```

This renders a static text string showing the final coin count. It does not draw individual `BONUS_BLK` sprites, does not animate them left-to-right, and does not call any per-frame coin decrement. The string is redrawn every frame at the same position with the same value once `BONUS_STATE_BONUS` is reached.

In the original, during `DoBonuses()`, `numBonus` is decremented from N down to 0 over N frame ticks, and one `BONUS_BLK` sprite appears per tick. The screen accumulates N coin sprites visually. The modern code shows one text line with the pre-computed total.

### BONUS_STATE_BULLET — placeholder text (line 554)

`src/game_render_ui.c:553-555`:

```c
if (state >= BONUS_STATE_BULLET)
    sdl2_font_draw_shadow_centred(..., "Bullet Bonus...", ...);
```

No bullet sprites drawn, no per-bullet animation, no `on_bullet_consumed` callback visible in the render path. The original `DoBullets()` draws one `DrawTheBullet()` sprite per frame tick across a centred row.

### bonus_system_dec_coins — wired but not called from renderer

`bonus_system_dec_coins()` is declared at `include/bonus_system.h:156` and implemented at `src/bonus_system.c:415-421`. The internal `do_bonuses()` function at `src/bonus_system.c:237-249` does decrement `ctx->coin_count` during the state machine update, and fires the `"bonus"` sound via callback.

However `game_render_bonus()` calls `bonus_system_get_coins()` (`src/game_render_ui.c:538`) which reads the live `coin_count` field. Because `do_bonuses()` in `bonus_system_update()` decrements `coin_count` each frame during `BONUS_STATE_BONUS`, the count shown in the rendered string changes each frame during the animation. The string is therefore not permanently static — it counts down — but it is a count string, not a sprite row. A player would see "Bonus Coins: 5 x 3000 = 15000" decrement to "0 x 3000 = 0" over 5 frames rather than watching 5 coin sprites appear one by one.

The `do_bullets()` handler at `src/bonus_system.c:265-298` does fire `on_bullet_consumed` callbacks and decrement `env.bullet_count` per frame. The render side does not query `env.bullet_count` through any public accessor; the placeholder string "Bullet Bonus..." does not reflect this decrement.

---

## 8. Behavior Preservation Requirements

A correct modern implementation of the bonus screen MUST preserve:

1. **State sequence order.** The 9 content states (`TEXT → SCORE → BONUS → LEVEL → BULLET → TIME → HSCORE → END_TEXT → FINISH`) must fire in this exact order. `original/include/bonus.h:72-82`.

2. **LINE_DELAY inter-state hold.** Each state transition (except TEXT→SCORE which uses 5 frames) must hold for exactly 100 frames before advancing. `original/bonus.c:88,275`.

3. **END_TEXT hold is double.** The BONUS_END_TEXT→BONUS_FINISH wait is `LINE_DELAY * 2 = 200` frames, giving the player time to register the "Prepare for level N+1" text before the next level loads. `original/bonus.c:602`.

4. **Lines accumulate top-to-bottom, persist.** Each state appends one text line below the previous. No line is erased once drawn. All prior lines remain visible while later states render.

5. **Per-coin animation, one sprite per frame tick.** During BONUS_BONUS, one `BONUS_BLK` sprite appears per frame tick (not per LINE_DELAY interval — the state loops at game speed until all coins are consumed). Each coin appears at an incrementally wider x-position in a centred row. All coins share a single y-row. `original/bonus.c:358-373`.

6. **Score increments with each coin.** Each coin drawn adds `ComputeScore(3000)` to the displayed score (not the real score, which was already committed). The real-time score display visibly climbs during the animation. `original/bonus.c:369-370`.

7. **Sound plays per coin.** `"bonus"` sound fires once per coin, at volume 50. The audio cadence is part of the animation's feel. `original/bonus.c:365-367`.

8. **Per-bullet animation, one sprite per frame tick.** Same structural contract as coins: one bullet sprite per frame, incrementally wider x, centred row, shared y, `"key"` sound at volume 50, `ComputeScore(500)` score increment per bullet. `original/bonus.c:469-480`.

9. **Coin sprite is BONUS_BLK, bullet sprite is the gun ammo image.** These are block-type sprites, not text glyphs. The player sees a graphical row of icons, not a number. `original/bonus.c:362,469`.

10. **Super bonus path replaces per-coin path.** When `numBonus > MAX_BONUS (8)`, no individual coins are animated; a single "Super Bonus - N" line appears, `SUPER_BONUS_SCORE (50000)` is added, and the sequence advances. `original/bonus.c:330-351`.

11. **Timer-void path skips coins and level bonus.** When `GetLevelTimeBonus() == 0`, BONUS_BONUS shows a void message and BONUS_LEVEL shows a no-bonus message; neither awards points for those categories. BULLET and TIME bonus still apply. `original/bonus.c:288-303, 414-421`.

12. **Time bonus is single-shot (not per-second animation).** All `secs * 100` points are awarded and displayed in a single frame. No per-second decrement animation. `original/bonus.c:492-526`.

13. **Score multiplier applies.** All bonus amounts pass through `ComputeScore()`, which applies the current game multiplier (×1 base, ×2 at 25 remaining blocks, ×4 at 15 remaining blocks). A player finishing with few remaining blocks sees larger per-coin increments. `original/bonus.c:337, 369, 411, 476, 509`.

14. **Coin row geometry (x-stride = 37 pixels, bullet stride = 10 pixels).** The original centred-row calculation places coins 37 px apart and bullets 10 px apart. These are tuned values that determine how many coins/bullets fit on screen before overflow. `original/bonus.c:354,358,462,466`.

15. **Space bar skips to finish.** The original's space-bar handler during bonus mode calls `SetBonusWait(BONUS_FINISH, frame + LINE_DELAY)`. The modern `bonus_system_skip()` at `src/bonus_system.c:392-401` matches this semantics.
