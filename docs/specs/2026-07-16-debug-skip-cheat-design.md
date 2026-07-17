# Debug Skip-Level Cheat: Clean Design

Mission: m-2026-07-17-005 (design, worker jdc, evaluator jck).
Follows PR #194 / ADR-072 / ADR-073. The maintainer flagged the
current handler: "this type of `-debug` = logic can easily get
hacky; it should be simple and clean."

## Problem statement

`game_input_global` (`src/game_input.c:247-341`) inlines ~80 lines
of gameplay logic behind the `=` keypress: a nested `MAX_ROW` x
`MAX_COL` grid scan, an 8-case block-type `switch`, a call into
`block_system_explode`, a single "touch" sound, `SFX_MODE_SHAKE`
arming, `ctx->cheated = true`, and a message post. None of this is
input handling — it is level-completion logic that happens to be
triggered by a key. It duplicates, inline, the same required-block
classification that `block_system_still_active`
(`src/block_system.c:1178-1243`) already encodes in its own switch,
with no shared source of truth between the two.

The original keeps the equivalent logic (`SkipToNextLevel`,
`original/blocks.c:2409-2462`) out of the key dispatcher entirely:
`handleGameKeys` (`original/main.c:511-522`) is a one-line call into
the block/level layer. This design restores that separation.

## Chosen structure

### `game_rules.c` / `game_rules.h` — new owner of the cheat body

```c
/*
 * Debug skip-level cheat -- the modern analog of SkipToNextLevel
 * (original/blocks.c:2409-2462).  Explodes every required block
 * (block_system_type_is_required()) through the real explosion
 * lifecycle, plays one "touch" sound if anything exploded, arms a
 * 140-frame screen shake, marks the session as cheated (ADR-073),
 * and posts the "Cheating, skip level ..." message.
 *
 * Caller must already have confirmed ctx->debug_mode and
 * SDL2ST_GAME -- this function does not re-check either.
 */
void game_rules_skip_level(game_ctx_t *ctx, int frame);
```

`game_rules_skip_level` owns:

- the grid scan + `block_system_type_is_required` filter
- `block_system_explode` calls
- the single deduplicated "touch" sound play
- `sfx_system_set_mode(ctx->sfx, SFX_MODE_SHAKE)` +
  `sfx_system_set_end_frame(ctx->sfx, frame + 140)`
- `ctx->cheated = true`
- `message_system_set(ctx->message, "Cheating, skip level ...", 1, frame)`

It does **not** re-derive `frame` from `ctx->state` — the caller
passes the value it already has in scope, matching the requested
signature and every other `game_rules_*` entry point's calling
convention.

### `src/game_input.c` — shrinks to a three-way dispatch

```c
if (sdl2_input_just_pressed(ctx->input, SDL2I_DEBUG_SKIP))
{
    if (sdl2_input_shift_held(ctx->input))
    {
        /* Shift+'=' ('+'): volume up -- unchanged, ADR-072. */
        if (ctx->audio)
        {
            int vol = sdl2_audio_volume_up(ctx->audio);
            char str[32];
            snprintf(str, sizeof(str), "Maximum volume: %d%%", vol);
            message_system_set(ctx->message, str, 1, frame);
        }
    }
    else if (mode == SDL2ST_GAME)
    {
        if (ctx->debug_mode)
            game_rules_skip_level(ctx, frame);
        else
            message_system_set(ctx->message, "Stop trying to cheat!!", 1, frame);
    }
}
```

Roughly 80 lines become roughly 18. `game_input.c` no longer
`#include`s anything block-grid-specific for this handler and no
longer needs to know what a "required block" is.

### `src/block_system.c` / `include/block_system.h` — shared predicate

```c
/* Return nonzero if block_type must be cleared before the level is
 * considered complete.  Single source of truth for
 * block_system_still_active() and game_rules_skip_level(). */
int block_system_type_is_required(int block_type);
```

`block_system_still_active` is rewritten to call it instead of
carrying its own inline switch:

```c
int block_system_still_active(const block_system_t *ctx)
{
    if (ctx == NULL)
        return 0;

    for (int r = 0; r < MAX_ROW; r++)
    {
        for (int c = 0; c < MAX_COL; c++)
        {
            const block_entry_t *bp = &ctx->blocks[r][c];
            if (bp->occupied && block_system_type_is_required(bp->block_type))
                return 1;
        }
    }

    if (ctx->blocks_exploding > 1)
        return 1;

    return 0;
}
```

`game_rules_skip_level`'s grid scan calls the same predicate instead
of its own `switch`:

```c
for (row, col)
{
    if (!block_system_is_occupied(ctx->block, row, col))
        continue;
    int block_type = block_system_get_type(ctx->block, row, col);
    if (!block_system_type_is_required(block_type))
        continue;
    if (block_system_explode(ctx->block, row, col, frame) == BLOCK_SYS_OK)
        cleared = true;
}
```

Return type is `int` (0/1), matching every other boolean-flavored
query in `block_system.h` (`block_system_is_occupied`,
`block_system_get_random`, etc. — none of this module uses
`stdbool.h`).

## Required-block set — exact enumeration

`block_system_type_is_required` returns nonzero for exactly these
8 types, and 0 for every other value (including `NONE_BLK`,
`KILL_BLK`, and any out-of-range value):

| Required (returns nonzero) | Value |
|---|---|
| `RED_BLK` | 0 |
| `BLUE_BLK` | 1 |
| `GREEN_BLK` | 2 |
| `TAN_BLK` | 3 |
| `YELLOW_BLK` | 4 |
| `PURPLE_BLK` | 5 |
| `COUNTER_BLK` | 8 |
| `DROP_BLK` | 20 |

This is a direct port of `StillActiveBlocks`'s exemption switch
(`original/blocks.c:2506-2533`): every type the original's `switch`
lists with a bare `break` is non-required; falling to `default`
returns `True` (still active, i.e. required). The break-list there
is: `BLACK_BLK`, `BULLET_BLK`, `ROAMER_BLK`, `BOMB_BLK`, `TIMER_BLK`,
`HYPERSPACE_BLK`, `STICKY_BLK`, `MULTIBALL_BLK`, `MAXAMMO_BLK`,
`PAD_SHRINK_BLK`, `PAD_EXPAND_BLK`, `REVERSE_BLK`, `MGUN_BLK`,
`WALLOFF_BLK`, `EXTRABALL_BLK`, `DEATH_BLK`, `BONUSX2_BLK`,
`BONUSX4_BLK`, `BONUS_BLK` — 19 types, the exact complement of the
8-type required set above.

**`RANDOM_BLK` and `DYNAMITE_BLK`/`BLACKHIT_BLK` are non-issues, not
edge cases needing special handling.** `block_system_add` never
stores `RANDOM_BLK` as `bp->block_type` — it immediately writes
`RED_BLK` and sets a `random` flag instead
(`src/block_system.c:455-460`), and the morph timer in
`block_system_update_movement` only ever writes one of `RED_BLK`,
`BLUE_BLK`, `GREEN_BLK`, `TAN_BLK`, `YELLOW_BLK`, `PURPLE_BLK`,
`BULLET_BLK` (`get_random_block_type`, `src/block_system.c:690-712`).
Every one of those is already covered by the 8-type table (or, for
`BULLET_BLK`, correctly excluded). `DYNAMITE_BLK` (25) never reaches
`block_system_add` at all — it drives the "clear all blocks of one
random color" bonus effect in `try_spawn_bonus`
(`src/game_rules.c:224-243`), never a placed block. `BLACKHIT_BLK`
(29) is a render-only state for a hit `BLACK_BLK`
(`src/block_sound.c:63-65`), never written to `bp->block_type`
either. None of these three ever appear as a live grid entry for
either `block_system_still_active` or the cheat to scan.

### Known divergence from `SkipToNextLevel` (documented, not fixed here)

`SkipToNextLevel`'s own exemption switch (`original/blocks.c:2427-
2452`) is **not** identical to `StillActiveBlocks`'s: it lists 18
non-required types and is missing `ROAMER_BLK` (22). Since
`ROAMER_BLK` falls to `SkipToNextLevel`'s `default` case, the 1996
cheat also explodes roamer blocks via `DrawBlock(..., KILL_BLK)` —
even though `StillActiveBlocks` does not count `ROAMER_BLK` toward
level completion. `ROAMER_BLK` is worth 400 points
(`src/score_logic.c:104-105`, matching `original/blocks.c` row-based
hit-point table), so the 1996 cheat awards bonus score, sound, and
animation for every roamer on the board, purely as an over-kill side
effect — not because roamers are required.

ADR-072 (`docs/DESIGN.md:4296-4301`) describes `SkipToNextLevel`'s
exemption list and `StillActiveBlocks`'s exemption list as "the same
exemption list" — that line is incorrect; they differ by exactly
`ROAMER_BLK`. The currently shipped cheat (`src/game_input.c`'s
explicit 8-case switch) already matches `StillActiveBlocks`, not
`SkipToNextLevel` — so this design changes no behavior, it only
gives the existing 8-type set a name and a single implementation.

**Decision: the shared predicate follows `StillActiveBlocks`, not
`SkipToNextLevel`.** The mission's requirement is
`cheat-cleared == still_active-counted, exactly` — a single
predicate cannot satisfy that AND reproduce `SkipToNextLevel`'s
roamer over-kill, because the two original functions disagree on
`ROAMER_BLK`. Matching `StillActiveBlocks` is the correct choice: it
keeps the cheat's completion effect exactly congruent with the
completion check it's short-circuiting (no risk of exploding a block
that doesn't unblock the level, or leaving one that does), at the
cost of not reproducing a roamer-only score bonus in a debug-only
cheat path. This is a below-the-radar deviation already present in
the shipped code; this design formalizes it rather than introduces
it. **Flagged for jck: if 1996 parity for the roamer over-kill
matters, it requires a second, cheat-specific predicate (or an
explicit `|| block_type == ROAMER_BLK` at the one call site) —
tell me and I'll add it; recommendation is to leave it out.**

## Input-path-synchronous vs deferred-to-tick

**Recommendation: (A) input-path-synchronous.** `game_input_global`
calls `game_rules_skip_level(ctx, frame)` directly, using the
`frame` value it already reads once per visual frame, relying on
the block-system explosion-timer `==` → `>=` fix
(`src/block_system.c:574`, landed in the mission the task brief
calls "ADR-070" — **that citation is wrong**; the actual ADR-070 in
`docs/DESIGN.md:4152` is about roamer/drop movement, not the
explosion-timer comparison. The `>=` fix is real and already in
`src/block_system.c:587`; it just isn't the ADR-070 entry. This
design cites the code location directly and leaves the correct ADR
number for whichever mission documents it).

Rationale:

1. **The frame-skew problem is already solved, independent of which
   option is chosen.** `game_input_global` reads `frame` before the
   tick loop runs (`src/game_main.c:150-157`); `block_system_add`
   (or here, `block_system_explode`) stamps `explode_next_frame =
   frame` at that pre-tick value. The later
   `block_system_update_explosions` check runs at tick time with
   `sdl2_state_frame(ctx->state)`, which is always >= the input-time
   value (frame only advances). The `>=` comparison
   (`src/block_system.c:587`) fires correctly on the very next tick
   regardless of how many ticks the fixed-timestep loop runs for
   that visual frame. This holds whether `game_rules_skip_level` is
   called from the input path or from the tick — the fix is in the
   explosion state machine, not the call site.

2. **Every other handler in `game_input_global` already mutates
   gameplay state synchronously from the input path** — volume
   changes, `ctx->cheated`, `message_system_set`,
   `sfx_system_set_mode`/`set_end_frame` for other keys. Deferring
   only the skip-level cheat to a tick-consumed edge flag would make
   this one handler inconsistent with every sibling handler in the
   same function, for a correctness problem that doesn't exist once
   the `>=` fix is in place. That is the "hacky, not simple and
   clean" failure mode the maintainer flagged, just moved rather
   than removed.

3. **Determinism is not at risk either way.** The replay/test harness
   (`tests/test_replay.c`) already calls `game_input_global` before
   `sdl2_state_update` inside a single `replay_tick`, matching
   production's ordering exactly (`docs/TESTING.md` Layer 2). Both
   `sdl2_input_just_pressed`/`shift_held` and `sdl2_state_frame` are
   deterministic given a fixed event script and a fixed-timestep
   frame counter (not wall-clock), so option (A) introduces no
   flakiness that option (B) would avoid.

4. **Option (B) costs more for no correctness gain.** Consuming an
   edge flag at the top of `mode_game_update` would require: a new
   `bool` field on `game_ctx_t` (another piece of session state to
   reset on `start_new_game`, same bookkeeping burden as
   `ctx->cheated` already carries), a consumption site in
   `game_modes.c`, and — if the sound/shake/message side effects are
   to keep firing at the same relative time as today — moving them
   out of the input path too, which is a strictly larger diff for a
   problem the `>=` fix already closed.

Option (B) remains available if a future architectural rule requires
"all gameplay mutations happen inside the tick, no exceptions" — that
would be a repo-wide convention change affecting every handler in
`game_input_global`, not a change scoped to this cheat, and is out of
scope here.

## Migration note

**Leaves `src/game_input.c`:**

- The `MAX_ROW` x `MAX_COL` grid scan and the 8-case block-type
  `switch` (lines 279-306 today)
- The `cleared` bool + single "touch" sound play (lines 316-317)
- The `SFX_MODE_SHAKE` arming (lines 322-326)
- `ctx->cheated = true` (line 332)
- `message_system_set(ctx->message, "Cheating, skip level ...", ...)` (line 334)

**Stays in `src/game_input.c`:** the `SDL2I_DEBUG_SKIP` just-pressed
check, the shift-held volume-up branch, the `mode == SDL2ST_GAME`
gate, the `ctx->debug_mode` gate, and the "Stop trying to cheat!!"
message — i.e. input dispatch only.

**Added to `include/game_rules.h` / `src/game_rules.c`:**

- `void game_rules_skip_level(game_ctx_t *ctx, int frame);`
  prototype + implementation, containing everything that left
  `game_input.c` above, rewritten to loop over
  `block_system_type_is_required` instead of an inline switch.

**Added to `include/block_system.h` / `src/block_system.c`:**

- `int block_system_type_is_required(int block_type);` — pure
  function, no `ctx` parameter, one `switch` on the 8-type required
  set (or its 19-type complement — either encoding is fine, they
  must stay total complements of each other).

**Changed in `src/block_system.c`:**

- `block_system_still_active` body replaced with the loop shown
  above, calling `block_system_type_is_required` instead of its own
  inline switch. No change to `block_system_still_active`'s
  signature, doc comment intent, or the `blocks_exploding > 1`
  tail check.

**No change** to `block_system_explode`, `block_system_update_explosions`,
`SFX_MODE_SHAKE` semantics, ADR-073's `ctx->cheated` gate on
high-score submission, or the two user-facing messages.

## Cross-links

- ADR-072 (`docs/DESIGN.md:4280`) — `-debug` functional, `=` cheat restored
- ADR-073 (`docs/DESIGN.md:4424`) — cheat disqualifies high-score submission
- `original/blocks.c:2409-2462` — `SkipToNextLevel`
- `original/blocks.c:2482-2544` — `StillActiveBlocks`
- `original/main.c:511-522` — `handleGameKeys` `XK_equal` dispatch
