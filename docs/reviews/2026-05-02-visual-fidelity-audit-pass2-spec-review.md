# Spec Review — Visual Fidelity Audit Pass 2

**Mission:** m-2026-05-02-018
**Reviewer:** gjm (Glenford J. Myers, test-expert)
**Spec under review:** `.tmp/missions/visual-fidelity-audit-pass2.yaml`
**Date:** 2026-05-02
**Verdict:** ACCEPT WITH CHANGES

---

## Summary

The pass-2 contract is well-scoped and fills the right gaps left by pass 1.
Six substantive findings below — three blocking (read-set gaps, a
verifiability gap, and a misidentified area), three non-blocking (output
shape, no-re-audit confirmation, missed hypothesis). All blocking findings
have verbatim revision text.

Pre-located gaps from source inspection are flagged in section
"Pre-Located Gaps" to give jck a head start.

---

## Finding 1 — Blocking: KILL\_BLK read-set is missing block\_system.c and game\_callbacks.c

**Severity:** Blocking
**Affected field:** `inputs.references` (KILL_BLK explosion lifecycle area)

### What the contract says

jck is pointed at `original/blocks.c` and `src/game_render.c` to document
the KILL_BLK explosion lifecycle. The success criterion asks for frame
count, sprite identity, tick rate, and when the cell clears.

### What source inspection reveals

The explosion lifecycle in the modern codebase is split across three
files that the contract does not include:

1. `src/block_system.c` — owns the per-block explosion state machine fields
   (`exploding`, `explode_start_frame`, `explode_next_frame`,
   `explode_slide`). The fields are initialized to 0 in `clear_entry()` and
   read in `block_system_get_render_info()`. They are **never written** after
   initialization — no function sets `bp->exploding = 1` or advances
   `bp->explode_slide`. The original's `SetBlockUpForExplosion()` /
   `ExplodeBlocksPending()` equivalents do not exist.

2. `src/game_callbacks.c` — the ball-hits-block callback at lines 71-168
   calls `block_system_clear()` directly on collision, bypassing any
   explosion animation entirely. KILL_BLK is not referenced.

3. `include/block_system.h:56` — defines `BLOCK_EXPLODE_DELAY 10`, matching
   original `EXPLODE_DELAY = 10` (`original/include/blocks.h:71`). The
   constant exists but is unused.

**Consequence for jck:** Without `src/block_system.c` and
`src/game_callbacks.c` in the read-set, jck cannot document the full
picture: the modern build has explosion state machine scaffolding but no
tick-advance logic. The gap is structural — `ExplodeBlocksPending()` is
absent, not just misconfigured.

**Pre-located gap:** `src/block_system.c:32-35` (fields declared, never
written to after init); `src/game_callbacks.c:90,101,112-168` (all collision
paths call `block_system_clear()` directly, bypassing explosion animation).

**Verbatim revision:**

```yaml
  references:
    - src/block_system.c        # explosion state machine fields + clear_entry
    - src/game_callbacks.c      # ball-hit dispatch — confirms bypasses explosion
    - include/block_system.h    # BLOCK_EXPLODE_DELAY constant
```

Add to `success_criteria`:

```yaml
  - KILL_BLK lifecycle: confirm whether modern ExplodeBlocksPending()
    equivalent exists. Cite src/block_system.c and src/game_callbacks.c.
    If absent, name it as a gap.
```

---

## Finding 2 — Blocking: Lives and level-number positions pre-located as wrong; verifiability criteria missing

**Severity:** Blocking
**Affected field:** `success_criteria` (area 2 — level info panel)

### What the contract says

jck is asked to compare `original/init.c levelWindow geometry` and
`original/level.c DisplayLevelInfo` against `game_render_lives` and
`game_render_ammo_belt`. The contract includes `src/game_render.c` but
success criteria lack concrete proxy values.

### What source inspection reveals

`game_render_lives()` and `game_render_ammo_belt()` are in
`src/game_render.c:420-510`. Two pre-located gaps:

**Lives display — pre-located gap:**

- Original (`original/level.c:224`): right-anchored at x=175 within
  levelWindow, stride 30px, centered per `DrawLife` at `163-(i*30)`.
- Modern (`src/game_render.c:435-441`): left-anchored at
  `LEVEL_AREA_X + 5 + i*18` (absolute x=289+i*18), stride 18px.

Both anchor direction and stride differ. Up to 5 lives at 30px stride
spans 120px; at 18px stride spans 72px. The cluster appears in the wrong
position and is more compressed.

**Level number — pre-located gap:**

- Original (`original/level.c:210`): `DrawOutNumber(..., level, 260, 5)` —
  x=260 within levelWindow (absolute 544). Digits drawn right-to-left.
- Modern (`src/game_render.c:450-458`): tens digit at `LEVEL_AREA_X+5=289`,
  units at `LEVEL_AREA_X+27=311`. Level number appears at the far left of
  the panel instead of the right side.

**Verbatim revision** — add to `success_criteria`:

```yaml
  - Lives display: document original right-anchor x=175 stride=30px vs modern
    left-anchor stride=18px (src/game_render.c:435); confirm gap.
  - Level number position: document original x=260 within levelWindow vs
    modern x=5,27 (src/game_render.c:450-458); confirm gap.
```

---

## Finding 3 — Blocking: Specials panel x4-cropping hypothesis can be pre-closed; verifiability criterion needs restatement

**Severity:** Blocking (closes a stated hypothesis rather than adding one)
**Affected field:** `success_criteria` (specials panel area)

### What source inspection reveals

The contract says "Pass 1 audit screenshot review noted potential x4
cropping." Pre-inspection confirms the modern column assignments at
`src/special_system.c:270-273` use `SPECIAL_COL3_X=155`. The label "x4"
is 2 characters. With a typical copy font width of ~8px/char plus shadow,
it renders within 20px. The panel is 180px wide, so "x4" at x=155 has 25px
clearance. Cropping is implausible.

Column layout matches original: Col0(x=5)=Reverse/Sticky,
Col1(x=55)=Save/FastGun, Col2(x=110)=NoWall/Killer, Col3(x=155)=x2/x4
per `original/special.c:153-222` and `src/special_system.c:233-273`.
No gap.

**Verbatim revision** — add to `success_criteria`:

```yaml
  - Specials x4 cropping: pre-located as non-issue (x4 at col_x=155 in
    180px panel, 25px clearance). Confirm label order matches original
    DrawSpecials() column layout (original/special.c:153-222).
    Document no-gap or any residual gap.
```

---

## Finding 4 — Non-blocking: Eyedude fire-reaction term is ambiguous

**Severity:** Non-blocking
**Affected field:** `success_criteria` (eyedude area)

### What source inspection reveals

The contract asks jck to document "fire-reaction state." Pre-inspection:

- Frame rate matches: original `EYEDUDE_FRAME_RATE = 30`
  (`original/include/eyedude.h:64`); modern `EYEDUDE_FRAME_RATE = 30`
  (`include/eyedude_system.h:27`). No gap.
- Frame count matches: original `s` cycles 0-5 (6 frames,
  `original/eyedude.c:318`); modern `EYEDUDE_WALK_FRAMES = 6`
  (`include/eyedude_system.h:30`). No gap.
- Death: original `EYEDUDE_DIE` (`original/eyedude.c:372-384`) erases the
  sprite and transitions to `EYEDUDE_NONE` — no multi-frame death
  animation. Modern `do_die()` mirrors this. No gap.
- Fire-reaction: `original/eyedude.h:76-84` defines no `FIRE_REACTION`
  state. The contract's term likely refers to the `deveyes` particle effect
  in `src/sfx_system.c`, which is a separate subsystem rendered via
  `game_render_deveyes()` (`src/game_render.c:722`).

**Verbatim revision** — add to `context`:

```text
Note: "fire-reaction state" for EyeDude refers to whether the deveyes SFX
plays on a bullet miss (src/sfx_system.c), not an EyeDude state-machine
state. jck should check original/sfx.c DrawDevilEye() trigger condition
and compare to src/sfx_system.c sfx_system_start_deveyes().
```

---

## Finding 5 — Non-blocking: Bonus screen animation gap pre-located; success criteria need it named

**Severity:** Non-blocking
**Affected field:** `success_criteria` (bonus screen area)

### What source inspection reveals

Modern bonus screen (`src/game_render_ui.c:510-568`) renders all lines
simultaneously as state advances — Score, Congratulations, Bonus Coins,
Level Bonus, Bullet Bonus, Time Bonus, Prepare for level N.

Original `original/bonus.c:270-602` sequence appends one line at a time
per `LINE_DELAY=100` frames and animates coins and bullets individually:

- `BONUS_BONUS`: draws `BONUS_BLK` sprites one per tick with score
  accumulation (`original/bonus.c:362`)
- `BONUS_BULLET`: depletes ammo belt bullet-by-bullet

Modern renders the full accumulated display at each state; original
appends one line at a time. Pre-located gap: no animated coin-by-coin
or bullet-by-bullet sequence.

**Verbatim revision** — add to `success_criteria`:

```yaml
  - Bonus screen animation: document original LINE_DELAY=100 line-by-line
    reveal vs modern all-lines-visible approach (src/game_render_ui.c:532-567).
    Confirm gap.
  - Bonus coin animation: original draws BONUS_BLK sprites one-per-tick
    (original/bonus.c:362); modern shows a count string. Confirm gap.
```

---

## Finding 6 — Non-blocking: Game over path is level.c not dialogue.c

**Severity:** Non-blocking
**Affected field:** `inputs.references` (game over area)

### What source inspection reveals

The contract points at `original/dialogue.c` for the game-over path. This
is wrong. The game-over path is `original/level.c:EndTheGame()` (lines
452-471), which sets the message, plays sound, calls `UpdateHighScores()`,
and transitions to `MODE_HIGHSCORE`. There is no animated game-over screen.
`original/dialogue.c` handles name-entry for the high-score table called
from `UpdateHighScores()` via `UserInputDialogueMessage()`.

Modern path (`src/game_rules.c:249-258`): sets `game_active=false`, plays
"game_over" sound, sets "GAME OVER" message, transitions to
`SDL2ST_HIGHSCORE`. Matches original. No gap.

**Verbatim revision:**

```yaml
  references:
    # original/level.c already present — EndTheGame() is lines 452-471
    # dialogue.c is name-entry, not game-over sequence; remove or annotate:
    # - original/dialogue.c  # name-entry dialog called from UpdateHighScores
```

Add to `context`:

```text
Area 8 (game over): the game-over sequence is original/level.c:EndTheGame()
(lines 452-471), not dialogue.c. dialogue.c is name-entry for the top-score
case. Modern src/game_rules.c:249-258 matches. Verify and close as no-gap.
```

---

## Pre-Located Gaps (summary for jck)

These are verified discrepancies, not hypotheses.
jck should cite these verbatim and mark them as confirmed gaps.

| # | Gap | src cite | Severity |
|---|-----|----------|----------|
| **PL-1** | `ExplodeBlocksPending()` equivalent absent — explosion animation never plays | `src/block_system.c:32-35`, `src/game_callbacks.c:90` | Critical |
| **PL-2** | `BLOCK_EXPLODE_DELAY=10` defined but unused | `include/block_system.h:56` | Confirms PL-1 |
| **PL-3** | Lives: right-anchor x=175 stride=30 (original) vs left-anchor x=289+i*18 stride=18 (modern) | `src/game_render.c:435`, `original/level.c:224` | High |
| **PL-4** | Level number: original x=260 in levelWindow (absolute 544) vs modern x=289/311 (far left) | `src/game_render.c:450-458`, `original/level.c:210` | High |
| **PL-5** | Bonus screen: simultaneous render vs original line-by-line reveal at LINE\_DELAY=100 | `src/game_render_ui.c:532-567`, `original/bonus.c:270-602` | Medium |
| **PL-6** | Bonus coin animation: original draws BONUS\_BLK sprite per coin; modern shows count string | `original/bonus.c:362`, `src/game_render_ui.c:538-542` | Medium |

---

## Read-Set Completeness Check

Area-by-area pass:

| Area | Needed files present? | Gap? |
|------|-----------------------|------|
| 1. KILL_BLK explosion | Missing `src/block_system.c`, `src/game_callbacks.c` | Yes — Finding 1 |
| 2. Level info panel | All needed files present | No (Finding 2 adds criteria) |
| 3. Specials panel | `src/game_render.c`, `original/special.c` present | No |
| 4. Paddle rendering | `original/paddle.c`, `src/game_render.c` present | No |
| 5. Eyedude animation | `original/eyedude.c`, `src/eyedude_system.c` present | No |
| 6. Particle effects | `original/sfx.c`, `src/sfx_system.c` present; `original/gun.c` absent | Minor |
| 7. Bonus screen | `original/bonus.c` present via `original/` reference | No |
| 8. Game over | `original/dialogue.c` wrong reference — Finding 6 | Minor |
| 9. Attract screens | `original/intro.c`, `original/demo.c`, `original/keys.c`, `original/highscore.c` present | No |
| 10. Window geometry | `original/stage.c`, `src/sdl2_regions.c` present | No |

Minor gap for area 6: `original/gun.c` is absent but its `TINK_DELAY=100`
(line 105) is already covered by `include/gun_system.h:50`
(`GUN_TINK_DELAY = 100` matches).

---

## No-Re-Audit Check

Pass 1 covered: DROP_BLK, ball animation, BONUSX2/X4/BONUS cycling,
BULLET_BLK vs MAXAMMO, bullet-sprite identity, pause overlay, instructions
partial, COUNTER_BLK, DEATH_BLK, EXTRABALL_BLK, ball birth/pop, NoWalls
border, presents letter spacing, score-digit alignment partial.

Pass 2 contract correctly excludes all of these. No wasted re-audit detected.

---

## Output Shape Check

Pass 1 produced 10 confirmed gaps in table-per-area format with a TOP-N
list. Pass 2 uses the same format. Combined output (pass 1 P1-P10 plus
pass 2 PL-1 through PL-6 plus new jck findings) will yield 16-20
prioritized gaps. This is implement-ready: each gap has a one-line fix
scope and a `src/` citation.

Requested improvement: the consolidated TOP-N list at the end of pass 2
should re-rank all gaps from both passes together, not just pass-2 gaps.
PL-1 (missing explosion animation) is higher priority than pass 1's
P3 (guide oscillation speed).

**Verbatim revision** — add to `success_criteria`:

```yaml
  - TOP-N list re-ranks ALL gaps from both passes combined, ordered by
    player visibility x frequency.
```

---

## Hypotheses Missed Check

Both passes together overlook three areas:

1. **TIMER_BLK rendering** — `sprite_block_key(TIMER_BLK)` returns
   `SPR_BLOCK_CLOCK` (`src/sprite_catalog.h:344-345`). The original renders
   the clock block with time remaining overlaid as text. No composite path
   exists in the modern renderer. Neither pass audits this.

2. **Score multiplier display** — When x2/x4 bonus is active, does the score
   window flash or indicate the multiplier? Neither pass checks
   `original/score.c` for a multiplier indicator.

3. **Black block hit flash** — `BLACKHIT_BLK` maps to `SPR_BLOCK_BLACK_HIT`
   (`src/sprite_catalog.h:356-357`). When does modern code transition to
   BLACKHIT? Neither pass audits this transition.

These are suggestions for a potential pass 3 or for jck to flag during
pass-2 work if encountered.

---

## Resolution by leader

To be filled in by claude/COO after this review.

| # | Finding | Action |
|---|---------|--------|
| F1 | Add block\_system.c + game\_callbacks.c to read-set | |
| F2 | Add lives/level-number verifiability criteria | |
| F3 | Refine x4-cropping criterion | |
| F4 | Clarify fire-reaction = deveyes | |
| F5 | Add bonus screen animation criteria | |
| F6 | Replace dialogue.c cite with level.c EndTheGame | |
