# Reverse Special — Canonical Lifecycle and Modern Alignment

**Research for:** bead xboing-c-qnk — does the Reverse special persist across levels and ball deaths in a way that matches the original?
**Mission:** m-2026-05-02-007 (worker: jck, evaluator: jmf-pobox)
**Original source read:** `original/paddle.c`, `original/special.c`, `original/level.c`, `original/file.c`, `original/main.c`, `original/bonus.c`
**Modern code traced:** `src/game_rules.c`, `src/paddle_system.c`, `src/special_system.c`
**Date:** 2026-05-01

> Note: jck's role is read-only (Read/Grep/Glob/WebFetch). The analytical work in this document is jck's; the COO acted as scribe to file the artifact through the runtime. Same systemic gap as prior missions; tracked as xboing-c-xzt.

---

## 1. Ownership in the original

`reverseOn` is declared at `original/paddle.c:86`, NOT in `special.c`. It is a global int living in the paddle module. This is intentional — reverse is a paddle-control inversion, not a game-rule modifier.

The two functions that operate on it are:

- `original/paddle.c:137-141` — `SetReverseOff()` unconditionally sets `reverseOn = False`
- `original/paddle.c:143-153` — `ToggleReverse()` flips it (called when the ball hits a `REVERSE_BLK`)

`original/special.c:84-95` — `TurnSpecialsOff()` clears sticky, fastGun, noWalls, x2Bonus, x4Bonus. It does **NOT** call `SetReverseOff()`. Reverse is intentionally absent from `TurnSpecialsOff`.

This is the key ownership fact that explains the two-callers pattern below: `TurnSpecialsOff` cannot clear reverse because reverse isn't owned by `special.c`. Every site that needs to clear reverse must call `SetReverseOff` directly.

---

## 2. Canonical clear sites — original source

### On ball death (last ball lost)

`original/level.c:492` — `DeadBall()` calls `SetReverseOff()` directly, **before** `ResetBallStart`. This path fires only when `GetAnActiveBall() == -1 && livesLeft > 0` — i.e., the last active ball is gone and there are lives remaining.

### On level transition (new level starting)

`original/file.c:122` — `SetupStage()` calls `SetReverseOff()` unconditionally. `SetupStage` is the single entry point for every new level (called from `bonus.c:610` in `DoFinish()` after the bonus screen completes, and from `handleGameMode()` on first game start).

### What does NOT clear reverse

- `TurnSpecialsOff()` — does not touch `reverseOn`
- `CheckGameRules()` — calls `Togglex2Bonus(False)` / `Togglex4Bonus(False)` but not `SetReverseOff`
- `EndTheGame()` calls `TurnSpecialsOff()` but not `SetReverseOff`; on game-over the value leaks but is harmless because a new game resets everything via `handleGameMode → SetupStage`

---

## 3. Canonical lifecycle summary

- **Reverse activates**: ball hits `REVERSE_BLK` → `ToggleReverse()` from block-hit handler
- **Reverse persists across ball deaths within the same level UNTIL the last ball on screen dies** — a single surviving multiball can keep reverse active while other balls are dying
- **Reverse clears on last-ball-lost life deduction**: `original/level.c:492`
- **Reverse clears on level advance**: `original/file.c:122` (inside `SetupStage`)
- Reverse does NOT clear when entering bonus screen

A 1995 player would absolutely notice if reverse persisted into the next level — the level transition is the canonical "reset to neutral controls" moment.

---

## 4. Modern code alignment

### `game_rules_next_level` (`src/game_rules.c:179-230`)

Calls `special_system_turn_off(ctx->special)` at line 198 and `paddle_system_reset(ctx->paddle)` at line 215. Neither clears reverse.

`paddle_system_reset` (`src/paddle_system.c:250-262`) only resets position, dx, motion, and old_mouse_x — it does **not** clear `reverse_on`.

There is no call to `paddle_system_set_reverse(ctx->paddle, 0)` in `game_rules_next_level`. **Confirmed gap versus the original.**

| Original | Modern |
|----------|--------|
| `original/file.c:122` calls `SetReverseOff()` inside `SetupStage` (level-advance path) | `game_rules_next_level` does not call `paddle_system_set_reverse(ctx->paddle, 0)` |

### `game_rules_ball_died` (`src/game_rules.c:236-264`)

Decrements lives and resets the ball via `ball_system_reset_start`. No call to `paddle_system_set_reverse(ctx->paddle, 0)`.

| Original | Modern |
|----------|--------|
| `original/level.c:492` — `DeadBall` calls `SetReverseOff()` before `ResetBallStart`, only when last ball dies and lives remain | `game_rules_ball_died` does not call `paddle_system_set_reverse(ctx->paddle, 0)` in any branch |

### `game_rules_check` (`src/game_rules.c:270-285`)

Calls `special_system_turn_off` when the level completes (before transitioning to bonus screen). Original `CheckGameRules` at `original/level.c:406-408` only clears x2/x4 bonuses at this point — it does not call `SetReverseOff`. So the modern `special_system_turn_off` call here is broader than the original (but harmless, since `special_system_turn_off` correctly excludes reverse).

---

## 5. Verdict

**Both lifecycle questions are bugs in the modern code.**

Two missing `paddle_system_set_reverse(ctx->paddle, 0)` calls:

1. In `game_rules_next_level` (should match `original/file.c:122` — `SetReverseOff()` inside `SetupStage`)
2. In `game_rules_ball_died`, inside the branch where the last ball died and lives remain (should match `original/level.c:492` — `SetReverseOff()` inside `DeadBall`)

The bug manifests as: if a player was in Reverse when they lost their last ball or completed a level, the next ball/level starts with controls still inverted. This is what the maintainer observed during dogfood.

The `special_system`'s explicit refusal to handle `SPECIAL_REVERSE` (`src/special_system.c:78-80`) is **correct** — it matches the ownership split. The gap is purely in the callers.

---

## 6. Proposed minimal fix scope

Two single-line additions:

### Fix 1 — `game_rules_next_level`

Add after `paddle_system_reset` or `special_system_turn_off`:

```c
paddle_system_set_reverse(ctx->paddle, 0);
```

### Fix 2 — `game_rules_ball_died`

Add inside the "still have lives" branch, before `ball_system_reset_start`:

```c
paddle_system_set_reverse(ctx->paddle, 0);
```

Minimal fix scope: 2 lines added to `src/game_rules.c`. No new APIs, no module changes. The implement worker should also add a characterization test asserting reverse clears at both transition points.

---

## 7. Source citations

| Fact | Source |
|------|--------|
| `reverseOn` declared in paddle module (not special) | `original/paddle.c:86` |
| `SetReverseOff()` definition | `original/paddle.c:137-141` |
| `ToggleReverse()` definition | `original/paddle.c:143-153` |
| `TurnSpecialsOff()` does not include reverse | `original/special.c:84-95` |
| `SetReverseOff()` called on last ball death | `original/level.c:492` |
| `SetReverseOff()` called on level advance | `original/file.c:122` |
| Modern `game_rules_next_level` lacks the call | `src/game_rules.c:179-230` |
| Modern `game_rules_ball_died` lacks the call | `src/game_rules.c:236-264` |
| Modern `paddle_system_reset` does not clear reverse | `src/paddle_system.c:250-262` |
| Modern `paddle_system_set_reverse` is the correct function to call | `src/paddle_system.c:320` |
| Modern special_system correctly rejects SPECIAL_REVERSE | `src/special_system.c:78-80` |
