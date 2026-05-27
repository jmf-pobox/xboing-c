---
paths:
  - "src/**/*.c"
  - "include/**/*.h"
---

# C Code Hygiene

## Naming

- `snake_case` for functions and variables
- `UPPER_SNAKE_CASE` for macros and constants
- Prefix public API functions with module name: `ball_system_update()`, `sfx_system_get_enabled()`
- Static (file-scope) functions: no prefix needed

## Includes

- System headers first, alphabetized
- Blank line
- Project headers second, alphabetized
- Include guards: `#ifndef MODULE_NAME_H` / `#define MODULE_NAME_H` (no `#pragma once`)

## Const Correctness

- Read-only pointer parameters: `const game_ctx_t *ctx`
- Getter functions: `const` on the context parameter
- Don't cast away const — if you need to, the API is wrong

## Constants

- No magic numbers. Define with a name that explains intent.
- Game constants from the original: cite `original/<file>.h:<line>`
- `#define` in the header for public constants, `static const` or `#define` in .c for private

## Opaque Context Pattern

- Public: `typedef struct module_name module_name_t;` in header
- Private: `struct module_name { ... };` in .c only
- Create/destroy lifecycle: `module_create()` returns pointer, `module_destroy()` frees
- No direct field access from outside the module

## Error Handling

- Check return values. NULL guards on all public API entry points.
- No silent failures — return an error code or log a warning.
- `calloc` over `malloc` (zero-initialized memory prevents UB from missed init).
- Avoid `atoi` for user input — use `strtol` with error checking. Exception: pre-validated input (e.g., dialogue NUMERIC mode guarantees digits-only).

## Functions

- One responsibility per function. If it does X and also Y, split it.
- Keep functions under 50 lines where practical.
- Pure functions (no side effects) are preferred and easier to test.

## Comments

- None by default.
- Only when WHY is non-obvious: hidden constraints, workarounds, surprising behavior.
- Never explain WHAT — well-named identifiers do that.
- Never reference the current task, PR, or caller — those rot.

## State

- No global mutable state in new code. Pass context structs.
- `static` for file-scope state only (module-internal persistence).
- Every mutable static must be resettable (for test isolation).
