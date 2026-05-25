# Visual Capture Infrastructure — Reflection

Date: 2026-05-25

## Problem

The visual fidelity process before this session had three failure modes:

1. **Frame-count guessing.** The original binary's `-snapshot N` required
   knowing which frame number lands on which screen. We tried 8000 (got
   presents), 18000 (got instruct). Every new screen was trial and error.

2. **Wall-clock capture.** The modern `capture_modern_one.sh` slept N
   seconds then grabbed the window. Non-reproducible across machines.

3. **Single-image-per-screen.** A single capture at one moment missed
   bugs in other phases of the same screen's lifecycle.

## What we built

A state-driven capture system that signals at each sub-state transition
AND at tunable intervals within persistent animation states:

- `-visual-capture mode[:interval]` flag on both binaries
- `XBOING_SNAPSHOT mode/substate/seq` protocol on stdout
- `scripts/visual_capture.sh` reads signals and captures via ImageMagick
- `make golden-screen SCREEN=intro INTERVAL=200` Makefile targets
- Sub-state detection centralized in one function (not per-file changes)

## Bug discovery: before vs. after

### Before (single-screenshot, frame-count approach)

Working screen-by-screen on intro/instruct across PRs #113-#115, we
found and fixed bugs iteratively over multiple sessions:

| Issue | How discovered |
|-------|---------------|
| Wrong sprites (22 of 22 block types) | LLM comparison of one screenshot |
| Text x-offset +50 vs +60 | User spotted in one screenshot |
| Wrong background tile | User spotted |
| Missing green border | User spotted |
| Missing HUD elements | User comparison with original |
| Devil eyes disappeared | User noticed during live play |
| Ammo belt y-position wrong | User noticed during live play |
| Font size 12pt vs 18pt | User noticed in side-by-side |
| Missing text shadow | User asked about font weight |
| Message color yellow vs green | User noticed in side-by-side |

Total: ~10 bugs found across multiple sessions, mostly by user visual
inspection of single screenshots or live play.

### After (state-driven capture with goldens)

On the presents screen — the FIRST screen we examined with the new
infrastructure — we found 5 rendering issues in a single pass by
comparing original goldens against the modern capture:

| Issue | How discovered |
|-------|---------------|
| Credits use TTF text instead of bitmap sprites | Original golden showed metallic bitmaps, modern showed plain text |
| Credits positioned over earth instead of below | Side-by-side golden comparison — immediately obvious |
| Credits shown simultaneously instead of sequenced | Golden filmstrip showed one-at-a-time reveal in original |
| Typewriter text green instead of red | Original golden showed red text |
| Wipe missing red edge lines | Original golden showed red lines at wipe boundary |

Total: 5 bugs found in one pass on one screen, all from golden
comparison. Zero required user spotting during live play.

## Key insight

The capture infrastructure didn't just find more bugs — it changed
**when** bugs are found. Before, bugs surfaced during user verification
(Gate 6 of the Definition of Done) or during live play. After, they
surface during implementation (Gate 4), before any PR is opened.

The filmstrip approach (multiple captures per screen across sub-states)
catches temporal bugs that static screenshots miss: animation sequencing,
transition effects, progressive content reveal. The devil-eyes speed
issue, the credits-shown-simultaneously bug, and the missing fade
transitions are all temporal bugs that require multiple capture points.

## What's still missing

1. ~~Modern exit after single-mode capture~~ — Fixed: mode-transition
   check runs before the vc_active gate. Root cause: the gate returned
   early when the mode had already transitioned away.
2. **Deeper attract screens** — demo, keys, keysedit, highscore, preview
   goldens captured; demo content partially implemented.
3. **Gameplay screens** — bonus, level complete, etc. require starting a
   game session, not just the attract cycle.
4. **Automated comparison** — the LLM comparison pipeline
   (`make visual-check`) hasn't been updated to consume the new filmstrip
   format yet.
