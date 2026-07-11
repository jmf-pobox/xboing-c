# Delegation and Mission Workflow

*No `paths:` frontmatter on purpose — this rule loads at session start, always in context: the mission mandate holds for ALL work, not just named paths.*

**Delegation is a mission, not a bare agent spawn.** Every gated unit of
work — implement, test, design, review, investigate — runs as an `ethos`
mission contract. A raw `Agent`/`Task` spawn does NOT satisfy a
Definition-of-Done gate and does NOT count as delegation. The leader
(`claude`, COO) writes the contract and the **worker agent** executes it.
"0 missions" for delegated work is a process failure, not a shortcut.

**Leader ≠ worker for code.** For `implement`/`test` the worker MUST be a
domain specialist (`jdc`/`gjm`/`sjl`), never the leader — the leader does
not write production code solo. The leader MAY be the worker ONLY for
`design`/`report` and doc-only/governance missions (e.g. this process doc).
The store enforces `worker ≠ evaluator`; `leader ≠ worker` for code is a
rule you uphold, not a store refusal — so do not rationalize around it.

## Mission Protocol (mandatory — no exceptions)

Use the `mcp__plugin_ethos_self__mission` tool (or `ethos mission`).

1. `method=create` with the contract YAML. Ethos assigns an ID, pins the
   evaluator's content hash, and opens an append-only event log.
2. Spawn the **worker** agent (table below) to execute the write-set. The
   PreToolUse hook blocks edits outside the write-set at runtime.
3. Worker submits a typed handoff: `method=result`.
4. Spawn the **evaluator** (≠ worker, ≠ leader). On findings, `method=reflect`
   then `method=advance` a round; worker fixes; re-evaluate.
5. `method=close` only after a valid result exists AND the evaluator passes.
   The store refuses self-review, close-without-result, advance-without-
   reflection, and overlapping write-sets — you cannot silently skip a step.

Close appends a trace line to `.punt-labs/ethos/missions.jsonl` (git-tracked)
and the commit-msg hook stamps `Mission:` / `Delegation:` trailers, so
`git blame → commit → trailer → contract → prompt → audit trail` reconstructs
who authorized each line and why. A bare `Agent` spawn produces none of this.

```yaml
leader: claude
worker: jdc                 # who does the work (NOT claude)
evaluator: { handle: gjm }  # who reviews (NOT worker, NOT leader)
type: implement
write_set: [src/foo.c, include/foo.h]
success_criteria: [make check passes, invariant X holds]
budget: { rounds: 3, reflection_after_each: true }
```

## Expert Agents

| Agent | Persona | Expertise | Consult when... |
|-------|---------|-----------|-----------------|
| `jck` | Justin C. Kibell | Original author. Game vision, feel, intent. | Gameplay, physics, scoring, level design, constants. **Must approve** gameplay-affecting changes. |
| `jdc` | John D. Carmack | Modern C, sanitizers. | C changes, warnings, sanitizer findings. Primary C implementer. |
| `sjl` | Sam J. Lantinga | SDL2 author. | Rendering/audio port, SDL2 abstraction, asset conversion. |
| `gjm` | Glenford J. Myers | *Art of Software Testing*. | Tests, harness design, extracting pure functions. |

`jck` is read-only (Read/Grep/Glob/WebFetch). Never `implement`/`test`.

## Archetypes

`implement` (3 rounds, any path) · `design` (2, `*.md`/`docs/**`, needs
`context`) · `test` (2, `tests/**`) · `review` (1, `*.md`/`.tmp/**`, needs
`inputs.files`) · `report` (1, empty OK) · `investigate` (1, empty OK).

## Pipelines

`quick` = implement→review · `standard` = design→implement→test→review→
document · `formal` = spec→design→implement→test→coverage→review→document
· `coe` = investigate→root-cause→fix→test→document · `coverage` =
measure→test→verify.

## Worker/Evaluator Pairing

Worker ≠ evaluator (DES-033): `jdc`→`gjm`, `sjl`→`jdc`, `gjm`→`jdc`,
`jck`→`jmf-pobox`.

Persist: specs→`docs/specs/`, reviews→`docs/reviews/`, contracts→
`.tmp/missions/`. Spawn workers `run_in_background: true` unless blocked.
