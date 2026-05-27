---
paths:
  - "CLAUDE.md"
  - "**/CLAUDE.md"
  - ".claude/rules/**"
---

# CLAUDE.md Maintenance Rules

## Root CLAUDE.md

- **Must stay under 200 lines.** Anthropic's recommendation. Longer files
  degrade adherence across ALL instructions uniformly.
- Contains only: behavioral contract, operating principles, session start,
  stop-and-ask, modernization rules, and `@` imports.
- No build commands, no test commands, no workflow details, no code
  conventions. Those live in the `@`-imported docs or `.claude/rules/`.
- `@` imports expand at launch — they ARE in context. Plain text references
  ("see docs/X.md") require explicit tool reads and WILL be skipped.

## Adding New Content

Before adding a line to root CLAUDE.md, ask:

1. Does this apply to EVERY session regardless of what files are touched?
   → Root CLAUDE.md
2. Does this apply only when touching certain files?
   → `.claude/rules/<name>.md` with `paths:` frontmatter
3. Is this reference material for a specific directory?
   → Sub-directory `CLAUDE.md` (e.g., `original/CLAUDE.md`)
4. Is this a detailed procedure (build, test, git, workflow)?
   → `docs/<NAME>.md`, `@`-imported from root

If none of the above, it probably doesn't belong in any CLAUDE.md.

## Sub-Directory CLAUDE.md Files

- Load on demand when Claude reads a file in that directory.
- Do NOT survive compaction — they reload on next file read.
- Can use `@path` to import docs (e.g., `@docs/TESTING.md`).
- Keep short: quick reminders + `@` import for the full doc.

## .claude/rules/ Files

- `paths:` frontmatter scopes when the rule loads.
- Rules without `paths:` load at session start (same as root CLAUDE.md).
- Use for agent-specific behavioral instructions, not human documentation.
- Keep each rule file focused on one domain.

## Line Budget

| Component | Budget |
|-----------|--------|
| Root CLAUDE.md | < 200 lines |
| Each .claude/rules/ file | < 80 lines |
| Each sub-directory CLAUDE.md | < 40 lines |
| `@`-imported docs | No hard limit (human docs) |
