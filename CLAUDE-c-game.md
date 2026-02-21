# Agent Instructions

This is a C game modernization project. The original codebase has not been actively maintained in over 20 years. Every change must be deliberate, tested, and reversible. We are not rewriting — we are modernizing incrementally while preserving the game's behavior.

I am a principal engineer. Every change I make leaves the codebase in a better state than I found it. I do not excuse new problems by pointing at existing ones. I do not defer quality to a future ticket. I do not create tech debt.

## Modernization Principles

- **Test before you change, not after.** Before modifying any subsystem, write tests that characterize its current behavior. Only then refactor.
- **Never rewrite from scratch.** Incremental modernization beats big-bang rewrites. Replace one subsystem at a time, prove equivalence with tests, move on.
- **Separate concerns in commits.** A commit that modernizes code must not also fix a bug or add a feature. Reviewers need to see that behavior is preserved.
- **Preserve original behavior first.** The game works (or worked). Understand why something was done before deciding it was done wrong. Idioms from 20 years ago may look wrong but encode real constraints.
- **Document architectural decisions.** Use `DESIGN.md` for non-trivial choices (replacing a subsystem, changing a data format, dropping platform support). Log the decision before writing the code.

## Toolchain

| Tool | Purpose | Install |
|------|---------|---------|
| **gcc / clang** | Compilation with strict warnings | System package manager |
| **CMake** | Build system | `brew install cmake` / `apt install cmake` |
| **clang-format** | Code formatting | Ships with LLVM / `brew install clang-format` |
| **clang-tidy** | Deep semantic static analysis | Ships with LLVM / `brew install llvm` |
| **cppcheck** | Syntactic static analysis (catches different issues than clang-tidy) | `brew install cppcheck` / `apt install cppcheck` |
| **CMocka** | Unit test framework (v2.0+, supports TAP 14) | `brew install cmocka` / build from source |
| **Valgrind** | Memory debugging (Linux only) | `apt install valgrind` |
| **shellcheck** | Shell script linting | `brew install shellcheck` |

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

# Sanitizer tests (separate build, CI runs this)
cmake --build build-asan --config Debug
ctest --test-dir build-asan --output-on-failure

# Shell scripts (if any)
shellcheck scripts/*.sh
```

Adapt glob patterns to match the actual project structure. The key principle: **every gate must be automatable and deterministic.**

### Compiler Warnings Policy

The base warning set for all translation units:

```text
-Wall -Wextra -Wpedantic -Werror
-Wconversion -Wshadow -Wdouble-promotion
-Wformat=2 -Wformat-overflow=2
-Wnull-dereference -Wuninitialized
-Wstrict-prototypes -Wold-style-definition
```

For legacy files not yet modernized, suppress specific warnings per-file in CMakeLists.txt rather than globally weakening the policy. Track each suppression as a bead to resolve.

### Sanitizer Builds

Maintain a separate CMake preset for sanitizer builds:

| Sanitizer | What it catches | Flag |
|-----------|----------------|------|
| AddressSanitizer (ASan) | Buffer overflows, use-after-free, double-free, stack overflow | `-fsanitize=address` |
| UndefinedBehaviorSanitizer (UBSan) | Integer overflow, null deref, alignment, shift errors | `-fsanitize=undefined` |
| MemorySanitizer (MSan) | Reads of uninitialized memory (clang only, Linux only) | `-fsanitize=memory` |

ASan + UBSan run in CI on every PR. MSan and Valgrind are periodic deep checks.

**This is critical for a 20-year-old C codebase.** Expect to find memory bugs. Sanitizers are not optional — they are the primary safety net during modernization.

## Testing Strategy

Testing a game is harder than testing a library. Most game code has tight coupling to global state, hardware, and frame timing. The strategy is to progressively decouple and test at every layer.

### Layer 1: Unit Tests (CMocka)

Pure logic with no side effects. This is the highest-value, lowest-cost testing.

**What to test:**

- Math utilities (vectors, matrices, fixed-point, random number generators)
- Data structures (linked lists, hash tables, spatial indexes, pools)
- State machines (AI behavior trees, menu state, game phase transitions)
- Serialization (save/load, level file parsing, config parsing)
- Game rules (damage calculation, collision detection math, inventory logic)
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
- Entity creation, update, and destruction lifecycle
- Event/message dispatch between systems
- Save → load → verify round-trip
- Level loading → entity spawning → state verification

**How to set up:**

- Create a headless build configuration that stubs rendering and audio
- Replace platform I/O with in-memory equivalents (virtual filesystem)
- Fixed random seed for reproducibility

### Layer 3: Replay / Golden Tests

The classic game testing technique. Record inputs, replay deterministically, compare results.

**What to test:**

- Full game sequences (menu → gameplay → victory/defeat)
- Known bug reproduction (record the sequence that triggers a bug, test the fix)
- Performance regression (measure frame time during replay, alert on regressions)

**How to set up:**

- Implement an input recording/playback system early in modernization
- Ensure deterministic updates (fixed timestep, seeded RNG, sorted entity lists)
- Store golden files in `tests/golden/` — version-controlled expected outputs
- CI compares replay output against golden files, fails on diff

### Layer 4: Fuzz Testing

Feed malformed data to parsers and deserializers.

**What to fuzz:**

- Level/map file parsers
- Save file deserializers
- Network packet handlers (if multiplayer)
- Config file parsers
- Asset loaders (textures, sounds, models)

**Tools:** libFuzzer (built into clang) or AFL++. These find crashes that no human tester would discover.

### Testing Priority for Modernization

When starting from zero tests on a 20-year-old codebase, prioritize in this order:

1. **Sanitizer builds first** — find memory bugs in existing code before any changes
2. **Serialization round-trips** — save/load is the highest-risk area for data loss
3. **Game rule unit tests** — capture current behavior before touching game logic
4. **Input replay infrastructure** — enables regression testing everything else
5. **Parser fuzz tests** — old C parsers are where the security and stability bugs live

## CI Workflows

### `lint.yml` — Static Analysis

```yaml
name: Lint
on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  lint:
    runs-on: ubuntu-latest
    timeout-minutes: 10
    steps:
      - uses: actions/checkout@v4
      - name: Install tools
        run: |
          sudo apt-get update
          sudo apt-get install -y clang-tidy cppcheck clang-format
      - name: clang-format check
        run: |
          find src include -name '*.c' -o -name '*.h' | xargs clang-format --dry-run --Werror
      - name: clang-tidy
        run: |
          cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
          clang-tidy -p build src/**/*.c
      - name: cppcheck
        run: |
          cppcheck --enable=warning,style,performance,portability \
            --error-exitcode=1 --suppress=missingIncludeSystem src/
```

### `test.yml` — Build, Test, and Sanitizers

```yaml
name: Test
on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  test:
    runs-on: ubuntu-latest
    timeout-minutes: 20
    strategy:
      matrix:
        build_type: [Debug, Sanitizers]
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake libcmocka-dev
      - name: Configure
        run: |
          if [ "${{ matrix.build_type }}" = "Sanitizers" ]; then
            cmake -B build -DCMAKE_BUILD_TYPE=Debug \
              -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
          else
            cmake -B build -DCMAKE_BUILD_TYPE=Debug
          fi
      - name: Build
        run: cmake --build build
      - name: Test
        run: ctest --test-dir build --output-on-failure
```

### `docs.yml` — Markdown Lint

```yaml
name: Docs
on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  docs:
    runs-on: ubuntu-latest
    timeout-minutes: 5
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: "22"
      - run: npx markdownlint-cli2 "**/*.md" "#node_modules"
```

Pin action SHAs once you set up the actual repo. These templates use version tags for readability.

## Development Workflow

### Branch Discipline

Feature work goes on feature branches. Never commit directly to main.

```bash
git checkout -b feat/short-description main
# ... work, commit, push ...
# create PR, complete code review, merge, then delete branch
```

| Prefix | Use |
|--------|-----|
| `feat/` | New features, new systems |
| `fix/` | Bug fixes |
| `refactor/` | Modernization, restructuring (no behavior change) |
| `port/` | Platform porting work |
| `docs/` | Documentation only |

### Micro-Commits

One logical change per commit. Quality gates pass before every commit.

Commit message format: `type(scope): description`

| Prefix | Use |
|--------|-----|
| `feat:` | New feature or capability |
| `fix:` | Bug fix |
| `refactor:` | Code modernization, no behavior change |
| `test:` | Adding or updating tests |
| `port:` | Platform-specific changes |
| `build:` | CMake, CI, dependency changes |
| `docs:` | Documentation |

### Code Review Flow

1. **Create PR** — push branch, open PR.
2. **Wait for CI** — lint, test, sanitizer builds must all pass.
3. **Request review** — Copilot automated review + human review.
4. **Address feedback** — commit fixes, push, ensure gates pass.
5. **Merge only when** — CI green, feedback addressed, quality gates pass.

### Session Close Protocol

Before ending any session:

```bash
git status              # Check for uncommitted work
git add <files>         # Stage changes
git commit -m "..."     # Commit
git push                # Push to remote
```

Work is **not** complete until `git push` succeeds.

## Code Style

### Formatting

Use `.clang-format` at the repo root. Recommended starting point for a legacy game codebase:

```yaml
BasedOnStyle: LLVM
IndentWidth: 4
ColumnLimit: 100
BreakBeforeBraces: Allman
AllowShortFunctionsOnASingleLine: None
SortIncludes: true
```

Adjust to match the prevailing style of the original codebase where reasonable. Consistency within a file beats adherence to any style guide. Reformat files only when you are already modifying them for another reason — never commit a format-only change to a file you are not otherwise touching.

### Naming

Preserve the original codebase's naming conventions unless they are actively harmful. When writing new code:

- `snake_case` for functions and variables
- `UPPER_SNAKE_CASE` for macros and constants
- `TypeName_t` or `TypeName` for typedefs (match the existing convention)
- Prefix public API functions with the module name: `render_init()`, `audio_play()`

### Headers

- Every `.c` file has a corresponding `.h` file
- Include guards: `#ifndef MODULE_NAME_H` / `#define MODULE_NAME_H` (not `#pragma once` — maximize portability)
- Minimize includes in headers — forward-declare where possible
- System includes before project includes, alphabetized within each group

## What NOT to Change Without Care

- **Global state and initialization order** — 20-year-old codebases often have implicit initialization dependencies. Map them before refactoring.
- **Fixed-point math or custom float handling** — may exist for platform compatibility. Understand the target hardware before replacing with standard float.
- **Platform abstraction layers** — even crude ones encode real portability knowledge.
- **Network protocol wire formats** — if multiplayer exists, wire compatibility matters.
- **Save file formats** — players may have save files. Maintain backward compatibility or provide migration.

## Modernization Roadmap

When approaching a 20-year-old codebase, work in this order:

1. **Build it** — get the code compiling with a modern toolchain (gcc 13+ / clang 17+). Fix errors, suppress warnings temporarily per-file.
2. **Run it** — get the game running. Verify basic gameplay works.
3. **Sanitize it** — build with ASan + UBSan. Fix every issue found. This is where the worst bugs live.
4. **Test it** — add characterization tests for the most critical subsystems (save/load, game rules, math).
5. **Format it** — apply clang-format incrementally (one module at a time, format-only commits).
6. **Lint it** — enable clang-tidy and cppcheck. Fix issues incrementally.
7. **Modernize it** — now you can safely refactor, replace subsystems, and add features.

Do not skip to step 7. Steps 1-6 build the safety net that makes step 7 possible.
