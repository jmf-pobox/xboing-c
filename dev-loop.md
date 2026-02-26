# Development Loop

This document defines the autonomous bead-by-bead development loop for
the XBoing modernization project. It is the reference specification for
the `/autopilot` skill.

## Loop: Repeat Until `bd ready` Returns Empty

### 1. Preflight

```
bd ready                          # Pick the highest-priority unblocked bead
bd show <id>                      # Read description and dependencies
```

Choose the **workflow tier** based on design ambiguity:

| Tier | When | Tool |
|------|------|------|
| T3: Direct | Obvious implementation, fewer than 3 files | Manual or plan mode |
| T2: Feature Dev | Multi-file, needs codebase exploration | `/feature-dev` |
| T1: Forge | Competing design approaches, cross-cutting | `/feature-forge` |

### 2. Claim and Branch

```
bd update <id> --status=in_progress
git checkout -b <prefix>/<slug> master
```

Branch prefixes: `feat/`, `fix/`, `refactor/`, `port/`, `docs/`, `test/`.

### 3. Do the Work

- Read before you write. Understand existing code before modifying it.
- Consult expert agents when appropriate (see CLAUDE.md Agent table).
- Separate concerns: format-only commits stay separate from logic changes.
- Write ADR in `docs/DESIGN.md` for non-trivial decisions.

### 4. Quality Gates (All Must Pass)

```bash
# Build both systems
cmake --build build --clean-first
make clean && make

# Static analysis (strict — zero warnings)
cppcheck --enable=warning,style,performance,portability \
  --inline-suppr --suppress=variableScope:ball_math.c \
  --error-exitcode=1 src/ ball_math.c
cppcheck --enable=warning,style,performance,portability \
  --inline-suppr --suppress=missingIncludeSystem \
  --suppress=variableScope --error-exitcode=1 \
  -I include/ $(ls *.c | grep -v '^ball_math\.c$')
cppcheck --enable=warning,style,performance,portability \
  --inline-suppr --suppress=missingIncludeSystem \
  --error-exitcode=1 -I include/ tests/
clang-tidy src/*.c ball_math.c -- -I include/ -I /usr/include/X11

# Formatting (modernized files only)
clang-format --dry-run --Werror src/*.c ball_math.c include/sdl2_*.h

# Unit tests
ctest --test-dir build --output-on-failure

# Sanitizer tests
cmake --build build-asan --clean-first
ctest --test-dir build-asan --output-on-failure

# Markdown lint (if .md files changed)
npx markdownlint-cli2 "*.{md,markdown}"
```

### 5. Commit

```bash
git add <specific files>
git commit -m "<type>(<scope>): <description>

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

Commit types: `feat:`, `fix:`, `refactor:`, `test:`, `port:`, `build:`, `docs:`.

### 6. Push and Create PR

```bash
git push -u origin <branch>
gh pr create --title "<type>(<scope>): <summary>" \
  --body "## Summary\n...\n## Test plan\n..."
```

### 7. Copilot Review Cycle

```bash
# Request review
gh api repos/{owner}/{repo}/pulls/{pr}/requested_reviewers \
  -f '{"reviewers":["copilot-pull-request-reviewer"]}'

# Wait for CI (all checks must pass)
gh pr checks <pr> --watch

# Check for review feedback
gh api repos/{owner}/{repo}/pulls/{pr}/reviews
gh api repos/{owner}/{repo}/pulls/{pr}/comments
```

**If feedback exists:** Address it. Push fixes. Re-check CI. Repeat up
to 3 rounds. If feedback is a false positive, respond with rationale.

**If no feedback after CI passes:** Proceed to merge.

### 8. Merge and Close

```bash
# Squash merge
gh pr merge <pr> --squash

# Update local
git checkout master && git pull origin master
git branch -d <branch>

# Close bead
bd close <id> -r "Merged PR #<n>"

# Close parent epic if all children are done
bd show <parent-id>   # Check children status
bd close <parent-id> -r "All children complete"

# Sync
bd sync
```

### 9. Loop

Go to step 1. Stop when `bd ready` returns empty.
