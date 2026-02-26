---
name: autopilot
description: Autonomous bead-by-bead development loop for XBoing modernization
user_invocable: true
---

# /autopilot

Run the autonomous development loop defined in `dev-loop.md`. Pick the
next unblocked bead, implement it, pass all quality gates, create a PR,
get Copilot review, merge, and repeat until no beads remain.

## Step 0: Permission Check

Before starting, verify the user project settings file exists and has
the required Bash allows. Read
`~/.claude/projects/-home-jfreeman-Coding-xboing-c-xboing/settings.json`
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

## Step 1: Preflight

```bash
bd ready
```

If empty, report "All beads complete" and stop.

Otherwise pick the highest-priority unblocked bead and run `bd show <id>`
to read the full description. Choose the workflow tier per CLAUDE.md:

- **T3 Direct** — obvious implementation, fewer than 3 files
- **T2 Feature Dev** (`/feature-dev`) — multi-file, needs exploration
- **T1 Forge** (`/feature-forge`) — competing designs, cross-cutting

## Step 2: Claim and Branch

```bash
bd update <id> --status=in_progress
git checkout master && git pull origin master
git checkout -b <prefix>/<slug> master
```

Use the branch prefix that matches the work type: `feat/`, `fix/`,
`refactor/`, `port/`, `docs/`, `test/`.

## Step 3: Do the Work

Follow the chosen workflow tier. Key rules:

- Read before you write.
- Consult expert agents when appropriate (see CLAUDE.md Agent table).
- Separate format-only commits from logic changes.
- Write an ADR in `docs/DESIGN.md` for non-trivial decisions.

## Step 4: Quality Gates (All Must Pass)

Run every gate from `dev-loop.md` section 4. Zero warnings, zero errors,
all tests green. Do not skip any gate. If a gate fails, fix the issue
and re-run.

## Step 5: Commit

```bash
git add <specific files>
git commit  # conventional commit message with Co-Authored-By trailer
```

## Step 6: Push and Create PR

```bash
git push -u origin <branch>
gh pr create --title "<type>(<scope>): <summary>" \
  --body "## Summary\n...\n## Test plan\n..."
```

## Step 7: Copilot Review Cycle

Request a Copilot review using the GitHub MCP tool
`mcp__github__request_copilot_review`. Then:

1. Wait for all CI checks to pass (`gh pr checks <pr> --watch`).
2. Check for review comments (`gh api repos/{owner}/{repo}/pulls/{pr}/comments`).
3. If feedback exists, address it, push fixes, re-check CI. Repeat up
   to 3 rounds. If feedback is a false positive, reply with rationale.
4. If no feedback after CI passes, proceed to merge.

## Step 8: Merge and Close

```bash
gh pr merge <pr> --squash
git checkout master && git pull origin master
git branch -d <branch>
bd close <id> -r "Merged PR #<n>"
```

Check if the parent epic has all children complete. If so, close it too:

```bash
bd show <parent-id>
bd close <parent-id> -r "All children complete"
```

Sync the beads database:

```bash
bd sync
```

## Step 9: Loop

Go back to Step 1. Stop when `bd ready` returns empty.
