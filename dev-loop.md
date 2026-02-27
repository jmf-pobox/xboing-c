# Development Loop

This document defines the autonomous phase-by-phase development loop for
the XBoing integration layer. It is the reference specification for the
`/autopilot` skill.

## Overview

The integration layer wires 35 static libraries into a playable SDL2 game.
Work is organized into 6 phases (see `docs/INTEGRATION_ROADMAP.md`), each
containing 2-6 beads. The loop processes one phase per iteration:

1. Load context (roadmap, existing code, module APIs)
2. Create beads for the phase (if they don't exist)
3. Implement bead-by-bead on a single branch
4. Run full quality gates
5. Create PR, get review, merge
6. Close phase, update roadmap, loop

## Loop: Repeat Until All Phases Complete

### 0. Load Context

Before writing any code, build a mental model:

```
Read docs/INTEGRATION_ROADMAP.md        — phase structure, dependencies
Read include/game_context.h             — master struct
Read src/game_init.c                    — module creation, callback wiring
Read src/game_render.c                  — current rendering
Read src/sprite_catalog.h               — texture key mappings
```

Check current state:
```bash
bd ready                                # Unblocked work
bd list --status=open                   # All open
bd list --status=in_progress            # Anything mid-flight
```

Verify clean build:
```bash
cmake --build build 2>&1 | grep -c "warning:"
ctest --test-dir build --output-on-failure
```

### 1. Plan the Phase

Determine the next phase from the roadmap. Create task beads if they
don't exist:

```bash
bd create "Bead N.M: <title>" --type=task --priority=1 \
  --parent=<phase-epic> \
  --deps=<predecessor> \
  --description="<files, wiring, verification>"
```

**Read module headers** for every system you'll touch. Understand create
signatures, callback structs, render info types, environment structs.

### 2. Branch

```bash
bd update <first-bead> --status=in_progress
git checkout master && git pull origin master
git checkout -b feat/<phase-slug> master
```

One branch per phase, not per bead.

### 3. Implement Bead-by-Bead

For each bead:

1. **Read** every file you'll modify and every module header you'll call
2. **Write** the code, following existing patterns
3. **Build and test** — `cmake --build build && ctest --test-dir build`
4. **Commit** — `git commit -m "feat(integration): <bead summary>\n\nBead <id>"`
5. **Close bead** — `bd close <id> -r "Committed in <sha>"`
6. **Claim next** — `bd update <next-id> --status=in_progress`

### 4. Quality Gates (All Must Pass)

```bash
# CMake build (strict)
cmake --build build --clean-first

# Legacy build
make clean && make

# Static analysis
cppcheck --enable=warning,style,performance,portability \
  --inline-suppr --suppress=variableScope:ball_math.c \
  --error-exitcode=1 src/ ball_math.c
clang-format --dry-run --Werror src/*.c include/game_*.h

# Tests
ctest --test-dir build --output-on-failure
cmake --build build-asan --clean-first
ctest --test-dir build-asan --output-on-failure

# Markdown lint (if changed)
npx markdownlint-cli2 "*.{md,markdown}"
```

### 5. Push and PR

```bash
git push -u origin <branch>
gh pr create --title "feat(integration): Phase N — <summary>" \
  --body "## Summary\n- Bead N.1: ...\n- Bead N.2: ...\n\n## Test plan\n..."
```

Request Copilot review via MCP tool. Wait for CI. Address feedback (up to
3 rounds).

### 6. Merge and Close

```bash
gh pr merge <pr> --squash
git checkout master && git pull origin master
git branch -d <branch>
bd close <phase-epic> -r "Merged PR #<n>"
bd sync
```

### 7. Update and Loop

Update `docs/INTEGRATION_ROADMAP.md` to mark the completed phase.
Go to step 0.

## Expert Agents

| When | Agent |
|------|-------|
| Gameplay mechanics, physics, constants | `xboing-author` |
| C code quality, sanitizer findings | `c-modernization-expert` |
| SDL2 rendering/audio, X11 porting | `av-platform-expert` |
| Test strategy, testability seams | `test-expert` |
