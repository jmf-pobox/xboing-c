---
paths:
  - "docs/specs/**"
  - "docs/reviews/**"
  - "docs/research/**"
  - ".tmp/missions/**"
---

# Delegation and Mission Workflow

## Expert Agents

| Agent | Persona | Expertise | Consult when... |
|-------|---------|-----------|-----------------|
| `jck` | Justin C. Kibell | Original XBoing author. Game vision, feel, design intent. | Gameplay mechanics, physics, scoring, level design, constants, player experience. **Must approve** gameplay-affecting changes. |
| `jdc` | John D. Carmack | Modern C, sanitizers, frame-time discipline. | Modernizing legacy code, compiler warnings, sanitizer findings, unsafe patterns. Primary implementer for C work. |
| `sjl` | Sam J. Lantinga | SDL2 author. Xlib internals, audio pipeline. | Porting rendering/audio, SDL2 abstraction, asset conversion (XPM→PNG, .au→WAV). |
| `gjm` | Glenford J. Myers | *The Art of Software Testing* (1979). | Tests for legacy code, test harness design, extracting pure functions. |

`jck` is read-only (Read/Grep/Glob/WebFetch only). Never `implement` or `test` archetype.

## Mission Archetypes

| Archetype | When | Budget | Write-set |
|-----------|------|--------|-----------|
| `implement` | C code change | 3 rounds | Any path |
| `design` | Design doc | 2 rounds | `*.md`, `docs/**` |
| `test` | Add/improve tests | 2 rounds | `tests/**` |
| `review` | Code/spec review | 1 round | `*.md`, `.tmp/**` |
| `report` | Research/summarize | 1 round | empty OK |
| `investigate` | Root-cause | 1 round | empty OK |

## Pipelines

| Pipeline | Stages | Use when |
|----------|--------|----------|
| `quick` | implement → review | Single-bead bug fix |
| `standard` | design → implement → test → review → document | Default feature work |
| `full` | prfaq → spec → design → implement → test → coverage → review → document → retro | Cross-cutting modernization |
| `formal` | spec → design → implement → test → coverage → review → document | State machines, protocols |
| `coe` | investigate → root-cause → fix → test → document | Recurring bugs |
| `coverage` | measure → test → verify | Test gap closure |

## Worker/Evaluator Pairing

Worker ≠ evaluator (DES-033). Defaults:

- `jdc` → evaluated by `gjm`
- `sjl` → evaluated by `jdc`
- `gjm` → evaluated by `jdc`
- `jck` → evaluated by `jmf-pobox` (maintainer)

## Spec Review Process

Every spec is peer-reviewed before execution. The reviewer is NOT the worker.

Workflow: Research → Draft contract → Peer review → Revise → `ethos mission create` → Spawn worker → Evaluate → Close

Persist artifacts: specs to `docs/specs/`, reviews to `docs/reviews/`, research to `docs/research/`.

## Background-by-Default

Every subagent spawn uses `run_in_background: true` unless the COO's next action depends on the result and no other useful work exists.
