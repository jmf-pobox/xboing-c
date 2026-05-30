# Spec Review: Visual Fidelity Research Contract

**Reviewer:** gjm (Glenford J. Myers, test-engineer)
**Reviewed:** `.tmp/missions/visual-fidelity-research.yaml`
**Date:** 2026-05-02
**Verdict:** ACCEPT WITH CHANGES

Two blocking findings, four non-blocking. The contract commissions
the right work but has read-set gaps for bullet rendering (area 5)
and animated blocks, a testability gap that leaves several areas
subjective, and a scope-sizing problem that makes 15 areas in 1 round
risky. Revision text is provided verbatim for each finding.

---

## Finding 1 — BLOCKING: `original/gun.c` missing from read-set

**Affected field:** `inputs.references`
**Severity:** Blocking

Areas 5 (bullet rendering) and the ammo belt portion of area 2 (level
info panel) both depend on `original/gun.c`. That file is the canonical
authority for:

- `BULLET_WIDTH 7`, `BULLET_HEIGHT 16` (gun.c:82-83)
- `DrawTheBullet()` implementation (gun.c:534) — the function called by
  `level.c:AddABullet` and `level.c:ReDrawBulletsLeft` to draw the
  ammo belt
- The confirmation that the in-flight bullet sprite (`bullet_xpm`,
  gun.c:56) and the ammo-belt sprite are **the same pixmap** — the
  contract explicitly requires answering this question (success
  criterion 3), but the file that proves it is absent

Without `original/gun.c`, jck cannot answer the "same sprite or
different?" question from source evidence.

**Verbatim revision — add to `inputs.references`:**

```yaml
    - original/gun.c                    # bullet sprite identity (BULLET_WIDTH=7,
                                        # BULLET_HEIGHT=16), DrawTheBullet() used
                                        # for both in-flight and ammo belt rendering
```

---

## Finding 2 — BLOCKING: `src/gun_system.c` missing from read-set

**Affected field:** `inputs.references`
**Severity:** Blocking

The bullet rendering area requires comparing constants. `src/game_render.c`
(already in the read-set) references `GUN_BULLET_FRAME_RATE`,
`GUN_BULLET_WC`, `GUN_BULLET_HC`, `GUN_MAX_BULLETS`, `GUN_TINK_WC`,
`GUN_TINK_HEIGHT` — all defined in `gun_system.h` / `gun_system.c`. To
flag a gap between `BULLET_WIDTH=7` (original) and any modern value,
jck must read the modern file. Same for `GUN_MAX_TINKS` vs.
`MAX_TINKS 40` in gun.c:103.

**Verbatim revision — add to `inputs.references`:**

```yaml
    - src/gun_system.c                  # modern bullet/tink constants and frame rate
    - src/gun_system.h                  # GUN_BULLET_WIDTH, GUN_TINK_HEIGHT, GUN_MAX_TINKS
```

---

## Finding 3 — NON-BLOCKING: Animated block system files missing

**Affected field:** `inputs.references`
**Severity:** Non-blocking (contract is still executable; finding flags
incomplete coverage for area 1 animated blocks)

Area 1 asks about "animated blocks (bomb fuse, hyperspace shimmer,
etc.)". The animation tick logic for these lives in
`src/block_system.c`, which is absent. `src/game_render.c` shows
the rendering call to `block_system_get_render_info()` but the per-type
animation slide advancement is in `block_system.c`. Without it, jck
cannot compare the original `blockP->nextFrame` tick logic
(original/blocks.c) against the modern advance cadence.

Also: `original/gun.c` includes `original/gun.h` which defines the
separate `tinks[]` pool constants. `src/block_system.h` defines
`block_system_render_info_t` fields that control which frame is displayed
(including `explode_slide` range). Without this, frame count verification
for the explosion is incomplete.

Additionally: the `DROP_BLK` sprite mapping in
`src/sprite_catalog.h:339` returns `SPR_BLOCK_RED` with a comment
"Drop uses row-dependent coloring." The original (`original/blocks.c:1727`)
draws `DROP_BLK` as a **green** block (`greenblock`) with a hitPoints
digit overlay — not red. This is a confirmed gap the contract's
reference set would not catch because `src/sprite_catalog.h` is
referenced but `original/blocks.c:1727` documents the discrepancy.
The contract should explicitly require jck to audit this case.

**Verbatim revision — add to `inputs.references`:**

```yaml
    - src/block_system.c                # per-type animation tick advancement,
                                        # explode_slide max values
    - src/block_system.h                # block_system_render_info_t fields
    - include/block_system.h            # if separate from src/block_system.h
```

**Verbatim addition to area 1 context text:**

```text
  - DROP_BLK specifically: original/blocks.c:1727 draws DROP_BLK as
    a green block with a hitPoints digit overlay. src/sprite_catalog.h:339
    maps DROP_BLK to SPR_BLOCK_RED — no digit overlay. Document gap.
```

---

## Finding 4 — NON-BLOCKING: Verifiability gaps — several areas need explicit proxy criteria

**Affected field:** `success_criteria` / `context`
**Severity:** Non-blocking

Five areas are currently described in ways the leader cannot pass/fail
objectively:

**Area 3 (score/lives/timer) — blink states:** "any blink/highlight
state for events (extra life, time warning, etc.)" is a behavioral
question. The proxy: does `game_render_score` or `game_render_timer`
have conditional color changes? If there is no conditional, that is the
gap. The criterion should ask jck to grep for conditional color logic in
the modern render and compare against `original/score.c` and
`original/main.c`.

**Area 7 (paddle) — sticky-mode highlight and reverse visual feedback:**
"visual feedback" is subjective without a proxy. The proxy is: does
`game_render_paddle` call any function conditioned on
`paddle_system_get_sticky()` or `paddle_system_get_reverse()` to modify
the sprite or draw an overlay? Binary yes/no.

**Area 10 (particle effects) — "paddle hit sparkle":** Original
`original/ball.c:417` has `BALL_ANIM_RATE`. The modern
`src/ball_system.c:918` uses `(env->frame % (BALL_FRAME_RATE * 3))`.
These are not the same constant. The contract should ask jck to
document the rate mismatch explicitly.

**Area 12 (pause overlay):** The modern `game_render_frame` (game_render.c:956)
renders `SDL2ST_PAUSE` through the same case as `SDL2ST_GAME` — no
dedicated pause overlay. `original/main.c:258` sets a message string
"- Game paused -" via `SetCurrentMessage()` which renders in the message
strip. The proxy: does the modern render show any pause-specific visual?
Check that `game_render_messages()` is called in the PAUSE case path.

**Area 13 (game over):** `original/dialogue.c` (in the read-set) handles
game-over overlays, but the modern `game_render_dialogue` renders a
generic TTF box, not a sequence animation. The proxy: count sprite
elements in `original/dialogue.c` DrawGameOver sequence vs. modern
dialogue_system state machine.

**Verbatim addition to `success_criteria`:**

```yaml
  - Area 3: game_render_score / game_render_timer color-switch logic
    documented — any blink or conditional color is either present (cite
    src line) or absent (gap flagged)
  - Area 7: game_render_paddle sticky/reverse conditional rendering
    documented — binary yes/no with src citation
  - Area 10: ball sparkle/star rate documented — BALL_ANIM_RATE vs
    BALL_FRAME_RATE*3 discrepancy surfaced explicitly
  - Area 12: pause overlay method documented — message-strip vs.
    dedicated overlay, with src citation
```

---

## Finding 5 — NON-BLOCKING: `original/inst.c` absent for instruct screen (area 14)

**Affected field:** `inputs.references`
**Severity:** Non-blocking

Area 14 covers "attract / intro / presents / keys / highscore screens."
The modern render includes `SDL2ST_INSTRUCT` mode (game_render.c:916)
calling `game_render_instruct()` which calls
`intro_system_get_instruct_text()`. The reference for the original
instructions screen layout is `original/inst.c` (present in the
repository at `original/inst.c`). That file is absent from the
read-set. The contract includes `original/intro.c` but the instruct
screen is a separate state with its own sequencing logic (inst.c:108-280).

**Verbatim revision — add to `inputs.references`:**

```yaml
    - original/inst.c                   # instructions screen state machine
                                        # and text layout (InstructState sequence)
```

---

## Finding 6 — NON-BLOCKING: Scope sizing — 15 areas in 1 round is feasible but tight

**Affected field:** `budget`, `context`
**Severity:** Non-blocking

jck's role is read-only — no code execution. The 15 areas require
reading approximately 25 source files and extracting concrete constants.
That is achievable in 1 round if:

1. The research artifact is organized as a table (area → gap → severity
   → fix scope) rather than prose
2. Subjective areas (attract screens) are bounded by "compare screenshot
   pixel dimensions of major elements" rather than pixel-level fidelity

The contract already asks jck to "surface the BIGGEST gaps first
(sized by user-visible impact)." That is the right shape. No split is
recommended — but the contract should explicitly authorize jck to use a
table structure per area to avoid bloat.

**Verbatim addition to `context` (append to final paragraph):**

```text
  Structure the output as a table per area: | Gap | Original evidence
  (file:line) | Modern evidence (file:line) | Verdict | Fix scope |.
  Prose summary is optional per area. This format keeps the artifact
  scannable for the implement mission.
```

---

## Finding 7 — NON-BLOCKING: Missing hypothesis — animated block types not explicitly enumerated

**Affected field:** `context` area 1
**Severity:** Non-blocking

Area 1 mentions "animated blocks (bomb fuse, hyperspace shimmer, etc.)"
but does not enumerate all animated types. The original animates:
BONUSX2_BLK, BONUSX4_BLK, BONUS_BLK (bonus coins), RANDOM_BLK
(cycles through bonus frames), TIMER_BLK (clock face). `src/sprite_catalog.h`
maps BONUSX2_BLK to `SPR_BLOCK_X2_1` (frame 1 only — no cycling in the
key lookup for normal display). For BONUSX4 and BONUS_BLK, same issue.
Without enumerating these, jck may not notice the modern static key
vs. original per-frame animation.

**Verbatim addition to area 1 context text:**

```text
  Also check animated cycling for BONUSX2_BLK (original cycles through
  x2bonus1-4), BONUSX4_BLK (x4bonus1-4), BONUS_BLK (bonus1-4), and
  RANDOM_BLK. sprite_catalog.h returns only frame-1 keys for these
  in normal display. Document whether block_system.c advances the slide
  for these types at all.
```

---

## Summary

| # | Severity | Field | One-line description |
|---|----------|-------|----------------------|
| 1 | BLOCKING | inputs.references | `original/gun.c` missing — required to answer bullet same/different |
| 2 | BLOCKING | inputs.references | `src/gun_system.c/.h` missing — modern bullet constants unverifiable |
| 3 | Non-blocking | inputs.references + context | `src/block_system.c/.h` missing; DROP_BLK green+digit not auditable |
| 4 | Non-blocking | success_criteria | Five areas lack objective pass/fail proxies |
| 5 | Non-blocking | inputs.references | `original/inst.c` missing for instructions screen |
| 6 | Non-blocking | context | Output format should be table-per-area for implement-mission usability |
| 7 | Non-blocking | context area 1 | Animated-cycling blocks (X2, X4, BONUS, RANDOM) not enumerated |

Resolve findings 1 and 2, then the contract is ready to execute.
