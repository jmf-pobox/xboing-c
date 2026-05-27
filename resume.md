# Session Resume — XBoing Modernization

**Date:** 2026-05-27
**Master at:** `548d56b` (PR #126 merged)
**Active branch:** `fix/attract-pixel-polish` (3 commits, not yet PR'd)

## What We Accomplished This Session

### Keybinding Infrastructure (PR #124, merged)

Built comprehensive keybinding test infrastructure (28 CMocka tests)
covering all implemented key bindings. Test-driven development found
and fixed multiple bugs:

- C key cycle handler was missing entirely
- C key cycle order wrong (PREVIEW/HIGHSCORE swapped vs natural cycle)
- J/L paddle keys ignored in mouse mode (keyboard and mouse both ran
  every frame; mouse overwrote keyboard)
- Paddle speed 5x too fast (original gates with PADDLE_ANIMATE_DELAY=5)
- Smooth paddle interpolation (VELOCITY=4px/frame, every frame)
- Ball stuck in BALL_CREATE state (exact-frame == checks; changed to >=)
- P pause toggled on/off within one frame (just_pressed in per-tick code)
- All just_pressed handlers moved from per-tick to once-per-frame
- D kill ball, T tilt, A audio mute, H/h highscores, G toggle sound

Extracted attract cycle order into `game_callbacks_attract_next()` as
single source of truth — both C-key and natural callbacks use it.

### Dialogue System (PR #125, merged)

Pixel-match dialogue renderer rewriting `game_render_dialogue()` to
match original/dialogue.c: stone tile background, 4px red border,
TEXT_ICON sprite, green shadow-centred message, white separator,
yellow input text or question mark sprite. User verified identical.

- Q key: "Exit XBoing you wimp? [y/n]" confirmation dialogue
- Escape key: "Abort current game? [y/n]" during gameplay
- W key: "Input game starting level number." numeric dialogue
- Pending flag pattern (quit/abort/level_pending) following
  wisdom_pending precedent
- Ball jitter fix during dialogue (render_alpha frozen)
- Dialogue sounds wired (click/key/tone)
- Full design with peer review (4 blocking findings addressed)

### Documentation Restructuring (PR #126, merged)

CLAUDE.md reduced from 741 to 136 lines based on Anthropic's
200-line recommendation. Research showed instruction adherence
degrades uniformly as CLAUDE.md length increases.

New architecture:

- **Tier 1:** Root CLAUDE.md (136 lines) — behavioral contract + @imports
- **Tier 2:** @-imported docs — BUILDING.md, TESTING.md, GIT.md, WORKFLOW.md
- **Tier 3:** .claude/rules/ — path-scoped rules for C code, tests,
  Makefile, scripts, delegation, markdown, CI, CLAUDE.md maintenance
- **Tier 4:** Sub-directory CLAUDE.md — original/, tests/, src/, .github/

Added `original/CLAUDE.md` with module mapping, key constants,
coordinate systems, font names, and citation rules for reading the
1996 source.

docs/TESTING.md now has 5-layer pyramid including Layer 4 (visual
fidelity) which was the missing layer that caused process failures.

### Attract Loop Pixel Polish (branch, in progress)

Golden references captured for all 8 attract screens. Three remaining
screens polished:

| Screen | Status | LLM Judge Result |
|--------|--------|------------------|
| KEYSEDIT | Done | 1 minor (bottom spacing) |
| HIGHSCORE | Done | Glow phase varies (correct behavior) |
| PREVIEW | Done | Content differs by level (expected) |

### Infrastructure

- Quarry enabled and synced for this project (semantic search active)
- xdotool installed for automated screenshot capture
- Highscore file generator for populating original binary's score table
- Automated filmstrip capture script (15s intervals, 6 min cycle)

## Where We Are

### Outer Loop: Attract Cycle Visual Fidelity

All 8 attract screens have golden references and polished rendering:

| Screen | Golden | Polished | Shipped |
|--------|--------|----------|---------|
| Presents | ✓ | ✓ | PR #116 |
| Intro | ✓ | ✓ | PR #115 |
| Instruct | ✓ | ✓ | PR #115 |
| Demo | ✓ | ✓ | PR #123 |
| Keys | ✓ | ✓ | PR #123 |
| Keysedit | ✓ | ✓ | On branch |
| Highscore | ✓ | ✓ | On branch |
| Preview | ✓ | ✓ | On branch |

### Inner Loop: Current Branch

`fix/attract-pixel-polish` — ready for PR.

Additional work this session:

- Visual capture infrastructure extended to all 8 attract screens
- `visual_capture.sh` FIFO hang fix (game exit without DONE signal)
- `make visual-check` screen list expanded from 3 to 8
- Window title changed to "- XBoing II -" (matching original)
- Keysedit: alternating green text, shadow on key bindings, wider spacing
- Highscore: spacing fixes, time/date color for current score highlight
- Preview: cycling backgrounds, removed spurious title overlay
- `make dogfood` updated for new window title

### Known Issues

- Screenshot test disabled in non-ASan builds (SDL_mixer teardown)
- Font width slightly wider than original (Liberation Sans vs Helvetica)

### What's Next

1. PR for attract pixel polish (review + merge)
2. Gameplay screens: bonus, level complete, game over
3. Remaining polish: animation timing, sparkle effects
