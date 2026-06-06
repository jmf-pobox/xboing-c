# Research: bonus capture via real CheckGameRules entry path

**Date:** 2026-06-06
**Author:** jck (research mission for xboing-c-z0n)
**Source tree:** original/ (1996 baseline, post-cleanup of PR #143 infrastructure)

## Summary

The real bonus-screen entry path goes through `CheckGameRules`
(`original/level.c:398-419`), which fires from `handleGameMode`
(`original/main.c:1140-1141`) on every tick where `mode == MODE_GAME`.
It sets `mode = MODE_BONUS` and calls `SetupBonusScreen` only when
`StillActiveBlocks()` returns False — i.e., the grid holds no killable
blocks and no explosions are in progress. The minimum state delta for
capture is: call `MapAllWindows` first (HUD subwindows are NOT mapped
at process start), set scenario values (`score`, `level`, `numBonus`,
time bonus, bullet count) before `SetupStage`, set `gameActive = True`
to bypass first-game init, then let one tick of `MODE_GAME` dispatch —
`CheckGameRules` fires and transitions to `MODE_BONUS` automatically.
`noSound` defaults to `True` so no audio flag is needed. The
`Togglex2Bonus`/`Togglex4Bonus`/`DrawSpecials` calls in `CheckGameRules`
are NOT redundant to add in the helper — `CheckGameRules` already does
them.

## Findings

### 1. HUD subwindow lifecycle

All subwindows — `scoreWindow`, `levelWindow`, `messWindow`,
`specialWindow`, `timeWindow`, `playWindow`, `bufferWindow`,
`inputWindow`, `blockWindow`, `typeWindow` — are created by
`CreateAllWindows` (`original/stage.c:218-369`), called from
`InitialiseGame` at `original/init.c:993`.

**None of them are mapped at creation time.** `XCreateSimpleWindow`
creates windows in the unmapped state by default. After
`CreateAllWindows`, only `mainWindow` is explicitly mapped via
`XMapWindow(display, mainWindow)` at `original/init.c:1058`.

The HUD subwindows are mapped exactly once: by `MapAllWindows`
(`original/stage.c:388-399`), which maps `specialWindow`, `timeWindow`,
`messWindow`, `playWindow`, `levelWindow`, `scoreWindow`, and then
`mainWindow` (redundant but harmless). `MapAllWindows` is called from
`DoFinish` in `original/presents.c:561` — the `PRESENT_FINISH` handler
that fires when the presents/splash screen completes.

In a fresh process started with `-visual-capture bonus` and no user
interaction:

- After `InitialiseGame`: `mainWindow` is mapped, all HUD subwindows
  are unmapped.
- The initial mode is `MODE_PRESENTS` (`original/main.c:1563`).
- `MapAllWindows` has not fired yet — it only fires when the presents
  screen completes.

**Consequence:** Any force-entry path that skips the presents cycle
MUST call `MapAllWindows` explicitly before entering `MODE_GAME`.
Without it, `scoreWindow`, `levelWindow`, `specialWindow`, `timeWindow`,
`messWindow` are invisible — exactly the symptom of PR #143's synthetic
capture approach.

`DisplayScore(display, scoreWindow, score)` at `original/file.c:123`
and `DisplayLevelInfo(display, levelWindow, level)` at
`original/file.c:124` (called by `SetupStage`) draw into those windows
but the draws are discarded if the windows are unmapped.

### 2. Mode entry side effects — what SetupBonusScreen reads and assumes

`SetupBonusScreen` (`original/bonus.c:209-228`) does:

1. `ClearMainWindow(display, window)` — sets `mainWindow` background
   to `spacePixmap`, clears it. No assumptions beyond valid GC and
   pixmap state (set up in `InitialiseGame`).
2. `DrawBallBorder` — draws animated ball sprites around the border.
   Reads ball pixmaps (initialized at `InitialiseGame`). No gameplay
   state dependency.
3. `DrawSmallIntroTitle` — draws the title XPM. No gameplay state
   dependency.
4. `ResetBonus()` (`original/bonus.c:764-775`) — this is the critical
   one:
   - Sets `BonusState = BONUS_TEXT`
   - Sets `firstTime = True`
   - Sets `bonusScore = score` — reads the global `score` variable
   - Calls `ComputeAndAddBonusScore()` which reads `numBonus`,
     `GetLevelTimeBonus()`, `level`, `GetStartingLevel()`,
     `GetNumberBullets()` to pre-add bonus points to `score`
   - Sets `ypos = 180`
   - Calls `SetGameSpeed(FAST_SPEED)`
5. `WindowFadeEffect(display, playWindow, ...)` in a loop — fades
   `playWindow`. Requires `playWindow` to be mapped (fade effect draws
   into it). If unmapped, the Xlib draw calls are no-ops but
   `XUnmapWindow` of an already-unmapped window is also harmless in X11
   (no BadWindow error; it's idempotent). However, the fade animation
   won't be visible.
6. `XUnmapWindow(display, playWindow)` — hides the play area so the
   bonus screen shows through on `mainWindow`.

**State `SetupBonusScreen` requires to be set before it runs:**

| Variable | Set by | Notes |
|---|---|---|
| `score` | `SetTheScore` | Read by `ResetBonus` → `bonusScore = score` |
| `level` | `SetLevelNumber` | Read by `DrawTitleText` for "Level N" display |
| `numBonus` | `ResetNumberBonus` + optional `IncNumberBonus` | Read by `ComputeAndAddBonusScore` |
| `GetLevelTimeBonus()` | `SetLevelTimeBonus` | Read by `ComputeAndAddBonusScore` and `DoBonuses`/`DoLevel`/`DoTimeBonus` |
| `GetNumberBullets()` | `SetNumberBullets` | Read by `ComputeAndAddBonusScore` and `DoBullets` |
| Ball state | irrelevant | No ball active required |
| Paddle position | irrelevant | Not read by `SetupBonusScreen` or `DoBonus` |
| Populated level grid | NOT required | `SetupBonusScreen` does not read grid |

`SetupBonusScreen` does NOT need: an active ball, a specific paddle
position, or any blocks in the grid. The GC state (`gc`, `gccopy`, etc.)
and pixmaps are initialized once at startup and persist.

### 3. Minimum state delta

`CheckGameRules` itself (`original/level.c:398-419`) calls before
`SetupBonusScreen`:

```text
Togglex2Bonus(display, False);
Togglex4Bonus(display, False);
DrawSpecials(display);
```

These three are therefore NOT required in the force-entry helper — the
real path does them. The PR #143 synthetic version called them
explicitly because it bypassed `CheckGameRules` and called
`SetupBonusScreen` directly.

**Setters still required** (because `CheckGameRules`/`SetupBonusScreen`
reads them):

- `SetTheScore(N)` — sets `score` for `bonusScore = score` in
  `ResetBonus`
- `SetLevelNumber(N)` — sets `level` for bonus display text
- `SetLevelTimeBonus(display, timeWindow, N)` — sets time bonus seconds
- `SetLivesLeft(N)` — not read by bonus screen directly but needed for
  `CheckAndAddExtraLife` which is also called by `CheckGameRules` at
  `original/level.c:400`
- `SetNumberBullets(N)` — sets bullet count for bullet bonus display
- `ResetNumberBonus()` + optional `IncNumberBonus()` calls — sets
  `numBonus` for coin display

**Setters that become redundant** (done by `CheckGameRules` itself):

- `Togglex2Bonus(display, False)` — done at `original/level.c:407`
- `Togglex4Bonus(display, False)` — done at `original/level.c:408`
- `DrawSpecials(display)` — done at `original/level.c:409`

**Setter `ToggleKiller`**: Not called by `CheckGameRules` or
`SetupBonusScreen`. If a scenario wants to show killer active in the
HUD specials display, it must be set before `DrawSpecials` is called.
Since `CheckGameRules` calls `DrawSpecials`, setting `ToggleKiller`
before the trigger tick is sufficient.

### 4. Cleanest insertion point

The visual-capture infrastructure lives at
`original/main.c:1144-1545`. The `-visual-capture` argument parser is
in `original/init.c:715-767`. The `vc_check` logic samples
mode/substate after each tick of `handleGameStates`.

The force-entry helper for bonus must run:

- After `InitialiseGame` returns (X11 up, all pixmaps loaded, GC
  initialized)
- After the event loop processes `MapNotify` (window is on screen)
- Before the first main-loop tick dispatches to `MODE_GAME`

The existing `-visual-capture` modes for attract screens are captured
by letting the game cycle naturally through `MODE_PRESENTS` →
`MODE_INTRO` etc. For bonus, we cannot wait for natural game play — we
need to force-enter.

**Recommended insertion point**: Between the `MapNotify` wait loop and
the main `while (True)` event loop at `original/main.c:1568-1649`.
After the `MapNotify` event is consumed, if
`visualCaptureMode == MODE_BONUS`:

1. Call `MapAllWindows(display)` — makes all HUD subwindows visible.
2. Set scenario state: `SetTheScore`, `SetLevelNumber`,
   `SetLevelTimeBonus`, `SetLivesLeft`, `SetNumberBullets`, bonus
   count.
3. Set `gameActive = True` — prevents `handleGameMode` from
   re-initializing on first tick.
4. Call `SetupStage(display, playWindow)` — this calls `ClearAllBalls`,
   resets paddle, resets bullets, calls `ReadNextLevel` which calls
   `ClearBlockArray` via the level file reader. The level file will
   load real blocks into the grid.
5. Call `ClearBlockArray()` explicitly after `SetupStage` — wipes the
   level file's blocks so `StillActiveBlocks()` returns False
   immediately.
6. Set `mode = MODE_GAME`.

On the very next call to `handleGameStates` → `handleGameMode`:

- `gameActive == True` → skips re-init block
- Runs `HandleBallMode` (no active balls — safe)
- Runs bonus block insertion attempt (harmless — adds non-killable
  type, `StillActiveBlocks` still returns False)
- Runs `CheckGameRules` → `StillActiveBlocks()` returns False →
  transitions to `MODE_BONUS`, calls `SetupBonusScreen`

The `vc_check` logic already detects mode transitions. Adding a
`MODE_BONUS` branch to `vc_check` in `handleGameStates` with a
`bonus_state_name` function (mapping `BonusState` enum values to
strings) then handles snapshot signaling identically to other modes.

### 5. Tick-level dispatch safety

`handleGameMode` (`original/main.c:926-1142`) on the first tick with
`gameActive == True` and `mode == MODE_GAME` runs:

1. **`handlePaddleMoving`** (`original/main.c:962`) — fires every
   `PADDLE_ANIMATE_DELAY` frames. On frame 0 this is `0 % 5 == 0`, so
   it runs. With `CONTROL_MOUSE` and no actual mouse movement,
   `MovePaddle` is a no-op. With `CONTROL_KEYS` and
   `paddleMotion == 0`, also a no-op. Safe.

2. **`HandleBallMode`** (`original/main.c:967`) — called only when
   `mode == MODE_GAME`. With no active balls (`ClearAllBalls` was
   called by `SetupStage`), this processes no balls. Safe.

3. **Random bonus block insertion** (`original/main.c:970-1126`) —
   fires because `nextBonusFrame == 0` and `bonusBlock == False` and
   `nextBonusFrame <= frame` (both zero). The `rand() % 27` picks one
   of the block types to add. All types that could be added
   (`BONUS_BLK`, `BONUSX2_BLK`, `BONUSX4_BLK`, special blocks) are
   non-killable in the `StillActiveBlocks` sense
   (`original/blocks.c:2510-2529`). Adding one does not prevent
   `StillActiveBlocks` from returning False. Safe. The block will be
   visible in the cleared play area for one frame before `playWindow`
   is unmapped by `SetupBonusScreen` — not player-visible in capture.

4. **`HandleBulletMode`** — no bullets active after `SetupStage`. Safe.

5. **`ExplodeBlocksPending`** — `blocksExploding == 0` after
   `ClearBlockArray`. Safe.

6. **`HandlePendingAnimations`**, **`HandleEyeDudeMode`** — both reset
   to inactive state by `SetupStage` →
   `ChangeEyeDudeMode(EYEDUDE_NONE)`. Safe.

7. **`CheckGameRules`** (`original/main.c:1140-1141`) — fires because
   `mode == MODE_GAME`. Calls `CheckAndAddExtraLife` (safe — just
   compares score to threshold). Calls `HandleGameTimer` (safe — just
   decrements timer). Calls `StillActiveBlocks()` which returns False
   (empty grid, `blocksExploding == 0`). Transitions to `MODE_BONUS`
   and calls `SetupBonusScreen`. The game is now in the bonus screen.
   Correct.

No crash or misbehavior risk on the first tick.

### 6. Sound side effect

`noSound` is initialized to `True` at `original/init.c:511`:

```c
/* The audio is off by default */
noSound = True;
```

It is set to `False` only when the user passes `-sound` on the command
line (`original/init.c:591`). Without `-sound`, audio is off globally.
`CheckGameRules`'s `playSoundFile("applause", 70)` call at
`original/level.c:413` is guarded by `if (noSound == False)`. With
default args (no `-sound`), no sound plays.

No special flag is required to suppress the applause — the default is
silence.

## Recommendation

The force-entry helper for `-visual-capture bonus` should be a
function (e.g., `setup_bonus_capture_scenario`) added in
`original/main.c`, called inside `handleEventLoop` between the
`MapNotify` wait loop and the main `while (True)` block.

**Function signature:**

```c
static void setup_bonus_capture_scenario(Display *display,
                                         int level_num,
                                         long score_val,
                                         int time_bonus_secs,
                                         int num_bullets,
                                         int num_bonus_coins,
                                         int killer_on);
```

**What it does, in order:**

1. `MapAllWindows(display)` — makes HUD subwindows visible.
2. `SetTheScore(score_val)` — sets `score`.
3. `SetLevelNumber(level_num)` — sets `level`.
4. `SetLevelTimeBonus(display, timeWindow, time_bonus_secs)` — draws
   time into HUD.
5. `SetLivesLeft(N)` — prevents `CheckAndAddExtraLife` from firing
   unexpectedly.
6. `SetNumberBullets(num_bullets)` — for bullet bonus display.
7. `ResetNumberBonus()` — zero coins.
8. For each bonus coin: `IncNumberBonus()`.
9. If `killer_on`: `ToggleKiller(display, True)`.
10. `gameActive = True` — prevents `handleGameMode` re-init.
11. `SetupStage(display, playWindow)` — resets paddle, ball, bullets,
    loads level file.
12. `ClearBlockArray()` — wipes grid so `StillActiveBlocks()` → False.
13. `mode = MODE_GAME` — next tick dispatches to `handleGameMode` →
    `CheckGameRules` → `SetupBonusScreen`.

**What NOT to do:**

- Do NOT call `SetupBonusScreen` directly — that was PR #143's
  approach and it skipped `CheckGameRules`'s side effects and the HUD
  initialization path.
- Do NOT call `Togglex2Bonus`, `Togglex4Bonus`, `DrawSpecials` in the
  helper — `CheckGameRules` does them.
- Do NOT skip `MapAllWindows` — HUD subwindows are unmapped at
  process start.
- Do NOT pass `-sound` to the binary — `noSound` defaults True,
  silence is free.
- Do NOT call `SetupBonusScreen` before `SetupStage` — `SetupStage`
  calls `DisplayScore` and `DisplayLevelInfo` which repaint the HUD
  windows, and `ClearAllBalls` which is needed.

**Signaling to vc_check:** Add a `bonus_state_name` function mapping
`BonusState` enum values to strings (e.g., `BONUS_TEXT` → `"title"`,
`BONUS_SCORE` → `"score"`, etc.) and a `MODE_BONUS` branch in the
`vc_check` block in `handleGameStates`. The fast LIVE/WAIT cycling
documented in `docs/TESTING.md` applies — `BONUS_WAIT` states are
silent (return `NULL` from `bonus_state_name`), and content states
(`BONUS_TEXT`, `BONUS_SCORE`, `BONUS_BONUS`, etc.) fire one signal
when entered. A `bonus_get_highest_reached()` monotonic accessor is
needed for the same reason as the modern
`bonus_system_get_highest_reached()` — content states are one
sub-frame wide.

## Risks

1. **Random bonus block on tick 1.** The random bonus-block insertion
   code fires on frame 0. In the visual capture window, the cleared
   `playWindow` will have one random bonus/special block drawn for one
   frame before `SetupBonusScreen` unmaps it. Not visible in the
   captured bonus screen itself (bonus screen is on `mainWindow`, not
   `playWindow`). Risk: cosmetic, zero.

2. **`SetupStage` loads a real level file.** `SetupStage` calls
   `ReadNextLevel` which reads from disk. If `XBOING_LEVELS_DIR` is
   not set and the levels are not installed, `ShutDown` is called.
   The capture environment must have levels installed. Same constraint
   as all other visual capture modes — not new.

3. **`ClearBlockArray` after `SetupStage` loses the level title.**
   `SetupStage` calls `SetCurrentMessage(display, messWindow, str2,
   True)` to display the level name in the message window.
   `ClearBlockArray` does not affect this — it only clears the block
   array. The level name remains in `messWindow` but
   `SetupBonusScreen` overwrites `messWindow` via
   `SetCurrentMessage(display, messWindow, "- Bonus Tally -", True)`.
   No issue.

4. **`WindowFadeEffect` on unmapped `playWindow`.** If `playWindow` is
   mapped by `MapAllWindows` but has no content drawn into it (it's
   cleared by `SetupStage`'s `DrawStageBackground`), the fade effect
   runs on a blank window. The fade completes normally. `XUnmapWindow`
   then hides it. Cosmetically correct.

5. **`BonusState` signaling in vc_check.** `BonusState` values
   interleave content states and `BONUS_WAIT`. A naively written
   `vc_check` that samples at render-frame granularity will miss
   content states (they last one sub-frame before transitioning to
   `BONUS_WAIT` for 100 frames). The fix is the monotonic
   `highest_reached` accessor documented in `docs/TESTING.md`. This
   applies to the original binary just as it does to the modern port.
