# tools/ — Agent Instructions

Project-linked C utilities that ship **outside** the game binary.

## Why a separate directory

These programs don't fit anywhere else:

- Not `src/` — that's translation units linked into `xboing` itself.
- Not `tests/` — they produce inputs that tests/capture pipelines consume; they are not tests.
- Not `scripts/` — those are shell + Python. C is the right language here
  because these tools link against project libraries (e.g. `savegame_io`) to
  round-trip the exact same schema the game uses. A reimplementation in
  another language would drift.

## Current contents

- `gen_bonus_fixtures.c` — writes savegame v2 JSON fixtures to
  `tests/fixtures/bonus/scenario-N/` for the bonus-screen visual capture
  pipeline. Built via `add_executable(gen_bonus_fixtures …)` in
  `CMakeLists.txt`. Driven by `make bonus-fixtures`; consumed by
  `make visual-check`, `make modern-bonus`, `make modern-bonus-all`.

## Adding a new tool

1. Drop a `.c` (and optional `.h`) here.
2. Add `add_executable` + `target_link_libraries` to `CMakeLists.txt`
   alongside the existing `gen_bonus_fixtures` block.
3. Apply `${XBOING_STRICT_WARNINGS} -Werror` like the rest of the codebase.
4. Wrap invocation in a Makefile target — never call the binary directly
   from a downstream script.
