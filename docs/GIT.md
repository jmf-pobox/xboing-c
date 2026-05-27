# Git Workflow

This document covers branching, commits, PRs, review, merge, and
cleanup. Read it in full before writing any code.

## Branch Protection

**master has branch protection. Direct pushes are rejected.** Every
change — including docs-only, single-line fixes, and formatting —
must go through a pull request. Never commit on master. Before any
commit, check `git branch --show-current`. If on master, create a
branch first.

## Branch Discipline

Feature work goes on feature branches created from master:

```bash
git checkout -b <prefix>/short-description master
```

| Prefix | Use |
|--------|-----|
| `feat/` | New features, new systems |
| `fix/` | Bug fixes |
| `refactor/` | Modernization, restructuring (no behavior change) |
| `port/` | Platform porting work |
| `docs/` | Documentation only |
| `test/` | Test additions or infrastructure |
| `chore/` | Tooling, packaging, dotfiles, bd state sync |
| `style/` | Format-only or lint-only passes |

**Never `git rebase` or `git push --force` on a branch with an open
PR.** Force-push rewrites commit SHAs, which orphans every reviewer's
in-flight comments (Copilot/Cursor anchor reviews to SHAs). Resolve
base conflicts via merge — `git merge origin/master` or "Update
branch" in the PR UI — both preserve SHAs.

**Plain English over git internals.** In PR discussion, say "the
latest commit" or paste the SHA. Avoid "HEAD" in user-facing prose.

## Commit Messages

Format: `type(scope): description`

| Prefix | Use |
|--------|-----|
| `feat:` | New feature or capability |
| `fix:` | Bug fix |
| `refactor:` | Code modernization, no behavior change |
| `test:` | Adding or updating tests |
| `port:` | Platform-specific changes |
| `build:` | CMake, CI, dependency changes |
| `docs:` | Documentation |

## Stop and Ask

The following modify shared state in ways that are hard to reverse.
**Stop and ask the user before doing any of them:**

- `git push --force` / `--force-with-lease` on a branch with an open PR
- `git rebase` on a branch with an open PR
- `git reset --hard` anywhere except a worktree just created
- Closing or re-opening a PR
- Deleting a branch the user may not have pulled
- Pushing to master directly (branch protection rejects this, but
  don't try)

## PR Workflow

### Creating the PR

1. `make check` must pass before creating a PR (see `docs/BUILDING.md`).
2. Push the branch: `git push -u origin <branch>`.
3. Create the PR via MCP GitHub tools
   (`mcp__plugin_github_github__create_pull_request`) or `gh pr create`.
4. PR body format:

```markdown
## Summary
<1-3 bullet points>

## Test plan
[Bulleted markdown checklist]

🤖 Generated with [Claude Code](https://claude.com/claude-code)
```

### Monitoring (mandatory, immediate)

**Arm active monitoring in the same turn as PR creation — no
exceptions for "trivial" or "docs-only" PRs.**

1. A 2-minute recurring cron polling:

   ```bash
   gh pr view <number> --json reviews,comments,reviewDecision,state,mergeable,statusCheckRollup
   ```

   The cron must continue until the PR merges. If you push new
   commits, the same cron is fine — it polls the latest state.
   Don't create a second cron.

2. Request Copilot review immediately:
   `mcp__plugin_github_github__request_copilot_review`.

### Review Rounds

**Re-request Copilot review every time you push a new commit.**
Copilot does not auto-re-review on push. Previous reviews refer to
the old SHA and don't carry forward.

For each new finding on the latest commit:

1. Address the finding in code.
2. Run `make check`.
3. Commit and `git push` (not force, not rebase).
4. Resolve the conversation thread via `gh api graphql`
   `resolveReviewThread` mutation. This is the one workflow operation
   without a structured MCP tool — it is the documented exception to
   the "prefer MCP over `gh api graphql`" rule.
5. Re-request Copilot review on the new SHA.

**Round budget:** 2-4 rounds typical. A round = push → review →
address findings → resolve threads. After ~4 rounds the reviewer
typically returns an empty pass — that's convergence. Don't quit
early on round 1; new findings often surface on round 2. Don't
grind past round 5; if findings keep multiplying, the change itself
has a problem.

### Thread Resolution

```bash
# Find unresolved thread IDs:
gh api graphql -f query='{
  repository(owner: "OWNER", name: "REPO") {
    pullRequest(number: N) {
      reviewThreads(first: 20) {
        nodes { id isResolved }
      }
    }
  }
}'

# Resolve a thread:
gh api graphql -f query='mutation {
  resolveReviewThread(input: {threadId: "THREAD_ID"}) {
    thread { isResolved }
  }
}'
```

### Merge Gate

**Never merge on the first green CI pass.** You must wait for at
least one reviewer to respond. CI green is necessary but not
sufficient.

Merge when ALL of these are true:

- All CI checks green on the latest commit
- At least one reviewer has responded (Copilot, Cursor, bugbot, or
  human). The only exception: if bugbot has not responded after 6
  minutes of polling, you may treat that as a clean pass and merge
  — but only for bugbot, not for other requested reviewers.
- The most recent reviewer pass produced no actionable findings
  (empty review, "no high-confidence vulnerabilities" Cursor pass,
  Copilot summary with zero new comments)
- Every conversation thread is resolved

That's the convergence signal. When it's met: merge. Don't wait for
explicit human approval — convergence is the approval.

### Merge Command

```bash
gh pr merge <number> --squash --delete-branch
```

### Conflict Resolution

If the branch goes `CONFLICTING`:

```bash
git fetch origin master
git merge origin/master
git push
```

Or click "Update branch" in the PR UI. Both preserve SHAs. **Never
`git rebase` or `git push --force` on a branch with an open PR.**

## Post-Merge Cleanup

**Mandatory after every merge. No asking, no waiting for
confirmation.** A merged PR with the local branch left behind is
unfinished work. Run all four steps in sequence:

```bash
git checkout master
git pull --ff-only origin master
git branch -d <branch>
git fetch --prune origin
```

**Order matters:** `git branch -d` must come before `git fetch --prune`.
A squash-merge creates a new SHA on master, so the local branch's
tip is not reachable from master. `git branch -d` succeeds because
the remote-tracking ref still exists and points at the same SHA.
If the order is reversed (prune before delete), `-d` will refuse.
Use `-D` only after verifying the branch has no commits beyond
`origin/<branch>` via `git log <branch> ^master`.

The `--delete-branch` flag on merge already deletes the remote
branch; the local branch and stale refs need explicit cleanup.

## CI Workflows

| Workflow | Triggers | What it runs |
|----------|----------|-------------|
| `lint.yml` | Push to master, PRs | clang-format, cppcheck |
| `test.yml` | Push to master, PRs | Matrix: Debug + ASan builds, ctest |
| `docs.yml` | Push to master, PRs | markdownlint |

## Session Close Protocol

Before ending any session, work is **not complete** until
`git push` succeeds. Follow AGENTS.md landing-the-plane workflow:

1. File issues for remaining work (`bd create`)
2. Run quality gates if code changed (`make check`)
3. Update issue status (`bd close`)
4. Push to remote (mandatory):

   ```bash
   git pull --ff-only origin master
   bd dolt push
   git push
   ```

5. Clean up stashes, prune remote branches
6. Verify all changes committed AND pushed
