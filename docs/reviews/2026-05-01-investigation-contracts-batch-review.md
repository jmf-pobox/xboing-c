# Batch Review: Four Investigation Contracts (2026-05-01 Dogfood Session)

Reviewer: gjm (Glenford J. Myers, test-expert)
Date: 2026-05-01

Contracts reviewed:

- `.tmp/missions/gun-firing-investigation.yaml` (xboing-jm8, worker jdc)
- `.tmp/missions/reverse-persistence-investigation.yaml` (xboing-c-qnk, worker jck)
- `.tmp/missions/skull-collision-investigation.yaml` (xboing-c-6x0, worker jdc)
- `.tmp/missions/ball-tunneling-investigation.yaml` (xboing-c-895, worker jdc)

---

## 1. Gun Firing Gating Bug (xboing-jm8)

### Verdict: accept-with-changes

### Finding 1.1

Severity: blocking | Field: context

The contract states both symptoms as bugs: "K fires without gun pickup AND K does not fire WITH pickup." The first symptom — firing without a pickup — is almost certainly the ORIGINAL intended behavior, not a regression. In `original/gun.c:491-513`, `shootBullet` checks only `GetNumberBullets() > 0`, with no guard on `fastGun`. Ammo is granted at ball creation (`original/ball.c:1803-1805`, 2 bullets per ball) and at level start. `fastGun` controls single vs dual fire, not whether firing is allowed at all. The worker needs to verify this before declaring the no-pickup case a bug — otherwise the fix scope will be wrong.

Verbatim revision text (append to context field):

```yaml
  NOTE: In original/gun.c:491-513, shootBullet() guards on numBullets > 0,
  not on fastGun. Firing without an MGUN pickup may be ORIGINAL behavior —
  ammo is granted by level start (original/level.c) and ball creation
  (original/ball.c:1803). The worker must check original/gun.c:491 and
  original/ball.c:1803 before classifying "fires without pickup" as a bug.
  If it is original behavior, document it as such; the fix scope covers only
  the "does not fire WITH pickup" direction.
```

### Finding 1.2

Severity: blocking | Field: inputs.references

`src/game_rules.c` is missing from the read-set. `game_rules_next_level` (line 222) calls `gun_system_set_ammo(ctx->gun, GUN_AMMO_PER_LEVEL)` — this is the source of ammo the player has at K-press before ever hitting an MGUN block. Without this file, the worker cannot fully trace "why does K fire without a pickup." The worker needs to see that ammo arrives unconditionally at every level start.

Verbatim revision text (add to references list):

```yaml
    - src/game_rules.c
    - include/game_rules.h
```

### Finding 1.3

Severity: non-blocking | Field: success_criteria

The success criterion "Proposed minimal fix scope" does not require the worker to distinguish whether direction-1 ("fires without pickup") is a bug. If it is not a bug, the implement mission only needs to fix direction-2. An ambiguous fix scope wastes the implement worker's time.

Verbatim revision text (add new bullet to success_criteria):

```yaml
  - Explicit verdict on whether "fires without pickup" is a legacy regression
    or original behavior, citing original/gun.c:<line> and original/ball.c:<line>
```

---

## 2. Reverse Persistence Research (xboing-c-qnk)

### Verdict: accept-with-changes

### Finding 2.1

Severity: blocking | Field: inputs.references

`src/game_rules.c` and `include/game_rules.h` are missing. These are the modern analogs of the two legacy transition sites the contract targets. `game_rules_next_level` (line 198) calls `special_system_turn_off` at level transition. `game_rules_ball_died` (lines 236-264) handles ball death but does NOT call `special_system_turn_off` or `paddle_system_set_reverse(0)`. This is the key modern-wiring question the contract asks — is `special_system_turn_off` called at ball deaths? — and the answer is in `src/game_rules.c`. Without it the worker cannot answer sub-question 3 ("Modern alignment") for the ball-death lifecycle.

Verbatim revision text (add to references list):

```yaml
    - src/game_rules.c
    - include/game_rules.h
```

### Finding 2.2

Severity: non-blocking | Field: success_criteria

The criterion "Modern wiring traced — does special_system_turn_off fire at the same trigger points? Does it cascade to paddle_system_set_reverse(0)?" is verifiable but underspecified for the ball-death path. The original `DeadBall` (`original/level.c:474`) calls `SetReverseOff()` only when `GetAnActiveBall() == -1` (the last ball died). The modern `game_rules_ball_died` has the same guard (`ball_system_get_active_count > 0 → return`), but calls neither `special_system_turn_off` nor `paddle_system_set_reverse`. The criterion should require the worker to document the guard condition in both codebases, not just whether the call exists.

Verbatim revision text (replace the wiring bullet):

```yaml
  - Modern wiring traced for BOTH trigger points:
      (a) Level transition: does game_rules_next_level call special_system_turn_off
          and/or paddle_system_set_reverse(0)?
      (b) Ball death: does game_rules_ball_died call special_system_turn_off
          and/or paddle_system_set_reverse(0), under the same guard condition
          (last active ball only) as DeadBall in original/level.c?
    Cite src/game_rules.c:<line> for each.
```

### Finding 2.3

Severity: non-blocking | Field: context

The context mentions `include/special_system.h:128-130` documenting `special_system_turn_off` as "matches legacy TurnSpecialsOff behavior — clears all specials except saving." The comment is accurate for the flags owned by `special_system`, but the worker needs to be aware that REVERSE is NOT owned by `special_system` (it is rejected by `special_system_set` per `include/special_system.h:123`). `TurnSpecialsOff` in the original handles `reverseOn` via `SetReverseOff()` (called at `original/special.c:160` or `original/level.c:492`). The contract correctly notes this in the wiring question but does not make it explicit enough in the context, risking the worker assuming `special_system_turn_off` clears reverse.

Verbatim revision text (append to context field):

```yaml
  IMPORTANT: REVERSE is not owned by special_system in the modern code.
  special_system_turn_off does NOT clear paddle reverse (see
  include/special_system.h:123). The worker must check separately whether
  paddle_system_set_reverse(0) is called at each transition point.
```

---

## 3. Skull (Death) Block Collision (xboing-c-6x0)

### Verdict: accept-with-changes

### Finding 3.1

Severity: blocking | Field: context

The contract assumes the skull collision dispatch is the bug site, but the evidence points to a different layer. In `src/game_callbacks.c:89-91`, `ball_cb_on_block_hit` handles `DEATH_BLK` by clearing the block and returning 1. Returning 1 causes `ball_system.c` to `return` from the inner loop — it stops bouncing, but does NOT transition the ball to `BALL_POP`. The `ball_index` parameter is cast to `(void)ball_index` at `src/game_callbacks.c:68` — the callback never calls `ball_system_change_mode`. The "no effect" symptom is not a missing dispatch case but a missing `ball_system_change_mode` call in the DEATH_BLK handler. The contract should require the worker to check this path.

Verbatim revision text (append to context field):

```yaml
  Additional hypothesis: the DEATH_BLK case may be reached and handled
  (block cleared, return 1) but the ball is not killed because
  ball_cb_on_block_hit casts ball_index to void and never calls
  ball_system_change_mode. The worker must check whether returning 1 from
  on_block_hit is sufficient to kill the ball, or whether an explicit
  ball_system_change_mode(BALL_POP) call is required. Cite
  src/game_callbacks.c:<line> and src/ball_system.c:<line> (where the
  return value is acted on).
```

### Finding 3.2

Severity: non-blocking | Field: context

The contract says "Verify level 1 actually contains skull blocks" as a precondition. Level01 has no `D` characters (checked against `levels/level01.data` and the parser mapping at `src/level_system.c:127-130`). The worker should be aware the skull blocks may not appear until a later level, and the dogfood report may refer to a different level. The contract should not restrict the verification to level 1 only.

Verbatim revision text (replace the skull-block-verification bullet in success_criteria):

```yaml
  - Verification of which levels contain skull blocks (search levels/*.data
    for 'D' character, cite the level file(s) and parsing path
    src/level_system.c:<line>). If level 1 has no skull blocks, note that
    the dogfood symptom may have occurred on a different level or may have
    been a different block type misidentified as skull.
```

### Finding 3.3

Severity: non-blocking | Field: inputs.references

`include/block_types.h` is absent from the read-set. It defines `DEATH_BLK 10` and is the canonical numeric mapping the worker will need to trace the type value through block_system and game_callbacks. Without it the worker reads constants without anchoring to their definition.

Verbatim revision text (add to references list):

```yaml
    - include/block_types.h
```

---

## 4. Ball Tunneling Through Block Rows (xboing-c-895)

### Verdict: accept-with-changes

### Finding 4.1

Severity: blocking | Field: inputs.references

Line 12 of the contract lists `ball_math.c` without a path prefix. The file lives at `src/ball_math.c`. A worker following the contract verbatim may fail to locate it (there is no `ball_math.c` at the repo root). The include header at `include/ball_math.h` is also absent.

Verbatim revision text (replace the bare `ball_math.c` entry):

```yaml
    - src/ball_math.c
    - include/ball_math.h
```

### Finding 4.2

Severity: non-blocking | Field: context / hypotheses

The three hypotheses are well-formed, but hypothesis 1 (tunneling at high speed) lacks a fourth candidate the worker should discriminate: the swept-collision step count may be computed correctly but the adjacency filter in `block_system_check_region` (which suppresses hits when the neighbor cell in the hit direction is occupied) could suppress hits on both rows when the ball is between them. This is a distinct mechanism from hypothesis 3 (off-by-one in bounds) and would produce the same "two rows passed without breaking" symptom. The worker should be asked to check `src/block_system.c` adjacency filter logic.

Verbatim revision text (append to context field):

```yaml
  4. Adjacency-filter over-suppression — block_system_check_region
     suppresses a hit when the neighbor in the hit direction is occupied
     (to prevent phantom bounces). If two rows of blocks are densely packed,
     row N suppresses the hit because row N+1 exists; when the ball reaches
     row N+1, it has already passed the edge. Worker should check
     src/block_system.c adjacency filter and cite whether it could suppress
     hits across two adjacent occupied rows.
```

### Finding 4.3

Severity: non-blocking | Field: success_criteria

"Proposed reproducer: a deterministic ball trajectory + block grid configuration that should reproduce the bug in a unit test" is good but needs one more constraint: the reproducer must include the ball speed (dx, dy) and frame parameters, because tunneling is speed-dependent. Without these values the implement worker cannot construct a deterministic unit test.

Verbatim revision text (replace the reproducer bullet):

```yaml
  - Proposed reproducer: a deterministic ball trajectory (initial x, y, dx, dy),
    block grid configuration (which rows/cols are occupied), and ball_system_env_t
    parameters (speed_level, play_width, play_height, col_width, row_height)
    sufficient for an implement worker to reproduce the tunnel in a CMocka unit
    test using ball_system_update with real block_system_check_region callbacks.
```

---

## Summary Table

| Contract | Verdict | Blocking findings | Non-blocking findings |
|---|---|---|---|
| gun-firing-investigation | accept-with-changes | 2 (1.1, 1.2) | 1 (1.3) |
| reverse-persistence-investigation | accept-with-changes | 1 (2.1) | 2 (2.2, 2.3) |
| skull-collision-investigation | accept-with-changes | 1 (3.1) | 2 (3.2, 3.3) |
| ball-tunneling-investigation | accept-with-changes | 1 (4.1) | 2 (4.2, 4.3) |

No contract is rejected. All four are viable with the changes above applied before spawning workers. The blocking findings in each contract must be resolved before execution — workers sent without them will either reach wrong conclusions (gun, skull) or be unable to locate key files (tunneling, reverse).
