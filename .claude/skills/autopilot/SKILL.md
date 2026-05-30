---
name: autopilot
description: Autonomous phase-by-phase development loop for XBoing integration layer
user_invocable: true
---

# /autopilot

Run the autonomous development loop for the XBoing SDL2 integration layer.
Works phase-by-phase: plan beads for the next phase, implement them on a
single branch, create one PR per phase, get review, merge, repeat.

## Step 0: Load Context

Before writing any code, build a mental model of the current state.

### 0a. Read the roadmap and existing integration code

```text
Read docs/INTEGRATION_ROADMAP.md        — phase structure, bead descriptions, dependencies
Read include/game_context.h             — master context struct
Read include/game_init.h                — init/destroy API
Read include/game_render.h              — current render API
Read src/game_init.c                    — module creation order, callback wiring
Read src/game_main.c                    — event loop
Read src/game_render.c                  — current rendering
Read src/sprite_catalog.h               — texture key mappings
```

### 0b. Check what exists and what's next

```bash
bd ready                                # Unblocked work
bd list --status=open                   # All open work
bd list --status=in_progress            # Anything mid-flight
```

### 0c. Verify the build is clean

```bash
cmake --build build 2>&1 | grep -c "warning:" && echo "WARNINGS PRESENT" || echo "Clean"
ctest --test-dir build --output-on-failure
```

If the build is broken or tests fail, fix that before proceeding.

## Step 0.5: Permission Check

Read `~/.claude/projects/-home-jfreeman-Coding-xboing-c-xboing/settings.json`
and confirm it contains allows for: `git`, `gh`, `bd`, `make`, `cmake`,
`ctest`, `cppcheck`, `clang-tidy`, `clang-format`, `npx markdownlint-cli2`.

If the file is missing or incomplete, create/update it with these allows
before proceeding:

```json
{
  "permissions": {
    "allow": [
      "Bash(git *)", "Bash(gh *)", "Bash(bd *)",
      "Bash(make *)", "Bash(make)",
      "Bash(cmake *)", "Bash(ctest *)",
      "Bash(cppcheck *)", "Bash(clang-tidy *)",
      "Bash(clang-format *)", "Bash(npx markdownlint-cli2 *)",
      "Bash(env *)", "Bash(echo *)", "Bash(ls *)",
      "Bash(which *)", "Bash(wc *)", "Bash(sort *)",
      "Bash(head *)", "Bash(tail *)", "Bash(cat *)",
      "Bash(grep *)", "Bash(find *)",
      "Bash(sleep *)", "Bash(python3 *)",
      "mcp__github__*"
    ]
  }
}
```

## Step 1: Plan the Next Phase

Determine which phase to work on next by checking the roadmap against
completed beads:

```bash
bd list --status=closed | grep "xboing-imr"    # What's done
bd list --status=open | grep "xboing-imr"      # What's open
```

### 1a. Create beads for the phase

If the next phase's task beads don't exist yet, create them from the
roadmap. Each bead should have:

- A clear title matching the roadmap (e.g., "Bead 2.1: Paddle rendering + input")
- A detailed `--description` with: files to create/modify, what to wire,
  verification criteria
- `--parent=<phase-epic-id>` to link to the phase epic
- `--deps=<predecessor-id>` for sequential dependencies within the phase

Example:

```bash
bd create "Bead 2.1: Paddle rendering + input" --type=task --priority=1 \
  --parent=xboing-imr.2 \
  --description="Render paddle from paddle_system_get_render_info(). Wire LEFT/RIGHT keyboard and mouse input. Files: game_render.c (paddle), game_input.c (new). Verification: paddle moves with keyboard and mouse."
```

### 1b. Read the module APIs you'll need

Before writing integration code, read the headers for every module
you'll touch in this phase. Understand:

- Create/destroy signatures and required parameters
- Callback struct fields and what they expect
- Render info struct fields
- Environment struct fields
- Any constants (play area dimensions, limits, etc.)

Use the Explore agent for thorough codebase analysis if the phase
touches unfamiliar modules.

## Step 2: Claim and Branch

```bash
# Claim the first bead
bd update <id> --status=in_progress

# Branch from master
git checkout master && git pull origin master
git checkout -b <prefix>/<phase-slug> master
```

Use a single branch per phase (e.g., `feat/integration-core-gameplay`),
not one branch per bead. Related beads get individual commits on the
same branch.

## Step 3: Implement Bead-by-Bead

For each bead in the phase:

### 3a. Read before you write

Read every file you're about to modify. Read the module headers for
APIs you'll call. Understand the existing patterns before adding to them.

### 3b. Write the code

- Follow existing patterns in game_init.c, game_render.c, etc.
- Wire callbacks at module creation time (can't update callbacks post-creation)
- Use sprite_catalog.h lookup helpers for all texture references
- All module queries return render info structs — iterate and draw

### 3c. Build and test

```bash
cmake --build build 2>&1 | grep -E "error:|warning:" | head -20
ctest --test-dir build --output-on-failure
```

### 3d. Commit the bead

```bash
git add <specific files>
git commit -m "<type>(<scope>): <summary>

Bead <bead-id>

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

### 3e. Close the bead and move to next

```bash
bd close <bead-id> -r "Committed in <sha>"
bd update <next-bead-id> --status=in_progress
```

Repeat 3a-3e for each bead in the phase.

## Step 4: Quality Gates (All Must Pass)

After all beads in the phase are committed, run the full gate suite:

```bash
# Build both systems
cmake --build build --clean-first
make clean && make

# Static analysis
cppcheck --enable=warning,style,performance,portability \
  --inline-suppr --suppress=variableScope:ball_math.c \
  --error-exitcode=1 src/ ball_math.c
clang-format --dry-run --Werror src/*.c include/game_*.h

# Tests (both builds)
ctest --test-dir build --output-on-failure
cmake --build build-asan --clean-first
ctest --test-dir build-asan --output-on-failure

# Markdown lint (if .md changed)
npx markdownlint-cli2 "*.{md,markdown}"
```

Fix any failures and commit the fix as a separate commit.

## Step 5: Push, PR, Review

```bash
git push -u origin <branch>
```

Create PR with summary covering all beads in the phase:

```bash
gh pr create --title "<type>(<scope>): Phase N — <one-line summary>" \
  --body "## Summary
- Bead N.1: ...
- Bead N.2: ...

## Test plan
- [ ] All tests pass (Debug + ASan)
- [ ] Legacy Makefile builds
- [ ] Manual verification: ...

🤖 Generated with [Claude Code](https://claude.com/claude-code)"
```

Request Copilot review:

```text
mcp__github__request_copilot_review(owner, repo, pullNumber)
```

Wait for CI, check feedback, address if needed (up to 3 rounds).

## Step 6: Merge and Close Phase

```bash
gh pr merge <pr> --squash
git checkout master && git pull origin master
git branch -d <branch>
```

Close the phase epic if all its beads are done:

```bash
bd close <phase-epic-id> -r "Merged PR #<n>"
bd sync
```

## Step 7: Update Roadmap

Update `docs/INTEGRATION_ROADMAP.md` to mark the completed phase and
note any deviations from the original plan.

## Step 8: Loop

Go to Step 1. Stop when all phases are complete.

## Expert Agent Consultation

Consult project-specific agents for domain expertise:

| When | Agent | Persona |
|------|-------|---------|
| Gameplay mechanics, physics, constants, scoring | `jck` | Justin C. Kibell |
| C code quality, sanitizer findings, safe patterns | `jdc` | John D. Carmack |
| SDL2 rendering/audio, asset pipeline, X11 porting | `sjl` | Sam J. Lantinga |
| Test strategy, making legacy code testable | `gjm` | Glenford J. Myers |

**Always consult `jck`** before changing any gameplay-affecting
constant, physics formula, or scoring rule.
