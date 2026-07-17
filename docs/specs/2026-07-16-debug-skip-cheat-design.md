# Debug Skip-Level Cheat: Clean Design

Mission: m-2026-07-17-005 (design). Follows PR #194 / ADR-072 /
ADR-073.

## Principle

We mirror the original's **gameplay behavior** — which blocks the
`=` cheat clears, that each awards full score, the "touch" burst, the
140-frame shake, the level then completing normally. We do **not**
mirror the original's **code structure**. The 1996 code inlined the
grid sweep inside `SkipToNextLevel` and again inside
`StillActiveBlocks`, duplicating the block classification and putting
grid iteration wherever it was convenient. Our structure is designed
for this codebase's invariants: `block_system` owns the grid behind an
opaque context; a rule is expressed once; every step is unit-testable
in isolation. Original-source citations below justify **behavior**,
never **placement**.

## Problem with the shipped code

`game_input_global` (`src/game_input.c`) inlined ~80 lines behind the
`=` keypress: a `MAX_ROW` x `MAX_COL` grid scan, an 8-case block-type
`switch`, per-cell `block_system_explode`, the "touch" sound, the
shake, `ctx->cheated`, and the message. Two structural faults:

1. **The input layer reaches into the block grid.** It calls
   `block_system_is_occupied` / `block_system_get_type` /
   `block_system_explode` cell-by-cell. Input dispatch has no business
   iterating a grid it does not own.
2. **The "required block" rule is encoded twice** — once as this
   handler's explicit `switch`, once (as the inverse) inside
   `block_system_still_active`. Two encodings of one fact can drift.

The first cut of this refactor moved the body to
`game_rules_skip_level` but **kept the grid loop there** — trading an
input-layer grid leak for a rules-layer grid leak. Still wrong: only
`block_system` should iterate the grid.

## Structure

Three layers, each with one responsibility. Grid knowledge lives only
in `block_system`.

### `block_system` — owns the grid, owns the rule

```c
/*
 * Return nonzero if block_type must be cleared for the level to
 * complete: RED, BLUE, GREEN, TAN, YELLOW, PURPLE, COUNTER, DROP.
 * 0 for every other value (NONE, KILL, the 19 non-required specials,
 * out-of-range). Single source of truth: block_system_still_active()
 * and block_system_explode_all_required() both consult it, so the
 * completion check and the cheat's kill-set can never disagree.
 */
int block_system_type_is_required(int block_type);

/*
 * Explode every occupied required block through the normal explosion
 * lifecycle (block_system_explode -> update_explosions -> finalize
 * callback -> score). Returns the number of blocks armed.
 *
 * The one place grid iteration for "clear the required blocks" lives.
 * Callers pass only a frame; they hold no grid coordinates and make
 * no per-cell decisions. Pure block-grid mutation — no audio, sfx,
 * score, or game-context dependency — so it unit-tests against a
 * bare block_system_t.
 */
int block_system_explode_all_required(block_system_t *ctx, int frame);
```

`block_system_still_active` consults the same predicate:

```c
for (r, c)
    if (bp->occupied && block_system_type_is_required(bp->block_type))
        return 1;
```

### `game_rules` — orchestrates cross-subsystem effects

`game_rules_skip_level` holds **zero** grid knowledge. It sequences
the subsystem effects the cheat produces and delegates the grid work:

```c
void game_rules_skip_level(game_ctx_t *ctx, int frame)
{
    if (ctx == NULL)
        return;

    int cleared = block_system_explode_all_required(ctx->block, frame);

    /* One "touch" for the whole wave. Every required type maps to the
     * same sound; ~44 per-block plays would exhaust the 16-channel
     * mixer (sdl2_audio.c) and spam "no free channel". Audibly
     * identical to the original's burst. */
    if (cleared > 0 && ctx->audio)
        sdl2_audio_play_at_percent(ctx->audio, "touch", 99);

    if (ctx->sfx)
    {
        sfx_system_set_mode(ctx->sfx, SFX_MODE_SHAKE);
        sfx_system_set_end_frame(ctx->sfx, frame + 140);
    }

    ctx->cheated = true;   /* forfeits every high-score board, ADR-073 */
    message_system_set(ctx->message, "Cheating, skip level ...", 1, frame);
}
```

Behavior provenance (not structure): the score-on-explode path is the
original's (`original/blocks.c:1543-1548`, `AddToScore` at explosion
finalize); the shake is `original/blocks.c:2460-2461`. The level then
completes on the normal per-tick `game_rules_check` path once
`block_system_still_active` reads 0 — the cheat forces no transition.

### `game_input` — dispatch only

```c
if (sdl2_input_just_pressed(ctx->input, SDL2I_DEBUG_SKIP))
{
    if (sdl2_input_shift_held(ctx->input))
        /* Shift+'=' ('+') is volume up (ADR-072). */;
    else if (mode == SDL2ST_GAME)
        ctx->debug_mode ? game_rules_skip_level(ctx, frame)
                        : message_system_set(ctx->message,
                              "Stop trying to cheat!!", 1, frame);
}
```

No grid types, no block includes. It decides *whether* to skip; it
never knows *how*.

## Required-block set

`block_system_type_is_required` returns nonzero for exactly these 8
types and 0 for everything else:

| Required | Value |
|---|---|
| `RED_BLK` | 0 |
| `BLUE_BLK` | 1 |
| `GREEN_BLK` | 2 |
| `TAN_BLK` | 3 |
| `YELLOW_BLK` | 4 |
| `PURPLE_BLK` | 5 |
| `COUNTER_BLK` | 8 |
| `DROP_BLK` | 20 |

This is a **behavior** decision, and it is the set the shipped code
already clears. `RANDOM_BLK`, `DYNAMITE_BLK`, `BLACKHIT_BLK` never
persist as a live `bp->block_type` (`RANDOM_BLK` becomes `RED_BLK` +
a flag at placement, `src/block_system.c:455-460`; the other two are
never placed), so the 8-type set is exhaustive.

### Roamer: a deliberate, documented behavior choice

The original's two functions disagree by exactly one type.
`StillActiveBlocks` (`original/blocks.c:2506-2533`) does **not** count
`ROAMER_BLK` toward completion; `SkipToNextLevel`
(`original/blocks.c:2427-2452`) **does** kill it (400 pts) as an
`default`-case side effect. A single predicate cannot both exclude
roamers from the completion check and include them in the kill-set.

**We exclude roamers (predicate mirrors the completion check).** This
is not "because the original did X" — it is our invariant: the cheat's
kill-set must equal the completion check it short-circuits, so the
cheat can never explode a block that fails to unblock the level, nor
leave one that would. The shipped code already behaves this way; the
lost roamer score was an incidental artifact of the original's own
inconsistency, unobservable to a 1995 player (the cheat is
debug-only). `docs/DESIGN.md` ADR-072's claim that the two original
lists are "the same" is factually wrong and is corrected as part of
this work.

## Synchronous on the input path

`game_input_global` calls `game_rules_skip_level(ctx, frame)`
directly. The explosion timer already advances on `frame >=
explode_next_frame` (`src/block_system.c`, committed 64b9976), so an
explosion armed at the input-path frame finalizes on the next tick
regardless of the input-vs-tick frame skew. Deferring to a
tick-consumed flag would add a `game_ctx_t` field and a consume site
for no correctness gain, and would make this one handler inconsistent
with every other synchronous handler in `game_input_global`. A
blanket "all gameplay mutations happen in the tick" rule would be a
separate, repo-wide decision.

## Testability

Each layer is checkable at its cheapest level:

1. `block_system_type_is_required` — pure function. Unit test every
   block type (all required return nonzero; all 19 specials + `NONE` +
   `KILL` + out-of-range return 0).
2. `block_system_explode_all_required` — unit test against a bare
   `block_system_t`: seed a grid mixing required + non-required +
   empty cells, call it, assert exactly the required cells are
   exploding, the non-required are untouched, and the return count
   matches. No `game_ctx_t`, audio, sfx, or message stubs needed.
3. `game_rules_skip_level` — integration test: after the call,
   `ctx->cheated` is set, the message is posted, and (ticking
   explosions to finalize) `block_system_still_active` drains to 0 so
   `game_rules_check` reaches `SDL2ST_BONUS`.

The pre-refactor code could only be tested through the input layer end
to end; this structure tests the rule and the sweep in isolation.

## Files

- `include/block_system.h` / `src/block_system.c` — keep
  `block_system_type_is_required`; add
  `block_system_explode_all_required`; `still_active` consults the
  predicate (drop its now-redundant inline classification comment).
- `include/game_rules.h` / `src/game_rules.c` —
  `game_rules_skip_level` becomes the thin orchestrator above (remove
  its grid loop; reframe the comment to cite behavior, not
  `SkipToNextLevel`'s placement).
- `src/game_input.c` — three-way dispatch only.
- `docs/DESIGN.md` — correct the ADR-072 "same exemption list" error;
  add one ADR covering the explosion-timer `>=` fix and this
  extraction (single-source predicate, grid sweep owned by
  `block_system`, roamer decision).

## Cross-links

- ADR-072 — `-debug` functional, `=` cheat restored
- ADR-073 — cheat forfeits high-score submission
- ADR-016 — `block_system` opaque-context ownership of the grid
- `original/blocks.c:1543-1548` — score at explosion finalize (behavior)
- `original/blocks.c:2460-2461` — shake (behavior)
