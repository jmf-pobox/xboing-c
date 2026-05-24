# Session Resume — XBoing Visual Fidelity

**Last session date:** 2026-05-09
**Master at:** `c1a3a48`
**Branch:** clean master, no outstanding branches

## What was accomplished

### Visual fidelity audit — baskets 2–6 (PRs #103–#107)

All 6 baskets of the comprehensive visual-fidelity audit are shipped.
12 beads closed across 5 PRs.  Each PR went through 2–4 rounds of
Copilot review with all findings addressed in-band.

| PR | Basket | Summary |
|---|---|---|
| #102 | 1 | Block animation frames (shipped pre-session) |
| #103 | 2 | Block composite rendering (DROP/RANDOM/BULLET sprite keys + text overlays) |
| #104 | 3 | Block explosion lifecycle state machine (40-tick KILL_BLK animation, hit-time vs finalize-time side-effect split, 14 cmocka tests) |
| #105 | 4 | Level info panel (right-anchor lives + level number to match original/level.c layout) |
| #106 | 5 | Bonus screen (coin-by-coin + bullet-by-bullet sprite animation replacing static text) |
| #107 | 6 | Quick fixes (guide rate ×8, NoWalls green border, presents letter spacing, eyedude bullet collision + double-award bug fix) |

### Visual-fidelity screenshot-testing methodology (PRs #108–#109)

Built a methodology for catching visual divergences between the 1996
original and the modern SDL2 port, using screenshots + LLM comparison.

**Phase 1 (PR #108):** Capture infrastructure.

- Patched `original/main.c` with `-snapshot N` flag (advances N frames,
  emits READY on stdout, sleeps 2 s for capture).
- `scripts/capture_original.sh` drives original/xboing under any X
  server, captures by window ID filtered by `_NET_WM_PID`.
- 4 reference PNGs committed to `tests/golden/original/`.
- `tests/golden/regions.h` with crop coordinates (currently unused
  under revised plan but available).

**Phase 2 slice 1 (PR #109):** LLM comparison loop.

- Revised methodology: collapsed the SSIM/hash pyramid into a single
  LLM-comparison phase per maintainer direction.
- `scripts/llm_compare.py` — compares two PNGs via Anthropic API,
  reads API key from env or `secret-tool lookup service anthropic`.
- `scripts/visual_check.py` — batch driver reading a YAML manifest,
  runs self-comparison sanity checks + original-vs-modern pairs.
- `tests/visual-check-manifest.yaml` — 4 pairs (2 self-comparison
  sanity, 2 real checks).
- `make visual-check` target.
- Thin slice validated: 6/6 LLM runs returned correct verdicts.
  Self-comparison → equivalent ✓; real pairs → diff with actionable
  findings ✓.  Cost: ~$0.07/pair.
- CLAUDE.md Phase 6 (Ship) tightened to make PR monitoring
  non-negotiable for every PR.

### Findings from the visual-check pipeline

The LLM identified these concrete divergences on the home screen
(presents/intro):

1. **"Made in Australia" caption missing** from modern presents screen.
2. **Copyright line missing** from modern presents screen.
3. **Modern jumps to "presents XBoing II" phase too fast** — at
   modern-1s, original is still showing just flag + earth.
4. **"Welcome, prepare for battle."** green text appears on modern
   intro where it shouldn't (stray gameplay message leaking onto
   attract screen).
5. **XBOING letter stamping is wrong phase** — original at frame 2500
   shows mid-stamp (only "X" visible); modern at 8s shows fully-
   stamped title overlapping the globe.

These are documented in:

- `docs/research/2026-05-08-llm-comparison-slice-findings.md`
- `.tmp/slice-experiment/` (captured PNGs + raw JSON results)

## Next step: Option A — true up the home screen

The maintainer approved using the existing pipeline to drive a real
fix on the presents/intro screen.  This validates the methodology
by using it to fix the very divergences it found.

### Implementation plan

1. **Read original presents/intro code** to understand the correct
   phase sequence:
   - `original/presents.c` — DrawTitleText, letters, sparkle, text
   - `original/intro.c` — intro state machine
   - Map the timing (which text appears when, frame delays between
     phases)

2. **Read modern equivalents:**
   - `src/presents_system.c` — the modern state machine
   - `src/game_render_ui.c game_render_presents` — the render path
   - Identify why the phase sequence diverges:
     - Missing "Made in Australia" text
     - Missing copyright line
     - Phase timing too fast (modern arrives at "presents" before
       original does)
     - "Welcome, prepare for battle." message leak

3. **Patch the modern code** to match original's phase sequence.

4. **Re-run `make visual-check`** — verify the LLM findings drop to
   "equivalent" or minor-only for both presents-early and intro-late
   pairs.

5. **Ship as a PR** with the standard Phase 6 protocol (Copilot
   review, 2–4 rounds, merge on convergence).

### Open beads

| Bead | Priority | Description |
|---|---|---|
| xboing-c-ktp | P2 | Phase 2 (revised): modern capture + LLM comparison loop — expand state catalog beyond presents/intro |
| xboing-c-hty | P3 | Game RNG never seeded — port original/init.c:825 srand |
| xboing-c-y7s | P1 | Visual fidelity audit epic (parent) |

### Environment notes

- Python venv at `.tmp/venv/` (uv-managed, `~/.local/bin/uv`).
  Contains `anthropic` + `pyyaml`.
- Anthropic API key stored in gnome-keyring: `secret-tool lookup
  service anthropic`.
- MCP `request_copilot_review` has been intermittently failing with
  "invalid session" — Copilot still receives reviews from auto-
  triggers on push.  Cron-driven polling continues to work.
