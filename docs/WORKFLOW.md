# Development Workflow

Every code change follows this pipeline. Steps are ordered — do not
skip ahead.

## Phase 1: Claim

1. Check `bd ready` or `bd list --status=open` for an existing bead.
   Create one with `bd create` if none exists.
2. `bd update <id> --status=in_progress` to claim it.
3. If biff is enabled: `/plan` with a 1-line summary of the work.
4. If `/who` shows more than 1 user active in this repo (human or
   agent), use a worktree to avoid conflicts.

## Phase 2: Branch

Create a feature branch from master. See `docs/GIT.md` for branch
prefixes. **master has branch protection — never commit directly.**

```bash
git checkout -b <prefix>/short-description master
```

## Phase 2.5: Design (for non-trivial changes)

Any change that introduces a new public API, touches the main loop,
restructures how modules interact, or adds a new subsystem must go
through a structured design exercise before implementation:

1. Read the original source to understand the problem space.
2. Open a `design` **ethos mission** (`.claude/rules/delegation.md`):
   the worker (e.g. `jdc`) produces the blueprint; the evaluator (≠ worker)
   reviews it. Design is reviewed *before* code starts. Do not solo-design.
3. Incorporate the evaluator's findings via `reflect`/`advance`; close the
   mission with a result artifact.
4. Write an ADR in `docs/DESIGN.md` for key decisions.
5. Get user approval on the final plan, then proceed to Phase 3.

Do not skip this for "simple" changes that turn out to involve
architectural decisions. If you find yourself reverting code, you
skipped the design phase.

## Phase 3: Implement (TDD when feasible)

**Implementation is delegated, not solo.** Open an `implement` (and/or
`test`) ethos mission and spawn the domain worker — `jdc` for C, `gjm` for
tests, `sjl` for SDL2/AV (`.claude/rules/delegation.md`). The leader
(`claude`) writes the contract and reviews; the leader does not write the
production code directly. "I did it faster myself" is the failure mode this
prevents — it severs the contract→commit→audit-trail chain ethos exists to
keep. See `.claude/rules/delegation.md`, Mission Protocol.

Within the mission:

1. Write failing tests first when feasible.
2. Write the code that makes the tests pass.
3. Run quality gates after each logical change.
4. Worker submits a `result`; evaluator (≠ worker) reviews; close the mission.

## Phase 4: Definition of Done

**The goal is working code, not a merged PR.** Every change must pass
ALL gates in order. Do not open a PR until gate 7 passes.

### Gate 1: Code complete

The change solves the actual problem, not a subset. Partial fixes
with visible artifacts are work in progress, not code-complete.

### Gate 2: Quality gates pass

`make check` must pass: format-check, cppcheck, markdownlint, ctest
debug, ctest asan, dpkg-buildpackage, lintian. Zero warnings, zero
errors.

### Gate 3: Local code review

Gate 3 is a `review` **ethos mission**, not a bare agent spawn. Open the
mission with an evaluator who is neither the worker nor the leader (`jdc`
reviews C, `gjm` reviews tests, `jck` approves gameplay-affecting changes —
`.claude/rules/delegation.md`). Address every valid finding via
`reflect`/`advance`; close with a result. A chat-only `code-reviewer` spawn
with no mission does **not** satisfy this gate — it leaves no contract,
no audit trail, and no `Mission:` commit trailer.

### Gate 4: Game run + visual verification

Build and launch the game. Navigate to the affected screen. Capture
a screenshot. Compare against `tests/golden/original/` or the
running `original/xboing` binary. See `docs/TESTING.md` Layer 4.

### Gate 5: LLM judge (visual changes)

Run `make visual-check` or targeted `llm_compare.py`. Fix any new
major findings. Skip for non-visual changes.

### Gate 6: User verification (visual changes)

Open screenshots in eog (one at a time). User confirms. Skip when
user has said to proceed autonomously.

### Gate 7: Documentation

- ADR in `docs/DESIGN.md` if design decisions were made
- Update `README.md` if user-facing behavior changed
- Update `resume.md` if project state materially advanced

### Gate 8: PR

Only now. See `docs/GIT.md` for the full PR workflow: create,
monitor, review rounds, merge.

## Workflow Tiers

Match the workflow to the scope. The deciding factor is **design
ambiguity**, not size.

| Tier | When | Tracking |
|------|------|----------|
| **T1: Forge** | Epics, cross-cutting, competing approaches | Pipeline (`formal`/`full`) + beads |
| **T2: Feature Dev** | Features, multi-file, clear goal | Pipeline (`standard`) + beads |
| **T3: Direct** | Tasks, bugs, obvious path | `quick` pipeline (implement→review) + beads |

**Every tier is tracked by ethos missions — there is no mission-free tier.**
The smallest bug fix (T3) is at minimum an `implement` mission plus a `review`
mission (or the `quick` pipeline), each with a worker and a distinct
evaluator, created and closed. Reporting "0 missions" for delegated work is a
process failure, not efficiency. If you are doing the coding yourself with no
open mission, stop and open one (`.claude/rules/delegation.md`).

**Escalation only goes up.** If T3 reveals unexpected scope,
escalate to T2. If T2 reveals competing designs, escalate to T1.
