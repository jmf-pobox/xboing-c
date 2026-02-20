# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

This is a C game modernization project. The original codebase has not been actively maintained in over 20 years. Every change must be deliberate, tested, and reversible. We are not rewriting — we are modernizing incrementally while preserving the game's behavior.

I am a principal engineer. Every change I make leaves the codebase in a better state than I found it. I do not excuse new problems by pointing at existing ones. I do not defer quality to a future ticket. I do not create tech debt.

## Project Overview

XBoing is a classic X11 breakout/blockout game (1993-1996, Justin C. Kibell) modernized for current Linux systems. It uses pure Xlib (no Motif/Xt) with the XPM library for pixmap graphics.

**Reference documents:**
- `docs/SPECIFICATION.md` — comprehensive technical spec of all 16 subsystems
- `docs/MODERNIZATION.md` — from-to architectural changes for SDL2-based modernization

## Modernization Principles

- **Test before you change, not after.** Before modifying any subsystem, write tests that characterize its current behavior. Only then refactor.
- **Never rewrite from scratch.** Incremental modernization beats big-bang rewrites. Replace one subsystem at a time, prove equivalence with tests, move on.
- **Separate concerns in commits.** A commit that modernizes code must not also fix a bug or add a feature. Reviewers need to see that behavior is preserved.
- **Preserve original behavior first.** The game works. Understand why something was done before deciding it was done wrong. Idioms from 20 years ago may look wrong but encode real constraints.
- **Document architectural decisions.** Use `docs/DESIGN.md` for non-trivial choices (replacing a subsystem, changing a data format, dropping platform support). Log the decision before writing the code.

## Modernization Roadmap

When approaching this codebase, work in this order:

1. **Build it** — get the code compiling with a modern toolchain. *(Done — compiles with gcc)*
2. **Run it** — get the game running. Verify basic gameplay works. *(Done — runs on modern Linux)*
3. **Sanitize it** — build with ASan + UBSan. Fix every issue found. This is where the worst bugs live.
4. **Test it** — add characterization tests for the most critical subsystems (save/load, game rules, collision math).
5. **Format it** — apply clang-format incrementally (one module at a time, format-only commits).
6. **Lint it** — enable clang-tidy and cppcheck. Fix issues incrementally.
7. **Modernize it** — now you can safely refactor, replace subsystems, and add features.

**Do not skip to step 7.** Steps 1-6 build the safety net that makes step 7 possible.

## Build Commands (Current — Legacy Xlib)

```bash
make          # Build (generates audio.c symlink, version.c, compiles all, links)
make clean    # Remove objects, binary, and generated files
./xboing      # Run the game (must run from repo root for asset paths)
./xboing -setup  # Print configuration/path info
./xboing -help   # Show command-line options
```

**Dependencies:** `libxpm-dev`, `libx11-dev`, GCC. Libraries linked: `-lXpm -lX11 -lm`.

## Build Commands (Target — CMake + SDL2)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

**Target dependencies:** `libsdl2-dev`, `libsdl2-image-dev`, `libsdl2-mixer-dev`, `libsdl2-ttf-dev`, `libcmocka-dev`.

## Toolchain

| Tool | Purpose | Install |
|------|---------|---------|
| **gcc / clang** | Compilation with strict warnings | System package manager |
| **CMake** | Build system | `apt install cmake` |
| **clang-format** | Code formatting | `apt install clang-format` |
| **clang-tidy** | Deep semantic static analysis | `apt install clang-tidy` |
| **cppcheck** | Syntactic static analysis (catches different issues than clang-tidy) | `apt install cppcheck` |
| **CMocka** | Unit test framework (v2.0+, supports TAP 14) | `apt install libcmocka-dev` |
| **Valgrind** | Memory debugging | `apt install valgrind` |
| **shellcheck** | Shell script linting | `apt install shellcheck` |

## Quality Gates

Run before every commit. Zero warnings, zero errors, all tests green.

```bash
# Build with strict warnings (treat warnings as errors)
cmake --build build --config Debug 2>&1 | grep -c "warning:" && exit 1

# Static analysis
clang-tidy src/**/*.c -- -I include/
cppcheck --enable=warning,style,performance,portability --error-exitcode=1 src/

# Formatting check
clang-format --dry-run --Werror src/**/*.c src/**/*.h include/**/*.h

# Unit tests
ctest --test-dir build --output-on-failure

# Sanitizer tests (separate build)
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure

# Shell scripts
shellcheck scripts/*.sh
```

### Compiler Warnings Policy

The base warning set for all translation units:

```
-Wall -Wextra -Wpedantic -Werror
-Wconversion -Wshadow -Wdouble-promotion
-Wformat=2 -Wformat-overflow=2
-Wnull-dereference -Wuninitialized
-Wstrict-prototypes -Wold-style-definition
```

For legacy files not yet modernized, suppress specific warnings per-file in CMakeLists.txt rather than globally weakening the policy. Track each suppression as a bead to resolve.

### Sanitizer Builds

| Sanitizer | What it catches | Flag |
|-----------|----------------|------|
| AddressSanitizer (ASan) | Buffer overflows, use-after-free, double-free, stack overflow | `-fsanitize=address` |
| UndefinedBehaviorSanitizer (UBSan) | Integer overflow, null deref, alignment, shift errors | `-fsanitize=undefined` |
| MemorySanitizer (MSan) | Reads of uninitialized memory (clang only, Linux only) | `-fsanitize=memory` |

ASan + UBSan run in CI on every PR. MSan and Valgrind are periodic deep checks. **This is critical for a 20-year-old C codebase.** Expect to find memory bugs. Sanitizers are not optional — they are the primary safety net during modernization.

## Testing Strategy

Testing a game is harder than testing a library. Most game code has tight coupling to global state, hardware, and frame timing. The strategy is to progressively decouple and test at every layer.

### Layer 1: Unit Tests (CMocka)

Pure logic with no side effects. Highest-value, lowest-cost testing.

**What to test:**
- Math utilities (collision geometry, trig paddle bounce, velocity clamping)
- State machines (game modes, ball states, block states, bonus sequence)
- Serialization (save/load round-trips, level file parsing, config parsing)
- Game rules (scoring, block point values, extra life thresholds, multipliers)
- String handling and text processing

**How to make legacy code testable:**
- Extract pure functions from modules that mix logic with I/O
- Introduce seams: pass function pointers instead of calling globals directly
- Replace `#include`-coupled singletons with struct pointers passed as parameters
- One test file per source file: `tests/test_<module>.c`

### Layer 2: Integration Tests

Multiple subsystems working together, but still deterministic.

**What to test:**
- Game loop ticking with fixed timestep (no real clock)
- Ball-block-paddle interaction lifecycle
- Level loading -> block spawning -> state verification
- Save -> load -> verify round-trip

**How to set up:**
- Create a headless build configuration that stubs rendering and audio
- Replace platform I/O with in-memory equivalents
- Fixed random seed for reproducibility

### Layer 3: Replay / Golden Tests

Record inputs, replay deterministically, compare results.

**What to test:**
- Full game sequences (menu -> gameplay -> victory/defeat)
- Known bug reproduction
- Performance regression (frame time during replay)

**How to set up:**
- Implement an input recording/playback system early in modernization
- Ensure deterministic updates (fixed timestep, seeded RNG)
- Store golden files in `tests/golden/`
- CI compares replay output against golden files

### Layer 4: Fuzz Testing

Feed malformed data to parsers and deserializers.

**What to fuzz:**
- Level file parsers
- Save file deserializers
- Config file parsers
- Asset loaders

**Tools:** libFuzzer (built into clang) or AFL++.

### Testing Priority for Modernization

1. **Sanitizer builds first** — find memory bugs in existing code before any changes
2. **Serialization round-trips** — save/load is the highest-risk area for data loss
3. **Game rule unit tests** — capture current behavior before touching game logic
4. **Input replay infrastructure** — enables regression testing everything else
5. **Parser fuzz tests** — old C parsers are where the security and stability bugs live

## Architecture

**State machine game loop** in `main.c`: 16 modes (intro, game, pause, demo, edit, highscore, etc.) driven by an X11 event loop with frame timing. Mode transitions are handled via a central `gameMode` variable.

**Key modules (each a .c/.h pair):**

| Module | Role |
|--------|------|
| `main.c` | Event loop, state machine, input dispatch |
| `init.c` | X11 display/window setup, colormap, pixmap loading |
| `ball.c` | Ball physics, collision detection (up to 5 simultaneous balls) |
| `blocks.c` | Block grid (18 rows x 9 cols), 30 block types, collision/explosion |
| `paddle.c` | Paddle control (keyboard/mouse), 3 sizes |
| `level.c` | Level loading from `levels/*.data` files |
| `stage.c` | Window management (play, score, level, message sub-windows) |
| `highscore.c` | Score file I/O with file locking |
| `editor.c` | Built-in level editor |
| `audio.c` | Symlink to platform-specific driver (default: `audio/LINUXaudio.c`) |

**Build-generated files:** `audio.c` is a symlink to `audio/LINUXaudio.c`. `version.c` is generated by `version.sh`.

**Assets:** XPM pixmaps in `bitmaps/`, level data in `levels/`, `.au` sounds in `sounds/`.

**Compile defines** control paths at build time: `HIGH_SCORE_FILE`, `LEVEL_INSTALL_DIR`, `SOUNDS_DIR`, `AUDIO_FILE`. These default to relative paths (game runs from repo root). Users can override at runtime via env vars: `XBOING_SCORE_FILE`, `XBOING_LEVELS_DIR`, `XBOING_SOUND_DIR`.

## Code Conventions

- C89/ANSI C with dual prototype support controlled by `NeedFunctionPrototypes` (legacy — to be removed)
- Modules use static variables for persistent state; globals declared `extern` in headers
- Graphics via XPM pixmaps (no direct drawing); double-buffered with backing store
- Region-based collision detection for blocks
- Frame-based animation with timing controls
- Audio is pluggable: swap the `audio.c` symlink target for different platforms

### Target Code Style (New Code)

Use `.clang-format` at the repo root:

```yaml
BasedOnStyle: LLVM
IndentWidth: 4
ColumnLimit: 100
BreakBeforeBraces: Allman
AllowShortFunctionsOnASingleLine: None
SortIncludes: true
```

Reformat files only when you are already modifying them for another reason — never commit a format-only change to a file you are not otherwise touching (except in dedicated formatting passes).

**Naming for new code:**
- `snake_case` for functions and variables
- `UPPER_SNAKE_CASE` for macros and constants
- Prefix public API functions with the module name: `ball_update()`, `audio_play()`

**Headers:**
- Every `.c` file has a corresponding `.h` file
- Include guards: `#ifndef MODULE_NAME_H` / `#define MODULE_NAME_H`
- System includes before project includes, alphabetized within each group

## CI Workflows

### `lint.yml` — Static Analysis

Runs clang-format check, clang-tidy, and cppcheck on push to main and PRs.

### `test.yml` — Build, Test, and Sanitizers

Matrix build: Debug and Sanitizers (ASan + UBSan). Runs CMocka tests via ctest.

### `docs.yml` — Markdown Lint

Runs markdownlint on all `.md` files.

## Development Workflow

### Workflow Tiers

Match the workflow to the scope. The deciding factor is **design ambiguity**, not size.

| Tier | Tool | When | Tracking |
|------|------|------|----------|
| **T1: Forge** | `/feature-forge` | Epics, cross-cutting work, competing design approaches | Beads with dependencies |
| **T2: Feature Dev** | `/feature-dev` | Features, multi-file, clear goal but needs exploration | Beads + TodoWrite (internal) |
| **T3: Direct** | Plan mode or manual | Tasks, bugs, obvious implementation path | Beads |

**Decision flow:**

1. Is there design ambiguity needing multi-perspective input? → **T1: Forge**
2. Does it touch multiple files and benefit from codebase exploration? → **T2: Feature Dev**
3. Otherwise → **T3: Direct** (plan mode if >3 files, manual if fewer)

**Escalation only goes up.** If T3 reveals unexpected scope, escalate to T2. If T2 reveals competing design approaches, escalate to T1. Never demote mid-flight.

**Game modernization examples:**

- Replacing the renderer with SDL2 (architectural choice, multiple valid approaches) → **T1: Forge**
- Adding CMocka tests to the save/load subsystem (multi-file, needs code exploration) → **T2: Feature Dev**
- Fixing a buffer overflow found by ASan (single root cause, obvious fix) → **T3: Direct**

### Expert Agents

Four project-specific agents in `.claude/agents/` provide domain expertise. Consult them via the Task tool or as hive-mind participants in `/feature-forge`.

| Agent | Expertise | Consult when... |
|-------|-----------|-----------------|
| `xboing-author` | Original author persona (Justin C. Kibell). Game vision, feel, design intent. | Any change touches gameplay mechanics, physics, scoring, level design, constants, or player experience. **Must approve** gameplay-affecting changes. |
| `c-modernization-expert` | Modern C (C11/C17/C23), sanitizers, static analysis, safe refactoring. | Modernizing legacy code, fixing compiler warnings, resolving sanitizer findings, reviewing unsafe patterns. |
| `av-platform-expert` | SDL2, legacy X11/Xlib, ALSA/PulseAudio, asset pipeline (XPM→PNG, .au→WAV). | Porting rendering or audio subsystems, designing the SDL2 abstraction layer, converting assets. |
| `test-expert` | CMocka, characterization testing, fuzz testing, creating testability seams. | Writing tests for legacy code, designing test harness, extracting pure functions from coupled modules. |

**Workflow integration:**

- **T1: Forge** — all four agents participate as hive-mind experts. `xboing-author` has veto power on gameplay changes.
- **T2: Feature Dev** — delegate to the relevant expert(s) as subagents for exploration and review.
- **T3: Direct** — consult `xboing-author` if the change could affect game feel; consult `c-modernization-expert` for any C code changes.

### Branch Discipline

Feature work goes on feature branches. Never commit directly to main.

| Prefix | Use |
|--------|-----|
| `feat/` | New features, new systems |
| `fix/` | Bug fixes |
| `refactor/` | Modernization, restructuring (no behavior change) |
| `port/` | Platform porting work |
| `docs/` | Documentation only |

### Commit Message Format

`type(scope): description`

| Prefix | Use |
|--------|-----|
| `feat:` | New feature or capability |
| `fix:` | Bug fix |
| `refactor:` | Code modernization, no behavior change |
| `test:` | Adding or updating tests |
| `port:` | Platform-specific changes |
| `build:` | CMake, CI, dependency changes |
| `docs:` | Documentation |

### Session Close Protocol

Before ending any session, follow AGENTS.md landing-the-plane workflow. Work is **not** complete until `git push` succeeds.

## What NOT to Change Without Care

- **Global state and initialization order** — implicit dependencies exist. Map them before refactoring.
- **Ball physics math** — the trigonometric paddle bounce, collision regions, and velocity clamping ARE the game feel. Test thoroughly before any changes.
- **Level file format** — keep identical so all 80+ levels work unchanged.
- **Game constants** — MAX_BALLS=5, grid 18x9, paddle sizes 40/50/70, DIST_BASE=30, all scoring values. These define the gameplay experience.
- **Save file formats** — players may have save files. Maintain backward compatibility or provide migration.
