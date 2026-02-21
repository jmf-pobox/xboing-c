# Design Decisions

This file records architectural decisions for the XBoing modernization using
the lightweight [Nygard ADR format](https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions).

## ADR-001: Use CMake as the sole build system

**Status:** Accepted

**Context:**
MODERNIZATION.md listed "Meson (or CMake)" as the target build system. Both are
capable of managing the SDL2 migration. However, the project already has a mature
CMake setup: a 154-line root `CMakeLists.txt`, a 134-line `tests/CMakeLists.txt`,
`CMakePresets.json` for Debug and ASan configurations, and a CI workflow that
exercises both presets. No Meson infrastructure exists.

Introducing Meson would mean maintaining two build systems in parallel during the
transition, doubling the surface area for CI and contributor onboarding. The
project has a single maintainer and a single platform target (Linux); the
polyglot flexibility of Meson offers no practical advantage here.

**Decision:**
CMake is the build system for the entire modernization. We will not introduce
Meson. SDL2 dependency discovery, backend switching, and all future build
infrastructure will be implemented in CMake.

**Consequences:**

- Contributors only need to learn one build system.
- Existing CI, presets, and test infrastructure carry forward unchanged.
- If a future need arises that CMake cannot satisfy, this decision can be
  revisited — but the burden of proof is on the alternative.
