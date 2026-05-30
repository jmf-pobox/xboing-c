# CI Workflows — Agent Instructions

## Workflows

| File | Name | Triggers | Jobs |
|------|------|----------|------|
| `lint.yml` | Lint | Push to master, PRs | `clang-format` check, `cppcheck` on src/ and tests/ |
| `test.yml` | Build & Test | Push to master, PRs | Matrix: Debug build + ctest, ASan build + ctest |
| `docs.yml` | Docs | Push to master, PRs | `markdownlint` on all .md files |

## Local Parity

`make check` runs every CI gate locally. Always run it before pushing.
CI should never find something `make check` didn't.

## Rules

- Never weaken CI checks to make a PR pass. Fix the code.
- If a new tool or check is added to CI, add a corresponding `make` target.
- Matrix builds test both Debug and ASan presets — both must pass.
- Copilot review is requested per PR, not part of CI YAML. See `docs/GIT.md`.
- Cursor security review runs automatically on PRs (GitHub app, not workflow).
