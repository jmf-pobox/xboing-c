# Peer Review: SFX Testability Design Spec

- **Spec under review:** [SFX Testability](../specs/2026-06-03-sfx-testability.md)
- **Reviewer:** `feature-dev:code-reviewer` (agent-to-agent)
- **Worker:** Claude (COO)
- **Date:** 2026-06-03
- **Verdict:** APPROVE-WITH-CHANGES

Four blocking items must be resolved before implementation starts. The design direction is correct and proportionate. The core three-component structure stands.

---

## Blocking findings

### B1 — `BLOCK_TYPE_COUNT` does not exist

`include/block_types.h` defines `MAX_BLOCKS 30` and `MAX_STATIC_BLOCKS 25`. There is no `BLOCK_TYPE_COUNT`. The spec uses it in two test snippets:

- `test_every_block_type_has_decision`: `for (int t = 0; t < BLOCK_TYPE_COUNT; t++)`
- `test_every_block_sound_name_resolves`: same loop

Both will fail to compile. `NONE_BLK(-2)` and `KILL_BLK(-1)` are negative and outside the 0..29 range, so a loop from 0 to `MAX_BLOCKS-1` correctly excludes them.

**Fix:** Replace every occurrence of `BLOCK_TYPE_COUNT` with `MAX_BLOCKS`.

### B2 — `test_every_block_type_has_decision` is vacuous

Proposed body:

```c
for (int t = 0; t < BLOCK_TYPE_COUNT; t++)
    (void)block_sound_name(t);  /* must not crash */
```

This asserts only that the function does not crash. A `block_sound_name` that always returns `NULL` passes. A developer who adds a new block type and falls through to `default: return NULL` sees a green test. The stated intent ("forces author to think about new block types") is not enforced.

**Fix:** Replace the loop with explicit per-type `assert_string_equal` / `assert_null` assertions, or introduce a registered-types sentinel mechanism that distinguishes "explicitly silent" from "forgotten." `test_known_mappings` with explicit per-type assertions is what actually catches the `"hyperspace"` class of bug; the loop test does not.

### B3 — Struct size is ~19KB, not ~6KB

`sdl2_audio_call_t` layout on x86-64 with standard alignment:

| Field | Size | Offset |
|-------|------|--------|
| `char name[SDL2A_MAX_KEY_LEN + 1]` = `name[65]` | 65 | 0 |
| padding | 3 | 65 |
| `sdl2_audio_status_t status` (enum, width = int) | 4 | 68 |
| `int frame` | 4 | 72 |
| **total** | **76** | |

`256 × 76 = 19,456 bytes ≈ 19KB`. The spec states "256 × 24 bytes ≈ 6KB." The always-on decision is still correct — 19KB is negligible — but the stated rationale is wrong and will confuse the implementer when `sizeof` returns 76 instead of 24.

**Fix:** Update cost to ~19KB throughout. (If `frame` is dropped per B4, the size drops to 72 bytes per entry, ~18KB total.)

### B4 — `frame` field has no supply path

`sdl2_audio_call_t.frame` is described as "caller-supplied or 0 if not used," but `sdl2_audio_play(sdl2_audio_t *ctx, const char *name)` has no frame parameter. The implementation note says `sdl2_audio_play` appends the entry. The field will always be 0.

Two choices:

1. Drop `frame` entirely. `name` + `status` is sufficient for every use case the spec describes.
2. Add `sdl2_audio_play_framed(ctx, name, frame)` as a second entry point. No current call site needs it.

**Recommendation:** Drop the field. A field that is always 0 degrades log readability without adding information. Add when a supply path exists.

---

## Non-blocking findings

### N1 — R2 grep lint target should be a hard prerequisite, not a future bead

The motivating bug was a misnamed string literal. The spec defers the grep lint target to a "future mitigation." This leaves a gap: the manually-maintained literal list in `test_audio_name_validation.c` can go stale the moment someone adds a call site and forgets to register the literal — and CI will not notice. The literal inventory is small: roughly 15 strings (`game_rules.c`: `"bomb"`, `"applause"`, `"game_over"`, `"balllost"`; `game_input.c`: `"tone"`, `"toggle"` ×2; `game_modes.c`: `"buzzer"`; block sounds: ~22). The ~10-line shell helper enforcing this is within scope. Promote to a deliverable.

### N2 — `DYNAMITE_BLK(25)` is absent from the switch and will carry forward silently

`block_types.h:43` defines `DYNAMITE_BLK 25`. The current `play_block_hit_sound` switch in `src/game_callbacks.c:78` has no `case DYNAMITE_BLK`. When extracted to `block_sound.c`, it silently falls through to `default: return NULL`. If silent is correct, add `case DYNAMITE_BLK: return NULL;` with a comment citing the original. If it should have a sound, this spec must assign one. The extraction is the moment to make this explicit — not a follow-up.

### N3 — `make_test_audio_ctx()` is undefined

`test_audio_name_validation.c` calls `make_test_audio_ctx()` which does not exist. The spec must either name it as a new helper to define (pattern: `sdl2_audio_create` with `sdl2_audio_config_defaults()`, pointing at the project's `sounds/` directory) or replace it with the explicit call. The `setup_audio_ctx` fixture in `test_sdl2_audio.c` is the model.

### N4 — Replay assertion validates attract-mode sounds, not block sounds

Component 3c correctly adds `assert_int_equal(sdl2_audio_log_error_count(ctx->audio), 0)` to `test_replay_gameplay.c`. The existing scripts traverse `SDL2ST_PRESENTS` (preamble frames 0–510), firing the `presents_system_get_sound().name` and `intro_system_get_sound().name` dynamic paths. No ball is ever launched in any existing script, so no block-hit sounds fire during the replay. The assertion is valuable and correct, but its primary coverage is attract-mode sounds and the `snd.name` dynamic paths — not block sounds. The spec's R3 is correct, but the Component 3c description slightly overstates its block-sound value. Tighten the language.

---

## Bug-class tracing

**Would the `"hyperspace"` typo have been caught?**

Yes, via Component 3b: `block_sound_name(HYPERSPACE_BLK)` returns `"hyperspace"` from the old code, `sdl2_audio_play(audio, "hyperspace")` returns `SDL2A_ERR_NOT_FOUND` (no `hyperspace.wav`), and `assert_int_equal(..., SDL2A_OK)` fails.

**Would silent `DEATH_BLK` have been caught?**

Only via `test_known_mappings` — specifically the `assert_string_equal(block_sound_name(DEATH_BLK), "evillaugh")` assertion. The vacuous loop test (B2) would not catch a `NULL` return for `DEATH_BLK`. Direct consequence of B2: explicit per-type assertions are required, not a no-op loop.

---

## Answers to open questions

**Q1: Always-on ring buffer vs `#ifdef TESTING`?**

Always-on. An `#ifdef` creates two struct layouts; divergence between production and test is a source of "passes in test, fails in prod" failures. The corrected cost (~19KB) is still negligible. The runtime troubleshooting value is real.

**Q2: Manually maintained literals list — acceptable or hard prerequisite?**

Hard prerequisite. The motivating bug was a literal misname. Shipping this spec without the guard that directly catches that class leaves CI blind to the exact bug pattern that motivated the work. Ten lines of shell, one `make check` target. Promote to a deliverable.

**Q3: `block_sound_name` scope — block-only or all gameplay-event sounds?**

Block-only. Paddle, gun, and special sounds are dispatched through callback indirection where `name` is supplied by the subsystem, not hardcoded in `game_callbacks.c`. A different extraction pattern applies there. Separate specs when those patterns need coverage.

**Q4: Three test files vs merging into `test_sdl2_audio.c`?**

Three files. `test_sdl2_audio.c` tests audio module mechanics. `test_block_sound.c` tests the block-type mapping with no audio context. `test_audio_name_validation.c` integrates both. Project convention is one test file per source file (docs/TESTING.md Layer 1).

---

## Proportionality assessment

The design is proportionate. ~150 LOC production / ~250 LOC tests for catching misnamed strings and missing event mappings is the correct order of magnitude. Alternative A (function-pointer seam) is correctly rejected — 24 call-site rewrites adds complexity with no incremental coverage benefit. Alternative B (grep-only) is correctly rejected for missing computed names. No better alternative was overlooked. The three-component structure is sound.

---

## Resolution checklist

- [ ] B1: Replace `BLOCK_TYPE_COUNT` with `MAX_BLOCKS`
- [ ] B2: Replace vacuous loop with explicit per-type assertions
- [ ] B3: Update struct cost to ~19KB (or ~18KB after B4)
- [ ] B4: Drop `frame` field from `sdl2_audio_call_t`
- [ ] N1 (recommended): Promote R2 grep lint target to a deliverable
- [ ] N2: Add explicit handling note for `DYNAMITE_BLK` in the `block_sound.c` extraction
- [ ] N3: Define or replace `make_test_audio_ctx()` with an explicit constructor call
- [ ] N4: Tighten Component 3c description to name attract-mode sounds as primary scope
