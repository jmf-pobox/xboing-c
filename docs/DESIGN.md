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

## ADR-005: SDL2 texture loading and caching

**Status:** Accepted

**Context:**
XBoing loads 188 XPM sprites at compile time via `#include "bitmaps/balls/ball1.xpm"`,
creates X11 Pixmap+Mask pairs with `XpmCreatePixmapFromData()`, and draws them with
`RenderShape()` (XSetClipMask + XCopyArea). All 180 PNG equivalents already exist
at `assets/images/` (8-bit RGBA with alpha transparency, same dimensions as originals).

The SDL2 renderer (ADR-004) provides the `SDL_Renderer*` needed to create
`SDL_Texture` objects. This module handles the loading and caching of those
textures. Rendering them to screen is a separate concern (future bead).

**Decision:**
Create an `sdl2_texture` module (`src/sdl2_texture.c`, `include/sdl2_texture.h`)
that loads PNG images into SDL_Texture objects and caches them for O(1) lookup
by string key. Key design points:

1. **String keys** (e.g., `"balls/ball1"`). Derived from the file path relative
   to the base directory with the `.png` extension stripped. No enum with 180
   entries to maintain. A future sprite catalog module can map game concepts to
   keys.

2. **Eager loading.** All PNGs are loaded at init, matching the legacy pattern
   where all XPM bitmaps are compiled in and created at startup. 180 small files
   (~100KB total) make this trivial.

3. **FNV-1a hash map with open addressing.** 256 slots for ~180 entries gives
   ~70% load factor. The table is write-once at init (plus optional `load_file()`
   calls) and read-many per frame. Linear probing is cache-friendly and simple.

4. **Recursive `opendir()`/`readdir()` for directory walking.** Avoids `nftw()`
   which uses static state in its callback and complicates parameter passing.
   Recursive `opendir()` keeps the scan state on the call stack.

5. **`sdl2_texture_load_file()` public seam.** Enables testing individual
   texture loads without scanning a directory tree. Also supports late-loading
   of assets that are not in the base directory.

6. **Opaque context struct.** Same pattern as `sdl2_renderer_t`: heap-allocated,
   no global state, fully testable.

7. **Borrowed renderer pointer.** The cache stores the `SDL_Renderer*` but does
   not own it. The caller must ensure the renderer outlives the texture cache
   (natural in practice: renderer is created first, destroyed last).

8. **Partial load tolerance.** Individual file failures are logged but do not
   abort cache creation. Only structural failures (NULL renderer, IMG_Init
   failure, unreadable base directory) cause `create()` to return NULL.

What this module does NOT do (deferred to later beads):

- Sprite rendering (separate concern: draw commands, source rects, dest rects)
- Animation frame management (game modules own frame sequencing)
- Sprite sheet/atlas packing (premature optimization for 180 small textures)
- Color tinting/palette manipulation (xboing-oaa.4)
- `paths.c` integration (base_dir string is the seam for future wiring)

**Consequences:**

- All PNG loading and caching lives in one module with a clean API.
- Game modules will look up textures by key (`"balls/ball1"`) rather than
  indexing into global Pixmap arrays.
- Tests use the dummy video driver and real PNGs, verifying actual SDL2
  texture creation end-to-end.
- The `load_file()` seam enables targeted tests without full directory scans.
- The 256-slot hash map is adequate for the current 180 textures with room for
  growth. If the asset count doubles, the constant can be increased.

## ADR-006: SDL2 TTF font rendering

**Status:** Accepted

**Context:**
XBoing loads four Adobe Helvetica XLFD bitmap fonts at startup via
`XLoadQueryFont()` in `init.c:225-253`:

| Global | XLFD pattern | Size | Style |
| -------- | ------------- | ------ | ------- |
| `titleFont` | `-adobe-helvetica-bold-r-*-*-24-*` | 24pt | Bold |
| `textFont` | `-adobe-helvetica-medium-r-*-*-18-*` | 18pt | Regular |
| `dataFont` | `-adobe-helvetica-bold-r-*-*-14-*` | 14pt | Bold |
| `copyFont` | `-adobe-helvetica-medium-r-*-*-12-*` | 12pt | Regular |

Three text drawing functions in `misc.c:122-185` handle all 161 text rendering
call sites across 18 files: `DrawText`, `DrawShadowText`, and
`DrawShadowCentredText`. XLFD bitmap fonts (`xfonts-75dpi`, `xfonts-100dpi`)
are disappearing from modern Linux distributions — they are no longer installed
by default.

Liberation Sans (metrically equivalent Helvetica substitute, SIL Open Font
License) is already bundled at `assets/fonts/` with Regular and Bold variants.
SDL2_TTF is already discovered by `pkg_check_modules` in CMakeLists.txt.

**Decision:**
Create an `sdl2_font` module (`src/sdl2_font.c`, `include/sdl2_font.h`) that
loads Liberation Sans TTF fonts via SDL2_TTF and provides text drawing functions
matching the legacy API surface. Key design points:

1. **Enum-indexed font slots.** Four fonts are stored in a fixed-size array
   indexed by `sdl2_font_id_t` (TITLE, TEXT, DATA, COPY). A hash map or
   dynamic container would be over-engineering for exactly four entries.

2. **Transient texture per draw.** Each text draw call follows the pattern:
   `TTF_RenderUTF8_Blended()` → `SDL_CreateTextureFromSurface()` →
   `SDL_RenderCopy()` → destroy texture. This matches the legacy immediate-mode
   rendering where `XDrawString()` draws directly without caching. At ~20 text
   draws per frame, the overhead is negligible. Text caching can be added later
   if profiling shows it matters.

3. **Fixed 2px shadow offset.** `SDL2F_SHADOW_OFFSET = 2` matches the legacy
   `DrawShadowText` which draws at `(x+2, y+2)` with black, then `(x, y)` with
   the foreground color.

4. **`y` = top of text area.** Both legacy `DrawText` (which adds
   `font->ascent` internally before calling `XDrawString`) and
   `SDL_RenderCopy` use top-left origin. The caller provides the top of the
   text area in both systems, preserving coordinate semantics.

5. **`SDL_Color` for color parameters.** The legacy code uses `int` X11 pixel
   values. The SDL2 path uses `SDL_Color` (RGBA). Bridging legacy color names
   to `SDL_Color` values is the responsibility of the color system module
   (xboing-oaa.4), not this module.

6. **No `numChar` parameter.** The legacy `DrawText` accepts a `numChar`
   parameter, but nearly all 161 call sites pass `-1` (full string). Callers
   that need a substring can truncate before calling. This simplifies the API.

7. **All-or-nothing font loading.** If any of the four TTF fonts fails to open,
   `sdl2_font_create()` returns NULL. All four fonts are required for the game
   to render correctly. This is stricter than the legacy code (which falls back
   to `fixed` font), but appropriate for a controlled asset set where missing
   fonts indicate a packaging or installation error.

8. **Opaque context struct.** Same pattern as `sdl2_renderer_t` and
   `sdl2_texture_t`: heap-allocated, no global state, fully testable.

What this module does NOT do (deferred to later beads):

- Wiring into legacy 161 call sites (separate migration task)
- Color name resolution (`"red"` → `SDL_Color`) (xboing-oaa.4)
- Text caching (premature; profile first if needed)
- Rich text, word wrapping, or multi-line rendering (game uses single-line text)
- Changes to the legacy `xboing` target

**Consequences:**

- All TTF font management lives in one module with a clean API.
- The four-slot enum provides compile-time safety — no string lookups for fonts.
- Tests run under `SDL_VIDEODRIVER=dummy` with real TTF files, verifying actual
  SDL2_TTF rendering end-to-end.
- Liberation Sans metrics closely match the original Helvetica, minimizing
  layout adjustments when wiring into legacy call sites.
- The transient texture approach is simple and correct; caching is a future
  optimization if profiling justifies it.

## ADR-007: SDL2 RGBA color system

**Status:** Accepted

**Context:**
The legacy XBoing uses X11 PseudoColor/TrueColor colormaps to manage colors.
Eight named color globals (`red`, `tann`, `yellow`, `green`, `white`, `black`,
`blue`, `purple`) are initialized via `ColourNameToPixel()` in `init.c:180-191`,
which calls `XParseColor()` + `XAllocColor()` to convert X11 color names to
pixel values. Two gradient arrays (`reds[7]`, `greens[7]`) initialized from hex
shorthand (`#f00` through `#300`, `#0f0` through `#030`) in
`init.c:193-217` drive the border glow animation in `sfx.c`.

SDL2 uses direct RGBA color values (`SDL_Color` structs) rather than colormap
indices. The X11 color indirection (name → pixel index → actual RGB) is
unnecessary in SDL2's TrueColor-only world.

**Decision:**
Create an `sdl2_color` module (`src/sdl2_color.c`, `include/sdl2_color.h`) that
provides precomputed RGBA color data matching the legacy X11 color values.
Key design points:

1. **No opaque context.** Unlike the renderer, texture, and font modules,
   the color system has no mutable state and owns no SDL resources. All data
   is `static const` in the `.c` file, accessed through pure functions.

2. **Enum-indexed named colors.** `sdl2_color_id_t` enumerates the 8 legacy
   globals (RED, TAN, YELLOW, GREEN, WHITE, BLACK, BLUE, PURPLE). Lookup by
   enum is O(1) array indexing with bounds checking.

3. **X11 rgb.txt values.** All RGBA values are verified against
   `/usr/share/X11/rgb.txt` to ensure exact color matching. Notably, X11
   "green" is RGB(0, 255, 0), not CSS "green" RGB(0, 128, 0).

4. **Precomputed gradient arrays.** `sdl2_color_red_gradient(i)` and
   `sdl2_color_green_gradient(i)` return 7-step gradients matching the legacy
   hex values. The X11 3-digit hex `#RGB` expansion rule (each digit repeated:
   `#d00` → `#DD0000`) is applied at compile time, not runtime.

5. **String lookup function.** `sdl2_color_by_name()` supports both named
   colors ("red", "yellow") and 3-digit hex shorthand ("#f00", "#0b0"),
   case-insensitive. This is the migration seam for `ColourNameToPixel()`
   call sites that use string color names.

6. **Static library.** Built as `libsdl2_color.a`, conditional on `SDL2_FOUND`.
   No SDL2_image or SDL2_ttf dependency — only the base SDL2 header for
   `SDL_Color`.

What this module does NOT do:

- Full X11 color name database (only the 8 names the game uses)
- 6-digit hex parsing `#RRGGBB` (not used by legacy code)
- Runtime colormap allocation or palette manipulation
- Wiring into legacy call sites (separate migration task)

**Consequences:**

- Color values are compile-time constants with zero runtime overhead.
- The enum + function API provides type safety over the legacy `int` pixel
  values — callers cannot accidentally pass a random integer as a color.
- Tests verify exact RGBA values against X11 rgb.txt, gradient monotonicity,
  and cross-consistency between named colors and gradient arrays.
- The `by_name()` function provides a drop-in replacement path for legacy
  `ColourNameToPixel()` calls during migration.

## ADR-008: SDL2 cursor management

**Status:** Accepted

**Context:**
The legacy XBoing uses 5 cursor types managed via `ChangePointer()` in
`init.c:702-745`. Four use `XCreateFontCursor()` with X11 cursor font
shapes (`XC_watch`, `XC_plus`, `XC_hand2`, `XC_pirate`). The invisible
cursor uses `XCreatePixmapCursor()` with a blank 1x1 pixmap. The function
frees the previous cursor with `XFreeCursor()` before creating a new one
(create-on-switch pattern). Call sites: 1 in `main.c` (initial hide),
8 in `editor.c` (mode-dependent cursor changes).

**Decision:**
Create an `sdl2_cursor` module (`src/sdl2_cursor.c`, `include/sdl2_cursor.h`)
that pre-creates all cursor types and switches between them. Key design points:

1. **Opaque context with pre-created cursors (best-effort).** Unlike the
   legacy create-on-switch pattern, cursor handles are created upfront in
   `sdl2_cursor_create()` and cached for the context lifetime. Switching
   is O(1) `SDL_SetCursor()` with no allocation. Creation is best-effort:
   some video drivers (e.g. dummy for CI) don't support system cursors.
   Missing cursors are stored as NULL and `set()` gracefully skips
   `SDL_SetCursor` while still tracking the active cursor ID.

2. **System cursors.** SDL2 system cursors (`SDL_CreateSystemCursor`) map
   directly to platform-native cursors:
   - `SDL2CUR_WAIT` → `SDL_SYSTEM_CURSOR_WAIT`
   - `SDL2CUR_PLUS` → `SDL_SYSTEM_CURSOR_CROSSHAIR`
   - `SDL2CUR_POINT` → `SDL_SYSTEM_CURSOR_HAND`
   - `SDL2CUR_SKULL` → `SDL_SYSTEM_CURSOR_CROSSHAIR` (no pirate equivalent)
   - `SDL2CUR_NONE` → hidden via `SDL_ShowCursor(SDL_DISABLE)`

3. **Skull cursor substitute.** SDL2 has no pirate/skull system cursor.
   Crosshair is used as a stand-in because the skull cursor only appears
   in the level editor during block deletion — crosshair is the closest
   contextual match. A custom cursor image could replace this later.

4. **Visibility toggle for NONE.** Rather than creating a transparent
   cursor (which is platform-dependent), `SDL2CUR_NONE` hides the cursor
   via `SDL_ShowCursor(SDL_DISABLE)` and re-shows it when switching to any
   visible cursor type.

5. **Current cursor tracking.** The context tracks which cursor ID is
   active, queryable via `sdl2_cursor_current()`. This enables editor code
   to check the current state without maintaining a separate variable.

What this module does NOT do:

- Custom cursor images (could be added for skull via `SDL_CreateColorCursor`)
- Pointer grabbing (SDL2 handles this differently; separate concern)
- Wiring into legacy call sites (separate migration task)

**Consequences:**

- Cursor creation is all-or-nothing at context init, matching the pattern
  of other SDL2 modules.
- No per-switch allocation or deallocation eliminates the create/free churn
  of the legacy pattern.
- Tests verify all cursor types can be created and set in headless mode
  (`SDL_VIDEODRIVER=dummy`).

## ADR-009: SDL2 logical render regions

**Status:** Accepted

**Context:**
The legacy XBoing creates 10 X11 child windows inside `mainWindow` via
`CreateAllWindows()` in `stage.c:218-371`. Each child window acts as an
independent rendering surface with its own coordinate system: `playWindow`
(495x580 at offset 35,60), `scoreWindow` (224x42 at 35,10),
`levelWindow` (286x52 at 284,5), `messWindow` (247x30 at 35,655),
`specialWindow` (180x35 at 292,655), `timeWindow` (61x35 at 477,655),
plus editor-only `blockWindow` and `typeWindow`, and a centered modal
`inputWindow`. A `bufferWindow` provides double-buffering.

SDL2 uses a single renderer with `SDL_RenderSetClipRect()` for region-scoped
drawing, eliminating the need for child windows entirely.

**Decision:**
Create an `sdl2_regions` module (`src/sdl2_regions.c`,
`include/sdl2_regions.h`) that defines named `SDL_Rect` constants for all
game regions. Key design points:

1. **No opaque context.** Like the color module, regions are pure static
   data — `SDL_Rect` constants with no mutable state. All coordinates are
   computed from the legacy stage.c formulas and verified by tests.

2. **Nine named regions.** `sdl2_region_id_t` enumerates all legacy
   sub-windows: PLAY, SCORE, LEVEL, MESSAGE, SPECIAL, TIMER (gameplay),
   EDITOR, EDITOR_TYPE (editor mode), and DIALOGUE (modal input).
   The `bufferWindow` is omitted — SDL2's renderer handles double-buffering
   internally.

3. **Logical coordinates.** All regions use the 575x720 logical coordinate
   system matching `SDL2R_LOGICAL_WIDTH` x `SDL2R_LOGICAL_HEIGHT` from the
   renderer module. `SDL_RenderSetLogicalSize()` handles scaling to physical
   window size.

4. **Hit testing.** `sdl2_region_hit_test(x, y)` maps a logical pixel
   coordinate to its containing region, useful for mouse click dispatch.

5. **Static library.** Built as `libsdl2_regions.a`, conditional on
   `SDL2_FOUND`. Only depends on base SDL2 for `SDL_Rect` and
   `SDL_PointInRect`.

What this module does NOT do:

- `SDL_RenderSetClipRect` calls (callers apply clip rects as needed)
- Region resizing or dynamic layout (fixed logical coordinates)
- Wiring into legacy call sites (separate migration task)

**Consequences:**

- Region coordinates are verified against the legacy stage.c formulas by
  20 characterization tests, ensuring pixel-accurate layout matching.
- The enum + function API provides type safety and centralized coordinate
  management — no magic numbers scattered across rendering code.
- Hit testing enables clean mouse event routing without manual coordinate
  comparisons at each call site.

## ADR-010: SDL2_mixer audio system

**Status:** Accepted
**Date:** 2026-02-25
**Bead:** xboing-0lu.1

**Context:**

The legacy audio system uses a fork+pipe architecture that writes raw audio
data to `/dev/dsp` (OSS). This requires a long-lived child process, platform-
specific driver files (12 drivers for different Unix variants), and only
supports sequential playback of Sun Audio (.au) files. OSS is deprecated on
modern Linux; `/dev/dsp` no longer exists without compatibility layers.

**Decision:**

Replace the fork+pipe architecture with SDL2_mixer:

- **Opaque context pattern** — `sdl2_audio_t` allocated by `create()`, freed
  by `destroy()`. No globals, fully testable.
- **Eager WAV caching** — `create()` scans the sound directory, loads all
  `.wav` files via `Mix_LoadWAV()`, and stores them in an FNV-1a hash map
  keyed by basename (e.g., "boing.wav" -> key "boing"). Same hash map
  pattern as `sdl2_texture.c`.
- **WAV format** — SDL2_mixer natively loads WAV. All 46 sounds already
  have `.wav` versions alongside the legacy `.au` files.
- **Concurrent playback** — `Mix_PlayChannel(-1, chunk, 0)` auto-picks the
  first available channel. 16 channels allocated by default (legacy could
  only play one sound at a time).
- **Global volume** — `set_volume()` applies to all channels via
  `Mix_Volume(-1, vol)`. The legacy `playSoundFile()` accepted a per-call
  volume parameter but every driver implementation ignored it.
- **Best-effort loading** — individual WAV load failures are logged but do
  not abort initialization. Only structural failures (Mix_OpenAudio failure,
  unreadable directory) cause `create()` to return NULL.
- **SDL_AUDIODRIVER=dummy** — headless CI testing works without a real audio
  device. Mix_OpenAudio, Mix_LoadWAV, and Mix_PlayChannel all succeed
  (silently) under the dummy audio driver.

**What we are NOT doing:**

- Wiring into legacy 161 call sites (separate migration task)
- Per-call volume control (legacy always ignored it; bead xboing-0lu.2)
- Streaming audio or music playback (game only uses short sound effects)
- Converting .au files (WAV versions already exist)

**Consequences:**

- The ~220-line module replaces 12 platform-specific audio drivers and the
  fork+pipe child process architecture.
- Sound effects are cached in memory for O(1) lookup and instant playback
  with no file I/O per play call.
- Concurrent playback enables overlapping sound effects (e.g., ball bounce
  and block break simultaneously), improving the audio experience.
- 24 characterization tests verify caching, playback, volume control, and
  error handling under the dummy audio driver.

## ADR-011: SDL2 input action mapping

**Status:** Accepted
**Date:** 2026-02-25
**Bead:** xboing-cks.3

**Context:**

The legacy input system uses `XLookupString()` to convert X11 KeyPress events
into `KeySym` values, then routes them through nested switch/if chains in
`main.c` (`handleGameKeys`, `handleIntroKeys`, `handleMiscKeys`,
`handleSpeedKeys`, etc.). Key bindings are hardcoded as `XK_*` constants.
Mouse input is polled separately via `XQueryPointer()`. There is no way to
rebind keys without recompiling.

**Decision:**

Replace the XKeySym switch dispatch with a scancode-to-action mapping layer:

- **Action enum** — 31 game actions covering movement, gameplay, menu
  navigation, global controls, and 9 speed levels. Editor-specific actions
  are deferred to the editor migration bead.
- **Dual binding slots** — each action supports up to 2 scancodes
  (primary + secondary), matching legacy dual bindings (e.g., Left arrow
  OR J for paddle left).
- **Reverse lookup table** — a `SDL_Scancode -> action` array
  (`SDL_NUM_SCANCODES` entries) provides O(1) event dispatch. Rebuilt
  automatically when bindings change.
- **Level + edge trigger** — `pressed()` returns true while a key is held;
  `just_pressed()` returns true only for the first frame (filtered by
  `event->key.repeat`). Matches legacy behavior where movement uses
  held state and one-shot actions use key-down events.
- **Mouse tracking** — position and button state updated from
  `SDL_MOUSEMOTION` / `SDL_MOUSEBUTTONDOWN` / `SDL_MOUSEBUTTONUP` events.
- **Modifier queries** — `shift_held()` for the few legacy actions that
  distinguish case (e.g., h vs H for global vs personal scores).
- **Rebindable at runtime** — `bind()` / `reset_bindings()` API with
  validation. No config file serialization yet (deferred to config bead).

**Default bindings match legacy XBoing controls:**

| Action | Primary | Secondary |
| ------ | ------- | --------- |
| Left | Left arrow | J |
| Right | Right arrow | L |
| Shoot | K | - |
| Pause | P | - |
| Quit | Q | - |
| Speed 1-9 | 1-9 | - |

**Known limitation — scancode vs keysym:**

Legacy used `XK_plus` (Shift+=) for volume up and `XK_equal` for the debug
next-level command. Since both share `SDL_SCANCODE_EQUALS`, scancode-only
mapping cannot distinguish them. Volume up keeps the `=` key (more commonly
used); next-level was moved to backslash (`\`). If modifier-aware bindings
are needed in the future, the binding model can be extended to include a
required modifier mask.

**What we are NOT doing:**

- Wiring into legacy event loop (separate migration task)
- Editor-specific key bindings (editor migration bead)
- Config file serialization of bindings (config bead xboing-1fr.3)
- Gamepad/joystick support (not in original game)
- Modifier-aware bindings (scancode+Shift combos — deferred, see above)

**Consequences:**

- Game logic queries semantic actions instead of raw keycodes, decoupling
  input handling from platform-specific key representations.
- The rebinding API enables future key configuration without recompilation.
- Dual-binding key-up correctly tracks per-scancode state — releasing one
  bound key does not clear the action while the other is still held.
- 40 characterization tests verify default bindings, key press/release state
  tracking, dual-binding behavior, edge triggers, mouse input, modifier
  queries, rebinding, and error handling.

## ADR-012: Game state machine with function pointer dispatch

**Status:** Accepted
**Date:** 2026-02-25
**Bead:** xboing-cks.2

**Context:**

The legacy game loop dispatches to mode handlers via a `switch(mode)` in
`main.c:handleGameStates()` (16 cases). Mode transitions are scattered across
key handlers and subsystem callbacks, with `Reset*()` initialization functions
called manually before each transition. There are no exit hooks — cleanup is
implicit. The `oldMode` global exists only for dialogue save/restore.

**Decision:**

Replace the switch dispatch with a function pointer table:

- **Opaque context pattern** — `sdl2_state_t` allocated by `create()`, freed
  by `destroy()`. No globals, fully testable.
- **Mode enum** — 16 values matching legacy `MODE_*` defines (include/main.h).
  `SDL2ST_BALL_WAIT` and `SDL2ST_WAIT` preserved for compatibility even though
  the legacy code never assigns them.
- **Handler registration** — `register()` takes a `sdl2_state_mode_def_t`
  struct with three optional callbacks: `on_enter`, `on_update`, `on_exit`.
  This replaces the scattered `Reset*()` pattern with centralized hooks.
- **Centralized transition** — `transition()` calls `on_exit` for the old mode,
  then `on_enter` for the new mode. Same-mode transitions are no-ops.
- **Dialogue push/pop** — `push_dialogue()` saves the current mode and enters
  `SDL2ST_DIALOGUE`; `pop_dialogue()` restores it. Replaces the legacy
  `oldMode` save/restore in dialogue.c.
- **Frame counter** — incremented by `update()` except in `SDL2ST_PAUSE` and
  `SDL2ST_DIALOGUE`, matching legacy behavior (main.c:1283).
- **Gameplay query** — `is_gameplay()` returns true for `GAME`, `PAUSE`,
  `BALL_WAIT`, `WAIT` — matching legacy key dispatch routing to
  `handleGameKeys()` vs `handleIntroKeys()`.
- **Pure C** — no SDL2 dependency. The state machine is game logic, not
  platform code.

**What we are NOT doing:**

- Wiring into legacy event loop (separate migration task)
- Implementing the actual mode handlers (each mode's update/enter/exit logic
  stays in its original module until migrated)
- Changing mode numbering or semantics

**Consequences:**

- The ~300-line module replaces the 16-case switch dispatch with O(1) function
  pointer lookup.
- Enter/exit hooks eliminate the error-prone pattern of manually calling
  `Reset*()` before every mode transition.
- Dialogue push/pop is type-safe with clear error states, replacing the
  fragile global `oldMode` variable.
- 37 tests across 10 groups verify transitions, callback ordering, dialogue
  save/restore, frame counter behavior, and all query functions.

## ADR-013: Fixed-timestep game loop with accumulator

**Status:** Accepted
**Date:** 2026-02-25
**Bead:** xboing-cks.1

**Context:**

The legacy game loop in `main.c` uses `XPending()`/`XPeekEvent()` for input and
`sleepSync()` with `usleep(speed * 300)` for frame pacing. The effective tick
interval is `1500 * (10 - speedLevel)` microseconds, giving 9 speed levels from
warp 1 (13.5ms, ~74 tps) to warp 9 (1.5ms, ~667 tps). This couples physics
rate to sleep precision and CPU scheduling jitter.

**Decision:**

Replace the sleep-based timing with a fixed-timestep accumulator pattern:

- **Opaque context pattern** — `sdl2_loop_t` allocated by `create()`, freed
  by `destroy()`. No globals, fully testable via synthetic time injection.
- **Time-source agnostic** — callers pass `elapsed_ms` from `SDL_GetTicks64()`
  delta or inject synthetic time for testing. No internal clock dependency.
- **Microsecond precision** — accumulator tracks time in microseconds to handle
  sub-millisecond tick intervals at high speed levels (warp 9 = 1.5ms).
- **Tick/render separation** — `tick_fn` fires zero or more times per update
  (once per consumed interval); `render_fn` fires exactly once with an
  interpolation alpha in [0.0, 1.0).
- **9 speed levels** — `tick_interval_us = 1500 * (10 - speed_level)`, matching
  the legacy formula exactly. Speed can be changed mid-game; the accumulator
  is preserved across speed changes.
- **Spiral-of-death prevention** — `SDL2L_MAX_TICKS_PER_UPDATE` (10) caps the
  number of logic ticks per frame. When hit, the accumulator is cleared to
  prevent perpetual catch-up after breakpoints or suspends.
- **Pause semantics** — when paused, `update()` returns 0 and does not dispatch
  callbacks. On unpause, the accumulator is cleared to prevent a burst of
  catch-up ticks from time elapsed while paused.
- **NULL-safe callbacks** — either `tick_fn` or `render_fn` may be NULL.
- **Pure C** — no SDL2 dependency. The loop timing is game logic, not
  platform code.

**What we are NOT doing:**

- Wiring into SDL2 event loop (separate migration task — the main loop will
  call `sdl2_loop_update()` with the delta from `SDL_GetTicks64()`)
- Implementing `SDL_WaitEvent` for pause mode (handled by the event loop layer)
- Variable timestep or frame-rate-independent physics (fixed timestep is
  simpler and matches legacy behavior)
- Render interpolation in game modules (alpha is provided but interpolation
  is a future enhancement)

**Consequences:**

- The ~210-line module replaces `sleepSync()` / `usleep()` with deterministic
  accumulator-based timing.
- All 9 legacy speed levels are reproduced exactly with sub-millisecond
  precision.
- 35 tests across 11 groups verify tick dispatch, accumulator behavior, speed
  changes, pause semantics, spiral-of-death clamping, and alpha interpolation.
- Synthetic time injection enables fully deterministic testing with no sleep
  or wall-clock dependency.

## ADR-014: Command-line option parsing

**Status:** Accepted
**Date:** 2026-02-25
**Bead:** xboing-1fr.4

**Context:**

The legacy CLI parser in `init.c:507-693` uses `compareArgument()` — a
case-sensitive prefix matcher implemented with `strncmp()` — to parse 17
command-line options. Four of
these are X11-specific (`-display`, `-sync`, `-usedefcmap`, `-noicon`) and
have no SDL2 equivalent. The parser is tightly coupled to global variables
(`noSound`, `grabPointer`, `debug`, etc.) and calls side-effecting functions
(`SetUserSpeed()`, `SetNickName()`, `PrintUsage()`) during parsing.

**Decision:**

Extract CLI parsing into a standalone pure-C module with no SDL2 dependency:

- **Config struct pattern** — `sdl2_cli_config_t` is a plain struct returned
  by `sdl2_cli_config_defaults()`, populated by `sdl2_cli_parse()`. Caller
  wires values into SDL2 subsystems; parser has no side effects.
- **Exact match** — drops the legacy prefix-matching behavior to avoid
  ambiguity. Options must be typed in full (e.g., `-speed`, not `-sp`).
- **13 options preserved** — all gameplay-affecting options from legacy:
  `-help`, `-usage`, `-version`, `-setup`, `-scores`, `-debug`, `-keys`,
  `-sound`, `-nosfx`, `-grab`, `-speed N`, `-startlevel N`, `-maxvol N`,
  `-nickname STR`.
- **4 options dropped** — `-display` (SDL2 uses `SDL_VIDEODRIVER`),
  `-sync` (no X protocol sync), `-usedefcmap` (no colormap),
  `-noicon` (SDL2 handles window icons via API).
- **Status code return** — `SDL2C_EXIT_*` codes signal informational flags
  (help, version, setup, scores) — caller decides how to print and exit.
  `SDL2C_ERR_*` codes signal parse errors with `bad_option` out-parameter.
- **Nickname truncation** — matches legacy behavior of silently truncating
  nicknames longer than 20 characters.
- **Integer validation** — `strtol()` with `ERANGE` and `INT_MIN`/`INT_MAX`
  bounds checking replaces legacy `atoi()` (which silently returns 0 on
  non-numeric input).  Non-numeric arguments return `ERR_INVALID_VALUE`
  (distinct from `ERR_MISSING_VALUE` for exhausted argv).

**What we are NOT doing:**

- Printing help text, version info, or scores (caller responsibility)
- Wiring parsed values into SDL2 subsystems (integration task)
- Supporting environment variable overrides for CLI options
- Implementing `--long-option` syntax (preserving legacy single-dash style)

**Consequences:**

- The ~240-line module replaces the 186-line coupled parser with a pure
  function that is fully testable without any display, audio, or game state.
- 44 tests across 11 groups verify defaults, NULL safety, all exit flags,
  all boolean flags, integer range validation, nickname handling, error
  paths, option combinations, and status strings.
- No SDL2 dependency — the library links with only libc.
