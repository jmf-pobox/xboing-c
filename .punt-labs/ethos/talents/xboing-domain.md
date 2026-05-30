# XBoing Domain

Game design, physics tuning, scoring balance, and level format for
XBoing. This is the canonical knowledge that drove the original 1993-
1996 implementation and that any modernization must preserve.

## Core Constants (immutable without explicit author approval)

```c
#define MAX_BALLS              5
#define PLAY_WIDTH             495
#define PLAY_HEIGHT            580
#define PADDLE_SMALL           40
#define PADDLE_MEDIUM          50
#define PADDLE_HUGE            70
#define DIST_BASE              30
#define MAX_NUM_LEVELS         80
#define SHOTS_TO_KILL_SPECIAL  3
#define BONUS_COIN_SCORE       3000
#define BULLET_SCORE           500
#define LEVEL_SCORE            100
#define TIME_BONUS             100   /* per second remaining */
#define SUPER_BONUS            50000
#define EXTRA_LIFE_THRESHOLD   100000
```

`DIST_BASE = 30` was hand-tuned for paddle-bounce feel. The
trigonometric paddle bounce (with four region splits across the
paddle face) and the four-region block collision define how the game
*feels* — they cannot be replaced with simpler math without losing
that feel.

## Block Grid

- Dimensions: 18 rows × 9 cols (162 blocks max)
- 30 distinct block types, each with own point value, hit count, and
  behavior (e.g. multiball, bonus coin, bullet, ammo, time bomb,
  death, mover, teleporter)
- Region-based collision: ball position relative to block divides
  into four quadrants, each producing a different bounce vector

## Scoring System

- Per-block points vary by type (single-hit blocks 100-500, special
  blocks higher)
- Multiplier ladder: ×2 at 25 blocks remaining, ×4 at 15 blocks
  remaining (rewards aggressive late-level play)
- `BONUS_COIN_SCORE = 3000` for collecting a bonus coin
- `BULLET_SCORE = 500` for shooting a block with a bullet
- `LEVEL_SCORE = 100` flat completion bonus
- `TIME_BONUS = 100` per second remaining in the level timer
- `SUPER_BONUS = 50000` for completing the final level
- Extra life every `EXTRA_LIFE_THRESHOLD = 100000` points

## Level Format

Plain ASCII, one character per grid cell, 18 lines × 9 chars per line.
Character → block-type mapping is fixed; changing it breaks every
existing level file.

```text
   abc d e
   ...
```

The 80+ levels in `levels/*.data` were authored by hand as a
difficulty arc. Treat them as canonical — never edit a level to make
a test pass.

## Game Modes (16-state machine)

Intro, title, game, pause, demo, edit, highscore, mainmenu, etc.
The modes interlock — transitions are one-way from intro through
gameplay to highscore, with edit and demo as orthogonal branches.
The transition rules are part of the design; rewriting them as
"clean state machine" risks losing intentional behavior.

## What "Modernization" Means Here

Modernization preserves these invariants. A change that:

- Lowers `MAX_BALLS` to 4 → redesign, not modernization
- Changes a block point value → redesign, not modernization
- Replaces region-based bounce with a simpler reflection → redesign
- Replaces XPM loading with PNG loading → modernization (no behavior
  change visible to player)
- Replaces `usleep` frame timing with fixed timestep → modernization
  (improves timing accuracy without changing felt speed)

## Canonical Reference: `original/`

The 1996 source is preserved verbatim under `original/`. **It is the
ground truth.** Any question about gameplay, physics, scoring, level
format, or design intent must be answered by reading the relevant
file there — not by inferring from the modernized port.

Key files for common questions:

| Question | File |
|----------|------|
| Ball physics, paddle bounce | `original/ball.c`, `original/paddle.c` |
| Block types, hit logic | `original/blocks.c` |
| Scoring, multipliers, bonus | `original/score.c`, `original/bonus.c` |
| Level file format, parsing | `original/level.c` |
| Save / load / high scores | `original/file.c`, `original/highscore.c` |
| Game mode state machine | `original/main.c` |
| Window/sub-window layout | `original/init.c`, `original/stage.c` |
| Audio playback model | `original/audio/LINUXaudio.c` |
| Constants and limits | `original/include/*.h` |

When citing a design decision, use `original/<file>.c:<line>` so the
reader can see the original code, not just trust the citation.

## Reference Documents

- `original/` — **canonical 1996 source — read this first**
- `docs/SPECIFICATION.md` — comprehensive technical spec of every
  subsystem
- `docs/MODERNIZATION.md` — from-to architectural plan
- `levels/*.data` — canonical level designs
- `original/docs/` — original 1996 documentation
