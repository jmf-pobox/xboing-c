---
paths:
  - "scripts/**/*.sh"
  - "scripts/**"
---

# Shell Script Hygiene

## Required Header

```bash
#!/usr/bin/env bash
set -euo pipefail
```

## Error Handling

- `die()` function for fatal errors: `die() { echo "ERROR: $*" >&2; exit 1; }`
- `require()` to check tool availability: `require() { command -v "$1" >/dev/null 2>&1 || die "missing '$1'"; }`
- Cleanup via `trap cleanup EXIT`

## Variables

- Quote all expansions: `"$var"` not `$var`
- Use `${var:-default}` for optional variables
- Use `${1:?Usage: ...}` for required positional args

## Style

- `shellcheck` clean (run manually; not yet in `make check`)
- No bashisms when POSIX sh suffices
- `[[ ]]` over `[ ]` when bash features are needed
- `local` for function-scoped variables
- Prefer `printf` over `echo` for portability
