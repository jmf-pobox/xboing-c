# C Modernization

Incremental modernization of legacy C codebases. Preserves behavior,
upgrades safety, and replaces 30-year-old idioms with patterns that
hold up under modern compilers and sanitizers.

## Standards Targeted

- **C89/C90** — what most legacy game code was written in
- **C11** — minimum modern target. `_Static_assert`, `_Alignof`,
  anonymous structs/unions, `_Generic`
- **C17** — defect fixes only
- **C23** — selectively. `typeof`, `nullptr`, `constexpr`, `#embed`
  where they add clarity

## Compiler Discipline

Base warning set, treated as errors:

```text
-Wall -Wextra -Wpedantic -Werror
-Wconversion -Wshadow -Wdouble-promotion
-Wformat=2 -Wformat-overflow=2
-Wnull-dereference -Wuninitialized
-Wstrict-prototypes -Wold-style-definition
```

Per-file warning suppressions for files not yet modernized — never
weaken the global policy. Each suppression is technical debt with a
ticket attached.

## Sanitizer Stack

| Sanitizer | Catches | Flag |
|-----------|---------|------|
| ASan | Buffer overflows, UAF, double-free, stack overflow | `-fsanitize=address` |
| UBSan | Integer overflow, null deref, alignment, shifts | `-fsanitize=undefined` |
| MSan (clang only) | Reads of uninitialized memory | `-fsanitize=memory` |
| Valgrind memcheck | Memory errors without recompile | `valgrind --tool=memcheck` |

ASan + UBSan in CI on every PR. MSan and Valgrind for periodic deep
checks. **Sanitizers are not optional for a 20-year-old codebase.**

## Legacy Pattern Catalog

| Legacy | Modern |
|--------|--------|
| K&R function definitions | ANSI prototypes |
| `#if NeedFunctionPrototypes` | Always use prototypes; remove the macro |
| `char *` for byte buffers | `unsigned char *` or `uint8_t *` |
| `sprintf` | `snprintf` (always size-bounded) |
| `strcpy` / `strcat` | `strncpy`/`strncat` or `snprintf` |
| `atoi` / `atol` | `strtol` with explicit error checking |
| Magic numbers | Named constants (`#define` or `enum`) |
| Global mutable state | Struct passed by pointer |
| Implicit function declarations | Proper `#include` and prototypes |
| `gets` | Always `fgets` with bound |

## Refactor Discipline

- **Test before changing.** Characterization test goes in *first* —
  records current behavior, then refactor proves equivalence.
- **Separate concerns in commits.** Modernization commit ≠ bug fix
  commit ≠ feature commit. Reviewers see one thing at a time.
- **Format-only commits stand alone.** Do not mix `clang-format` runs
  with logic changes.
- **One module at a time.** Replace, prove equivalence, move on. Big-
  bang rewrites lose history and reviewers can't follow.

## Resource and Bounds Discipline

Every `malloc` has a matching `free`. Every `fopen` has `fclose`.
Every X11/SDL allocation has its destructor. Use cleanup attributes
or goto-cleanup pattern at function scope to centralize teardown.

Bounds: every array access has a verifiable bound. Index variables
that come from external sources (file parse, network, user input)
get range-checked at the boundary.

## Static Analysis

- **clang-tidy** — semantic checks; auto-fix many cases
- **cppcheck** — syntactic checks; catches different things than
  clang-tidy (worth running both)
- **clang-format** — `.clang-format` at repo root, one canonical style

## Reference

- `CLAUDE.md` — project warnings policy and quality gates
- `docs/SPECIFICATION.md` — what the code does today
