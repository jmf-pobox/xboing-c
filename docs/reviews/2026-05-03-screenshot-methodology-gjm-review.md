# Spec Review: Visual-Fidelity Screenshot Testing Methodology

**Spec under review:** `docs/research/2026-05-03-visual-fidelity-screenshot-testing.md`
**Reviewer:** gjm (Glenford J. Myers, test-expert)
**Date:** 2026-05-03

---

## Verdict: PASS WITH REVISIONS

The overall shape of the pyramid is sound.  L1 (pure-logic geometry)
is already working and correctly placed.  L3 (SSIM vs. original) is
the right answer to the "are we faithful to the 1996 behavior"
question.  L4 (LLM judge) is accurately scoped as opt-in appeals, not
a CI gate.

Two blocking findings must be resolved before Phase 2 (L2
implementation) begins.  The remaining findings are non-blocking but
several carry real flake risk if ignored.

---

## Blocking Findings

**B-1 [determinism] L2 hash check is not viable without an explicit
`srand` seeding contract.**

`ball_system.c` calls `rand()` directly at velocity randomization and
hyperspace teleport (lines 895–902, 1013–1014).  `sfx_system.c`
falls through to `rand()` when `rand_fn` is NULL (`sfx_system_create`
in `game_init.c:537` passes `NULL` as the rand_fn).  `game_rules.c:73`
also calls `rand()` for bonus-block spawn timing.  The modern codebase
has **no `srand` call** — the original's `srand(time(NULL))` at
`original/init.c:825` was not ported.  Without seeding, the POSIX
default is implementation-defined (glibc seeds from 1, but this is not
guaranteed across libc versions or platforms).

Consequence: a gameplay-frame L2 hash test will either (a) work by
accident on glibc today and break on a toolchain update, or (b) hash a
state that has no ball/bonus activity and accidentally avoid the RNG
entirely.  Neither is a real test.

Fix required before Phase 2: add `srand(0)` (or a fixed seed
constant) at the top of the `test_render_screenshots.c` fixture setup,
AND document that `game_create()` must not call `srand` internally
(or, better, expose a `game_ctx_set_rng_seed(ctx, seed)` function so
tests can set the seed without depending on global `srand` state).

**B-2 [state drive-up] The bonus screen mid-animation capture requires
a per-state setup helper, not just `replay_tick_until`.**

The existing replay infrastructure (`test_replay_smoke.c`) advances the
game frame-by-frame via `replay_tick_until(&rctx, N)`.  The problem is
that "frame 5 of bonus coin animation" is not reachable by counting
replay ticks from PRESENTS: it requires knowing exactly when the
BONUS state is entered, which depends on when the last block is
destroyed, which depends on ball physics, which depends on RNG (B-1).

For the 8–10 bonus-screen baseline states in particular, replay
drive-up cannot be made robust without either (a) a `bonus_system`
direct-construct helper that sets `ctx->bonus` to a specific
mid-animation state without going through gameplay, or (b) seeding
RNG (B-1) AND recording the exact frame count for each named state
against that seed.  Option (a) is strongly preferred: it makes each
test independent, readable, and immune to upstream game-logic changes.

Fix required before Phase 2: add `game_ctx_set_bonus_state_for_test()`
or equivalent in `tests/test_helpers.h` that directly sets
`ctx->bonus` to a named mid-animation state.  This is the "seam"
pattern — the test bypasses the state machine to get to the scene it
wants to verify.

---

## Per-Question Responses

### Q1: Determinism of L2 hash check

**Verdict: red flag — fails without intervention.**

Three confirmed sources of non-determinism in the renderer path:

1. `render_alpha` (`game_context.h:128`) is a `double` in `[0.0, 1.0]`
   representing fractional ticks elapsed since the last physics step.
   In a test that calls `game_render_frame(ctx)` once from a static
   game state with no game loop running, `render_alpha` will be
   whatever it was initialized to (likely 0.0 via `calloc`).  This is
   actually deterministic if the test construct path zeros the struct.
   **Confirm** `game_create()` zeroes `render_alpha` via `calloc` and
   add an assertion to that effect in the fixture.

2. `rand()` in `ball_system.c`, `sfx_system.c`, `game_rules.c` — see
   B-1 above.  States that do not involve a moving ball or a spawning
   bonus block avoid this problem.  The proposal's baseline list
   includes "gameplay frame at level 1 (3 lives, full ammo, no
   specials)," which almost certainly has a ball in motion.

3. No wall-clock or `SDL_GetTicks` calls were found in the renderer
   paths (game_render.c, game_render_ui.c, sdl2_renderer.c).
   The renderer is driven purely by game state and `render_alpha`.
   This is good architecture for testability.

Summary: `render_alpha = 0.0` is fine (no timer in the test loop),
but ball physics RNG must be seeded.  Static scenes (bonus screen,
menus, highscore) are safe to hash without RNG seeding because they
have no ball or bonus-spawn activity.

### Q2: State drive-up for named baseline states

**Verdict: replay is sufficient for mode transitions; insufficient for
mid-animation captures.**

`replay_tick_until` works well for reaching `SDL2ST_BONUS` (the bonus
mode entry point) — see `test_replay_space_starts_game` pattern,
extended with a "clear all blocks" synthetic event or by placing a
level with exactly one easy-to-hit block.  This is the correct approach
for the top-level mode baseline states (attract, intro, gameplay, pause,
highscore, editor).

For mid-animation bonus sub-states (BONUS_STATE_TEXT,
BONUS_STATE_BONUS at coin N, BONUS_STATE_BULLET), replay drive-up
forces the test to know the exact frame count from game start to each
animation frame.  That count is RNG-sensitive (ball-to-last-block) and
fragile.

Recommendation: use `bonus_system` direct construction for all bonus
sub-state captures.  `bonus_system_begin(ctx, &env, 0)` then advance
`bonus_system_update(ctx, frame)` exactly N times — this is fully
deterministic and already exercised by `test_bonus_system.c`.

### Q3: Threshold strategy

**Verdict: the "floor − 0.01" rule is sound as a starting point but
needs a ratchet discipline.**

Running 100 captures of the same deterministic state and taking the
worst SSIM is the right characterization approach — it measures the
noise floor of the test environment, not the noise floor of the code.
Setting threshold = floor − 0.01 gives a 1% safety margin above that
noise.

The drift risk is real and the proposal understates it.  The common
failure mode: CI fails on a rendering PR → maintainer runs
`make capture-modern --update` → CI passes → nobody inspects the diff →
a regression is normalized.  Three safeguards:

1. Require a diff image alongside every golden update (`make
   capture-diff` that generates before/after PNGs and writes them to
   `.tmp/golden-diff/`).  Commit the diff image alongside the golden
   update.  Reviewers can see the visual change in the PR.

2. Treat threshold values as source-controlled constants, not computed
   on update.  The `regions.json` (or `.h` — see Q4) stores both the
   region coordinates and the thresholds.  Updating a threshold
   requires a deliberate edit to that file, not an automatic float
   adjustment.

3. Add a CI lint step that flags any threshold decrease (e.g.,
   `scripts/check_threshold_drift.py` comparing old vs. new
   `regions.json`).  A threshold increase (tighter) is always fine.
   A decrease (looser) requires a comment in the PR explaining why.

### Q4: Per-region crop coordinates

**Verdict: use a `.h` header, not `regions.json`.**

A `regions.json` file requires a JSON parser.  C has no stdlib JSON
parser.  Every L2 test would need either a third-party dependency or
a hand-rolled 50-line parser — both are costs that accumulate into
maintenance burden.

The right pattern for a C test suite: a single header file
`tests/golden/regions.h` with `#define` constants per region:

```c
/* All coordinates relative to the SDL surface top-left (0,0). */
#define REGION_PLAYFIELD_BORDER_X  0
#define REGION_PLAYFIELD_BORDER_Y  0
#define REGION_PLAYFIELD_BORDER_W  640
#define REGION_PLAYFIELD_BORDER_H  480
#define REGION_LIVES_PANEL_X       284
#define REGION_LIVES_PANEL_Y       5
/* ... etc */
```

The Python L3 script `scripts/ssim_compare.py` can `#include`-parse
this with a one-line regex: `re.findall(r'#define\s+(\w+)\s+(\d+)',
open('tests/golden/regions.h').read())`.  Single source of truth,
no JSON dep, auditable in git diff.

Coordinates derive from `original/stage.c` layout constants — cite
those lines in comments inside `regions.h`.

### Q5: Golden maintenance burden

**Verdict: the proposed workflow has one footgun; fix it with a
mandatory diff step.**

The "CI fails → `make capture-modern --update` → CI passes" loop is
a regression normalizer if the human doesn't inspect the diff.  The
diff image safeguard from Q3 is the primary mitigation.

Additional recommendation: `make capture-modern` should NOT silently
overwrite golden hashes.  It should write the new hashes to
`.tmp/hashes-candidate.txt` and require an explicit `make accept-modern`
(which does the actual copy) to complete the update.  This two-step
pattern is stolen from snapshot-testing frameworks (Jest, etc.) that
distinguish "record" from "accept."

For L3 PNGs: 3 MB in-repo is acceptable for 30 shots at 100 KB each.
Git LFS adds operational overhead (requires LFS-enabled clones, CI
LFS credentials) that is not worth it below ~10 MB.  Use in-repo PNGs.
Track total size in a CI lint step; add LFS only if the collection
grows past 20 MB.

### Q6: L4 LLM judge

**Verdict: not a test layer — an approval gate.  Useful, but don't
call it a test.**

An LLM judge that sometimes returns "equivalent" and sometimes
returns "diff at lives panel" is not a test in the characterization
sense.  A test has a deterministic pass/fail contract.  An LLM call
has neither: the same two images may produce different verdicts
across API versions, prompt changes, or temperature variations.

The proposal correctly scopes it as opt-in.  But the cron "drift
detector" variant is riskier than it appears: a cron LLM run that
silently produces "equivalent" on a real regression is worse than
no test at all, because it provides false confidence.

Recommended framing: L4 is a **human-in-the-loop approval gate** for
golden updates, not a CI layer.  When a maintainer wants to update a
golden, they run `make llm-review-golden OLD=old.png NEW=new.png`.
The LLM produces a structured diff description (layout, color, sprite
differences).  The maintainer reads it and types `make accept-golden`.
The LLM's verdict and rationale are committed alongside the new golden
as `tests/golden/<state>-llm-review.txt`.

This makes L4 genuinely useful — it reduces cognitive load for
golden review — without letting it gate CI.

Drop the weekly cron drift detector.  If L2 and L3 are running on
every visual PR, gradual drift is already caught.  A cron that runs
over all baselines with non-deterministic output adds noise, not signal.

### Q7: CI gating

**Verdict: path-filter is correct; the path list needs careful
definition.**

L3 running on every PR would add Xvfb + Python + 30 s to every CI
run, including pure-logic and documentation PRs.  Path-filter is the
right choice.

Recommended trigger paths (GitHub Actions `paths` filter):

```yaml
paths:
  - 'src/game_render*.c'
  - 'src/sdl2_renderer.c'
  - 'src/game_render*.h'
  - 'include/game_render*.h'
  - 'include/sdl2_renderer.h'
  - 'bitmaps/**'
  - 'tests/golden/**'
  - 'scripts/ssim_compare.py'
```

The "refactor that touches `src/game_render.c` but doesn't change
pixels" concern is real, but it is the correct behavior — any change
to a rendering source file should trigger the visual test.  If the
refactor genuinely preserves pixels, L3 passes quickly and the cost
is 30 s.  If L3 fails, the refactor changed something unexpectedly.
That's the test doing its job.

L2 (headless hash, ~10 ms) should run on every PR — it is in the same
CMocka suite as other unit tests and adds negligible cost.

### Q8: The "old code still executes" foundation

**Verdict: take the PNGs now, drop the live original/ rebuild
dependency from CI.**

The proposal's reliance on `original/xboing` as a live reference in
CI is fragile.  The current build dependency chain:
`original/Makefile` → `original/xboing` → Xvfb capture.  Risks:

1. Ubuntu 30.04 / 32.04 may drop `libXpm-dev` or change Xlib ABI.
   The original 1996 Makefile has no CMake or pkg-config integration.
2. CI image updates that drop the X11 development headers (already
   rare in headless runners).
3. `original/xboing` capture is a 3-step process (build, run under
   Xvfb, drive to state) that is fragile per Q1/Open Question 1 in
   the proposal.

Recommendation: capture `original/xboing` goldens **once**, on a
developer machine with a verified X11 environment, commit the PNGs
to `tests/golden/original/`, and treat them as immutable reference
artifacts.  CI never rebuilds or re-runs `original/xboing`.  The
only time `original/xboing` re-runs is when a maintainer believes
the reference PNG is wrong and wants to regenerate it deliberately.

This turns the "canonical source" from a live binary dependency into
a checked-in artifact.  The live binary remains available in
`original/` for manual dogfood, but CI does not depend on it.

Driving `original/xboing` to specific states (Open Question 1):
`xdotool` is indeed fragile.  The cleanest option is the proposal's
suggestion: add a `--snapshot STATE` flag to `original/xboing` that
runs to a specific state, emits a PNG, and exits.  This is a minimal
change to the original (one `getopt` branch + a call to
`XWriteBitmapFile` or ImageMagick `import`).  Do this once, capture
the 30 goldens, commit, and never touch `original/xboing` in CI again.

---

## Summary of Blocking Findings

| ID | Layer | Finding | Resolution |
|----|-------|---------|------------|
| B-1 | L2 | No RNG seed — gameplay-frame hash is non-deterministic | Add `game_ctx_set_rng_seed(ctx, 0)` or equivalent; document seed contract |
| B-2 | L2 | Bonus mid-animation states unreachable via replay drive-up alone | Add `game_ctx_set_bonus_state_for_test()` seam in `tests/test_helpers.h` |

All other findings are non-blocking but carry flake or maintenance
risks.  The Q3 threshold drift safeguard and the Q5 two-step golden
acceptance workflow are highest priority among them.
