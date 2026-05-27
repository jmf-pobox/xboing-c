---
paths:
  - "**/*.md"
  - "docs/**"
---

# Markdown Hygiene

## Lint

- All `.md` files must pass `markdownlint` (part of `make check`).
- Blank lines around lists, fences, and headings.
- Fenced code blocks must have a language specifier (`c`, `bash`, `text`, `yaml`).
- Ordered lists use `1.` prefix (markdownlint MD029).
- Line length: no hard limit, but prefer wrapping prose at ~72 chars for readability.

## Structure

- One `# Title` per file (H1). Sections use `##`, subsections `###`.
- Tables: use pipes, align header separators.
- No HTML unless markdown can't express it.
- No trailing whitespace.

## Docs Directory

| Path | Content |
|------|---------|
| `docs/BUILDING.md` | Build, toolchain, quality gates |
| `docs/TESTING.md` | 5-layer testing pyramid, visual fidelity |
| `docs/GIT.md` | Branching, PRs, review, merge, cleanup |
| `docs/WORKFLOW.md` | Development lifecycle, phases, DoD gates |
| `docs/DESIGN.md` | Architectural Decision Records (ADRs) |
| `docs/SPECIFICATION.md` | Technical spec of all 16 subsystems |
| `docs/MODERNIZATION.md` | SDL2 modernization from-to changes |
| `docs/specs/` | Per-feature design specs |
| `docs/reviews/` | Peer review reports |
| `docs/research/` | Original-source research |

## Specs, Reviews, Research

- Filename: `YYYY-MM-DD-<topic>.md`
- Specs cross-link to their review and input research.
- Reviews include verdict, findings (blocking/non-blocking), and resolution section.
- Research cites `original/<file>.c:<line>` verbatim.
