# SFX Testability — Design Spec

- **Status:** Revised after peer review — ready for implementation
- **Worker:** Claude (COO)
- **Reviewer:** `feature-dev:code-reviewer` (agent-to-agent)
- **Review:** [docs/reviews/2026-06-03-sfx-testability.md](../reviews/2026-06-03-sfx-testability.md) — APPROVE-WITH-CHANGES
- **Created:** 2026-06-03
- **Motivating bead:** `xboing-c-buv` (PR #138 — restored 15 silent block hit
  sounds + fixed broken `"hyperspace"` → `"hypspc"` misname). That bug class
  was invisible to the existing test suite.

## Problem

Sound effects have ~24 call sites across `src/`, all calling
`sdl2_audio_play(ctx->audio, "<name>")` with a string literal. Three
classes of bugs are currently invisible to CI:

1. **Misnamed sounds** — `"hyperspace"` was used for ~20+ commits; the
   real asset is `"hypspc"`. `sdl2_audio_play` returned
   `SDL2A_ERR_NOT_FOUND` silently. Nothing failed.
2. **Missing sounds at events** — 15 of 19 block types had no sound at
   all. No test asserts which gameplay event should fire which sound, so
   the gap was invisible until a user noticed silence.
3. **Wrong-sound regressions** — if someone refactors and accidentally
   plays `"bomb"` on DEATH_BLK instead of `"evillaugh"`, all tests pass.

Test coverage today:

- `tests/test_sdl2_audio.c` (40 tests) — module mechanics (create/destroy,
  volume, NULL guards, cache count, status strings, play-returns-OK for
  known names). Does NOT assert which event triggers which sound.
- `tests/test_game_callbacks.c` — 0 references to `audio`. Asserts block
  cleared / ball state / ammo count after `ball_cb_on_block_hit`; never
  the resulting sound.
- Integration/replay — no audio observability.

Net: a misnamed string or a deleted `sdl2_audio_play` call survives CI.

## Goals

- **G1.** A misnamed string literal (`"hyperspace"` vs `"hypspc"`) at any
  call site fails CI before merge.
- **G2.** The block-type → sound mapping table is exhaustively tested
  (one assert per `BlockType`). A new block type without a sound entry
  fails the test, forcing the author to make an explicit decision
  (sound name or "intentionally silent").
- **G3.** A representative gameplay run (replay) asserts zero
  `SDL2A_ERR_NOT_FOUND` returns. Catches dynamic name bugs (e.g.,
  `snd.name` in `game_modes.c:353`).
- **G4.** Low production cost — no function-pointer indirection on the
  hot path, no per-callsite refactor.

## Non-goals

- Verifying audio output (waveform comparison). Out of scope; SDL_mixer
  with dummy driver is the trust boundary.
- Per-call volume reproduction (modern API has fixed master volume).
- Testing chained explosion sounds (BOMB chain) — same gap as before,
  follow-up bead.

## Design

Three components, each independent and low-cost.

### Component 1: Audio call log in `sdl2_audio`

Add a small ring buffer to `struct sdl2_audio` that records every
`sdl2_audio_play()` call. Always on (cost is ~18KB per audio context;
see size calc below).

```c
/* in sdl2_audio.h */
#define SDL2A_LOG_CAPACITY 256

typedef struct
{
    char name[SDL2A_MAX_KEY_LEN + 1];   /* 65 bytes + 3 padding */
    sdl2_audio_status_t status;         /* 4 bytes (enum = int) */
} sdl2_audio_call_t;                    /* sizeof = 72 bytes */

/* Snapshot the last N calls into `out`, oldest first.  Returns the
 * number written, capped at `out_capacity` and at the number of calls
 * since last clear. */
int sdl2_audio_log_snapshot(const sdl2_audio_t *ctx,
                            sdl2_audio_call_t *out, int out_capacity);

/* Reset the log to empty.  Call this between test scenarios. */
void sdl2_audio_log_clear(sdl2_audio_t *ctx);

/* Count of calls in the log with status != SDL2A_OK. */
int sdl2_audio_log_error_count(const sdl2_audio_t *ctx);
```

Implementation: extend `struct sdl2_audio` with a fixed array
`sdl2_audio_call_t log[SDL2A_LOG_CAPACITY]`, a head index, and a count.
`sdl2_audio_play` appends one entry on every call (including
`ERR_NOT_FOUND` for misnamed lookups and `SDL2A_OK` for muted plays;
for `ctx == NULL` we obviously can't log).

Memory cost: 256 × 72 = 18,432 bytes ≈ 18KB per audio context. The
`frame` field originally proposed is dropped — `sdl2_audio_play` has
no frame parameter, so the field would always be 0 and degrade log
readability. Add a `_framed` variant later if a real supply path
emerges (peer review B4).

The log is always-on so production binaries record too. No
conditional compilation — production code and test code share the
same struct layout. We accept this cost because: (a) ~18KB is
negligible for a single audio context, (b) it gives a runtime
troubleshooting tool ("game went silent — dump the log"), and
(c) eliminates `#ifdef` divergence between test and prod.

### Component 2: Extract block → sound mapping to a pure function

Move `play_block_hit_sound` from `static` in `game_callbacks.c` to:

```c
/* include/block_sound.h */
#ifndef BLOCK_SOUND_H
#define BLOCK_SOUND_H

/* Return the sound name for a block hit, or NULL if the block type
 * is silent (DEATH_BLK in the original made the eyes laugh; we map
 * silently for unknown types).  Pure function — no globals, no I/O. */
const char *block_sound_name(int block_type);

#endif
```

```c
/* src/block_sound.c */
#include "block_sound.h"
#include "block_types.h"

const char *block_sound_name(int block_type)
{
    switch (block_type)
    {
        case BOMB_BLK: return "bomb";
        case BULLET_BLK:
        case MAXAMMO_BLK: return "ammo";
        /* ...all 19 cases as in the current static helper... */
        case DYNAMITE_BLK:
            /* DYNAMITE_BLK is currently silent in modern code.
             * Original/blocks.c PlaySoundForBlock has no entry for it
             * (DYNAMITE was added in a later level format).  Mark
             * explicitly silent so a future block type doesn't
             * inadvertently inherit silence via `default:`. */
            return NULL;
        default: return NULL;
    }
}
```

The extraction must enumerate every defined block type, including
`DYNAMITE_BLK(25)` which has no case in the current static helper
(peer review N2). Falling through `default:` for a defined type is a
silent gap and forbidden.

Then `game_callbacks.c` becomes:

```c
static void play_block_hit_sound(sdl2_audio_t *audio, int block_type)
{
    const char *name = block_sound_name(block_type);
    if (audio && name)
        sdl2_audio_play(audio, name);
}
```

The helper now wraps a pure function plus the SDL audio dependency.
The pure function is exhaustively testable without an audio context.

### Component 3: Three new test files

**`tests/test_block_sound.c`** — exhaustive table test. **Every block
type defined in `block_types.h` (0..`MAX_BLOCKS`-1 = 0..29) must have
an explicit assertion**, not a no-op loop (peer review B2). A vacuous
`(void)block_sound_name(t)` loop catches nothing — `block_sound_name`
returning `NULL` everywhere would still pass.

```c
static void test_block_sound_exhaustive(void **state)
{
    /* One assertion per block type.  A new block type added to
     * block_types.h without a corresponding line here will fail to
     * compile (missing case in the test author's mind == missing
     * case in block_sound.c). */
    assert_string_equal(block_sound_name(BOMB_BLK),       "bomb");
    assert_string_equal(block_sound_name(BULLET_BLK),     "ammo");
    assert_string_equal(block_sound_name(MAXAMMO_BLK),    "ammo");
    assert_string_equal(block_sound_name(RED_BLK),        "touch");
    assert_string_equal(block_sound_name(GREEN_BLK),      "touch");
    assert_string_equal(block_sound_name(BLUE_BLK),       "touch");
    assert_string_equal(block_sound_name(TAN_BLK),        "touch");
    assert_string_equal(block_sound_name(PURPLE_BLK),     "touch");
    assert_string_equal(block_sound_name(YELLOW_BLK),     "touch");
    assert_string_equal(block_sound_name(COUNTER_BLK),    "touch");
    assert_string_equal(block_sound_name(RANDOM_BLK),     "touch");
    assert_string_equal(block_sound_name(DROP_BLK),       "touch");
    assert_string_equal(block_sound_name(ROAMER_BLK),     "ouch");
    assert_string_equal(block_sound_name(EXTRABALL_BLK),  "ddloo");
    assert_string_equal(block_sound_name(MGUN_BLK),       "mgun");
    assert_string_equal(block_sound_name(WALLOFF_BLK),    "wallsoff");
    assert_string_equal(block_sound_name(BONUSX2_BLK),    "gate");
    assert_string_equal(block_sound_name(BONUSX4_BLK),    "gate");
    assert_string_equal(block_sound_name(BONUS_BLK),      "gate");
    assert_string_equal(block_sound_name(REVERSE_BLK),    "warp");
    assert_string_equal(block_sound_name(PAD_SHRINK_BLK), "wzzz2");
    assert_string_equal(block_sound_name(PAD_EXPAND_BLK), "wzzz");
    assert_string_equal(block_sound_name(MULTIBALL_BLK),  "spring");
    assert_string_equal(block_sound_name(TIMER_BLK),      "bonus");
    assert_string_equal(block_sound_name(STICKY_BLK),     "sticky");
    assert_string_equal(block_sound_name(DEATH_BLK),      "evillaugh");
    assert_string_equal(block_sound_name(BLACK_BLK),      "metal");
    assert_string_equal(block_sound_name(HYPERSPACE_BLK), "hypspc");
    assert_null(block_sound_name(DYNAMITE_BLK));  /* explicitly silent */
}

static void test_block_sound_invalid_types(void **state)
{
    /* NONE_BLK and KILL_BLK are sentinels, not destructible blocks. */
    assert_null(block_sound_name(NONE_BLK));
    assert_null(block_sound_name(KILL_BLK));
    assert_null(block_sound_name(-99));      /* arbitrary invalid */
    assert_null(block_sound_name(MAX_BLOCKS)); /* one past valid */
}
```

**`tests/test_audio_name_validation.c`** — integrates Components 1+2
to catch misnames at CI time without needing to trigger every event.
Uses the existing `setup_audio_ctx` fixture pattern from
`test_sdl2_audio.c` (peer review N3) — no new helper required.

```c
static int setup_audio(void **state)
{
    sdl2_audio_config_t cfg = sdl2_audio_config_defaults();
    cfg.sound_dir = "sounds";  /* relative to project root, as in test_sdl2_audio.c */
    sdl2_audio_status_t st;
    sdl2_audio_t *audio = sdl2_audio_create(&cfg, &st);
    assert_non_null(audio);
    *state = audio;
    return 0;
}

static int teardown_audio(void **state)
{
    sdl2_audio_destroy(*state);
    return 0;
}

static void test_every_block_sound_name_resolves(void **state)
{
    /* For each block_type, look up its sound name and assert
     * sdl2_audio_play returns OK (= the cache contains it). */
    sdl2_audio_t *audio = *state;
    for (int t = 0; t < MAX_BLOCKS; t++)
    {
        const char *name = block_sound_name(t);
        if (name == NULL) continue;
        assert_int_equal(sdl2_audio_play(audio, name), SDL2A_OK);
    }
}

/* Every string literal currently passed to sdl2_audio_play in src/.
 * Generated by scripts/audio-literals.sh — see Component 4. */
static const char *const k_known_literals[] = {
    /* game_callbacks.c (block hits go through block_sound_name) */
    "paddle",       /* ball-paddle */
    /* game_input.c */
    "tone", "toggle",
    /* game_modes.c */
    "buzzer",
    /* game_rules.c */
    "applause", "game_over", "balllost", "bomb",
    NULL
};

static void test_every_known_literal_resolves(void **state)
{
    sdl2_audio_t *audio = *state;
    for (const char *const *p = k_known_literals; *p; p++)
        assert_int_equal(sdl2_audio_play(audio, *p), SDL2A_OK);
}
```

**Augment `tests/test_replay_gameplay.c`** with a final assertion:

```c
/* After replaying the canned gameplay sequence: */
assert_int_equal(sdl2_audio_log_error_count(ctx->audio), 0);
```

If any `ERR_NOT_FOUND` was logged during the run, the test fails and
the offending name is in the snapshot.

**Scope clarification (peer review N4):** the existing replay scripts
traverse `SDL2ST_PRESENTS` (preamble frames 0–510) and fire the
`presents_system_get_sound().name`, `intro_system_get_sound().name`,
and `demo_system_get_sound().name` dynamic paths in `game_modes.c`.
**No ball is launched in any existing script**, so no block-hit
sounds fire during the replay. Component 3c's primary value is
catching misnames in the attract-mode dynamic-name paths;
block-hit-sound misnames are caught by Components 2+3a+3b.

### Component 4: Literal-list lint target (promoted from R2 per peer review N1)

Add `scripts/audio-literals.sh` that greps all `.c` files in `src/`
for `sdl2_audio_play(<expr>, "<LITERAL>")` and prints the literal set.
A `make audio-literals-check` target diffs that set against the
`k_known_literals[]` array in `test_audio_name_validation.c` and
fails CI if they differ.

```bash
#!/usr/bin/env bash
# scripts/audio-literals.sh — extract every string literal passed to
# sdl2_audio_play in src/.  Used by the audio-literals-check make
# target to prevent the test's k_known_literals[] from going stale.
set -euo pipefail
grep -rho 'sdl2_audio_play([^,]*,[[:space:]]*"[^"]*"' src/ \
    | grep -oP '"[^"]*"' \
    | sort -u
```

The Makefile target compares this list against an extraction of
`k_known_literals[]` from the test file. Any drift fails CI. The
~10-line lint adds 0 LOC to production and ~20 LOC across the script
and Makefile target.

## Implementation order

1. **Component 1** — Add the ring buffer + 3 new APIs to
   `sdl2_audio`. Update existing `test_sdl2_audio.c` with ~4 new
   tests covering snapshot, clear, error count, wrap-around at 256.
2. **Component 2** — Extract `block_sound.c/h` with explicit
   enumeration of all defined block types including `DYNAMITE_BLK`.
   Update `game_callbacks.c` to call the pure function. No behavior
   change.
3. **Component 3a** — `test_block_sound.c` (exhaustive table test,
   no audio context needed). One assertion per block type, no loops.
4. **Component 3b** — `test_audio_name_validation.c` (uses real
   audio ctx with dummy driver via `setup_audio_ctx` pattern).
5. **Component 3c** — Augment `test_replay_gameplay.c` with the
   error-count assertion (catches attract-mode dynamic-name bugs).
6. **Component 4** — `scripts/audio-literals.sh` +
   `make audio-literals-check` + wire into `make check`.
7. `make check` — all 6 gates pass.

Estimated size: ~170 LOC production + scripts, ~280 LOC tests.

## Alternatives considered

### A. Function-pointer seam (`audio_play_fn`)

Replace direct calls with a function pointer on the context. Tests
inject a spy.

**Why rejected:** Touches every one of 24 call sites for refactor.
Adds indirection on the hot path (one indirect call per sound
trigger). Provides no benefit over the ring buffer for catching the
bug classes we care about. Classic over-engineering for the actual
problem.

### B. Source-grep validation test only

A test that greps source for `sdl2_audio_play(_, "X")` literals and
asserts each is in `sounds/`.

**Why rejected:** Misses computed names (`snd.name`). Doesn't address
G2 (which event fires which sound). Brittle — regex breaks on
multiline calls. The ring buffer in Component 1 covers the dynamic
case for ~6KB of memory.

### C. Compile-time validation via X-macros

Define block types and sound names in one X-macro list, generate the
mapping table from it. Misnames become compile errors.

**Why rejected:** Touches block type enum (cross-cutting), and the
asset names live in a directory on disk, not in code. Compile-time
can't check "name X corresponds to a .wav file on disk." This is
fundamentally a runtime check.

## Risks and complications

- **R1.** Ring buffer adds 18KB to every audio context. For a single
  context per game, negligible. If we ever have multiple audio
  contexts (e.g., level preview + main), still negligible.
- **R2.** **Resolved by Component 4.** `test_audio_name_validation.c`
  requires a manually maintained list of "all string literals passed
  to `sdl2_audio_play`". Component 4's `make audio-literals-check`
  target diffs the test's array against a grep of the source and
  fails CI on any drift. If someone adds a new call site and forgets
  to register the literal, CI catches it.
- **R3.** Replay test asserting zero errors couples test correctness
  to the canned input sequence. The existing scripts traverse
  attract-mode (`SDL2ST_PRESENTS`) and fire dynamic-name paths
  (`snd.name` in `game_modes.c`), so Component 3c primarily catches
  attract-mode misnames. Block-hit sounds don't fire in any current
  replay script — Components 2+3a+3b carry that load. Acceptable.

## Reviewer-resolved questions

1. **Always-on vs `#ifdef TESTING`** — always-on. Avoids two struct
   layouts. ~18KB is negligible. Live troubleshooting value is real.
2. **Literal-list lint** — promoted to a deliverable (Component 4),
   not a future bead.
3. **`block_sound_name` scope** — block-only. Paddle/gun/special
   sounds dispatch through callback indirection with names supplied
   by the subsystem; different pattern, separate spec when needed.
4. **Test file split** — three files, per project convention (one
   test file per source file, docs/TESTING.md Layer 1).
