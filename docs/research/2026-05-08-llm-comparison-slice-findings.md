# LLM Visual-Comparison Slice — Findings

**Date:** 2026-05-08
**Mission:** m-2026-05-07-001 (slice spec); m-2026-05-07-002 (gjm review)
**Author:** claude (COO; self-executed slice)
**Verdict:** **PROCEED**

The slice tested whether an LLM (Anthropic API, `claude-opus-4-7`)
can reliably identify whole-screen visual divergences between
`original/xboing` reference captures and modern SDL2 captures, given
two PNGs and a structured prompt.  Six runs across two image pairs;
all six returned `verdict=diff` and identified concrete real
differences with stable wording.  Build full Phase 2.

---

## Setup

- **Original captures:** `tests/golden/original/presents-early.png`
  and `intro-late.png` (frame 1 and frame 2500 of `original/xboing`,
  shipped in PR #108).
- **Modern captures:** `scripts/capture_modern_one.sh` (throwaway,
  ~50 LOC) ran `build/xboing` for 1 s and 8 s respectively, then
  ImageMagick `import` captured the SDL2 window.  PNGs landed in
  `.tmp/slice-experiment/`.
- **LLM script:** `scripts/llm_compare.py` (~140 LOC), reads
  `ANTHROPIC_API_KEY` from environment or `secret-tool lookup
  service anthropic`, calls Anthropic API with both images
  base64-encoded plus the methodology prompt, prints structured JSON.
- **Runs:** 3 per pair = 6 total.  `temperature` parameter was
  attempted at 0.0 but returned `400 invalid_request_error:
  temperature is deprecated for this model`.  Removed.
  Verdict-consistency tested empirically across the 3 runs instead.
- **Cost:** ~$0.44 total (6 calls; ~3360 input + ~300 output tokens
  each at Opus pricing).  Per pair: ~$0.07.
- **Latency:** 7.0 – 12.8 s per call.  Median ~8 s.

## Findings — presents-early

| Run | Verdict | Findings (count) | Stable across runs? |
|----|---|----|----|
| 1 | diff | 3 (text, animation, layout) | ✓ |
| 2 | diff | 3 (text, animation, layout) | ✓ |
| 3 | diff | 3 (text, animation, layout) | ✓ |

Consistent findings across all 3 runs:

- **[major] text:** Original shows "Made in Australia" below the flag
  *and* copyright line at bottom.  Modern shows neither, but instead
  displays "Justin C. Kibell / presents / XBoing II" overlaid on the
  globe.
- **[major] animation:** Modern is at a different point in the intro
  sequence than original — original is in the "Made in Australia"
  phase; modern is already in the "presents" phase.
- **[minor] layout:** Globe positioned lower and more centered in
  modern; flag closer to top in original.

These are **real divergences**, not false positives.  The modern game
renders the "presents" intro state immediately on boot, where the
original first shows the "Made in Australia" caption phase before
transitioning.

## Findings — intro-late

| Run | Verdict | Findings (count) |
|----|---|----|
| 1 | diff | 4 (text, text, sprite, animation) |
| 2 | diff | 4 (text, text, sprite, animation) |
| 3 | diff | 3 (text, text, animation) |

Consistent across all 3 runs:

- **[major] text:** "Made in Australia" present in original, missing
  in modern.
- **[major] text:** Copyright line present in original, missing in
  modern.  Modern instead shows "Welcome, prepare for battle." in
  green at bottom-left.
- **[major] animation:** Original is mid-XBOING-letter-stamping
  animation (only the first "X" visible to the left of the globe);
  modern shows the fully-stamped XBOING title overlapping the globe.

Run 3 omitted the standalone "sprite" finding (folded it into the
"animation" finding).  Verdict was the same; severity was the same;
the underlying observation was the same.  This is wording variation,
not verdict instability.

## Verdict-stability assessment (gjm review B-2)

Despite `temperature` being non-settable on Opus 4.7, **verdict was
identical across all 6 runs**.  Findings counts varied by ±1 between
runs of the same pair, with the variation always being one of:

1. Two related observations folded into one finding instead of two.
2. Minor wording differences (e.g., "first image" vs. "original").

No verdict flips.  No fabricated findings.  No dropped real
divergences.  At Opus 4.7's natural sampling, output is stable
enough for the proceed/iterate/pivot decision.

## False-positive / false-negative audit (gjm review B-1, Q5)

The slice deliberately omitted an "expected-equivalent" pair because
no committed golden has a known modern-matches-original equivalent.
Both pairs were expected to diverge, and both did.  This is a
limitation: we have not yet shown the LLM does NOT hallucinate
differences when none exist.  Two paths to address this in Phase 2:

1. Capture a screen where the modern port is known to render
   correctly (a fixed gameplay frame at level 1 with no animations
   active).  Use that as the no-false-positive sanity check.
2. Or accept it as a Phase 2 acceptance criterion: the first thing
   the full Phase 2 runs is the same image against itself
   (`original.png` vs. `original.png`).  Any "diff" verdict on that
   self-comparison is a model-side false positive and a hard fail.

## Cost / latency check against gjm Q5 invalidation criteria

| Criterion | Threshold | Observed | Result |
|----|----|----|----|
| Verdict instability at temp=0 | 3 runs disagree | 6/6 agree (verdict=diff) | PASS |
| False negative on known-divergent pair | LLM says "equivalent" on real diff | All 6 say "diff", real diffs identified | PASS |
| False positive on equivalent pair | LLM invents diffs | Not tested in slice; deferred to Phase 2 | DEFERRED |
| Latency per pair | > 60 s | 7–13 s observed | PASS |

3 of 4 criteria pass cleanly.  The deferred criterion is testable in
Phase 2 with a self-comparison sanity check.

## Quality of findings

The findings are **actionable**: each one identifies a specific UI
element with a specific divergence ("Made in Australia text missing,"
"XBOING title overlaps globe instead of mid-stamp").  A maintainer
reading the report can go fix the modern port without further
investigation.

This is the bar the methodology was meant to clear.  Pyramid
SSIM/hash tooling would have produced "regions C, D, F SSIM below
threshold" — telling us *where* to look but not *what* is wrong.
The LLM tells us what is wrong in plain English.

## Recommendation: PROCEED with full Phase 2

Build out:

1. `scripts/capture_modern.sh` — proper modern capture matching the
   capture_original.sh pattern.  State setup helpers for gameplay /
   bonus / highscore screens.  Software renderer +
   `render_alpha=0.0` per sjl B-3, B-4.
2. Animation keyframe enumeration in both capture scripts (block
   explosion stages, bonus coin counts, XBOING letter stamping
   frames).
3. `scripts/llm_compare.py` hardening — graduate from throwaway to
   production-ready: input validation on the parsed JSON, retry on
   transient API errors, batch mode that compares all
   `(original, modern)` pairs in `tests/golden/` and writes a single
   report.
4. `make visual-check` Makefile target — capture both, compare,
   write report to `.tmp/visual-check-report.md`.
5. **Self-comparison sanity check** — first thing the script does is
   compare each original image against itself.  Any "diff" verdict
   there fails the run.

Phase 2 budget estimate: ~1 day of engineering.  CI integration is a
separate decision (cost vs. benefit) — recommend manual / opt-in
until the prompt and keyframe catalog stabilise across 2–3 PRs.

## Slice artifacts (not committed)

- `.tmp/slice-experiment/modern-presents-early.png`
- `.tmp/slice-experiment/modern-intro-late.png`
- `.tmp/slice-experiment/presents-early-run{1,2,3}.json`
- `.tmp/slice-experiment/intro-late-run{1,2,3}.json`

The throwaway scripts (`scripts/capture_modern_one.sh`,
`scripts/llm_compare.py`) are committed because they validate the
approach and Phase 2 will incrementally evolve them in place rather
than rewrite from scratch.
