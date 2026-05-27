---
paths:
  - "Makefile"
  - "CMakeLists.txt"
  - "tests/CMakeLists.txt"
  - "cmake/**"
---

# Build System Hygiene

Read `docs/BUILDING.md` for the full build and toolchain guide.

## Makefile

- `make` is the source of truth. Never re-derive flags from CI YAML.
- Wrap every compound command in a target — the user granted blanket `make *` permission.
- Target names: lowercase, hyphenated (`format-check` not `formatCheck`).
- All quality gates accessible via `make check`.
- `.PHONY` for all non-file targets.
- Test targets use `ctest` not raw test binary invocation.

## CMakeLists.txt

- New test files: register via `xboing_add_integration_test(test_name)`.
- Additional source files: list as extra args after the test name.
- Environment: default is `SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy`.
- Override with `set_tests_properties` for special cases (offscreen, timeout).
- Library dependencies: add to `target_link_libraries` in the integration test function.

## Compiler Warnings

- Never weaken the global warning policy.
- Suppress per-file in CMakeLists.txt with `set_source_files_properties`.
- Track each suppression as a bead to resolve.
