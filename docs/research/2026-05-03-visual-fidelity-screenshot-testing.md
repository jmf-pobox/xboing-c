# Visual-Fidelity Screenshot Testing Methodology — Proposal

**Date:** 2026-05-03
**Author:** claude (COO)
**Status:** REVISED 2026-05-03 — gjm review (m-2026-05-03-002) and sjl review
(m-2026-05-03-003) both PASS WITH REVISIONS.  Revisions section at the
bottom captures the deltas from the two reviews.  Awaiting maintainer
approval before any implementation.
**Motivation:** the basket 1–6 audit was code-reading. Alignment defects
slipped through (basket 4 lives left-anchored vs. right-anchored;
basket 5 bullet row 2-px shifted; PR #107 NoWalls border overdrawn by
glow). Future visual-rendering PRs need automated screenshot-level
verification, not just human dogfood.

The original 1996 XBoing **still executes** — `original/xboing` builds
and runs under X11. That is the canonical source of truth. Any modern
rendering claim ("this matches the original") should be falsifiable
against a captured reference.

---

## Approaches Considered

### A. Pixel-exact diff vs. reference

Capture reference PNGs from `original/xboing` under Xvfb. Capture
modern PNGs from the SDL2 game (already supports `SDL_VIDEODRIVER=dummy`
for headless render-to-surface). Diff pixel-by-pixel; fail if any pixel
differs.

**Pros:** simplest mental model. Fully deterministic.
**Cons:** will never pass. X11 vs. SDL2 differs in font hinting,
anti-aliasing, subpixel placement. XPM vs. PNG color quantization
shifts ~1–2 RGB units per pixel. Realistic threshold is "0% pixel
matches"; this approach degrades to "pixel-exact within tolerance T,"
which is just (B) with T=0.

### B. Per-pixel diff with tolerance

Same as (A) but accept N pixels different OR each pixel different by
≤K RGB units. Tunable. Fast (microseconds for a 700×630 surface).

**Pros:** deterministic, fast, easy to debug ("diff at (x,y): expected
RGB(0,200,0), got RGB(2,201,1)").
**Cons:** thresholds are arbitrary. Setting T too tight produces flaky
tests; too loose hides real regressions. No semantic understanding —
a 5-pixel shift of an entire icon and a 5-pixel anti-alias bleed look
the same to the threshold.

### C. SSIM (structural similarity) per region

Crop to regions-of-interest (lives panel, level number, bonus row,
playfield border, etc.). Compute SSIM per crop. Pass if SSIM > 0.95
per region.

**Pros:** SSIM is robust to anti-aliasing and color-quantization
differences but sensitive to structural changes (icon shifted by 5px
shows up as a real dip). Faster than human review. Per-region
thresholds let us be strict about layout (lives row stride) and lenient
about glyph rendering (font hinting).
**Cons:** SSIM still needs a tuned threshold per region. Implementation
is ~50 LOC per region pair plus an OpenCV/image-similarity dep. Crop
boundaries themselves have to be specified — moving them defeats the
test.

### D. LLM-judge accept test

Send the original PNG and modern PNG to an LLM with a prompt: "Are
these visually equivalent for the purpose of XBoing visual fidelity?
Identify any layout, color, or alignment differences." Pass if the LLM
returns "equivalent" (or below a defined diff severity).

**Pros:** handles every category of difference humans care about.
Catches subtle "wrong sprite" cases that pixel-tolerance/SSIM miss.
Produces human-readable failure explanations.
**Cons:** non-deterministic; slow (10–30 s per comparison); requires
API key + network; cost per CI run scales with screenshot count.
Doesn't fit the unit-test pyramid bottom layers — fits as a gating
appeals layer above SSIM.

### E. Hybrid testing pyramid (RECOMMENDED)

Combine (B), (C), (D) into the existing test pyramid, with the original
1996 binary as the canonical reference source.

```text
┌──────────────────────────────────────────────────────────┐
│  L4  LLM-judge (manual / opt-in)                  ~30 s  │  ← appeals
├──────────────────────────────────────────────────────────┤
│  L3  SSIM-per-region vs. original/ reference     ~1 s    │  ← visual PRs
├──────────────────────────────────────────────────────────┤
│  L2  Headless surface hash + per-pixel tolerance  ~10 ms │  ← every PR
├──────────────────────────────────────────────────────────┤
│  L1  Pure-logic geometry helpers (existing)       <1 ms  │  ← every PR
└──────────────────────────────────────────────────────────┘
```

**Layer 1 — pure-logic geometry helpers (existing).** `bonus_row_item_x`,
`level_life_position`, `level_number_digit_position`,
`block_overlay_text_pos`, `game_render_specials_coords`. CMocka tests.
Catch off-by-one math errors before rendering. Fast, deterministic, no
SDL2.

**Layer 2 — headless surface hash + per-pixel tolerance (NEW).** Drive
the modern game to a known state via the existing test_integration /
test_replay infrastructure. Render one frame to an SDL surface (already
possible with `SDL_VIDEODRIVER=dummy`). Hash the surface (e.g., SHA-256).
Compare to a checked-in golden hash. On mismatch, write the diff PNG to
`.tmp/` and fail with the path so the maintainer can inspect.

This layer catches **regressions** — any unintentional rendering change
fails the test. Goldens are regenerated when an intentional visual
change ships (one make-target invocation, one commit per PR).

**Layer 3 — SSIM vs. original/ reference (NEW).** A separate make
target captures reference PNGs from `original/xboing` under Xvfb at
the same states. Per-region SSIM comparison. Acceptance threshold per
region, e.g.:

| Region | SSIM threshold | Rationale |
|---|---|---|
| Playfield border | 0.98 | Solid color, structural |
| Lives panel | 0.95 | Sprites with possible color quant |
| Level number digits | 0.92 | Font hinting differences |
| Bonus row sprites | 0.95 | Same as lives |
| Score area | 0.92 | Font hinting |

Runs on PRs labeled `visual-fidelity` (or that touch
`src/game_render*.c`). Skipped for purely-logic PRs. Requires Xvfb in
CI image (already cheap — a few MB).

**Layer 4 — LLM-judge appeals (NEW, opt-in).** When L3 fails on a
threshold but the maintainer believes the diff is acceptable (e.g.,
XPM-to-PNG quantization caused a 0.94 instead of 0.95), the maintainer
can opt in to an LLM-judge run. The LLM gets the original + modern + a
specific prompt. If the LLM says "equivalent," the maintainer can
update the golden + threshold with the LLM's rationale committed
alongside.

Optionally L4 can run on a weekly cron over all baselines as a "drift
detector" — catches gradual regressions that individual PR threshold
nudges accumulate.

---

## Implementation Phases

### Phase 1 — capture infrastructure (one make target, one script)

Add `make capture-original` target:

```makefile
capture-original:
	# Build original (one-time cost)
	$(MAKE) -C original
	# Run under Xvfb at known states; ImageMagick `import` for capture
	./scripts/capture_original.sh tests/golden/original/
```

`scripts/capture_original.sh` drives `original/xboing` to:

- attract / presents screen
- intro screen
- demo screen
- gameplay frame at level 1 (3 lives, full ammo, no specials)
- gameplay frame at level 1 with NoWalls active
- gameplay frame at level 1 with X2 / X4 active
- bonus screen at each state (TEXT, SCORE, BONUS mid-animation,
  BONUS post-animation, LEVEL, BULLET, TIME, HSCORE, END_TEXT)
- highscore screen
- editor screen

Output: `tests/golden/original/<state>.png`.

How to drive original/ to specific states is the harder problem.
Original supports `-level N` and `-nosound`. For mid-bonus-animation
captures, may need a deterministic-replay fixture or scripted input
via `xdotool`.

### Phase 2 — L2 headless capture + hash (CMocka)

New test file `tests/test_render_screenshots.c`. For each named state:

1. Set up `game_ctx_t` to that state.
2. `game_render_frame(ctx)` once.
3. Read SDL surface via `SDL_RenderReadPixels`.
4. Compute SHA-256 of pixel buffer.
5. Compare to checked-in golden hash from `tests/golden/hashes.txt`.
6. On mismatch: write surface to `.tmp/<state>-actual.png`, fail with
   path.

Add `make capture-modern` to regenerate hashes after intentional changes.

### Phase 3 — L3 SSIM comparison

New script `scripts/ssim_compare.py` (Python with `Pillow` +
`scikit-image` — both small, already common). For each
`(state, region)` pair, crop both PNGs and compute SSIM. Output JSON
report; CI fails if any region falls below its threshold.

Add `make check-visual` target. Initially manual; later wire into a
CI workflow gated on file paths.

### Phase 4 — L4 LLM judge (opt-in)

New script `scripts/llm_judge.py`. Takes two PNG paths and a region
description. Calls Anthropic API (or local model). Returns
`{verdict, rationale}` JSON. Maintainer-only (requires `ANTHROPIC_API_KEY`).

---

## Open Questions for Reviewers

1. **Driving original/ to states:** scripted input via `xdotool` is
   fragile. Better options? Patch original/ to accept a `--snapshot`
   flag that emits a PNG and exits? (One-line modification, doesn't
   change gameplay.)

2. **Per-region crop coordinates:** define them once (e.g., in
   `tests/golden/regions.json`) shared between L2 and L3. Coordinates
   are pixel-exact, so what's the source of truth? `original/stage.c`
   layout constants seem natural.

3. **Goldens in git:** PNGs are 10–100 KB each. ~30 baseline shots × 100 KB =
   3 MB. Acceptable in-repo? Or use Git LFS? Or hashes-only in repo +
   PNGs only locally?

4. **CI cost:** L3 needs Xvfb + Python in the CI runner. Adds maybe 30 s
   to PR turnaround. Worth it for visual PRs only (path-filter)?

5. **Threshold tuning:** initial thresholds are guesses. Recommendation:
   establish baseline via 100 captures of the same state (deterministic),
   take the worst SSIM as the floor, set threshold = floor − 0.01.

6. **Regression vs. fidelity:** L2 (hash) catches regressions of modern
   against modern. L3 (SSIM vs. original) catches fidelity drift. Both
   are needed — L2 alone misses original-vs-modern shifts; L3 alone
   produces noisy results when intentional modern improvements happen.

7. **Sub-pixel rendering:** SDL2 with `SDL_HINT_RENDER_SCALE_QUALITY=0`
   forces nearest-neighbor (closest to X11). Worth setting in tests?

---

## Why This is Worth the Effort

The basket 1–6 audit found 12 visual-fidelity defects. Of those:

- 4 were caught by code reading (basket 1, 3, 4, 6: pure off-by-one)
- 5 were caught by Copilot review during PR (#103 F1, #105 F1+F2,
  #106 F1+F2)
- 3 slipped past code reading AND PR review and were only caught by
  later gjm spec reviews (basket 3 B-1 stage-4 sprite key off-by-one,
  PR #106 F1 bullet padding, PR #107 F5 double-award)

A single screenshot of the bonus screen vs. the original would have
flagged the bullet-row 2-pixel shift instantly. A NoWalls border
screenshot would have flagged that the green never displayed.

This methodology turns "looks right to a human reading code" into
"looks right to a pixel-by-pixel comparison with the canonical source."
That's the verification rigor a 30-year-old game modernization deserves.

---

## Revisions Applied (2026-05-03 post-review)

Two reviewers landed PASS WITH REVISIONS.  Five blocking findings
total; seven non-blocking refinements.  Each is folded into the
methodology below.

### Blocking findings — methodology adjustments

**B-1 (gjm) — RNG non-determinism in modern code blocks L2 hashing.**
Modern XBoing has zero `srand()` calls (the original's
`srand(time(NULL))` at `original/init.c:825` was not ported).  `rand()`
is invoked by `ball_system.c:895-902` (velocity randomization),
`game_rules.c:73` (bonus-block spawn timing), and `sfx_system.c:74-78`
(NULL `rand_fn` fallback).  L2 hash check on any state involving these
systems would be flaky across runs.

**Resolution.** Two-part:

1. Add `game_ctx_set_rng_seed(ctx, seed)` accessor.  Test fixtures pin
   the seed.  Implementation: a single `srand(seed)` call inside the
   accessor.  Production paths (`game_create`) call it with
   `time(NULL)` to preserve current behavior.
2. Scope L2 hashing to states with no live RNG-driven activity (menus,
   intro, presents, bonus screen, highscore screen).  Gameplay frames
   require the seeded fixture.

**B-2 (gjm) — Bonus mid-animation states unreachable via replay.**
"Frame 5 of bonus coin animation" requires knowing the frame count from
game start to bonus entry, which depends on RNG, which depends on B-1.
Replay drive-up is too brittle.

**Resolution.** Bypass replay for sub-states.  Use the pattern already
in `tests/test_bonus_system.c` — directly construct `bonus_system`,
call `bonus_system_begin(ctx, &env, 0)`, then drive
`bonus_system_update(ctx, frame)` N times to reach the target sub-state.
Add `tests/test_helpers.h` with `game_ctx_set_bonus_state_for_test()`
helpers for each named state.

**B-3 (sjl) — SDL dummy driver + `SDL_RenderReadPixels` is unreliable
for pixel capture.** Whether the software renderer allocates a real
backing surface depends on SDL2 version.

**Resolution.** Use `SDL_CreateSoftwareRenderer(SDL_Surface*)` directly
with a surface created via `SDL_CreateRGBSurfaceWithFormat`.  The
surface's `pixels` pointer is always populated.  No `SDL_VIDEODRIVER`
env var needed.  L2 capture path becomes:

```c
SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
    0, SDL2R_LOGICAL_WIDTH, SDL2R_LOGICAL_HEIGHT, 32, SDL_PIXELFORMAT_RGBA32);
SDL_Renderer *r = SDL_CreateSoftwareRenderer(surface);
/* render with r exactly as the production renderer does */
/* surface->pixels now has the rendered RGBA frame */
```

**B-4 (sjl) — `render_alpha` non-determinism.** The loop computes
`alpha = accumulator_us / tick_interval_us` (`src/sdl2_loop.c:138-143`)
from wall-clock time.  L2 tests routed through `sdl2_loop_update` would
hash different output each run.

**Resolution.** L2 tests call `game_render_frame()` directly with
`ctx->render_alpha = 0.0` (or `1.0`) pinned.  Never route through
`sdl2_loop_update`.

**B-5 (sjl) — `-nosound` flag does not exist in `original/`.** The
proposal had this wrong.  `original/init.c:489` sets `noSound = True`
as the default; there is no `-nosound` flag in `ParseCommandLine`.
Capture invocation would have crashed with `PrintUsage()`.

**Resolution.** Capture invocation: `./original/xboing -startlevel N`
with no audio flag.  Audio is off by default.

### Non-blocking refinements applied

- **gjm Q4** — region coordinates in `tests/golden/regions.h` `#define`
  constants (no JSON parser in C).  Python L3 script parses the
  `#define` lines via regex.  Source coordinates from
  `original/stage.c` layout constants, cited in comments.
- **gjm Q5** — two-step golden update.  `make capture-modern` writes
  `.tmp/hashes-candidate.txt` and per-state diff PNGs to `.tmp/`.
  Maintainer inspects diffs.  Separate `make accept-modern` overwrites
  `tests/golden/hashes.txt`.  Prevents accidental regression
  normalization.
- **gjm Q6** — L4 LLM judge reframed as a one-shot human-in-the-loop
  approval gate, not a continuous test layer.  Invocation:
  `make llm-review-golden OLD=path NEW=path`.  Drop the weekly
  drift-detector cron — L2 + L3 already cover drift.
- **gjm Q8 + sjl Q2 (convergent)** — both reviewers independently
  recommended a `--snapshot STATE` flag in `original/main.c` instead of
  `xdotool` for mid-state captures.  One-time `getopt` branch.  CI
  never rebuilds `original/`; goldens are captured ONCE and committed
  to `tests/golden/original/`.  Treat the captured PNGs as the
  permanent reference; the original binary stays available for manual
  dogfood but is not a CI dependency.
- **sjl Q1 + Q10** — CI runner needs `xfonts-75dpi xfonts-100dpi`
  (~4 MB; without them Xvfb falls back to the `fixed` bitmap font and
  glyphs differ from a developer machine).  But since we capture once
  and commit, this only matters on the capture machine, not in CI.
  L3 SSIM script needs `python3-pillow` + `python3-scikit-image`
  (~18 MB total).  Replaceable with ~80 LOC pure-Pillow SSIM if size
  matters.  All packages in Ubuntu 24.04 universe.
- **sjl Q5 (significant)** — text-rendering regions are NOT
  SSIM-comparable.  X11 bitmap fonts vs. SDL2_ttf TrueType lands at
  SSIM 0.50–0.75.  Drop "Level number digits" and "Score area" from
  the L3 region table.  Sprite-only regions land at 0.95–0.99 and
  are testable.  The bugs we want to catch (alignment, color, layout)
  are sprite-region bugs.  Font hinting is not what we're verifying.
- **sjl Q6** — confirmed window dimensions match (575×720 both
  binaries; `original/include/stage.h:59-63` and
  `include/sdl2_renderer.h:21-22`).  Account for the +2 px `playWindow`
  border when computing crop coordinates.

### Revised L3 region table

| Region | SSIM threshold | Rationale |
|---|---|---|
| Playfield border | 0.98 | Solid color, structural |
| Block grid (per-level template) | 0.96 | Sprite alignment in 18×9 cells |
| Lives panel | 0.95 | Lives icons (right-anchored, 30 stride) |
| Bonus coin row | 0.95 | BONUS_BLK sprites (37 stride) |
| Bonus bullet row | 0.95 | bullet sprites (10 stride) |
| Paddle | 0.97 | Single sprite |
| Ball(s) | 0.95 | Up to 5 sprites |
| ~~Level number digits~~ | ~~0.92~~ | **DROPPED — text region, not SSIM-comparable** |
| ~~Score area~~ | ~~0.92~~ | **DROPPED — text region, not SSIM-comparable** |

Text regions are validated indirectly: their sprite-bearing siblings
(lives, paddle, blocks) align correctly only if the surrounding panel
math is right.  Layer 1 unit tests on `level_number_digit_position`
already lock the digit positions; pixel-level glyph rendering is
explicitly out of scope for L3.

### Revised implementation phases

Same four phases as the original proposal, with these adjustments:

- **Phase 1** (capture infrastructure): patch `original/main.c` to add
  a `--snapshot STATE` getopt branch.  Single capture run produces all
  baseline PNGs; commit them.  Drop live `original/xboing` rebuild
  from CI.
- **Phase 2** (L2 implementation): use `SDL_CreateSoftwareRenderer`
  path (not dummy driver).  Pin `ctx->render_alpha = 0.0` and seed
  RNG via new `game_ctx_set_rng_seed` accessor before rendering.
  Hash candidate writes to `.tmp/hashes-candidate.txt`.
- **Phase 3** (L3 SSIM): sprite-only regions per the revised table.
  Coordinates from `tests/golden/regions.h`.  Path-filter to
  `src/game_render*.c`, `src/sdl2_renderer.c`, `bitmaps/**`,
  `tests/golden/**`, `scripts/ssim_compare.py`.
- **Phase 4** (L4 LLM judge): one-shot approval gate
  (`make llm-review-golden`).  Not a continuous test layer.  Maintainer
  invokes manually; LLM rationale committed alongside the updated
  golden as `<state>-llm-review.txt`.

### What this changes about the test pyramid claim

The pyramid stays 4 layers, but L4 is reclassified from "test" to
"approval gate."  Continuous coverage is L1+L2 on every PR, L3 on
visual-rendering PRs only.  L4 fires manually on golden-update PRs
where the maintainer wants a second opinion before committing the new
golden.

### Decision points for the maintainer

1. **Approve the methodology as revised?**  If yes, Phase 1 + Phase 2
   can proceed in parallel as separate PRs.
2. **Patch `original/main.c` to add `--snapshot`?**  This is a
   one-line modification to the legacy code.  Project ethos says
   `original/` is "preserved verbatim," but the `--snapshot` flag is
   non-gameplay-affecting (renders one frame, exits).  Confirm the
   exception is acceptable.
3. **B-1 RNG seeding scope.**  The `game_ctx_set_rng_seed` accessor
   is a real fix for a real determinism bug, not just a test
   affordance.  Should it ship as part of this work, or as its own
   bd issue (since it has gameplay implications — replay tests would
   benefit too)?
4. **Goldens in repo size.**  ~30 baseline PNGs × ~30 KB each = ~900 KB
   in-repo for `tests/golden/original/`.  Acceptable, or LFS?
   sjl's review confirms acceptable in-repo until > 20 MB.
