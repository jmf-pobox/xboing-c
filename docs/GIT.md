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

### Process (pseudocode)

```text
function create_pr(branch, title, body):
    make_check()                       # quality gates before push
    git push -u origin {branch}
    pr_num = gh pr create --title --body
    schedule_loop("*/2 * * * *", poll_and_check(pr_num))
    return pr_num


# Outer loop — fires every 2 minutes via /loop cron
function poll_and_check(pr_num):
    state = gh pr view {pr_num} \
        --json reviews,comments,statusCheckRollup

    # Inner loop runs here, synchronously, before any merge check
    for each new_finding in state.unaddressed_comments:
        address_immediately(new_finding, pr_num)

    if merge_gate_satisfied(state):
        gh pr merge {pr_num} --squash --delete-branch
        post_merge_cleanup()
        cancel_loop()


# Inner loop — runs whenever feedback arrives, not deferred to tick
function address_immediately(finding, pr_num):
    if finding.is_valid:
        edit_code_to_fix(finding)
        make_check()                   # must pass locally
        git commit -m "fix: address <reviewer> finding"
        git push                       # triggers fresh CI + auto re-review
    else:
        counter_argue(finding, pr_num) # comment back explaining why

    resolve_review_thread(finding.thread_id)


# Merge gate checklist — every item must be true
function merge_gate_satisfied(state):
    latest_sha = state.head_sha

    # 1. At least one round of substantive feedback received and
    #    addressed
    if no_substantive_review_yet(state, latest_sha):
        return false

    # 2. EVERY reviewer responded on latest_sha
    for reviewer in [copilot, cursor, every_requested_human]:
        if not has_review_on(reviewer, latest_sha):
            return false
    # Bugbot exception: skip only if 3 full 2-min loops elapsed
    if bugbot_missing(latest_sha) and bugbot_wait < 6_minutes:
        return false

    # 3. CI green on latest_sha
    if any(c.conclusion != "SUCCESS" for c in state.checks_on(latest_sha)):
        return false

    # 4. All review threads resolved
    if any(t.isResolved == false for t in state.threads):
        return false

    # 5. Re-requested Copilot after each push (auto for Copilot)

    # 6. Most recent pass from each reviewer has no actionable findings
    for reviewer in state.reviewers:
        if reviewer.latest_review.has_actionable_findings:
            return false

    return true


function post_merge_cleanup():
    git checkout master
    git pull --ff-only origin master
    git branch -d {branch}
    git fetch --prune origin
```

Key invariants:

1. The inner loop never defers. Feedback arrives → read the moment
   it appears → address before the next merge-gate check.
2. The outer loop is the only path to merge. No "convergence is
   approval" shortcut.
3. Every push restarts the gate — new CI run, fresh reviews
   required on the new SHA.
4. Bugbot is the only reviewer with a timeout exception (6 min =
   3 loops).
5. A "summary only / no comments" review counts as a clean pass
   on that reviewer for that SHA — but only if it is on the
   **latest** SHA.

### Monitoring (mandatory, immediate)

**Arm active monitoring in the same turn as PR creation — no
exceptions for "trivial" or "docs-only" PRs.**

1. Set up `/loop` with a 2-minute interval polling the PR. The
   `/loop` invocation MUST be created in the same turn as the PR.
   Polling command:

   ```bash
   gh pr view <number> --json reviews,comments,reviewDecision,state,mergeable,statusCheckRollup
   ```

   The loop must continue until the PR merges. If you push new
   commits, the same loop is fine — it polls the latest state.
   Don't create a second loop.

2. Request Copilot review immediately:
   `mcp__plugin_github_github__request_copilot_review`.

3. **A normal review cycle is 2–6 rounds of feedback, not zero.**
   Expect findings; address them; push fixes; re-request review;
   resolve threads. Skipping rounds means rushing the PR.

### Two-Loop Process

PR workflow is two concurrent loops, not a sequence:

**Inner loop (review cycle):** When feedback arrives, act
immediately — do NOT wait for the next outer-loop tick. Each finding
triggers an inner iteration:

1. Read the finding the moment it arrives.
2. Address it in code (or counter-argue if it's wrong).
3. Run `make check`.
4. Commit and `git push` (not force, not rebase).
5. Resolve the conversation thread via `gh api graphql`
   `resolveReviewThread` mutation. This is the one workflow
   operation without a structured MCP tool — it is the documented
   exception to the "prefer MCP over `gh api graphql`" rule.
6. Re-request Copilot review on the new SHA.

**Outer loop (merge gate):** The `/loop` cron polls every 2 minutes
checking the merge gate checklist below. It only fires `gh pr merge`
when every checklist item is satisfied. The outer loop never bypasses
the inner loop — if feedback arrives between outer-loop ticks, you
handle it then and there, not on the next tick.

**Re-request Copilot review every time you push a new commit.**
Copilot does not auto-re-review on push. Previous reviews refer to
the old SHA and don't carry forward.

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

### Merge Gate Checklist

**Never merge until every item below is checked.** No exceptions for
"trivial" or "docs-only" PRs.

1. **[ ] Received and addressed at least one round of feedback.**
   A normal review cycle is 2–6 rounds. Zero rounds means you
   rushed. You must have a substantive review (not just a summary)
   from at least one reviewer, with findings either addressed or
   confirmed as non-issues.

2. **[ ] Waited for a response from EVERY reviewer.** The reviewers
   are Copilot, Cursor, any human reviewer, and bugbot. Wait for
   each one across each push.
   - **Bugbot exception**: if bugbot has not responded after 3 full
     2-minute loop iterations (6 minutes total) with all other
     reviewers in and CI green, you may treat bugbot as a clean
     pass. This exception applies to bugbot only.
   - **No exception for Copilot, Cursor, or humans.** A review
     summary with zero comments still counts as a response, but
     only after the reviewer's full review (not just an
     auto-generated overview) is posted.

3. **[ ] All CI checks are green on the latest commit.** If you
   pushed fixes, wait for the new CI run to complete. Old CI
   results don't count.

4. **[ ] Every conversation thread is resolved.** Run the
   `resolveReviewThread` mutation for each thread you addressed.
   Unresolved threads block merge.

5. **[ ] Re-requested Copilot review after each push.** Copilot
   does not auto-re-review. Previous reviews refer to old SHAs.

6. **[ ] Most recent pass from each reviewer produced no actionable
   findings.** "No high-confidence vulnerabilities" from Cursor,
   empty review from Copilot, or "looks good" from a human all
   qualify. Open findings block merge.

When every box is checked: merge with `gh pr merge <N> --squash
--delete-branch`. Then do post-merge cleanup per the section below.

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
