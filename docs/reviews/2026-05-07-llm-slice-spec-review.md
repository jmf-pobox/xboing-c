# Spec Review: LLM Visual-Comparison Thin-Slice

**Spec under review:** `.tmp/missions/visual-llm-slice-spec.yaml`
**Reviewer:** gjm (Glenford J. Myers, test-expert)
**Date:** 2026-05-07

---

## Verdict: PASS WITH REVISIONS

The slice is correctly scoped as a falsification experiment, not a
production build.  The throwaway-code constraint is well enforced.
Two revisions are required before execution.  The remaining four
questions have actionable answers that the worker should read before
running the experiment.

---

## Blocking Findings

**B-1 — presents-early is too easy; it cannot demonstrate the LLM
catches real divergences.**

Looking at the four committed goldens (`presents-early.png`,
`presents-mid.png`, `intro-stable.png`, `intro-late.png`) and
comparing them: the presents screen is a static composition — black
background, starfield, earth globe sprite, Australian flag sprite,
copyright text.  Both binaries render this scene from the same XPM/PNG
assets with no layout logic beyond centering.  The modern SDL2 port
maps the same sprites through `sdl2_renderer`.  If there is no known
regression on this screen (and the basket 1–6 audit found none here),
the LLM will very likely return "equivalent."

That outcome tells us nothing.  The slice needs to answer: "Can the
LLM identify a screen where the two binaries DIFFER?"  A run on an
"equivalent" pair proves only that the LLM does not hallucinate
differences when none exist.  That is worth one of the three runs, not
all three.

**Required change:** add a second screen pair that is known to
diverge.  The candidates are constrained by what goldens exist.  The
four committed goldens are all presents/intro states — there are no
gameplay, bonus, or HUD-bearing goldens yet (see `tests/golden/original/README.md`).
This means the only "known divergent" pair available without capturing
new goldens is `intro-late.png`: by frame 2500 the original is
mid-XBOING-title stamp animation.  Whether the modern port shows the
same animation state at the same frame count is testable.

Recommendation: run 2 pairs, not 1.  Pair A: `presents-early` (expect
"equivalent" — tests no-false-positive behavior).  Pair B: `intro-late`
(the X-stamp in progress; expect the modern port to differ or match —
the result is informative either way).  If pair B returns "equivalent"
when the modern binary shows a different animation state, that is a
concrete false-negative and informs the pivot decision.

**B-2 — 3 runs at default temperature is insufficient; temperature
must be pinned to 0.**

The spec says "3 repeated runs" but does not specify temperature.
Default temperature for claude-opus-4-7 is 1.0.  At temperature 1.0,
three runs of the same image pair will show natural output variation.
Whether that variation is "the LLM changed its verdict" or "the LLM
used different words to say the same thing" cannot be distinguished
from 3 samples.

Two separate questions are being conflated:

1. Does the LLM identify real differences?
2. Are the findings stable across runs?

To answer question 1 cleanly: run at temperature 0.  To answer
question 2: run at temperature 1.0 and look for verdict flips, not
just wording variation.

**Required change:** set `temperature=0` for the three consistency
runs.  Add one additional run at `temperature=1` to characterize
variance.  That gives 4 runs total: 3 at temp=0 (consistency), 1 at
temp=1 (variance sample).

---

## Per-Question Responses

### Q1 — Is presents-early too easy?

Yes.  See B-1 above.  It is likely to return "equivalent" for the
wrong reason: the modern port does render it correctly, not because
the LLM methodology is validated.  One "equivalent" pair is useful as
a no-false-positive sanity check.  The slice must also include a pair
where the verdict tests the LLM's ability to detect real divergence.

### Q2 — Are 3 runs enough?

3 runs at temperature 0 is enough to check for determinism at temp=0
(they should be identical if the API determinism guarantee holds).
3 runs at temperature 1 is insufficient to characterise jitter: you
need at least 5 samples to distinguish noise floor from a real
inconsistency.

The spec is testing a binary question — "is the LLM consistent enough
to trust?" — not fitting a distribution.  For that binary question, the
right criterion is: "Do all temp=0 runs return the same verdict AND the
same top-level findings list?"  Wording variation is acceptable.  Verdict
flip is not.

**Recommendation:** 3 runs at temp=0 on each pair.  Optional 1 run at
temp=1 per pair.  Report verdict consistency, not prose similarity.
The 3-run requirement in the spec success criteria matches this.

### Q3 — Is the output format under-specified?

For this slice: leave it open.  The research goal is to see what the
LLM emits naturally before constraining the schema.  If we pre-constrain
the shape, the findings doc cannot answer "does the LLM structure its
output usefully when unprompted?"

The methodology doc (lines 553–572) already specifies a minimal envelope:
`{verdict: "equivalent" | "diff", findings: [...]}`.  That is sufficient
for parseable JSON without over-constraining the findings array.

**Recommendation:** `llm_compare.py` should assert that the response
parses as JSON and that the top-level `verdict` key is present.  Everything
else is left open.  Print the raw `findings` array to stdout for manual
inspection.  This is exactly what "no JSON validation — throwaway"
means, applied correctly.

For the full Phase 2 build-out, constrain findings shape to
`{category, description, severity}` per finding — that is when you
need to drive automated pass/fail from the output.

### Q4 — Are the decision outcomes distinguishable?

The three outcomes (proceed / iterate / pivot) are distinguishable IF
the experiment includes a known-divergent pair (B-1).  Without it,
"the LLM said equivalent on presents-early" tells you nothing: all
three outcomes remain open.

With B-1 addressed (two pairs, one expected-equivalent, one
expected-different):

- **Proceed:** temp=0 runs on both pairs are consistent.  The
  expected-equivalent pair returns "equivalent."  The
  expected-different pair returns "diff" with findings that match
  actual visual differences.  No false positives on the equivalent
  pair.

- **Iterate:** temp=0 runs are consistent, but findings on the
  expected-different pair are too vague to be actionable ("colors
  look slightly different" without specifying which element), OR
  the findings format is not structured enough to drive automated
  decisions.  Prompt needs tightening.

- **Pivot:** temp=0 runs produce different verdicts on the same
  pair, OR the expected-different pair returns "equivalent," OR
  the equivalent pair returns "diff" with fabricated findings.  Any
  of these is a hard no-go.

These three outcomes are now clearly distinguishable.  Write the
findings doc after running both pairs.

### Q5 — What would invalidate the larger plan?

The slice INVALIDATES the larger plan if any of the following is observed:

1. **Verdict instability at temp=0:** the three temp=0 runs on the same
   pair return different `verdict` values.  An LLM that cannot reproduce
   the same verdict on identical inputs is not a viable CI gate.

2. **False negative on a known-divergent pair:** the LLM returns
   "equivalent" on a pair where the modern binary is visibly different
   (e.g., XBOING title animation state mismatch).  This means the LLM
   cannot detect the category of divergence we care about.

3. **False positive on the equivalent pair:** the LLM invents differences
   on `presents-early` where none exist.  This means the signal-to-noise
   ratio is too low.

4. **Latency > 60 s per pair:** at 80 pairs in the full run, 60 s/pair
   = 80 minutes.  That is not a practical manual gate.  10–30 s/pair is
   acceptable.

Adding a second pair (B-1) is what makes invalidation distinguishable
from a non-result.  Without it, "LLM said equivalent on presents-early"
is not invalidation evidence.

The slice does NOT need a second screen pair to test generalisation
beyond intro states.  That is over-investing.  Two pairs (one
equivalent, one expected-different) is the minimum viable falsification.

### Q6 — API failure in throwaway code

Yes, this is a real risk for misreading outcomes.  A single API timeout
or rate-limit error in a 3-run experiment means 1/3 runs fail to
return, and the findings doc will report "one run failed" next to two
successful runs.  That looks like inconsistency even though it is
infrastructure noise.

The spec correctly says "no retry" (production-quality scripting is
out of scope).  But the findings doc must distinguish "API error" from
"verdict variation."  The `llm_compare.py` script needs to:

1. Print the HTTP status code and raw response body on failure (not
   just raise an exception and produce no output).
2. Label each run's output with a run number and timestamp.

These are two lines of code, not production retry logic.  They prevent
a transient 429 from being misread as "LLM returned inconsistent
output on run 3."

**Recommendation:** add these two lines.  The "no production-quality
scripting" constraint refers to retry loops and batching, not to basic
error labeling.

---

## Recommended Revisions to the Spec

| ID | Required | Change |
|----|----------|--------|
| B-1 | Yes | Run 2 image pairs: `presents-early` (expected equiv) + `intro-late` (expected different if animation state differs) |
| B-2 | Yes | Pin `temperature=0` for the 3 consistency runs; optionally add 1 run at temp=1 |
| NB-1 | No | Print HTTP status + run label on API failure (2 lines in `llm_compare.py`) |
| NB-2 | No | Assert `verdict` key present in parsed JSON; otherwise print raw response |

---

## Notes on the Success Criteria

The criterion "At least 3 runs of the same image pair are completed;
findings consistency is reported" is correct in count but will produce
an ambiguous result without B-1 (the second pair).  The findings doc
should explicitly report:

- Per-pair: expected outcome, actual verdict from each run, whether
  the verdict was stable.
- Overall: was the expected-equivalent pair correctly identified?  Was
  the expected-different pair correctly identified?  Any false positives
  or false negatives observed?

That structure makes the proceed/iterate/pivot decision defensible.
