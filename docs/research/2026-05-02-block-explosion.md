# Block Explosion Animation — Original 1996 Behavior

Research for bead **xboing-c-tmr** (basket 3, visual-fidelity audit).
Ground truth drawn exclusively from `original/blocks.c` and
`original/include/blocks.h`. Descriptive only — no implementation
prescriptions.

---

## 1. Constants

`EXPLODE_DELAY` is defined as `10` at `original/include/blocks.h:71`:

```c
#define EXPLODE_DELAY 10
```

The modern port mirrors this value: `BLOCK_EXPLODE_DELAY` is defined as
`10` at `include/block_system.h:56`. The numeric values match. However
`BLOCK_EXPLODE_DELAY` has no readers in the modern codebase (confirmed in
section 7).

Adjacent timing constants defined in the same header block
(`original/include/blocks.h:72-83`): `BONUS_DELAY=150`,
`BONUS_LENGTH=1500`, `NUMBER_OF_BULLETS_NEW_LEVEL=4`, `EXTRA_TIME=20`
(used by TIMER_BLK side effect).

---

## 2. Per-Block State

Four fields on `struct aBlock` track explosion state. They are declared
together at `original/include/blocks.h:104-108` under the comment
"Used when block explodes":

```c
int exploding;           /* line 105 — True while animation is active */
int explodeStartFrame;   /* line 106 — game frame when explosion was triggered */
int explodeNextFrame;    /* line 107 — game frame when next stage fires */
int explodeSlide;        /* line 108 — current stage (1..4, >4 terminates) */
```

The global counter `blocksExploding` is declared `extern int blocksExploding`
at `original/include/blocks.h:178`. `ExplodeBlocksPending` fast-returns when
it is zero (`original/blocks.c:1487-1488`).

---

## 3. Trigger — SetBlockUpForExplosion

`static void SetBlockUpForExplosion(int row, int col, int frame)` is defined
at `original/blocks.c:1808`.

**Pre-conditions checked** (`original/blocks.c:1812-1825`):

- `row < 0 || row >= MAX_ROW` or `col < 0 || col >= MAX_COL` — return
  immediately (bounds check).
- `blockP->blockType == HYPERSPACE_BLK` — return immediately
  (`original/blocks.c:1821-1822`). HYPERSPACE_BLK is immune to explosion,
  including chain reactions.
- `blockP->occupied == 1 && blockP->exploding == False` must both be true
  (`original/blocks.c:1825`). An already-exploding block cannot be
  re-triggered.

**State written when conditions pass** (`original/blocks.c:1828-1834`):

```c
blocksExploding++;                       /* original/blocks.c:1828 */
blockP->explodeStartFrame = frame;       /* original/blocks.c:1831 */
blockP->explodeNextFrame  = frame;       /* original/blocks.c:1832 */
blockP->explodeSlide      = 1;           /* original/blocks.c:1833 */
blockP->exploding         = True;        /* original/blocks.c:1834 */
```

**Additional state written at arming time** (`original/blocks.c:1837-1842`):

- `blockP->specialPopup == True` clears `bonusBlock = False`.
- `blockP->drop == True` sets `blockP->drop = False` (stops drop animation).

**Entry point from ball collision**: `DrawBlock(display, window, row, col, KILL_BLK)`
at `original/blocks.c:1846`. When `blockType == KILL_BLK`, `DrawBlock`
calls `PlaySoundForBlock(blockP->blockType)` at `original/blocks.c:1868`,
then `SetBlockUpForExplosion(row, col, frame)` at `original/blocks.c:1870`.
Sound fires at trigger time, before animation.

`ball.c` calls `DrawBlock(..., KILL_BLK)` for every destructible block hit:
`original/ball.c:854` (DEATH_BLK), `:885` (WALLOFF_BLK), `:934`
(PAD_EXPAND_BLK), `:949` (EXTRABALL_BLK), `:964` (STICKY_BLK), `:977`
(MULTIBALL_BLK), `:990` (BLACK_BLK when expired), `:1008` (default —
all remaining destructible types).

---

## 4. State Machine — ExplodeBlocksPending

`void ExplodeBlocksPending(Display *display, Window window)` is defined at
`original/blocks.c:1480`. Called once per game-loop tick. It iterates the
full 18x9 grid and for each block where `explodeStartFrame != 0`
(`original/blocks.c:1499`) and `explodeNextFrame == frame`
(`original/blocks.c:1502`), advances the animation by one stage.

**The 4-stage cadence** (`original/blocks.c:1509-1534`):

| explodeSlide | Lines | Action |
|---|---|---|
| 1 | 1511-1518 | `ExplodeBlockType(..., slide=0)` — first explosion sprite. Resets `explodeNextFrame = explodeStartFrame` before post-switch `+= EXPLODE_DELAY` (net: F+10). If `explodeAll == True`, calls `ExplodeAllOfOneType`. |
| 2 | 1520-1522 | `ExplodeBlockType(..., slide=1)` — second explosion sprite. |
| 3 | 1523-1526 | `ExplodeBlockType(..., slide=2)` — third explosion sprite. |
| 4 | 1528-1530 | `XClearArea(display, window, x, y, width, height, False)` — terminal clear, no sprite. |

After every stage, unconditionally (`original/blocks.c:1537-1538`):

```c
blockP->explodeSlide++;
blockP->explodeNextFrame += EXPLODE_DELAY;
```

With `EXPLODE_DELAY=10` and trigger at frame F:

- Stage 1 fires at F (same tick as the hit).
- Stage 2 fires at F+10.
- Stage 3 fires at F+20.
- Stage 4 fires at F+30.
- Finalize taken when `explodeSlide > 4` (`original/blocks.c:1541`),
  checked after stage 4 increments slide to 5.

Total animation duration: 40 game-loop ticks.

**Finalize logic** (`original/blocks.c:1541-1641`):

1. `blocksExploding--` (`original/blocks.c:1543`).
2. `blockP->occupied = 0` (`original/blocks.c:1544`).
3. `blockP->exploding = False` (`original/blocks.c:1545`).
4. `AddToScore((u_long)blockP->hitPoints)` — score deferred to completion
   (`original/blocks.c:1547`).
5. `DisplayScore(display, scoreWindow, score)` (`original/blocks.c:1548`).
6. Per-type side effects switch (`original/blocks.c:1550-1637`).
7. `ClearBlock(r, c)` (`original/blocks.c:1640`).

---

## 5. Completion Side Effects

All side effects fire at explosion completion, not at ball-collision time.
Dispatched in `switch (blockP->blockType)` at `original/blocks.c:1550`.

**BLACK_BLK, PAD_SHRINK_BLK, PAD_EXPAND_BLK** (`original/blocks.c:1552-1555`):
no additional side effects at finalize.

**BOMB_BLK** (`original/blocks.c:1557-1573`): 8-neighbor chain reaction.
Calls `SetBlockUpForExplosion` on all 8 adjacent cells with
`frame + EXPLODE_DELAY` as their start frame
(`original/blocks.c:1559-1566`). Screen shake: `SetSfxEndFrame(frame + 70)`
and `changeSfxMode(SFX_SHAKE)` (`original/blocks.c:1571-1572`).

**TIMER_BLK** (`original/blocks.c:1575-1579`):
`AddToLevelTimeBonus(display, timeWindow, EXTRA_TIME)` where `EXTRA_TIME=20`
(`original/include/blocks.h:83`). Message: "- Extra Time = 20 seconds -".

**BULLET_BLK** (`original/blocks.c:1581-1585`):
`AddABullet(display)` called `NUMBER_OF_BULLETS_NEW_LEVEL` (=4) times
(`original/include/blocks.h:74`). Message: "More ammunition, cool!".

**MAXAMMO_BLK** (`original/blocks.c:1588-1593`):
`SetUnlimitedBullets(True)`, `SetNumberBullets(MAX_BULLETS + 1)`,
`DisplayLevelInfo`. Message: "Unlimited bullets!".

**BONUS_BLK** (`original/blocks.c:1595-1615`):
`IncNumberBonus()`. Message "- Bonus #N -" when `<= MAX_BONUS`, else
"<<< Super Bonus >>>". Sets `bonusBlock = False`. At `GetNumberBonus() == 10`:
`ToggleKiller(display, True)` + `DrawSpecials` + message "- Killer Mode -"
(`original/blocks.c:1607-1614`).

**BONUSX2_BLK** (`original/blocks.c:1617-1624`):
`Togglex2Bonus(display, True)`, `Togglex4Bonus(display, False)`,
`DrawSpecials`. Sets `bonusBlock = False`. Message: "- x2 Bonus -".

**BONUSX4_BLK** (`original/blocks.c:1626-1633`):
`Togglex2Bonus(display, False)`, `Togglex4Bonus(display, True)`,
`DrawSpecials`. Sets `bonusBlock = False`. Message: "- x4 Bonus -".

All other types: fall through to `default: break` — score and clear only.

---

## 6. Visual Cadence

`ExplodeBlockType(display, window, x, y, row, col, type, slide)` is
defined at `original/blocks.c:863`. Dispatches on `type` to select
the per-color XPM pixmap array.

**Standard color blocks** (RED, BLUE, GREEN, TAN, PURPLE, YELLOW, BULLET,
COUNTER, DROP, MULTIBALL): 3-element pixmap arrays, slide indices 0, 1, 2,
rendered at 40x20 pixels. Example: `original/blocks.c:878-879` (RED/MULTIBALL),
`original/blocks.c:903-905` (YELLOW/BULLET).

**BOMB_BLK** (`original/blocks.c:872-875`): `exbombblock[slide]`,
3 elements, 30x30 pixels.

**BONUS_BLK, BONUSX2_BLK, BONUSX4_BLK, TIMER_BLK** (`original/blocks.c:913-917`):
share `exx2bonus[slide]`, 3 elements, 27x27 pixels.

**DEATH_BLK** (`original/blocks.c:920-921`): `exdeath[slide]`, 3 elements,
30x30 pixels.

**BLACK_BLK** (`original/blocks.c:924-925`): no sprite rendered. Empty
case body.

**REVERSE_BLK, HYPERSPACE_BLK, EXTRABALL_BLK, MGUN_BLK, WALLOFF_BLK,
STICKY_BLK, PAD_SHRINK_BLK, PAD_EXPAND_BLK, MAXAMMO_BLK, ROAMER_BLK**
(`original/blocks.c:927-963`): no colored explosion sprite. Progressive
`XClearArea` calls wipe the block in bands across slides 0, 1, 2,
producing a shrinking-to-center visual.

**Extern declaration confirming 3-frame array dimension**:
`extern Pixmap exyellowblock[3], exyellowblockM[3]`
at `original/include/blocks.h:180`.

**Total sequence**: 3 visible sprite frames (stages 1-3) + 1 clear frame
(stage 4) = 4 stages x 10 ticks = 40 ticks total.

---

## 7. Modern Gap Reproduction

### 7a. Four explosion fields on the modern block struct are never written after init

`block_entry_t` (opaque typedef inside `src/block_system.c`) declares
all four fields at `src/block_system.c:32-35`:

```c
int exploding;            /* line 32 */
int explode_start_frame;  /* line 33 */
int explode_next_frame;   /* line 34 */
int explode_slide;        /* line 35 */
```

The only function that writes these fields is `clear_entry()` at
`src/block_system.c:81-113`, which zeroes all of them. No function in
`src/block_system.c` sets `exploding = 1`, assigns a nonzero
`explode_start_frame`, or increments `explode_slide`. The explosion state
machine is structurally absent from the modern codebase.

### 7b. BLOCK_EXPLODE_DELAY=10 has no readers

`BLOCK_EXPLODE_DELAY` is defined at `include/block_system.h:56`. No
reference to this constant exists outside its definition in `src/` or
`include/`.

### 7c. Ball-vs-block collision path calls block_system_clear directly

`ball_cb_on_block_hit` in `src/game_callbacks.c` is the modern collision
handler. Every branch calls `block_system_clear` immediately at hit time:

| Block type | Call site |
|---|---|
| DEATH_BLK | `src/game_callbacks.c:90` |
| BOMB_BLK | `src/game_callbacks.c:101` |
| REVERSE_BLK | `src/game_callbacks.c:112` |
| MULTIBALL_BLK | `src/game_callbacks.c:118` |
| STICKY_BLK | `src/game_callbacks.c:125` |
| PAD_SHRINK_BLK | `src/game_callbacks.c:131` |
| PAD_EXPAND_BLK | `src/game_callbacks.c:136` |
| MGUN_BLK | `src/game_callbacks.c:143` |
| WALLOFF_BLK | `src/game_callbacks.c:148` |
| EXTRABALL_BLK | `src/game_callbacks.c:153` |
| BONUSX2_BLK | `src/game_callbacks.c:159` |
| BONUSX4_BLK | `src/game_callbacks.c:163` |
| HYPERSPACE_BLK | `src/game_callbacks.c:168` |
| default | `src/game_callbacks.c:172` |

Score is awarded at `src/game_callbacks.c:76-83` — before the switch, at
hit time. The original awards score at explosion completion
(`original/blocks.c:1547`). The ordering is inverted in the modern code.

No equivalent of `SetBlockUpForExplosion` exists in the modern codebase.

### 7d. game_render.c has explosion render scaffolding but it is never reached

`game_render_blocks` at `src/game_render.c:85` checks `info.exploding`
and calls `sprite_block_explode_key(info.block_type, info.explode_slide)`
at `src/game_render.c:94` when set. Since `exploding` is never set to 1
by the collision path, this branch is dead code during gameplay.
`sprite_block_explode_key()` at `src/sprite_catalog.h:466-538` and all
explosion texture key constants at `src/sprite_catalog.h:113-143` are
fully defined — the rendering infrastructure is complete.

---

## 8. Behavior Preservation Requirements

Invariants a correct modern implementation must satisfy, derived from
`original/blocks.c:1480-1646` and `original/blocks.c:1808-1844`.

- **Score at completion, not at hit time.** `AddToScore` fires when
  `explodeSlide > 4` (`original/blocks.c:1547`). The block's `hit_points`
  must survive the full 40-tick animation.

- **Block occupancy persists through animation.** The cell must remain
  "occupied" (unavailable for teleport and block placement) until finalize.
  `occupied = 0` fires only at `original/blocks.c:1544`.

- **4-stage cadence at 10 ticks per stage.** Stage 1 fires the same tick
  as the hit. Stages 2, 3, 4 fire at +10, +20, +30 ticks. Constant source:
  `original/include/blocks.h:71`.

- **3 visible sprite frames + 1 clear frame.** Stages 1-3 render explosion
  sprite indices 0, 1, 2. Stage 4 clears pixel area only
  (`original/blocks.c:1528-1530`).

- **Sound at trigger, not at finalize.** `PlaySoundForBlock` is called at
  `original/blocks.c:1868`, inside `DrawBlock(KILL_BLK)`, before
  `SetBlockUpForExplosion`.

- **BOMB_BLK chain reaction at finalize.** All 8 orthogonal and diagonal
  neighbors receive `SetBlockUpForExplosion` with `frame + EXPLODE_DELAY`
  (`original/blocks.c:1559-1566`). Screen-shake SFX fires simultaneously
  (`original/blocks.c:1571-1572`).

- **HYPERSPACE_BLK immune.** Must be skipped unconditionally in
  `SetBlockUpForExplosion` (`original/blocks.c:1821-1822`), including
  during BOMB chain reactions.

- **Re-entry guard.** `exploding == True` prevents re-arming
  (`original/blocks.c:1825`).

- **TIMER_BLK: +20 seconds at finalize.** `EXTRA_TIME=20` at
  `original/include/blocks.h:83`.

- **BULLET_BLK: +4 bullets at finalize.** `NUMBER_OF_BULLETS_NEW_LEVEL=4`
  at `original/include/blocks.h:74`.

- **BONUS_BLK: killer mode at exactly the 10th bonus.** Hard-coded
  threshold at `original/blocks.c:1607`.

- **BONUSX2_BLK / BONUSX4_BLK: mutual exclusion.** Each explicitly
  deactivates the other (`original/blocks.c:1619`, `original/blocks.c:1628`).

- **explodeAll checked at stage 1 only.** `ExplodeAllOfOneType` fires
  inside `case 1` at `original/blocks.c:1516-1517`. Must not fire at
  other stages.
