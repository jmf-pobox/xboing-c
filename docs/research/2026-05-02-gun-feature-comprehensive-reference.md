# XBoing Gun Feature: Comprehensive Reference Audit

**Date:** 2026-05-02
**Author:** jck (Justin C. Kibell)
**Mission:** m-2026-05-02-013
**Purpose:** Canonical reference for an implement mission to reconstruct the gun feature end-to-end in the SDL2 port.

> Note: jck's role is read-only (Read/Grep/Glob/WebFetch). The analytical work in this document is jck's; the COO acted as scribe to file the artifact through the runtime. Same systemic gap as prior missions (xboing-c-xzt).

---

## Topic 1: Key Binding and Input Dispatch

### Original

`original/keys.c:213` — keys screen displays `<k> = Shoot`. K is the canonical shoot key.

`original/main.c:490-494` — game key handler:

```c
case XK_k:
case XK_K:
    /* Shoot a bullet if available */
    if (ActivateWaitingBall(display, playWindow) == False)
        shootBullet(display, playWindow);
    break;
```

Both lowercase and uppercase K. Logic is **dual-use**: K first tries to activate a waiting ball; only if no ball is waiting does it shoot. On first press when ball is on paddle, K launches the ball. On subsequent presses, K fires.

`original/main.c:357-366` — mouse buttons (Button1/2/3) fire in MODE_GAME with the same dual logic.

### Modern current state

`src/game_input.c:298-301` — K wired via `SDL2I_SHOOT`:

```c
if (sdl2_input_just_pressed(ctx->input, SDL2I_SHOOT))
{
    gun_system_env_t genv = game_callbacks_gun_env(ctx);
    gun_system_shoot(ctx->gun, &genv);
}
```

`gun_system_shoot` calls `is_ball_waiting` callback and returns 0 if ball is waiting. **But it does NOT call `ball_system_activate_waiting`.** The original used K for both launch and shoot. The modern code uses SPACE/START for launch and K only for shooting.

### Gaps

- K-activates-ball-if-waiting behavior absent. A 1995 player would expect K to launch.
- Mouse button fire absent. Mouse is read for paddle positioning but not for shooting.

### Testable scenario

State: ball in BALL_READY (on paddle). Action: `gun_system_shoot`. Expected: no bullet fired, `is_ball_waiting` returns nonzero, AND ball activates. Currently the no-fire side passes; the activate-on-K side is the gap.

---

## Topic 2: Block Hit Handling — BULLET_BLK and MAXAMMO_BLK

### Original

`original/blocks.c:1547-1548` — score awarded first.

`original/blocks.c:1581-1593` — handlers:

```c
case BULLET_BLK:
    SetCurrentMessage(display, messWindow, "More ammunition, cool!", True);
    for (i = 0; i < NUMBER_OF_BULLETS_NEW_LEVEL; i++)
        AddABullet(display);
    break;

case MAXAMMO_BLK:
    SetCurrentMessage(display, messWindow, "Unlimited bullets!", True);
    SetUnlimitedBullets(True);
    SetNumberBullets(MAX_BULLETS + 1);
    DisplayLevelInfo(display, levelWindow, level);
    break;
```

`NUMBER_OF_BULLETS_NEW_LEVEL = 4` (`original/include/blocks.h:74`). BULLET_BLK adds 4 bullets via `AddABullet` × 4. MAXAMMO_BLK sets unlimited + ammo to 21 (MAX_BULLETS+1).

`original/blocks.c:762-780` — `PlaySoundForBlock`: `"ammo"` at volume 30 for BULLET_BLK, volume 70 for MAXAMMO_BLK.

`original/blocks.c:2333-2335` — both award `hitPoints = 50`.

### Modern current state

`gun_cb_on_block_hit` at `src/game_callbacks.c:280-297` awards score (correct, 50 pts), clears the block. **No case for BULLET_BLK or MAXAMMO_BLK** — falls through to default which just clears.

### Gaps

- BULLET_BLK: no `gun_system_add_ammo` call — should add 4
- MAXAMMO_BLK: no `gun_system_set_unlimited` or `gun_system_set_ammo(GUN_MAX_AMMO+1)`
- Neither sets a message ("More ammunition, cool!" / "Unlimited bullets!")
- Neither plays the `"ammo"` sound

### Testable scenarios

1. State: BULLET_BLK at (3,4), ammo=2, unlimited=0. Action: `gun_cb_on_block_hit(3,4,ud)`. Expected: ammo=6, "ammo" played. Current: ammo=2 (no change). GAP.
2. State: MAXAMMO_BLK at (2,1), ammo=0, unlimited=0. Action: `gun_cb_on_block_hit(2,1,ud)`. Expected: unlimited=1, ammo=GUN_MAX_AMMO+1. Current: unchanged. GAP.

---

## Topic 3: Ammo Belt UI — ReDrawBulletsLeft

### Original

`original/level.c:304-334` — three functions handle the ammo belt in `levelWindow`:

- `AddABullet(display)` — increments count, draws one bullet sprite at computed X (y=43)
- `DeleteABullet(display)` — erases one sprite, decrements
- `ReDrawBulletsLeft(display)` — redraws all current ammo, called from `DisplayLevelInfo`

X position formula: `bulletPos = 192 - (GetNumberBullets() * 9)`. Bullets right-aligned, 9 px apart, ending at x=192.

`original/level.c:213-233` — `DisplayLevelInfo` calls `ReDrawBulletsLeft` on every level load, life loss, score update.

### Modern current state

No `game_render_ammo` or ammo-belt rendering. `game_render_lives` (`src/game_render.c:420`) draws life balls + level digits but no ammo. `src/game_render_ui.c` handles attract screens and has no ammo belt either.

### Gap

Ammo belt is not rendered anywhere. Players have no visual feedback for ammo count. `gun_system_get_ammo()` returns the correct value, but no render call reads it.

### Insert point

`src/game_render.c:game_render_lives` (around line 460, after level-number digits). Level window is at `LEVEL_AREA_X=284, LEVEL_AREA_Y=5` (matches original `levelWindow: x=284, y=5, w=286, h=52`). Ammo belt should render at `y = LEVEL_AREA_Y + 43`.

### Testable scenario

State: `gun_system_get_ammo == 4`, unlimited=0. Frame rendered. Expected: 4 bullet sprites visible in level panel. Gap confirmed.

---

## Topic 4: In-Flight Bullet Rendering

### Original

`original/gun.c:237-386` — `UpdateBullet` moves bullets by `BULLET_DY = -7` per update. `DrawBullet` erases at old position, draws at new. Bullet 7×16 px centered at (xpos, ypos).

`original/gun.c:634-642` — `HandleBulletMode` runs every frame; bullets move every `(frame % BULLET_FRAME_RATE) == 3` (`BULLET_FRAME_RATE = 3`).

### Modern current state

`src/gun_system.c:284-292` — `gun_system_update` correctly throttles at `GUN_BULLET_FRAME_RATE = 3`. Matches original.

`src/game_render.c:349-410` — `game_render_bullets` renders active bullets at `PLAY_AREA_X + x - GUN_BULLET_WC, PLAY_AREA_Y + y - GUN_BULLET_HC` using `SPR_BULLET`, with Y-axis interpolation. Tinks at `PLAY_AREA_Y + 2` matches `original/gun.c:187`.

**Status: implemented and correct.**

---

## Topic 5: Ammo Lifecycle — Level Transitions and Game Over

### Original

`original/file.c:115-117` — `SetupStage` (each level load):

```c
SetUnlimitedBullets(False);
ClearBullets();
SetNumberBullets(NUMBER_OF_BULLETS_NEW_LEVEL);
```

Canonical sequence: unlimited off, in-flight bullets cleared, ammo set to 4.

`original/special.c:84-95` — `TurnSpecialsOff` does NOT reset ammo directly; it calls `ToggleFastGun(display, False)`.

`original/file.c:220-223` — save/load restores `saveGame.numBullets`.

`original/level.c:453-471` — `EndTheGame` calls `TurnSpecialsOff` (disables fastGun); ammo counter left as-is on game over.

### Modern current state

`src/game_rules.c:179-232` — `game_rules_next_level`:

```c
gun_system_clear(ctx->gun);              /* clears in-flight bullets/tinks, not ammo */
special_system_turn_off(ctx->special);   /* disables SPECIAL_FAST_GUN */
gun_system_set_ammo(ctx->gun, GUN_AMMO_PER_LEVEL);  /* = 4, matches original */
```

`GUN_AMMO_PER_LEVEL = 4` (`include/gun_system.h:34`).

### Gap

`gun_system_set_unlimited(0)` is NOT called at level start. If the player hit a MAXAMMO_BLK and advanced, unlimited stays active across levels. Original explicitly calls `SetUnlimitedBullets(False)` at `original/file.c:115` on every level load.

### Testable scenario

State: level=5, unlimited=1, ammo=21. `game_rules_next_level` called. Expected: unlimited=0, ammo=4. Current: ammo=4 (correct), unlimited=1 (gap).

---

## Topic 6: Ammo at Ball Death — AddABullet × 2 in ResetBallStart

### Original

`original/ball.c:1803-1805` — `ResetBallStart`:

```c
/* Add 2 bullets every ball death or creation as it happens */
AddABullet(display);
AddABullet(display);
```

Every ball reset (death or creation) grants +2 ammo. Consolation mechanic: losing a ball gives ammo.

### Modern current state

`src/game_rules.c:266-268` — `game_rules_ball_died`:

```c
ball_system_env_t env = game_callbacks_ball_env(ctx);
ball_system_reset_start(ctx->ball, &env);
```

No `gun_system_add_ammo` call. The +2 ammo grant is absent.

### Testable scenario

State: ammo=1, ball dies, `game_rules_ball_died` called. Expected: ammo=3 (1+2). Current: ammo=1. GAP.

---

## Topic 7: FAST_GUN (MGUN_BLK) Behavior

### Original

`original/special.c:78` — `int fastGun;` global.
`original/special.c:127-131` — `ToggleFastGun(state)` sets `fastGun = state`.
`original/special.c:184-188` — `DrawSpecials` renders "FastGun" yellow when active, white otherwise.
`original/special.c:88` — `TurnSpecialsOff` calls `ToggleFastGun(False)`.

`original/gun.c:618-628` — `ResetBulletStart` when `fastGun == True`:

```c
if (fastGun == True)
{
    size = GetPaddleSize();
    (void)StartABullet(display, window, paddlePos - (size / 3));
    status = StartABullet(display, window, paddlePos + (size / 3));
}
else
    status = StartABullet(display, window, paddlePos);
```

Dual-fire at ±(paddle_size/3) from center.

`original/blocks.c MGUN_BLK case` — ball-hit MGUN_BLK ONLY calls `ToggleFastGun(True)`. **Ammo unchanged. Unlimited NOT granted.**

### Modern current state

`src/game_callbacks.c:139-143` — ball-hit MGUN_BLK case:

```c
case MGUN_BLK:
    block_system_clear(ctx->block, row, col);
    special_system_set(ctx->special, SPECIAL_FAST_GUN, 1);
    gun_system_set_unlimited(ctx->gun, 1);  /* WRONG */
    return 0;
```

### Gap — wrong assumption

Modern code grants unlimited on MGUN pickup. Original does NOT. Unlimited is exclusively a MAXAMMO_BLK effect. Modern MGUN_BLK should ONLY enable dual-fire.

`src/gun_system.c:319-325` — `gun_system_shoot` correctly uses `env->fast_gun` for dual-fire. That part works.

### Testable scenario

State: MGUN_BLK hit by ball, ammo=4, unlimited=0. Expected: SPECIAL_FAST_GUN active, unlimited=0, ammo=4. Current: unlimited=1 (wrong). GAP.

---

## Topic 8: Mouse Click Fire

### Original

`original/main.c:343-373` — `handleMouseButtons`: all three mouse buttons fire in MODE_GAME with dual-use logic. Standard play style on Sun/SGI workstations.

### Modern current state

`src/game_input.c` — mouse position read for paddle. No mouse click fire.

### Gap

Mouse fire absent. Quality-of-life regression on desktop Linux.

---

## Topic 9: Save/Load Game — numBullets Persistence

### Original

`original/file.c:268-269` — saves `saveGame.numBullets`.
`original/file.c:223` — restores via `SetNumberBullets`.
`unlimitedBullets` and `fastGun` are NOT saved (they reset at `LoadSavedGame` via `SetupStage` + `TurnSpecialsOff`).

### Modern current state

`src/game_input.c:86-88` — `input_save_game`: `.num_bullets = gun_system_get_ammo(ctx->gun)`.
`src/game_input.c:119` — `input_load_game`: `gun_system_set_ammo(ctx->gun, data.num_bullets)`.

`unlimited` and `SPECIAL_FAST_GUN` not saved (correct — original didn't save them).

**Status: correct.** Save/load is faithful.

---

## Topic 10: Bullet Kills Ball — Score Behavior

### Original

`original/gun.c:276-289`:

```c
ClearBallNow(display, window, j);
if (noSound == False)
    playSoundFile("ballshot", 50);
```

`ClearBallNow` removes the ball. **No score awarded.** Bullet-kills-ball yields zero score.

### Modern current state

`src/game_callbacks.c:320-325`:

```c
static void gun_cb_on_ball_hit(int ball_index, void *ud)
{
    (void)ball_index;
    (void)ud;
    /* Ball activation on bullet hit — simplified for now */
}
```

No score awarded — correct.
`src/gun_system.c:172-185` — bullet cleared, `on_ball_hit` fires, "ballshot" plays. Sound matches original.

### Gap — behavioral

`gun_cb_on_ball_hit` is a no-op stub. In the original a bullet killing a ball triggers `ClearBallNow` → ball-death sequence. The modern callback doesn't change ball state. Bullet hitting an active ball should transition ball to BALL_POP, which fires `BALL_EVT_DIED` → `game_rules_ball_died`.

### Testable scenario

State: active ball at (100,200), bullet at (100,205). `gun_cb_check_ball_hit` returns ball_index=0. Expected: ball state → BALL_POP. Current: no-op. GAP.

---

## Topic 11: Multi-Hit Special Blocks (SHOTS_TO_KILL_SPECIAL)

### Original

`original/gun.c:325-340` — bullet hits a special block (REVERSE_BLK, MGUN_BLK, STICKY_BLK, WALLOFF_BLK, MULTIBALL_BLK, PAD_EXPAND_BLK, PAD_SHRINK_BLK, DEATH_BLK):

```c
blockP->counterSlide--;
if (blockP->counterSlide == 0)
    DrawBlock(display, window, row, col, KILL_BLK);
```

Special blocks require `SHOTS_TO_KILL_SPECIAL` (3) bullet hits before dying. `counterSlide` is initialized at `SHOTS_TO_KILL_SPECIAL` and decrements per hit.

`original/gun.c:341-350`:

```c
case HYPERSPACE_BLK:
    DrawBlock(display, window, row, col, HYPERSPACE_BLK);
    break;
case BLACK_BLK:
    DrawBlock(display, window, row, col, BLACK_BLK);
    break;
```

HYPERSPACE_BLK and BLACK_BLK absorb bullets — redrawn, not killed.

### Modern current state

`src/game_callbacks.c:280-297` — `gun_cb_on_block_hit` calls `block_system_clear` unconditionally for all block types. No multi-hit logic. A single bullet kills any block instantly.

### Gaps

- Multi-hit special blocks killed by single bullet (should require 3 hits via counterSlide)
- HYPERSPACE_BLK should absorb (modern clears — wrong)
- BLACK_BLK should absorb (modern clears — wrong)
- COUNTER_BLK should decrement counter (modern clears — wrong)

### Testable scenarios

1. HYPERSPACE_BLK at (2,3); bullet hits. Expected: occupied. Current: cleared. GAP.
2. BLACK_BLK at (1,5); bullet hits. Expected: occupied. Current: cleared. GAP.
3. MGUN_BLK at (3,4) with counterSlide=3; three bullet hits. Expected: dies on 3rd. Current: dies on 1st. GAP.
4. COUNTER_BLK at (2,2) with counterSlide=3; bullet hit. Expected: counter decrements to 2. Current: cleared. GAP.

---

## Summary — 13 Gaps by Site

| # | Gap | File(s) | Original citation |
|---|-----|---------|-------------------|
| 1 | BULLET_BLK hit adds no ammo (+4 missing) | `src/game_callbacks.c gun_cb_on_block_hit` | `original/blocks.c:1581-1585` |
| 2 | MAXAMMO_BLK hit doesn't set unlimited or ammo=MAX+1 | `src/game_callbacks.c gun_cb_on_block_hit` | `original/blocks.c:1588-1593` |
| 3 | BULLET/MAXAMMO_BLK hit plays no sound | `src/game_callbacks.c gun_cb_on_block_hit` | `original/blocks.c:775-780` |
| 4 | BULLET/MAXAMMO_BLK hit sets no message | `src/game_callbacks.c gun_cb_on_block_hit` | `original/blocks.c:1582,1589` |
| 5 | Ammo belt UI not rendered | `src/game_render.c game_render_lives` | `original/level.c:304-334` |
| 6 | +2 ammo on ball death/reset missing | `src/game_rules.c game_rules_ball_died` | `original/ball.c:1803-1805` |
| 7 | unlimited not reset on level start | `src/game_rules.c game_rules_next_level` | `original/file.c:115` |
| 8 | MGUN_BLK ball-hit wrongly sets unlimited | `src/game_callbacks.c ball_cb_on_block_hit` | `original/blocks.c MGUN_BLK case (no unlimited)` |
| 9 | Bullet-kills-ball: on_ball_hit no-op stub | `src/game_callbacks.c gun_cb_on_ball_hit` | `original/gun.c:284` |
| 10 | Multi-hit special blocks killed by single bullet | `src/game_callbacks.c gun_cb_on_block_hit` | `original/gun.c:325-340` |
| 11 | HYPERSPACE_BLK and BLACK_BLK should absorb bullets | `src/game_callbacks.c gun_cb_on_block_hit` | `original/gun.c:341-350` |
| 12 | Mouse click fire not wired | `src/game_input.c` | `original/main.c:358-366` |
| 13 | K key does not activate waiting ball | `src/game_input.c` | `original/main.c:493` |

---

## Key Constants Reference

| Constant | Value | Location | Purpose |
|----------|-------|----------|---------|
| `NUMBER_OF_BULLETS_NEW_LEVEL` | 4 | `original/include/blocks.h:74` | Ammo per level start and per BULLET_BLK hit |
| `MAX_BULLETS` | 20 | `original/include/gun.h:59` | Hard cap |
| `GUN_AMMO_PER_LEVEL` | 4 | `include/gun_system.h:34` | Modern equivalent |
| `GUN_MAX_AMMO` | 20 | `include/gun_system.h:33` | Modern equivalent |
| `BULLET_DY` | -7 | `original/gun.c:80` | Upward velocity |
| `BULLET_FRAME_RATE` | 3 | `original/gun.c:99` | Update every 3rd frame |
| `MAX_MOVING_BULLETS` | 40 | `original/gun.c:102` | Bullet slots |
| `SHOTS_TO_KILL_SPECIAL` | 3 | (likely `original/include/blocks.h`) | Multi-hit special block cap |

---

## Sound Cues Reference

| Cue | Volume | Trigger | Original citation |
|-----|--------|---------|-------------------|
| `"shotgun"` | 50 | Successful fire | `original/gun.c:503` |
| `"click"` | 99 | Fire with ammo=0 | `original/gun.c:510-511` |
| `"ballshot"` | 50 | Bullet kills ball | `original/gun.c:288` |
| `"shoot"` | 80 | Tink (bullet hits top wall) | `original/gun.c:215` |
| `"ammo"` | 30 | BULLET_BLK destroyed | `original/blocks.c:776` |
| `"ammo"` | 70 | MAXAMMO_BLK destroyed | `original/blocks.c:779` |

SDL2 note: `sdl2_audio_play` does not currently use volume parameters. Volume differences (30 vs 70) need implementing if volume control is added.

---

## Files Relevant to Implement Mission

**Modern (files to modify):**

- `src/game_callbacks.c` — `gun_cb_on_block_hit`, `ball_cb_on_block_hit`, `gun_cb_on_ball_hit`
- `src/game_rules.c` — `game_rules_next_level`, `game_rules_ball_died`
- `src/game_render.c` — `game_render_lives` (add ammo belt)
- `src/game_input.c` — K-activates-ball, mouse-click fire
- Tests for each module under `tests/`

**No changes needed:**

- `src/gun_system.c` — core physics correct
- `include/gun_system.h` — constants correct
- `src/score_logic.c` — block point values correct (50 for BULLET/MAXAMMO)
