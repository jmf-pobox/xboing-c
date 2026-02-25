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

## ADR-002: XDG Base Directory path resolution

**Status:** Accepted

**Context:**
The game resolves file paths via compile-time `#define`s (`HIGH_SCORE_FILE`,
`LEVEL_INSTALL_DIR`, `SOUNDS_DIR`) with optional `getenv()` overrides
(`XBOING_SCORE_FILE`, `XBOING_LEVELS_DIR`, `XBOING_SOUND_DIR`). This pattern is
duplicated across 12+ callsites in 7 files. Save and score files go to `$HOME`
as dotfiles (`.xboing-scores`, `.xboing.scr`).

MODERNIZATION.md calls for XDG Base Directory compliance at runtime. The XDG
spec defines standard locations for user data (`$XDG_DATA_HOME`, defaulting to
`$HOME/.local/share`) and system data (`$XDG_DATA_DIRS`, defaulting to
`/usr/local/share:/usr/share`).

**Decision:**
Introduce a `paths` module (`src/paths.c`, `include/paths.h`) that centralizes
all file path resolution. The module is pure C with no X11 or SDL2 dependency.

Resolution priority for read-only assets (levels, sounds):

1. Legacy env var (`XBOING_LEVELS_DIR` / `XBOING_SOUND_DIR`) — backward compat
2. `XDG_DATA_DIRS` search — each dir + `/xboing/levels/<file>`, first match
3. `XDG_DATA_HOME` — `$HOME/.local/share/xboing/levels/<file>`
4. CWD fallback — `./levels/<file>` (development mode)

Resolution priority for writable state (scores, saves):

1. Legacy env var (`XBOING_SCORE_FILE` — global scores only)
2. Existing legacy file on disk — `$HOME/.xboing-scores` etc. (migration compat)
3. XDG default — `$XDG_DATA_HOME/xboing/scores.dat` etc. (fresh installs)

Design constraints:

- Caller-provided buffers (no malloc, no static buffers).
- `paths_init_explicit()` accepts injected env values for deterministic testing.
- The module is introduced and tested in isolation. Wiring into legacy callsites
  is a separate follow-up to keep commits focused and reviewable.

**Consequences:**

- All path logic lives in one place instead of 12+ scattered callsites.
- Legacy env vars continue to work — no user-visible behavior change.
- XDG-compliant locations are available for packaged installs.
- `paths_init_explicit()` makes the module fully testable without `setenv()`.
- Wiring requires touching 7 legacy files; deferring that to a separate PR
  limits blast radius and keeps this change reviewable.

## ADR-003: FHS-compliant CMake install targets

**Status:** Accepted

**Context:**
The CMake build had zero `install()` rules. The binary and assets only worked when
run from the repository root because all path defines were relative (`./levels`,
`./sounds`). This made it impossible for downstream packagers to install the game
to standard system locations.

The legacy code already has a two-tier path resolution mechanism: environment
variable first (`XBOING_LEVELS_DIR`, etc.), then compile-time define
(`LEVEL_INSTALL_DIR`, etc.). This means we can switch between development and
installed paths purely through compile definitions — no C source changes needed.

**Decision:**
Add a CMake option `XBOING_USE_INSTALLED_PATHS` (default `OFF`) that controls
whether compile definitions use relative paths (development) or absolute FHS paths
(installed). Add `install()` rules for the binary, levels, sounds, docs, and man
page regardless of the option setting.

Two modes:

- **Default** (`OFF`): relative paths (`./levels`, `./sounds`). The existing
  development workflow is unchanged — `./xboing` runs from the repo root.
- **Packager** (`ON`): absolute paths via `GNUInstallDirs`
  (`${CMAKE_INSTALL_FULL_DATADIR}/xboing/levels`, etc.). The installed binary
  finds assets at their FHS locations.

FHS install layout:

```text
${CMAKE_INSTALL_BINDIR}/xboing
${CMAKE_INSTALL_DATADIR}/xboing/levels/*.data
${CMAKE_INSTALL_DATADIR}/xboing/sounds/*.au
${CMAKE_INSTALL_DATADIR}/xboing/docs/problems.doc
${CMAKE_INSTALL_MANDIR}/man6/xboing.6
```

A CMake preset (`install`) provides the packager configuration with
`CMAKE_INSTALL_PREFIX=/usr` and `XBOING_USE_INSTALLED_PATHS=ON`.

What is explicitly out of scope:

- `paths.c` XDG integration (ADR-002, separate wiring task)
- `.desktop` file, AppStream metadata, icons
- CPack packaging configuration
- `HIGH_SCORE_FILE` relocation (deferred to `paths.c` wiring)

**Consequences:**

- Packagers can build and install with `cmake --preset install && cmake --install`.
- Development workflow is completely unchanged (default OFF).
- No C source files are modified — the existing compile-define mechanism is reused.
- `FILES_MATCHING PATTERN` in install rules excludes stale `CVS/` directories.
- The `install` preset disables tests (`BUILD_TESTING=OFF`) since installed
  builds do not need the test harness.

## ADR-004: SDL2 window and renderer architecture

**Status:** Accepted

**Context:**
The original XBoing creates 10 X11 sub-windows via `CreateAllWindows()` in
`stage.c` (main, play, score, level, message, special, time, input, block,
type). The main window is 575x720 pixels (`PLAY_WIDTH + MAIN_WIDTH + 10` by
`PLAY_HEIGHT + MAIN_HEIGHT + 10`, per `stage.c:240-242`). All rendering goes
through Xlib calls (`XCopyArea`, `XFillRectangle`) targeting these sub-windows.

SDL2 uses a fundamentally different model: one window, one GPU-accelerated
renderer, with all drawing expressed as texture copies and draw primitives.
There is no concept of sub-windows. The multi-window Xlib layout must be
replaced with logical regions drawn into a single render target.

MODERNIZATION.md calls for SDL2 rendering with integer scaling and
fullscreen support. The game targets pixel-art aesthetics (XPM bitmaps),
so nearest-neighbor scaling is essential.

**Decision:**
Create an `sdl2_renderer` module (`src/sdl2_renderer.c`, `include/sdl2_renderer.h`)
that manages a single SDL2 window and renderer. Key design points:

1. **Fixed logical resolution (575x720).** The game always renders at the
   original XBoing resolution. `SDL_RenderSetLogicalSize()` handles
   letterboxing and scaling to the physical window.

2. **2x default scale, resizable window.** The physical window opens at
   1150x1440 (2x logical) with `SDL_WINDOW_RESIZABLE`. Users can resize
   freely; SDL2 letterboxes automatically.

3. **Fullscreen-desktop mode.** `SDL_WINDOW_FULLSCREEN_DESKTOP` (no mode
   switch) for instant toggle. The logical size is preserved.

4. **Opaque context struct.** `sdl2_renderer_t` is heap-allocated and
   opaque. No global state. All functions take a context pointer. This
   makes the module testable and supports future multi-window scenarios
   (e.g., detached level editor).

5. **Static library boundary.** Built as `libsdl2_renderer.a`, conditional
   on `SDL2_FOUND`. The legacy `xboing` target is completely untouched —
   SDL2 stays optional. Test targets link against the static library.

6. **Nearest-neighbor scaling.** `SDL_HINT_RENDER_SCALE_QUALITY` set to
   `"nearest"` to preserve pixel-art sharpness at all scale factors.

7. **Software renderer fallback.** If `SDL_RENDERER_ACCELERATED` fails
   (e.g., CI dummy driver), falls back to `SDL_RENDERER_SOFTWARE`.

What this module does NOT do (deferred to later beads):

- Logical region definitions (xboing-oaa.6 — layout module)
- Texture loading (xboing-oaa.2 — asset pipeline)
- Font rendering (xboing-oaa.3 — SDL2_ttf integration)
- Color system (xboing-oaa.4 — colormap replacement)
- Event loop (the module creates the window, not the event pump)

**Consequences:**

- All SDL2 window/renderer setup lives in one module with a clean API.
- The 575x720 logical resolution preserves exact layout compatibility with
  the original X11 sub-window geometry.
- Tests run under `SDL_VIDEODRIVER=dummy` — no display required in CI.
- The opaque context pattern avoids the global state that made the original
  X11 code hard to test.
- Future layout work can define logical regions that map 1:1 to the original
  sub-window positions, rendering into the single SDL2 target.
