# Review: bonus capture via real CheckGameRules entry path

**Date:** 2026-06-06
**Reviewer:** jdc (peer review for xboing-c-z0n)
**Subject:** docs/research/2026-06-06-bonus-capture-real-entry.md

## Verdict

APPROVED WITH CHANGES

The core approach is correct. The sequence (MapAllWindows → setters →
gameActive=True → SetupStage → ClearBlockArray → mode=MODE_GAME) will
produce an authentic bonus-screen entry via the real `CheckGameRules`
path. Two bugs in the proposed implementation order must be fixed before
code is written.

## Blocking findings

### BF-1: `SetupStage` overwrites scenario setters — wrong call order

`SetupStage` (`original/file.c:117-118`) calls:

```c
SetNumberBullets(NUMBER_OF_BULLETS_NEW_LEVEL);
ResetNumberBonus();
```

It also calls `TurnSpecialsOff(display)` (`original/file.c:121`) which
calls `ToggleKiller(display, False)` (`original/special.c:94`).

The research's recommended function call order (Recommendation section,
steps 6-11) places `SetLivesLeft`, `SetNumberBullets`, `ResetNumberBonus`,
`IncNumberBonus`, and `ToggleKiller` **before** `SetupStage`. Every one
of those is silently overwritten by `SetupStage`. The bonus screen will
always show 0 bullets, 0 bonus coins, no killer, regardless of scenario
parameters.

**Fix:** Call `SetNumberBullets`, `ResetNumberBonus`, `IncNumberBonus`,
and `ToggleKiller` **after** `SetupStage`. `SetLivesLeft` is safe before
or after (not touched by `SetupStage`). The corrected tail of
`setup_bonus_capture_scenario` must be:

```text
1.  MapAllWindows(display)
2.  SetTheScore(score_val)
3.  SetLevelNumber(level_num)
4.  SetLevelTimeBonus(display, timeWindow, time_bonus_secs)
5.  SetLivesLeft(N)
6.  gameActive = True
7.  SetupStage(display, playWindow)         ← resets bullets, bonus,
                                               specials, loads level
8.  ClearBlockArray()
9.  SetNumberBullets(num_bullets)           ← must be after SetupStage
10. ResetNumberBonus()                      ← must be after SetupStage
11. for each coin: IncNumberBonus()         ← must be after SetupStage
12. if (killer_on): ToggleKiller(display, True)  ← must be after SetupStage
13. mode = MODE_GAME
```

No additional `DrawSpecials` call is needed: `CheckGameRules`
(`original/level.c:409`) calls `DrawSpecials(display)` before
`SetupBonusScreen`, so the killer indicator is correct by the time the
bonus screen renders.

### BF-2: Bonus block insertion claim is factually wrong (consequence is safe, reasoning is not)

Research section 5 states:

> fires because `nextBonusFrame == 0` and `bonusBlock == False` and
> `nextBonusFrame <= frame` (both zero)

`frame` starts at 0, is incremented to 1 by `frame++` at
`original/main.c:1662` before `handleGameStates` is called. On the first
tick with `mode == MODE_GAME`, `frame == 1`.

In `handleGameMode` (`original/main.c:970-974`):

```c
if (nextBonusFrame == 0 && bonusBlock == False)
    nextBonusFrame = frame + (rand() % BONUS_SEED);  /* BONUS_SEED=2000 */

if (nextBonusFrame <= frame && bonusBlock == False)
    /* ... add block ... */
```

Line 971 sets `nextBonusFrame = 1 + (rand() % 2000)`, range [1..2000].
Line 974 then checks `[1..2000] <= 1` — true only when `rand() % 2000 ==
0` (probability 1/2000). The bonus block insertion fires on tick 1 in
roughly 0.05% of runs, not always.

The conclusion (the block is non-killable so `StillActiveBlocks` still
returns False) remains correct — but the reasoning in the research is
wrong and would cause confusion during implementation.

## Non-blocking findings

### NB-1: `HandleGameTimer` decrements time bonus on tick 1

`HandleGameTimer` (`original/level.c:385-396`) uses `static time_t
oldTime = 0`. On the first call, `time(NULL) > 0` is always true, so
`DecLevelTimeBonus` fires immediately, decrementing the time bonus by 1
second before `StillActiveBlocks` is checked and `SetupBonusScreen` is
called. The bonus screen will display `time_bonus_secs - 1` seconds of
time bonus.

This is inherent to the real game path — in actual play the timer also
fires on the level-completion tick. The 1-second discrepancy is authentic
behavior, not a capture artifact. Capture scenarios should account for
this: to display N seconds on the bonus screen, pass N+1 to
`SetLevelTimeBonus`.

### NB-2: `CheckAndAddExtraLife` can fire spuriously for high-score scenarios

`CheckAndAddExtraLife` (`original/level.c:370-383`) uses `static int
ballInc = 0`. At process start `ballInc == 0`. For any scenario with
`score >= NEW_LIVE_SCORE_INC (100000L)`, the condition
`score / 100000 != 0` fires and grants an extra life. Sound is off by
default so no audio side effect. The lives counter in `specialWindow` may
briefly update. For scenarios with score < 100000 this is a non-issue.

Fix if needed: call `SetLivesLeft` after `CheckAndAddExtraLife` fires, or
limit scenario scores to <100000. For capture purposes this is cosmetic.

### NB-3: `gameTime` is zero (epoch) for highscore duration

The `gameActive == False` init block sets `gameTime = time(NULL)`
(`original/main.c:958`). The force-entry helper sets `gameActive = True`
to skip that block, so `gameTime` stays at its BSS zero-initialization
(epoch, 1970-01-01). `UpdateHighScores` (`original/level.c:428`) computes
`endTime = time(NULL) - gameTime - pausedTime` — gives ~56 years of
game duration. This only fires after `DoFinish` (bonus sequence complete),
which is after capture is done. Non-issue for bonus-screen capture.

### NB-4: `vc_pre_bonus` variable must be declared alongside other `vc_pre_*` variables

The existing `vc_check` logic (`original/main.c:1351-1358`) snapshots
pre-dispatch substates:

```c
int vc_pre_presents = PresentState;
int vc_pre_intro = IntroState;
/* ... */
int vc_pre_preview = PreviewState;
```

The `MODE_BONUS` branch needs `int vc_pre_bonus = BonusState;` added
there. The research describes the pattern but does not name this variable
explicitly. Missing it produces wrong `pre_substate` in `vc_check`.

### NB-5: `DoBonusWait` exact-equality check is safe in the original, fragile to document

`DoBonusWait` (`original/bonus.c:636`): `if (frame == waitingFrame)`.
This works because the original runs exactly one tick per loop iteration
— `frame` increments by 1, and `waitingFrame = frame + LINE_DELAY` is
set during the same tick the state transitions. Every subsequent tick
increments `frame` by exactly 1, so the target is hit exactly once.

The modern port runs 6 sub-ticks per visual frame
(`ATTRACT_FRAME_MULTIPLIER`). `frame == waitingFrame` in the modern port
would only fire on the one sub-tick where `frame` crosses the target value
by exactly 1, which is unreliable. The `highest_reached` accessor
documented in `docs/TESTING.md` is the correct fix for the modern port.
The research correctly identifies this but should note it is a porting
artifact, not present in the original.

## Verified claims

- `original/init.c:511` — `noSound = True` default. Confirmed.
- `original/init.c:591` — `noSound = False` on `-sound`. Confirmed.
- `original/init.c:1058` — only `mainWindow` is mapped after
  `InitialiseGame`. Confirmed.
- `original/stage.c:388-399` — `MapAllWindows` maps `specialWindow`,
  `timeWindow`, `messWindow`, `playWindow`, `levelWindow`, `scoreWindow`,
  `mainWindow`. Confirmed.
- `original/presents.c:561` — `MapAllWindows` is called from
  `DoFinish` in the presents handler. Confirmed.
- `original/bonus.c:764-775` — `ResetBonus` sets `BonusState = BONUS_TEXT`,
  `firstTime = True`, `bonusScore = score`, calls
  `ComputeAndAddBonusScore()`, sets `ypos = 180`, calls
  `SetGameSpeed(FAST_SPEED)`. Confirmed.
- `original/level.c:398-419` — `CheckGameRules` calls
  `CheckAndAddExtraLife`, `HandleGameTimer`, `Togglex2Bonus`,
  `Togglex4Bonus`, `DrawSpecials`, then `SetupBonusScreen` when
  `StillActiveBlocks() == False`. Confirmed. The three toggle/draw calls
  do NOT need to be in the helper.
- `original/blocks.c:2510-2529` — `BONUS_BLK`, `BONUSX2_BLK`,
  `BONUSX4_BLK` and all special block types are excluded from
  `StillActiveBlocks` return-True path. Confirmed.
- `original/blocks.c:2538-2543` — `blocksExploding > 1` also causes
  `StillActiveBlocks` to return True. `ClearBlockArray` does not reset
  `blocksExploding` — verify that `SetupStage` does, or add an explicit
  reset. `ClearBlock` does not touch `blocksExploding` directly.
- `original/main.c:1452-1507` — existing `vc_check` `MODE_PREVIEW`
  branch follows exactly the pattern jck described (pre/post substate,
  `name_fn`). The `MODE_BONUS` branch should follow the same structure.
  Confirmed pattern is sound.
- `original/bonus.c:636` — `DoBonusWait`: `if (frame == waitingFrame)`.
  Single exact-equality check. Confirmed.
- `original/init.c:715-767` — `-visual-capture` parser does not yet
  include `bonus`. The `else` branch at line 760-763 warns and prints
  usage. `bonus` must be added. Confirmed.

## Disputed claims

### DC-1: Section 5 bonus block insertion mechanism

Research claims the insertion "fires because `nextBonusFrame <= frame`
(both zero)." This is incorrect. See BF-2 above. Both are not zero on
tick 1: `frame == 1`, and `nextBonusFrame` is set to `1 + (rand() % 2000)`
before the `<=` check runs. The claim that the block insertion is
guaranteed on tick 1 is wrong; it fires with probability 1/2000.

## Recommendations for implementation

1. **Fix the call order (BF-1):** In `setup_bonus_capture_scenario`,
   call `SetNumberBullets`, `ResetNumberBonus`, `IncNumberBonus`, and
   `ToggleKiller` after `SetupStage`. Not before.

2. **Check `blocksExploding` reset:** Verify that `SetupStage`
   (`original/file.c:96`) or `ClearBlockArray` (`original/blocks.c:2618`)
   resets `blocksExploding` to 0. If not, add an explicit reset or a
   call to whatever setter exists. `StillActiveBlocks` checks
   `blocksExploding > 1` at `original/blocks.c:2538` — if this is nonzero
   after `ClearBlockArray`, the bonus transition will not fire.

3. **Add `int vc_pre_bonus = BonusState;`** to the pre-dispatch snapshot
   block at `original/main.c:1351-1358` (alongside the existing
   `vc_pre_*` variables). The `MODE_BONUS` branch in `vc_check` needs this.

4. **Scenario time values:** Pass `time_bonus_secs + 1` to account for
   the 1-second decrement in `HandleGameTimer` on tick 1 (NB-1).

5. **Add `bonus` to the `-visual-capture` parser** at
   `original/init.c:715-767` before the `else` branch that prints usage.
   Map to `MODE_BONUS`.

6. **`highest_reached` accessor pattern is mandatory for
   `BONUS_TEXT`/`BONUS_WAIT` cycling.** Confirm this before writing the
   `bonus_state_name` function. Content states last exactly one tick
   before transitioning to `BONUS_WAIT`. Without the monotonic accessor,
   pre/post sampling will frequently miss `BONUS_TEXT` and similar
   one-tick states.
