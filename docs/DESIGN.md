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

## ADR-015: Callback-based ball physics system

**Status:** Accepted
**Date:** 2026-02-25
**Bead:** xboing-1ka.1

**Context:**

`ball.c` is 2093 lines with tight X11 coupling: `Display`/`Window` parameters
on every function, 48 XPM pixmap loads, `XRectInRegion()` for block collision,
and 10+ extern globals (`frame`, `speedLevel`, `paddlePos`, `paddleDx`, etc.).
This makes it impossible to test, impossible to port incrementally, and the
single largest obstacle to SDL2 migration.

Six pure physics functions are already extracted to `ball_math.c` (207 lines,
16 characterization tests). `ball_types.h` has the `BALL` struct and constants
with no X11 dependency. The extraction proved the approach works.

**Decision:**

Create a `ball_system` module (`src/ball_system.c`, `include/ball_system.h`)
that owns the `BALL` array and all physics logic, communicating side effects
through an injected callback table. Key design points:

1. **Opaque context pattern.** `ball_system_t` is heap-allocated and opaque.
   No global state. All functions take a context pointer. Same pattern as
   `sdl2_state_t`, `sdl2_loop_t`, and other modernized modules.

2. **Environment struct replaces extern globals.** `ball_system_env_t`
   carries the 10 values that `ball.c` reads from globals (`frame`,
   `speedLevel`, `paddlePos`, `paddleDx`, `GetPaddleSize()`, etc.).
   Passed per-frame to `ball_system_update()`, never cached. This makes
   the module deterministic and testable without global state setup.

3. **Callback table for side effects.** `ball_system_callbacks_t` has 7
   function pointers replacing direct calls to other modules:
   - `check_region` — replaces `XRectInRegion()` for block collision
   - `on_block_hit` — replaces `HandleTheBlocks()` side-effect dispatch
   - `cell_available` — replaces direct block grid queries in teleport
   - `on_sound` — replaces `playSoundFile()` calls
   - `on_score` — replaces `AddToScore()` calls
   - `on_message` — replaces `SetCurrentMessage()` calls
   - `on_event` — replaces lifecycle side effects (DeadBall, AddABullet)

   Any callback may be NULL (no-op). This enables testing with stubs and
   allows the integration layer to wire in either legacy X11 or SDL2
   implementations.

4. **No rendering.** The module reports render state via
   `ball_system_get_render_info()`. The integration layer draws using
   SDL2 textures or legacy X11 pixmaps. Rendering is never the ball
   system's concern.

5. **Block collision abstracted.** The `check_region` callback replaces
   `XRectInRegion()`, allowing the ball system to work with legacy X11
   regions during transition OR a future pure-C collision system.

6. **Reuses ball_math.c.** Calls existing extracted functions for paddle
   bounce, speed normalization, ball-to-ball collision, and grid conversion.
   No duplication.

7. **Preserves known bugs.** The line 1744 bug (via `ball_math_collide()`)
   and zero-velocity clamp (via `ball_math_normalize_speed()`) are preserved
   as characterized by the existing 16 ball_math tests.

8. **Three-PR decomposition.** The module is too large for a single PR:
   - PR 1: Header + lifecycle + ball management + queries (~550 lines)
   - PR 2: State machine + wall/paddle collision (~550 lines added)
   - PR 3: Block collision + ball-to-ball + multiball (~400 lines added)

**What we are NOT doing:**

- Block system port (xboing-1ka.2 — separate bead)
- SDL2 rendering of balls (integration layer, future task)
- EyeDude collision (separate module concern)
- Asset conversion XPM to PNG (separate infrastructure)
- Wiring into the game loop (integration layer)
- Modifying ball_math.c or ball_types.h (reused as-is)

**Consequences:**

- The ball physics module becomes fully testable with CMocka stubs, no
  display or window system required.
- The callback table provides a clean seam between ball physics and the
  rest of the game — either legacy X11 or SDL2 can implement the callbacks.
- The environment struct eliminates 10+ extern globals, making data flow
  explicit and deterministic.
- The three-PR approach keeps each change reviewable while building toward
  the complete port.
- The guide direction table, state machine dispatch, and all physics math
  are preserved exactly as characterized by existing tests.

## ADR-016: Pure C block grid system with diagonal cross-product collision

**Status:** Accepted

**Context:**

The legacy `blocks.c` (2,614 lines) owns the 18x9 block grid and uses X11
Region objects (`XPolygonRegion`, `XRectInRegion`) for collision detection.
Each block has 4 triangular regions (top, bottom, left, right) created by
`CalculateBlockGeometry()`.  These X11 Region pointers are stored in
`struct aBlock` and must be explicitly destroyed with `XDestroyRegion()`.
This is the tightest X11 coupling in the block system.

The `ball_system` module (ADR-015) already abstracts collision via a
`check_region` callback.  We need a pure C implementation of this callback
that produces equivalent results without any X11 dependency.

**Decision:**

Create a `block_system` module (`src/block_system.c`, `include/block_system.h`)
that owns the block grid and replaces X11 Region objects with on-the-fly
diagonal cross-product math:

1. **No stored regions.** Each block stores only its bounding box
   (`x, y, width, height`).  The 4 triangular regions are fully defined by
   these values and computed during collision checks.

2. **Diagonal cross-product algorithm.** The block rectangle is divided into
   4 triangles by its two diagonals.  Two cross-product values (`d1`, `d2`)
   determine which triangle the ball center falls in:

   ```text
   d1 = w * (by - y) - h * (bx - x)     (TL-BR diagonal)
   d2 = h * (x + w - bx) - w * (by - y) (TR-BL diagonal)

   d1 <= 0, d2 >= 0  =>  TOP
   d1 >= 0, d2 <= 0  =>  BOTTOM
   d1 >= 0, d2 >= 0  =>  LEFT
   d1 <= 0, d2 <= 0  =>  RIGHT
   ```

3. **Three-step collision check:**
   - AABB overlap test (ball bounding box vs block bounding box)
   - Diagonal cross-product to determine hit face
   - Adjacency filter (suppress hit if neighbor in that direction is occupied)

4. **Callback-compatible API.** `block_system_check_region()` and
   `block_system_cell_available()` match the callback signatures in
   `ball_system.h`, allowing direct use as callbacks.

5. **Opaque context pattern** matching `ball_system_t` — heap-allocated,
   no globals, fully testable.

**Consequences:**

- Eliminates all X11 Region dependency from block collision detection.
- The center-point approach (vs. legacy rect-region intersection) may produce
  slightly different results at extreme corners, but the ball system already
  simplifies compound regions to single values, so gameplay impact is minimal.
- The adjacency filter preserves the key legacy behavior of suppressing
  bounces at block junctions.
- Block info catalog, geometry calculation, and grid queries are all pure C
  with zero platform dependencies.

## ADR-017: Pure C paddle system

**Status:** Accepted

**Context:**

The legacy `paddle.c` (346 lines) manages paddle position, size, and
control flags.  Every function takes `Display *display, Window window`
parameters for X11 rendering (pixmap clear/draw).  The actual logic is
simple: keyboard velocity steps, mouse position mapping, wall clamping,
reverse controls, and three-step size transitions.

The `ball_system_env_t` already reads paddle state (`paddle_pos`,
`paddle_dx`, `paddle_size`) from the environment struct.  We need a
pure C module to own and compute these values.

**Decision:**

Create a `paddle_system` module (`src/paddle_system.c`,
`include/paddle_system.h`) with opaque context pattern:

1. **Position update** via `paddle_system_update(ctx, direction, mouse_x)`.
   Keyboard mode adds/subtracts `PADDLE_VELOCITY` (10).  Mouse mode uses
   the legacy formula: `pos = mouse_x - (main_width / 2) + half_width`.

2. **Reverse controls** swap keyboard direction and mirror mouse X
   (`play_width - mouse_x`), matching legacy `MovePaddle()` exactly.

3. **Size transitions** via `change_size(ctx, shrink)`: HUGE to MEDIUM to
   SMALL (or reverse), clamped at extremes.

4. **Delta tracking:** mouse mode computes `dx = mouse_x - previous_mouse_x`
   per frame.  Keyboard mode returns dx=0, matching legacy behavior where
   `paddleDx` is only set for mouse control.

5. **Render info snapshot** provides position, Y coordinate
   (`play_height - DIST_BASE`), pixel width, and size type for the
   integration layer to draw.

**Consequences:**

- Eliminates all X11 dependency from paddle logic.
- The mouse position formula preserves the per-size offset from legacy
  `MovePaddle()` (cursor-to-center offset varies by paddle width).
- The module does not own sticky bat behavior (ball catching) — that
  remains in `ball_system`.  The sticky flag is stored here as a
  convenience for the render layer.

### ADR-018: Pure C gun/bullet system

**Status:** Accepted
**Date:** 2026-02-25
**Bead:** xboing-1ka.4

**Context:**

`gun.c` (643 lines) provides the bullet shooting system.  It has tight
X11 coupling (Display/Window on every function), directly references
global `paddlePos`/`fastGun`/`frame`/`noSound`, calls into `ball.c`,
`blocks.c`, and `eyedude.c` for collision checks, and handles block
type-specific damage dispatch.

**Decision:**

Create `gun_system` as a pure C module using the same opaque context
and callback injection patterns as `ball_system`, `block_system`, and
`paddle_system`.

Key design choices:

1. **Callback-based collision dispatch:** 8 function pointers replace
   direct calls to other subsystems.  `check_block_hit` + `on_block_hit`
   abstract the X2COL/Y2ROW grid lookup and block type-specific damage
   (COUNTER decrement, HYPERSPACE/BLACK absorb, special block SHOTS_TO_KILL).
   This separates "does bullet hit something?" from "what happens?"

2. **Collision priority preserved:** ball > eyedude > block, matching
   the loop order in legacy `UpdateBullet()`.  A bullet that hits a ball
   is consumed before eyedude or block checks run.

3. **Environment struct replaces globals:** `gun_system_env_t` carries
   `frame`, `paddle_pos`, `paddle_size`, and `fast_gun` per-update.

4. **Bullet array:** 40 slots with sentinel `xpos == -1`.  Normal mode
   uses only slot 0 (returns early if occupied).  Fast gun mode searches
   all 40 slots, spawning 2 bullets per shot at `paddle_pos ± size/3`.

5. **Tink array:** 40 slots with frame-based expiry (`clearFrame =
   creation_frame + 100`).  Checked every frame (not gated by
   BULLET_FRAME_RATE).

6. **Ammo model:** `set_ammo`, `add_ammo` (clamp at 20), `use_ammo`
   (no-op if unlimited).  Matches legacy `SetNumberBullets` / `IncNumber
   Bullets` / `DecNumberBullets` / `SetUnlimitedBullets`.

**Consequences:**

- Eliminates all X11 dependency from gun/bullet logic.
- Block type-specific damage dispatch moves to the integration layer's
  `on_block_hit` callback, keeping the gun system free of block type
  knowledge.
- The `is_ball_waiting` callback preserves the legacy guard that prevents
  shooting while the ball sits on the paddle.
- Render info queries (`get_bullet_info`, `get_tink_info`) provide
  position data for the integration layer to draw bullet/tink sprites.

### ADR-019: Pure C score display system

**Status:** Accepted
**Date:** 2026-02-25
**Bead:** xboing-1ka.5

**Context:**

`score.c` (234 lines) owns the game score value, multiplier application
(x2/x4 bonus), extra life threshold tracking (every 100k points), and
right-aligned digit rendering into a 224x42 pixel window.  It reads
global `x2Bonus`/`x4Bonus` flags and `score`, calls `DrawOutNumber()`
for rendering, and directly references X11 Display/Window types.

Four pure arithmetic functions are already extracted to `score_logic.c`
(96 lines, 16 characterization tests): `score_apply_multiplier`,
`score_extra_life_threshold`, `score_compute_bonus`, and
`score_block_hit_points`.

**Decision:**

Create `score_system` as a pure C module using the opaque context and
callback injection patterns established by the other system modules.

Key design choices:

1. **Reuses score_logic.c:** Multiplier application and extra life
   threshold calculation delegate to the already-extracted and tested
   functions.  No duplication of arithmetic.

2. **Callback table (2 pointers):** `on_score_changed` notifies the
   integration layer to redraw; `on_extra_life` notifies when a life
   threshold is crossed.  Far simpler than the 8-callback gun system
   because score has no collision dispatch.

3. **Environment struct:** `score_system_env_t` carries `x2_active` and
   `x4_active` flags, passed to `score_system_add()`.  The module never
   caches these values.

4. **Digit layout as pure function:** `score_system_get_digit_layout()`
   is a stateless utility function in the public API that takes a score
   value (not a context) and returns a `score_system_digit_layout_t`
   with digit values and pixel positions.  This matches legacy
   `DrawOutNumber()`'s right-aligned
   rendering: rightmost digit at x=192 (224-32), stride 32px left per
   digit, max 7 digits (9,999,999).

5. **Extra life tracking:** `score_system_set()` resets the threshold
   index without awarding lives (matching legacy `SetTheScore`).
   `score_system_add()` and `score_system_add_raw()` check for threshold
   crossings and fire `on_extra_life` callbacks.

6. **x2 wins over x4:** When both multipliers are active, x2 takes
   precedence (legacy if/else-if bug preserved via `score_apply_multiplier`).

**Consequences:**

- Eliminates all X11 dependency from score management.
- `score_logic.c` is now built as a dedicated `score_logic` static
  library that is linked by both `block_system` (for hit points) and
  `score_system` (for multiplier and threshold), ensuring a single copy
  of the translation unit and avoiding duplicate-symbol issues.
- The digit layout function enables the SDL2 integration layer to render
  score digits without reimplementing the right-alignment math.
- `add_raw()` supports bonus score commits where the multiplier was
  already applied by the caller.

### ADR-020: Pure C level file loading system

**Status:** Accepted
**Date:** 2026-02-26
**Bead:** xboing-1ka.6

**Context:**

`file.c` (643 lines) contains `ReadNextLevel()` which parses level data
files (title, time bonus, 15x9 character grid) and calls `AddNewBlock()`
for each non-empty cell.  It has tight X11 coupling (Display/Window
parameters, `XDestroyRegion` calls via `ClearBlockArray`), reads globals
(`frame`, `debug`, `level`), and directly references the block grid.

The level file format is plain text: line 1 = title, line 2 = time
bonus (seconds), lines 3-17 = 15 rows of 9 characters.  26 distinct
characters map to block types.  80 level files cycle via modular
wrapping.  Background cycling (2→3→4→5→2) is managed per level
transition.

**Decision:**

Create `level_system` as a pure C module using the opaque context and
callback injection patterns established by the other system modules.

Key design choices:

1. **Single callback:** `on_add_block(row, col, block_type, counter_slide)`
   fires for each non-empty cell during parsing.  The integration layer
   calls `block_system_add()` in its callback, keeping level_system
   independent of block_system.

2. **Character mapping as public utility:** `level_system_char_to_block()`
   is exposed so tests can verify the mapping table independently of
   file I/O.  It encodes all 26 character-to-block-type mappings
   including `LEVEL_SHOTS_TO_KILL=3` for special blocks.

3. **Level wrapping as stateless utility:** `level_system_wrap_number()`
   implements the modular formula: `level % 80`, mapping 0 to 80.
   Produces range 1..80 for any positive input.

4. **Background cycling on context:** `level_system_advance_background()`
   cycles 2→3→4→5→2.  Initial state is 1 so the first advance produces
   2, matching legacy `SetupStage()` behavior.

5. **Path resolution delegated:** The caller resolves the absolute file
   path (via `paths_level_file()` or direct construction) and passes it
   to `level_system_load_file()`.  This keeps the parser maximally
   testable — tests pass local file paths directly.

6. **Newline guard:** Legacy code dereferences `strchr(title, '\n')`
   without null check.  The port guards against missing newlines.

**Consequences:**

- Eliminates all X11 dependency from level file parsing.
- The 26-character mapping table and `SHOTS_TO_KILL` dispatch are
  centralized in one switch statement, replacing the scattered if/else
  chain in legacy `ReadNextLevel()`.
- Tests load real level files from the `levels/` directory, providing
  characterization coverage of the actual game data.
- `SaveLevelDataFile()` (the reverse mapping) is not yet ported —
  it belongs to the editor/save-game subsystem.

## ADR-021: Pure C special/power-up system

**Status:** Accepted

**Context:**
Legacy `special.c` (253 lines) manages 7 boolean special flags (sticky,
saving, fastGun, noWalls, killer, x2, x4) plus rendering for an 8th
(reverse, owned by `paddle.c`).  Every public function takes `Display *`
as its first argument.  `ToggleWallsOn()` directly calls
`XSetWindowBorder()` to change the play area border color.
`DrawSpecials()` renders all 8 labels in a 4x2 grid using
`DrawShadowText()` with X11 font metrics.  `RandomDrawSpecials()` is
attract-mode only, randomizing all flags for visual effect.

The 7 specials are consumed by 5 subsystems (ball, paddle, gun, score,
file) via extern globals — tight coupling that prevents testing and
complicates the SDL2 migration.

**Decision:**
Create `special_system.c`/`special_system.h` as a pure C module using
the opaque context pattern established by all prior game systems.

Key design choices:

1. **Callback for wall state change:** The single X11 side effect
   (`XSetWindowBorder`) is replaced by an `on_wall_state_changed`
   callback.  The integration layer translates this to whatever border
   visual the SDL2 renderer uses.

2. **No rendering in the module:** `DrawSpecials()` is eliminated.
   Instead, `special_system_get_labels()` returns an array of
   `special_label_info_t` structs with label text, column X offset,
   row number, and active flag.  The integration layer renders using
   `sdl2_font` + `sdl2_renderer`.

3. **Reverse injected, not owned:** The reverse special is owned by
   the paddle system.  The special system accepts `reverse_on` as a
   parameter to `get_state()`, `get_labels()`, and returns it in the
   randomized state from `randomize()`.  It never stores or toggles
   the reverse flag internally.

4. **Mutual exclusion preserved:** Setting x2 deactivates x4 and vice
   versa, matching `blocks.c:1603-1613` behavior.

5. **Saving persistence preserved:** `special_system_turn_off()` clears
   all specials except saving, matching the intentionally commented-out
   `ToggleSaving` in legacy `TurnSpecialsOff()`.

6. **Attract-mode randomization:** `special_system_randomize()` accepts
   an injectable `rand_fn` for deterministic testing, with fallback to
   `stdlib rand()`.  Uses `(rand() % 100) > 50` matching legacy 49%
   activation probability.

**Consequences:**

- Eliminates all X11 dependency from special/power-up state management.
- All 7 boolean flags are encapsulated in the opaque context —
  consumers read state via `special_system_is_active()` instead of
  extern globals.
- The wall border side effect is cleanly abstracted — `turn_off()`
  correctly fires the callback only when walls were actually off.
- Panel rendering geometry is exported as constants for the
  integration layer but no rendering logic exists in the module.
- 23 CMocka tests cover lifecycle, individual toggles, mutual
  exclusion, turn-off semantics, state queries, labels, and
  attract-mode randomization.

## ADR-022: Pure C bonus tally sequence state machine

**Status:** Accepted

**Context:**
Legacy `bonus.c` (776 lines) implements the end-of-level bonus screen —
a 10-state sequence that tallies coins, level bonus, bullets, and time
bonus with animated counters, then shows a high-score ranking and
transitions to the next level.  Every function takes `Display *display,
Window window` as its first two arguments.  The module reads 13 extern
globals (`score`, `level`, `bonusBlock`, `numBullets`, etc.) and calls
into 5 other modules (`score.c`, `ball.c`, `highscore.c`, `audio.c`,
`file.c`).

Key legacy behavior: `ComputeAndAddBonusScore()` pre-commits all bonus
points to the real score at screen entry — the animated tally is purely
cosmetic.  Save is triggered every `SAVE_LEVEL` (5) levels relative to
starting level.

**Decision:**
Create `bonus_system.c`/`bonus_system.h` as a pure C module using the
opaque context pattern established by all prior game systems.

Key design choices:

1. **Score pre-computation preserved:** `bonus_system_begin()` computes
   the total bonus and fires `on_score_add` immediately, matching the
   legacy pattern where all points are committed before the animation
   starts.

2. **Environment struct replaces globals:** A `bonus_system_env_t`
   snapshot (score, level, starting_level, time_bonus_secs, bullet_count,
   highscore_rank) is passed to `begin()` and stored internally.

3. **Stateless computation exported:** `bonus_system_compute_total()` and
   `bonus_system_should_save()` are pure functions callable without a
   context, enabling direct unit testing of scoring rules.

4. **Coin counting during gameplay:** `inc_coins`/`dec_coins`/`get_coins`/
   `reset_coins` manage the bonus coin count accumulated during play.
   The count is consumed during the bonus sequence animation.

5. **Wait timer pattern:** State transitions use a uniform
   `set_bonus_wait()` mechanism: set `wait_mode` and `wait_frame`, enter
   `BONUS_STATE_WAIT`, and let `do_bonus_wait()` poll the frame counter.
   `BONUS_LINE_DELAY` (100 frames) matches the legacy `BONUSDELAY`.

6. **Finished flag:** `is_finished()` checks a dedicated `finished` flag
   set by `do_finish()`, not the state enum.  This avoids a race where
   the WAIT→FINISH transition would make `is_finished()` true before the
   `on_finished` callback fires.

**Consequences:**

- Eliminates all X11 dependency from bonus sequence management.
- Score computation logic is independently testable as pure functions.
- The 10-state animation is fully deterministic given frame numbers.
- All rendering responsibility is delegated to the integration layer
  via `get_display_score()` and `get_state()` queries.
- 23 CMocka tests cover lifecycle, score computation, coin management,
  save trigger logic, full state machine sequences, and skip behavior.

## ADR-023: Pure C visual SFX state machine

**Status:** Accepted

**Context:**
Legacy `sfx.c` (384 lines) implements 5 visual effects (SHAKE, FADE,
BLIND, SHATTER, STATIC), a BorderGlow ambient animation, and a
FadeAwayArea utility.  Every function takes `Display *display, Window
window`.  SHAKE calls `XMoveWindow()` to physically shift the play
window.  FADE draws black grid lines via `XDrawLine()`.  BLIND and
SHATTER use `XCopyArea()` from the off-screen buffer.  STATIC is an
unimplemented stub (`/* Do somehting in here */`).

Effects are globally gated by `useSfx` and `DoesBackingStore()`.
State is stored in module-static and function-local static variables,
creating hidden state that is never reset on reactivation.

**Decision:**
Create `sfx_system.c`/`sfx_system.h` as a pure C module using the
opaque context pattern.

Key design choices:

1. **No rendering in the module:** The module computes coordinates,
   timing, and tile order.  Query functions (`get_shake_pos`,
   `get_fade_frame`, `get_shatter_tiles`, `get_blind_strips`) return
   data for the integration layer to render.

2. **Callback for window movement:** SHAKE's `XMoveWindow()` is replaced
   by `on_move_window` callback.  `reset_effect()` always fires the
   callback to restore the canonical position (35, 60).

3. **Per-effect state in struct:** Function-local statics are replaced
   by fields in the opaque context, initialized on `set_mode()`.  This
   eliminates the dirty-state bug where interrupted effects leave stale
   locals.

4. **Injectable random function:** `sfx_rand_fn` parameter allows
   deterministic testing of SHAKE offsets and SHATTER tile order.

5. **STATIC preserved as stub:** The placeholder behavior (50-frame
   timer with no drawing) is preserved exactly.

6. **Synchronous effects preserved:** BLIND and SHATTER complete in a
   single `update()` call returning 0, matching legacy behavior where
   the `while` loop idiom at call sites runs exactly once.

7. **BorderGlow as independent animation:** The glow cycles through
   7-step red/green color ramps on a 40-frame interval, independent
   of the effect mode.

**Consequences:**

- Eliminates all X11 dependency from visual effect management.
- Effect state is explicitly initialized on activation, preventing
  stale-state bugs from interrupted effects.
- SHATTER and BLIND tile/strip data is generated on demand via query
  functions, giving the integration layer full control over rendering.
- 22 CMocka tests cover lifecycle, enable/disable, all 5 effect modes,
  BorderGlow animation, null safety, and the FadeAwayArea utility.

## ADR-024: Pure C EyeDude animated character system

**Status:** Accepted

**Context:**
Legacy `eyedude.c` (396 lines) implements an animated character that
walks across the top row of the play area.  It has 6 walk animation
frames per direction, random direction and turn-at-midpoint behavior,
collision detection against the ball, and awards a 10000-point bonus
when hit.  Every function takes `Display *display, Window window`
parameters and directly calls `XCopyArea()` for rendering and `XPM`
for pixmap loading.

State is stored in module-static variables (mode, direction, x/y
position, animation frame, velocity increment, turn flag).  The
character is triggered by external game events via `HandleEyeDudeMode()`
with mode constants (`EYEDUDE_RESET`, `EYEDUDE_WALK`, `EYEDUDE_DIE`).

**Decision:**
Create `eyedude_system.c`/`eyedude_system.h` as a pure C module using
the opaque context pattern.

Key design choices:

1. **State machine with 6 states:** NONE, RESET, WAIT, WALK, TURN, DIE
   match the legacy mode constants.  RESET initializes direction and
   position, WALK advances animation, DIE awards bonus and resets.

2. **Callback table for side effects:** `is_path_clear` (query),
   `on_score`, `on_sound`, `on_message` replace direct calls to
   other legacy modules.

3. **Injectable random function:** `eyedude_rand_fn` parameter allows
   deterministic testing of direction selection and turn probability.

4. **No rendering in the module:** `get_render_info()` returns position,
   animation frame index, direction, and visibility for the integration
   layer to draw.

5. **AABB collision detection:** `check_collision()` implements the same
   axis-aligned bounding box overlap test as legacy
   `CheckBallEyeDudeCollision()`, using half-width/half-height center
   coordinates.

6. **Constants match legacy exactly:** WIDTH=32, HEIGHT=32,
   FRAME_RATE=30, WALK_SPEED=5, HIT_BONUS=10000, WALK_FRAMES=6,
   TURN_CHANCE=30%.

**Consequences:**

- Eliminates all X11 dependency from EyeDude character management.
- Character state is explicitly owned by the opaque context, with no
  module-static or global variables.
- 16 CMocka tests cover lifecycle, reset/path-check, walk animation,
  turn-at-midpoint, collision/death, and null safety.

## ADR-025: Pure C presents/splash screen sequencer

**Status:** Accepted

**Context:**
Legacy `presents.c` (700 lines) implements the startup animation
sequence: Australian flag + earth, author credits ("Justin" / "Kibell"
/ "Presents" bitmaps), stamping the letters X-B-O-I-N-G one at a time,
a sparkle animation, typewriter-style welcome text (3 lines), and a
curtain-wipe clear.  Every function takes `Display *display, Window
window`.  State is stored in module-static and function-local static
variables.  The WAIT state implements a frame-delay timer.

The three typewriter text functions (DoSpecialText1/2/3) are
copy-pasted code differing only in string content and y-offset.
Function-local statics in DoLetters, DoSparkle, and DoClear create
stale-state bugs on re-entry.

**Decision:**
Create `presents_system.c`/`presents_system.h` as a pure C sequencer
module using the opaque context pattern.

Key design choices:

1. **Sequencer, not renderer:** The module advances state and timing.
   Query functions (`get_flag_info`, `get_letter_info`, `get_ii_info`,
   `get_sparkle_info`, `get_typewriter_info`, `get_wipe_info`) return
   position/frame/text data for the integration layer to render.

2. **WAIT state as generic timer:** `set_wait(target, frame)` replaces
   legacy `SetPresentWait()`.  WAIT transitions to `wait_target` when
   `current_frame >= wait_frame`.

3. **Unified typewriter:** The three identical DoSpecialText functions
   are replaced by a single `do_typewriter()` parameterized by line
   index, next state, and delay.

4. **Sound events as data:** `get_sound()` returns the sound name and
   volume for the current frame, rather than calling audio functions.

5. **Callbacks for external queries:** `get_nickname` and
   `get_fullname` callbacks replace direct calls to `GetNickName()`
   and `getUsersFullName()`.

6. **All timing constants exposed:** Frame delays are `#define`
   constants matching legacy values, enabling verification.

**Consequences:**

- Eliminates all X11 dependency from the presents sequence.
- All sub-state (letter index, sparkle frame, typewriter progress,
  wipe position) is owned by the opaque context.
- 18 CMocka tests across 7 groups cover lifecycle, flag state, letter
  stamping, sparkle, typewriter, curtain wipe, skip, and null safety.

## ADR-026: Pure C intro and instructions screen sequencer

**Status:** Accepted

**Context:**
Legacy `intro.c` (472 lines) and `inst.c` (290 lines) implement two
related screens: the intro screen (title + 22 block descriptions +
sparkle animation + devil eyes blink) and the instructions screen
(title + 20-line instruction text + sparkle animation).  Both share
the sparkle star animation pattern and big title bitmap.

Both modules use function-local statics for sparkle state and global
variables for frame timing.  The block descriptions in DoBlocks() are
hardcoded as 22 sequential draw calls with varying x/y offsets.
The instruction text in inst.c is a static string array with NULL
spacers.

**Decision:**
Create a single `intro_system.c`/`intro_system.h` module that handles
both screens, selected by an `intro_screen_mode_t` parameter.

Key design choices:

1. **Two modes, one module:** INTRO mode flows TITLE -> BLOCKS -> TEXT
   -> SPARKLE.  INSTRUCT mode flows TITLE -> TEXT -> SPARKLE (skips
   BLOCKS).  Both share sparkle animation code.

2. **Static data tables:** The 22 block descriptions and 20 instruction
   text lines are const static arrays, replacing hardcoded draw calls.
   Each block entry stores type enum, x/y position, x/y adjustments,
   and description string.

3. **Injectable random function:** `intro_rand_fn` for deterministic
   sparkle position testing.

4. **Timing queries:** `should_blink()` and `should_draw_specials()`
   let the integration layer know when to trigger devil eyes and
   special effect draws, matching legacy `HandleBlink()` and
   `FLASH` interval logic.

5. **End frame offset:** Intro ends at +3000 frames, instructions at
   +7000 frames, matching legacy values.

**Consequences:**

- Eliminates all X11 dependency from both intro and instructions
  screens.
- Block descriptions are data-driven instead of hardcoded draw calls.
- Shared sparkle animation eliminates code duplication between the
  two modules.
- 17 CMocka tests across 7 groups cover lifecycle, both state flows,
  block table, instruction text, sparkle/blink, and null safety.

### ADR-027: Pure C demo and preview screen sequencer

**Status:** Accepted
**Date:** 2026-02-25
**Bead:** xboing-dm8.3

**Context:**
Legacy `demo.c` (343 lines) and `preview.c` (242 lines) implement two
attract-mode screens that share the sparkle animation loop and FLASH
interval logic for specials redraw.  Both use X11 Display/Window
parameters, extern globals, and direct draw calls.  The demo screen
shows a ball trail illustration with descriptive text, then loops in
a sparkle animation.  The preview screen loads a random level and
waits before cycling.

**Decision:**
Create a single `demo_system.c`/`demo_system.h` module that handles
both screens, selected by a `demo_screen_mode_t` parameter.

Key design choices:

1. **Two modes, one module:** DEMO mode flows TITLE -> BLOCKS -> TEXT
   -> SPARKLE(5000) -> FINISH.  PREVIEW mode flows TITLE (loads random
   level via callback) -> TEXT -> WAIT(5000) -> FINISH.  Both share
   sparkle animation and FLASH interval logic.

2. **Static data tables:** The 10 ball trail positions and 5 descriptive
   text lines are const static arrays, replacing hardcoded draw calls.

3. **Injectable random function:** `demo_rand_fn` for deterministic
   level selection and sparkle position testing.

4. **Level loading via callback:** `on_load_level(level_num, user_data)`
   lets the integration layer handle actual level file I/O.

5. **Specials query:** `should_draw_specials()` returns true at FLASH
   intervals (every 30 frames) during SPARKLE and WAIT states,
   matching legacy behavior.

**Consequences:**

- Eliminates all X11 dependency from both demo and preview screens.
- Ball trail and text are data-driven instead of hardcoded draw calls.
- Shared sparkle and FLASH logic eliminates code duplication.
- 15 CMocka tests across 6 groups cover lifecycle, both state flows,
  data tables, sparkle/specials, and null safety.

### ADR-028: Pure C keys and editor controls screen sequencer

**Status:** Accepted
**Date:** 2026-02-25
**Bead:** xboing-dm8.4

**Context:**
Legacy `keys.c` (385 lines) and `keysedit.c` (328 lines) implement two
help screens that share a 5-state machine (TITLE -> TEXT -> SPARKLE ->
FINISH) with identical sparkle animation and 4000-frame end timers.
Both use X11 Display/Window parameters, font metrics, and direct draw
calls.  The game controls screen shows a mouse diagram with arrows
and 20 key bindings in two columns.  The editor controls screen shows
7 lines of instructions and 10 key bindings in two columns.

**Decision:**
Create a single `keys_system.c`/`keys_system.h` module that handles
both screens, selected by a `keys_screen_mode_t` parameter.

Key design choices:

1. **Two modes, one module:** Both GAME and EDITOR modes share the
   same state flow (TITLE -> TEXT -> SPARKLE -> FINISH).  The module
   provides separate data table queries for each mode's content.

2. **Static data tables:** 20 game key bindings, 7 editor info lines,
   and 10 editor key bindings are const static arrays with column
   assignment, replacing hardcoded `strcpy`+draw call sequences.

3. **Blink timing:** Game mode fires blink events at configurable
   intervals.  Editor mode never blinks, matching legacy behavior
   where only `keys.c` calls `HandleBlink()`.

4. **Sound differentiation:** Game finish plays "boing" (transitions
   to keysedit), editor finish plays "warp" (transitions to highscore).

5. **Layout delegation:** Column x positions and spacing constants are
   provided as `#define`s.  Font-dependent y positioning is left to the
   integration layer.

**Consequences:**

- Eliminates all X11 dependency from both keys screens.
- Key bindings are data-driven instead of hardcoded draw call sequences.
- Shared sparkle and state machine eliminate code duplication.
- 16 CMocka tests across 7 groups cover lifecycle, both state flows,
  data tables, sparkle/specials, blink, and null safety.

### ADR-029: Pure C modal input dialogue system

**Status:** Accepted
**Date:** 2026-02-25
**Bead:** xboing-dm8.5

**Context:**
Legacy `dialogue.c` (399 lines) implements a modal text input dialog
with X11 event loop, XPM icon pixmaps, font metrics for text width
checking, and keysym-based character validation.  It has 4 validation
modes and runs its own blocking event loop inside the game.

**Decision:**
Create `dialogue_system.c`/`dialogue_system.h` as a non-blocking
input component that the integration layer drives frame-by-frame.

Key design choices:

1. **Non-blocking design:** Legacy runs its own X11 event loop.  The
   pure C module exposes `key_input()` for the integration layer to
   feed key events, and `update()` to advance state.

2. **ASCII validation:** Validation uses ASCII ranges matching the
   X11 keysym ranges (keysyms equal ASCII for printable characters).
   TEXT: space-z, NUMERIC: 0-9, ALL: space-tilde, YES_NO: yYnN.

3. **Configurable max chars:** `set_max_chars()` replaces the legacy
   `XTextWidth()` pixel check.  Integration layer converts pixel
   budget to character count based on its font metrics.

4. **Cancel vs submit tracking:** `was_cancelled()` reports whether
   the dialogue was dismissed via Escape (cancel) or Return (submit).

5. **Sound events per key:** "click" on valid char, "tone" on buffer
   full, "key" on backspace, matching legacy behavior.

**Consequences:**

- Eliminates all X11 dependency from the dialogue system.
- Non-blocking design integrates with the frame-driven game loop.
- Character validation is unit-testable without X11 keysyms.
- 16 CMocka tests across 7 groups cover lifecycle, state flow,
  all 4 validation modes, backspace/overflow, and null safety.

### ADR-030: Pure C high score display sequencer

**Status:** Accepted
**Date:** 2026-02-25
**Bead:** xboing-dm8.6

**Context:**
Legacy `highscore.c` (1068 lines) combines score file I/O with
display sequencing.  The display portion uses X11 pixmap rendering,
XPM sparkle animations, region-based text layout, and 30+ direct
X11 draw calls.  File I/O is tracked separately under xboing-1fr.

**Decision:**
Create `highscore_system.c`/`highscore_system.h` covering only the
display sequencer — the animation state machine that drives the high
score screen presentation.

Key design choices:

1. **Display only:** File I/O (read/write/lock) is out of scope.
   Score data is injected via `set_table()` with a structured table
   containing master name/text and up to 10 scored entries.

2. **Two sparkle subsystems:** Title sparkle (two stars flanking
   the title with fast/slow delay alternation) and row sparkle
   (walks down score rows, skips empty entries, wraps around).

3. **Score type enum:** GLOBAL vs PERSONAL distinguishes the two
   high score screens.  The `on_finished` callback reports which
   type completed so the integration layer can transition correctly.

4. **Current score highlight:** `set_current_score()` allows the
   integration layer to highlight the player's score in the table.

5. **Standard 4000-frame timing:** Matches the other screen
   sequencers (keys, demo, presents, intro).

**Consequences:**

- Cleanly separates display sequencing from file I/O concerns.
- Title and row sparkle animations are fully testable without X11.
- 14 CMocka tests across 6 groups cover lifecycle, state flow,
  score table data, title sparkle, row sparkle, and null safety.

### ADR-031: Pure C message display system

**Status:** Accepted
**Date:** 2026-02-25
**Bead:** xboing-dm8.7

**Context:**
Legacy `mess.c` (160 lines) manages the single-line message bar in
the play area.  It stores a message, optionally auto-clears after
2000 frames, and reverts to the level name.  All rendering is via
X11 `XClearWindow`, `XTextWidth`, and `DrawTextFast`.

**Decision:**
Create `message_system.c`/`message_system.h` as a pure data module.
The integration layer reads the current text and renders it.

Key design choices:

1. **Data-only module:** No rendering.  The integration layer calls
   `get_text()` and draws it however it likes.

2. **Auto-clear with default:** `set(msg, auto_clear, frame)` stores
   the message and schedules a revert after `CLEAR_DELAY=2000` frames.
   The revert target is `set_default()` (the level name), matching
   the legacy behavior.

3. **Changed flag:** `text_changed()` and the return value of
   `update()` report when the displayed text changes, so the
   integration layer only re-renders when necessary.

4. **Safe string handling:** Uses `strncpy` with explicit null
   termination, matching `MESSAGE_MAX_LEN=1024` (legacy buffer size).

**Consequences:**

- Eliminates all X11 dependency from the message system.
- Auto-clear timing is deterministic and testable.
- 12 CMocka tests across 5 groups cover lifecycle, set/query,
  auto-clear, default message, and null safety.

### ADR-032: JSON-based high score file I/O

**Status:** Accepted
**Date:** 2026-02-25
**Bead:** xboing-1fr.1

**Context:**
Legacy `highscore.c` stores scores in a binary format: a header
struct (`version` + `masterText[80]`) followed by 10 entry structs
with all fields in network byte order (`htonl`/`ntohl`).  File
locking uses `lockf`/`flock`.  The binary format is fragile
(struct padding, byte order, version coupling).

**Decision:**
Create `highscore_io.c`/`highscore_io.h` that reads and writes
high score tables as JSON files.

Key design choices:

1. **JSON format:** Human-readable, no byte order issues, resilient
   to struct layout changes.  Minimal hand-written parser for the
   specific schema (not a general JSON library).

2. **Atomic writes:** Write to `.tmp` file, then `rename()`.  No
   file locking needed — `rename()` is atomic on POSIX.

3. **Reuses `highscore_table_t`:** The in-memory representation is
   the same struct used by `highscore_system.h` for display.  The
   I/O module reads/writes this struct to/from JSON.

4. **Score management:** `highscore_io_insert()` handles sorted
   insertion with shift-down, and `highscore_io_sort()` provides
   bubble sort (adequate for 10 entries).

5. **Parent directory creation:** `highscore_io_write()` creates
   parent directories if needed, supporting XDG data paths.

6. **JSON escaping:** Handles `\`, `"`, and control characters
   in names (quotes in player names are preserved).

**Consequences:**

- Eliminates all `htonl`/`ntohl`/`flock` dependencies.
- Score files are human-readable and editable.
- Round-trip fidelity verified including special characters.
- 18 CMocka tests across 7 groups cover initialization, sorting,
  insert/ranking, write/read round-trips, edge cases, error
  handling, and null safety.

### ADR-033: JSON-based save game I/O

**Status:** Accepted
**Date:** 2026-02-25
**Bead:** xboing-1fr.2

**Context:**
Legacy `file.c` saves game state as a binary `saveGameStruct`
(9 fields written with `fwrite`).  The format has the same issues
as the high score binary format: struct padding, platform byte
order, and version fragility.

**Decision:**
Create `savegame_io.c`/`savegame_io.h` that reads and writes game
state as JSON files.

Key design choices:

1. **Same JSON pattern as highscore_io:** Minimal hand-written
   parser for the specific 9-field schema.  Atomic writes via
   temp file + `rename()`.

2. **Level data out of scope:** Level block data continues to use
   the existing `.data` file format.  This module handles only
   the game state (score, level, lives, paddle, bullets, time).

3. **File lifecycle helpers:** `exists()` and `delete()` support
   the "one save slot" gameplay pattern.

4. **Sensible defaults:** `init()` sets level=1, lives=3,
   paddle_size=50, matching the new-game starting state.

**Consequences:**

- Save files are human-readable JSON.
- Atomic writes prevent corruption on crash.
- 13 CMocka tests across 6 groups cover initialization, round-trips,
  file operations, error handling, edge cases, and null safety.

## ADR-034: TOML-based user preferences file

**Status:** Accepted

**Context:**
Legacy XBoing has no persistent user preferences.  All settings
(speed, control mode, volume, nickname, SFX) are set via command-line
flags on every launch and lost when the program exits.  Compile-time
defines control asset paths but not user preferences.

Users expect a config file at `XDG_CONFIG_HOME/xboing/config.toml`
(typically `~/.config/xboing/config.toml`) that persists preferences
across sessions, with CLI flags overriding config values at startup.

**Decision:**
Create `config_io.c`/`config_io.h` that reads and writes user
preferences as a minimal TOML subset (flat key=value pairs).

Key design choices:

1. **Minimal TOML subset:** Only flat key=value pairs with integer,
   boolean, and basic string types.  No tables, arrays, or datetime.
   Sufficient for 7 preference fields; avoids a TOML library dependency.

2. **Forward-compatible parsing:** Unknown keys are silently ignored.
   Out-of-range values leave defaults in place.  Empty and
   comment-only files are valid (all defaults apply).

3. **Same atomic write pattern:** Temp file + `rename()` consistent
   with `highscore_io` and `savegame_io`.

4. **CLI override by design:** `config_io_read()` populates defaults
   first; the integration layer then applies CLI overrides on top.

5. **Matches `sdl2_cli_config_t` fields:** speed, start_level,
   control (keys/mouse), sfx, sound, max_volume, nickname.  Debug
   and grab are runtime-only flags, not persisted.

**Consequences:**

- User preferences survive across sessions without CLI flags.
- Config file is human-readable and hand-editable TOML.
- 17 CMocka tests across 7 groups cover initialization, round-trips,
  TOML parsing, file operations, error handling, edge cases, and
  null safety.

## ADR-035: cppcheck pass across entire codebase

**Status:** Accepted

**Context:**

cppcheck with `--enable=warning,style,performance,portability` reported
143+ findings across 29 legacy `.c` files, 9 test files, and 33
modernized `src/` files. Findings ranged from genuine bugs (missing
return statements, resource leaks, uninitialized reads, null-pointer
dereferences) to style improvements (const correctness, dead code,
shadow variables) and noise (C89-style variable declarations at
function tops).

**Decision:**

1. **Fix all genuine bugs:** `missingReturn` (2), `resourceLeak` (1),
   `legacyUninitvar` (1), `nullPointerRedundantCheck` (2),
   `charLiteralWithCharPtrCompare` (2).

2. **Fix all style issues that improve code quality:** `const`
   qualifiers on 48 pointers (parameters and locals),
   `unreadVariable` (12), `redundantAssignment` (6),
   `unusedVariable` (2), `shadowVariable` (3), `duplicateBreak` (1).

3. **Suppress intentional patterns with inline comments:**
   `knownConditionTrueFalse` (6 defensive bounds checks in ball.c),
   `nullPointerRedundantCheck` (2 after `ShutDown` which calls `exit`),
   callback signatures that cannot take `const` due to API contracts.

4. **Suppress `variableScope` globally for legacy files:** 66
   findings are all C89 top-of-function declarations. Refactoring
   these is high-risk, low-value churn in code that will be replaced.

5. **Promote legacy cppcheck CI step to strict:** Changed from
   informational (`|| true`) to `--error-exitcode=1`. Added test
   files as a third strict cppcheck step.

**Consequences:**

- All three cppcheck CI steps (modernized, legacy, tests) now enforce
  zero warnings with `--error-exitcode=1`.
- Genuine bugs fixed: 2 missing returns, 1 file handle leak,
  1 uninitialized read, 2 pointer-vs-char comparisons.
- 48 pointer parameters/variables gained `const` qualifiers, improving
  API contracts and catching accidental mutations at compile time.
- Header files updated in lockstep with function signature changes
  (10 headers, plus test stubs).
- `variableScope` suppressed globally for legacy files only;
  modernized code in `src/` has no suppressions for this category.

## ADR-036: clang-tidy pass with .clang-tidy configuration

**Status:** Accepted

**Context:**

clang-tidy with `bugprone-*`, `performance-*`, and targeted
`readability-*` checks reported 136 findings across modernized and
legacy files. Many findings were genuine improvements (missing default
cases, redundant else-after-return, float-precision math functions,
implicit widening casts). Others were C idioms that clang-tidy
incorrectly flagged as bugs (assignment-in-if, branch clones in
state machines, integer division for pixel coordinates).

**Decision:**

1. **Fix all actionable findings:** Added `default: break;` to 28
   switch statements. Removed 10 redundant `else` blocks after
   `return`. Replaced 10 double-precision math calls with float
   variants (`sqrtf`, `atanf`, `sinf`, `cosf`). Fixed 6 integer
   divisions used in float context with explicit casts. Fixed 5
   implicit widening multiplication results. Collapsed 3 duplicate
   switch branches. Fixed 1 misleading indentation. Fixed 1
   `BUFFER_SIZE` macro widening.

2. **Create `.clang-tidy` config:** Enables `bugprone-*`,
   `performance-*`, and four targeted `readability-*` checks.
   Disables 6 checks that conflict with legitimate C idioms:
   `assignment-in-if-condition`, `branch-clone`, `signal-handler`,
   `integer-division`, `easily-swappable-parameters`,
   `narrowing-conversions`, `macro-parentheses`.

3. **Zero NOLINT comments in production code:** All suppressions
   are handled by the `.clang-tidy` config file, not inline comments.
   Only `cppcheck-suppress` comments remain for cppcheck-specific
   findings.

**Consequences:**

- Zero clang-tidy warnings across entire codebase (src/, legacy,
  ball_math.c) with the project `.clang-tidy` configuration.
- `.clang-tidy` config documents each disabled check with rationale.
- Ball physics float precision improved (eliminates unnecessary
  double promotion in 10 math calls).
- All switch statements now have default cases, preventing silent
  fallthrough on unexpected enum values.

## ADR-037: Asset directory resolution for installed binaries

**Status:** Accepted

**Context:**

The .deb produced by PR #91 installed and lintian-cleaned, but the
binary failed immediately when launched outside the source tree
(GNOME launcher, /tmp, $HOME) with `cannot open directory
'assets/images'`.  Two underlying bugs:

1. `assets/images/` was never installed by CMakeLists.txt — only
   levels/, sounds/, fonts/, and docs/ were.  /usr/share/xboing/
   shipped without any image content.
2. Each asset subsystem hardcoded a cwd-relative default path with
   no install fallback: sdl2_texture (`assets/images`), sdl2_font
   (`assets/fonts`), sdl2_audio (`sounds`).  paths.c handled level
   and sound *files* via XDG_DATA_DIRS lookup, but the subsystem
   *base directories* bypassed paths.c entirely.

The asymmetry meant the binary worked from a developer's source-tree
checkout (where ./assets/images exists) and nowhere else.

Three industry patterns considered:

| Pattern | Used by | --prefix override | Notes |
|---------|---------|-------------------|-------|
| Compile-time DATADIR (autotools/cmake default) | Most Debian games — frozen-bubble, supertux, gnubg, openttd | No | Fast, simple. Configure-time prefix is final. `cmake --install --prefix <alt>` doesn't propagate to compile defines. |
| XDG_DATA_DIRS lookup at runtime (freedesktop spec) | GNOME, KDE, GTK/GLib apps, libnotify | Yes (for standard prefixes) | Searches `$XDG_DATA_DIRS` (default `/usr/local/share:/usr/share`) for `<dir>/<app>/<subdir>`. Handles prefix=/usr and /usr/local transparently. |
| readlink /proc/self/exe + relative | Snap, AppImage, Flatpak | Yes (any prefix) | Bundle-relocatable. Overkill for non-bundle apps. |

**Decision:**

1. **Install the missing asset directory.** CMakeLists.txt adds
   `install(DIRECTORY assets/images/)` → `${CMAKE_INSTALL_DATADIR}/
   xboing/images` (PNGs only, *.xcf source files stay in-repo).
2. **Resolve subsystem base directories via XDG_DATA_DIRS first.**
   New helper `paths_install_data_dir(cfg, subdir, buf, bufsize)` in
   paths.c iterates `cfg->xdg_data_dirs` (already populated by
   `paths_init`) for `<dir>/xboing/<subdir>`.  Returns the first
   match that opendir() succeeds on (proves it's a readable
   directory, not just a path that stat()s).  Mirrors the existing
   `resolve_asset()` pattern paths.c uses for level and sound files.
3. **Three-level fallback chain in game_init.c.**  For each of the
   texture, font, and audio subsystems:
   1. `paths_install_data_dir` XDG lookup (handles prefix=/usr,
      prefix=/usr/local, and any prefix the user has added to
      $XDG_DATA_DIRS).
   2. Compile-time `XBOING_INSTALLED_*_DIR` macros from the new
      `include/xboing_paths.h` header (safety net for unusual
      installs not in $XDG_DATA_DIRS).
   3. Cwd-relative source-tree default already in each
      subsystem's `*_config_defaults()` (dev mode).
4. **Compile-time prefix is honored via target_compile_definitions.**
   CMakeLists.txt sets `XBOING_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/
   xboing"` for the xboing target, so the safety-net path tracks the
   configure-time prefix.  Header default of `/usr/share/xboing` is
   the static-analysis / non-CMake fallback only.
5. **Read/write API split for level directory accessors.**  The 1996
   source (`original/editor.c:857-860, 887-931, 912-915`) used a
   single `XBOING_DIR` (default `.`) with subdirs `levels/` and
   `sounds/`, and the `Imakefile` (`original/Imakefile:35-38, 208`)
   set `LEVEL_INSTALL_DIR`, `SOUNDS_DIR`, and did `chmod a+rw` on
   installed data files — a single-dir read/write model.  That model
   is incompatible with modern multi-user Linux installs where system
   data directories (`/usr/share/xboing/levels`) are read-only;
   `chmod a+rw` on shared system files is no longer acceptable.
   The modernized port therefore splits the level directory API into
   three accessors:
   - `paths_levels_dir_readable` — for load operations:
     `XBOING_LEVELS_DIR` env override → `$XDG_DATA_DIRS/xboing/levels`
     (opendir-based readability check) → cwd-relative `"levels"`.
   - `paths_levels_dir_writable` — for editor save operations:
     `XBOING_LEVELS_DIR` env override → `$XDG_DATA_HOME/xboing/levels`
     (directory not required to exist; editor creates it) → cwd-relative
     `"levels"`.
   - `paths_sounds_dir_readable` — for audio load operations (same
     chain as `paths_levels_dir_readable` but for `sounds/`).  No
     writable counterpart — the editor does not write sounds.
   The env override `XBOING_LEVELS_DIR` applies to **both** read and
   write paths, preserving the 1996 single-dir contract for users who
   set it.  The user assumes write-permission responsibility when
   setting this override.
   The old `paths_levels_dir()` and `paths_sounds_dir()` functions are
   removed; no backward-compat shim.
6. **Editor struct and create signature updated.**  `editor_system_t`
   now has `levels_dir_readable[512]` and `levels_dir_writable[512]`
   fields.  `editor_system_create` accepts both.  The load path
   (`do_load_level`, `do_load`) uses `levels_dir_readable`; the save
   path (`do_save`) uses `levels_dir_writable` and calls `mkdir_p`
   before the first write.
7. **`paths_install_data_dir` uses opendir, not stat+S_ISDIR.**  opendir
   succeeds iff the path exists, is a directory, and is read+execute
   accessible — exactly the precondition the subsystem's directory
   scan needs.  stat+S_ISDIR alone wouldn't catch a perms-locked
   dir; the later scan would still fail with a worse error message.
   This check was switched from stat to opendir in an earlier commit
   on this branch.

**Consequences:**

- Installed binary launches correctly from any cwd.  Verified:
  desktop launcher icon, `cd /tmp && xboing`, `xboing` from $HOME.
- `cmake --install --prefix /usr/local` works without recompiling —
  /usr/local/share is in default $XDG_DATA_DIRS, so XDG lookup
  finds the assets at the override prefix.  Same mechanism most
  Debian-packaged GNOME apps rely on.
- Editor save under installed mode writes to `$XDG_DATA_HOME/xboing/
  levels` instead of the read-only `/usr/share/xboing/levels`.  No
  more EROFS failure on first save.
- No regression in dev mode: cwd-relative source-tree paths still
  win when neither XDG lookup nor compile-time fallback succeeds
  (e.g., running `./build/xboing` from a fresh checkout with no
  installed .deb).
- `paths.c` remains pure / testable.  All filesystem-stat calls for
  install-vs-cwd selection live within paths.c's accessors; callers
  receive resolved strings only.
- For unusual prefixes (`/opt/xboing`), users add to $XDG_DATA_DIRS
  themselves.  This matches the standard Linux contract — no app
  handles arbitrary prefixes without env var configuration.
