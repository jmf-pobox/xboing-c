# Spec Review: gun-feature-research (jck research contract)

**Date:** 2026-05-02
**Reviewer:** gjm (Glenford J. Myers, test-engineer)
**Mission:** m-2026-05-02-012
**Subject contract:** `.tmp/missions/gun-feature-research.yaml`
**Worker named in contract:** jck
**Evaluator named in contract:** jmf-pobox

---

## Verdict

**Accept with changes.**

Two blocking findings and three non-blocking findings. The blocking
findings prevent jck from answering contract questions completely
without accessing files not in his read-set.

---

## Findings

### F-1 — BLOCKING — `references` field — wrong file labeled "sound playback layer"

**Affected field:** `inputs.references` entry `original/sfx.c`

`original/sfx.c` is the visual special-effects system (screen shake,
scatter animation, sparkle). It has no `playSoundFile()` definition
and no gun audio content whatsoever. The audio dispatch jck needs is
declared in `original/include/audio.h` and implemented in
`original/audio/` (platform drivers). For the contract's specific
questions (gun audio cue names and volumes), the call sites are already
visible in `original/blocks.c:762-860` (`PlaySoundForBlock`) and
`original/gun.c:214-215, 287-289, 500-511` (`shootBullet`, `AddTink`).
No additional audio driver file is required, but `original/sfx.c`
should be removed and replaced.

**Verbatim revision — remove the sfx.c entry and replace it:**

```yaml
    - original/include/audio.h      # playSoundFile() declaration (name + volume params)
```

If jck needs to understand the volume scale or how playback works
internally, add:

```yaml
    - original/audio/ALSAaudio.c    # reference audio driver (volume behaviour)
```

The call sites in `original/gun.c` and `original/blocks.c` already
surface the function name, filename argument, and volume argument.
For the research document purposes `original/include/audio.h` alone
is sufficient.

---

### F-2 — BLOCKING — `references` field — `original/special.c` is missing

**Affected field:** `inputs.references`

The prior research document (`docs/research/2026-05-01-gun-firing-gating-investigation.md`)
already cited `original/special.c:127-130` for `ToggleFastGun()` and
the `fastGun` global. The contract asks jck to document FAST_GUN
behavior (contract question 7) and the lifecycle of `fastGun` across
level transitions (question 5). Both questions require reading
`original/special.c`:

- `original/special.c:78` — `int fastGun` global declaration
- `original/special.c:88` — `TurnSpecialsOff()` resets `fastGun` to
  False on level start
- `original/special.c:127-130` — `ToggleFastGun()` sets/clears flag
- `original/special.c:185` — usage of `fastGun` in special rendering
- `original/special.c:242` — save-game restore of `fastGun`

Without `original/special.c`, jck cannot answer question 7 completely,
and cannot fully document the lifecycle (question 5) — specifically
whether `fastGun` persists across level transitions (it does not:
`TurnSpecialsOff()` is called in `original/file.c:115` on every level
load, before `SetNumberBullets`).

**Verbatim revision — add to references:**

```yaml
    - original/special.c            # fastGun global, ToggleFastGun(), TurnSpecialsOff() lifecycle
    - original/include/special.h    # ToggleFastGun() prototype and FAST_GUN flag
```

---

### F-3 — NON-BLOCKING — success criterion 9 (modern current state) is missing `game_render_ui.c`

**Affected field:** `success_criteria`, criterion 9

The contract says the implement mission targets four files:
`src/gun_system.c`, `src/game_callbacks.c`, `src/game_render.c`,
`src/game_input.c`. The bullet counter UI (original topic 3, the ammo
belt in levelWindow) is rendered via `src/game_render_ui.c` — this
file exists and handles the level-window panel. The modern code has no
call to draw the ammo counter in `game_render_ui.c` (confirmed by
grep). If jck surveys modern current state only in the four named
files, he will not identify that `game_render_ui.c` is the correct
insert point for the bullet counter.

**Verbatim revision — add to references and to success criterion 9:**

In `inputs.references`:

```yaml
    - src/game_render_ui.c          # level-window panel — insert point for ammo counter
```

In success criterion 9, append:

```text
  including identifying src/game_render_ui.c as the rendering site
  for the bullet counter UI
```

---

### F-4 — NON-BLOCKING — test-coverage angle: contract does not ask jck to suggest testable scenarios

**Affected field:** `success_criteria`

The contract produces a research artifact consumed by an implement
mission. The implement mission will need characterization tests for
the gun feature. The current success criteria are silent on whether
jck should suggest deterministic test inputs. A research artifact
that names the pure-logic entry points (e.g. `gun_system_shoot`
precondition matrix, `gun_system_use_ammo` decrement/unlimited path,
`score_block_hit_points(BULLET_BLK, row)`) with concrete state
preconditions would let the implement worker (jdc) write CMocka unit
tests directly without re-reading the research.

**Verbatim revision — add a success criterion:**

```yaml
  - For each behavioral gap identified, suggest at least one
    deterministic test scenario: the system state entering the
    function, the function called, and the expected observable output
    (return value, state delta, or audio event name). No X11/SDL2
    dependencies — pure-logic seams only.
```

This is non-blocking because the implement worker can derive tests
from the research without it, but the cost of the additional criterion
is low and the value to the test writer is high.

---

### F-5 — NON-BLOCKING — scope gap: bullet-kills-ball score and ammo-at-ball-death not asked

**Affected field:** `context` question list, `success_criteria`

Two observable behaviors are not covered by the nine contract questions:

1. **Score for bullet killing a ball** — `original/gun.c:286` calls
   `ClearBallNow()` when a bullet hits a ball, but no score is awarded
   in that path. The modern `game_callbacks.c` bullet-block handler
   (`src/game_callbacks.c:279-296`) does award points for
   bullet-block hits via `score_block_hit_points()`. Whether a
   bullet-killed ball should yield score is not addressed, and an
   implement worker may incorrectly award or omit score here.

2. **Ammo granted on ball death/creation** — `original/ball.c:1804-1805`
   calls `AddABullet(display)` twice inside `ResetBallStart()`. The
   contract only asks about ammo lifecycle at level transitions and
   game-over (question 5). The per-ball-death grant of +2 ammo is
   mentioned in the prior research document but is not a named
   success criterion in this contract. The modern code's equivalent
   site (wherever `gun_system_add_ammo` is called in the ball
   lifecycle) needs to be confirmed.

**Verbatim revision — add to context question list:**

```text
  10. **Bullet kills ball — score** — does the original award points
      when a bullet kills a ball? Cite original/gun.c:ClearBallNow
      call and any score calls around it.
  11. **Ammo at ball death** — does each ball death grant ammo?
      Confirm original/ball.c:1804-1805 (AddABullet x2 in
      ResetBallStart) and identify the modern equivalent or gap.
```

**Verbatim revision — add to success_criteria:**

```yaml
  - Bullet-kills-ball score behavior documented (original awards
    zero; modern code confirmed to match or flagged as gap)
  - Ammo-at-ball-death grant confirmed: original/ball.c:1804-1805
    (AddABullet x2); modern equivalent or gap named with
    src/<file>.c:<line> citation
```

---

## Summary table

| ID | Severity | Field | Action |
|----|----------|-------|--------|
| F-1 | Blocking | `inputs.references` | Replace `original/sfx.c` with `original/include/audio.h` |
| F-2 | Blocking | `inputs.references` | Add `original/special.c` and `original/include/special.h` |
| F-3 | Non-blocking | `inputs.references`, success criterion 9 | Add `src/game_render_ui.c` as a reference and name it in criterion 9 |
| F-4 | Non-blocking | `success_criteria` | Add criterion requiring deterministic test scenarios per gap |
| F-5 | Non-blocking | `context`, `success_criteria` | Add questions 10-11 and matching criteria for bullet-kills-ball score and ammo-at-ball-death |

---

## Read-set completeness audit (full)

Files in draft read-set verified against gun feature questions:

| File | Status | Notes |
|------|--------|-------|
| `original/gun.c` | Required, present | Core mechanics, all present |
| `original/include/gun.h` | Required, present | MAX_BULLETS constant |
| `original/keys.c` | Required, present | Confirms `<k>` = Shoot at line 213 |
| `original/keysedit.c` | Useful, present | No additional gun content found |
| `original/main.c` | Required, present | `original/main.c:490-494` — `XK_k`/`XK_K` dispatch to `shootBullet()` and mouse button fire |
| `original/blocks.c` | Required, present | `PlaySoundForBlock()`, BULLET_BLK/MAXAMMO_BLK handlers |
| `original/include/blocks.h` | Required, present | `NUMBER_OF_BULLETS_NEW_LEVEL=4`, `BONUS_DELAY` constants |
| `original/level.c` | Required, present | `ReDrawBulletsLeft()`, `AddABullet()`, `DeleteABullet()` |
| `original/sfx.c` | **Wrong file** | Visual effects only — replace per F-1 |
| `original/init.c` | Useful, present | Calls `InitialiseLevelInfo` but geometry is in stage.c |
| `original/stage.c` | Required, present | `levelWindow` creation at line 247 |
| `original/file.c` | Required, present | Level-load sets ammo at line 117 |
| `original/ball.c` | Required, present | `AddABullet()` x2 at line 1804-1805 |
| `original/special.c` | **Missing** | fastGun global and lifecycle — add per F-2 |
| `original/include/special.h` | **Missing** | Prototype — add per F-2 |
| `src/gun_system.c` | Required, present | Modern state machine |
| `src/game_callbacks.c` | Required, present | Block hit handlers — BULLET_BLK/MAXAMMO_BLK missing |
| `src/game_input.c` | Required, present | SDL2I_SHOOT binding |
| `src/keys_system.c` | Required, present | SDL key mapping |
| `src/game_render.c` | Required, present | In-flight bullet render |
| `src/game_render_ui.c` | **Missing** | Level-window panel — bullet counter insert point |
| `include/gun_system.h` | Required, present | GUN_AMMO_PER_LEVEL constant |

---

## Verifiability assessment

All nine original success criteria are objectively pass/failable by the
evaluator (jmf-pobox):

- Criteria 1-8 each require one or more `original/<file>.c:<line>`
  citations and a verdict statement. Pass = citation present and
  verdict clearly stated; fail = citation absent or verdict absent.
- Criterion 9 requires `src/<file>.c:<line>` citations per topic with
  a gap label. Pass = every topic in criteria 1-8 has a corresponding
  modern citation or explicit "not implemented" statement; fail =
  topics missing.
- The additional criterion from F-4 (test scenarios) is verifiable:
  pass = at least one scenario (state + call + expected output) per gap;
  fail = any gap with no scenario.

No timing-dependent, hardware-dependent, or render-output criteria
are present. Verifiability is adequate.

---

## Output-shape assessment

The research document as specified will be actionable for an implement
mission. The nine-section structure maps directly to the four target
files. The implement worker can read section 2 (blocks) to know what
to add to `game_callbacks.c`, section 3 (UI) to know what to add to
`game_render_ui.c`, section 4 (in-flight bullets) to confirm
`game_render.c`, and sections 7-8 (FAST_GUN, click) to complete
`gun_system.c`.

The output-shape weakness is the absence of F-4 test scenarios. An
implement worker who writes code without test scenarios is completing
a bead with untested behavior. The contract should require jck to
surface the testable seams explicitly.

---

## Revision summary for contract author (claude/COO)

Apply these changes to `.tmp/missions/gun-feature-research.yaml`
before running `ethos mission create`:

1. In `inputs.references`: replace `original/sfx.c` with
   `original/include/audio.h` (and optionally `original/audio/ALSAaudio.c`).
2. In `inputs.references`: add `original/special.c` and
   `original/include/special.h`.
3. In `inputs.references`: add `src/game_render_ui.c`.
4. In `success_criteria`: append the test-scenario criterion (F-4).
5. In `context` and `success_criteria`: add questions 10-11 and
   matching criteria for bullet-kills-ball score and ammo-at-ball-death
   (F-5, optional but low-cost).
