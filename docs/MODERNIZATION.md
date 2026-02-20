# XBoing Modernization Plan

> Architecture and implementation changes for rewriting XBoing as a modern Linux game
> with distro-quality packaging, while preserving the original gameplay experience.

---

## Table of Contents

1. [Graphics & Windowing](#1-graphics--windowing)
2. [Audio](#2-audio)
3. [Build System & Distribution](#3-build-system--distribution)
4. [Game Loop & Timing](#4-game-loop--timing)
5. [State Machine & Game Logic](#5-state-machine--game-logic)
6. [Data Formats](#6-data-formats)
7. [Code Organization](#7-code-organization)
8. [What Stays the Same](#8-what-stays-the-same)
9. [What Gets Eliminated](#9-what-gets-eliminated)

---

## 1. Graphics & Windowing

| Aspect | Current | Proposed | Rationale |
|--------|---------|----------|-----------|
| Windowing | Raw Xlib (`XCreateWindow`, `XMapWindow`, event loop) | **SDL2** | Cross-distro, handles DPI, fullscreen, still purely 2D. One library replaces Xlib + colormap + GC + cursor + font machinery |
| Rendering | 6 GCs with XPM pixmap blitting, `XCopyArea`, backing store | **SDL2 GPU-accelerated 2D renderer** (`SDL_Renderer` + `SDL_Texture`) | Hardware-accelerated but still sprite-based. Identical visual result, trivial vsync |
| Image format | XPM (C-header format, requires `libXpm`) | **PNG** (via `SDL_image`) | Convert existing XPMs to PNGs once. Universally supported, smaller, no X11 dependency |
| Colormap | Private PseudoColor colormap, `reds[7]`/`greens[7]` cycling | **Direct RGBA colors** | TrueColor is universal now. Color cycling becomes tinting or palette swap in a shader or precomputed sprite variants |
| Fonts | 4 Adobe Helvetica XLFD bitmap fonts, `XLoadQueryFont` | **Bundled TTF** (via `SDL_ttf`) or **pre-rendered bitmap font atlas** | XLFD fonts are disappearing from distros. A bundled Helvetica-alike (e.g., Liberation Sans, already in Ubuntu) guarantees identical look |
| SFX compositing | `GXxor`, `GXand`, `GXor` blend modes via GCs | **SDL blend modes** (`SDL_BLENDMODE_*`) or **render-to-texture** | SDL2 has equivalent blend modes. Shake/shatter/blind effects become texture copies with offset/clip |
| Collision regions | `XPolygonRegion` + `XRectInRegion` (X server-side) | **Client-side geometry** (simple triangle point-in-region or AABB) | Remove X server dependency for game logic. The 4-triangle-per-block scheme is ~20 lines of pure C math |
| Cursors | `XCreateFontCursor` / `XCreatePixmapCursor` | `SDL_CreateCursor` / `SDL_CreateColorCursor` | Direct equivalent |
| Window hierarchy | 9+ sub-windows (play, score, level, mess, special, time...) | **Single window, logical regions** | Sub-windows were an Xlib pattern for clipping/event routing. With SDL2, just render to regions of one surface. Simplifies everything |

---

## 2. Audio

| Aspect | Current | Proposed | Rationale |
|--------|---------|----------|-----------|
| Architecture | `fork()` + pipe IPC, child writes to `/dev/dsp` | **SDL2_mixer** (or **miniaudio** single-header lib) | No child process, no raw device access. SDL2_mixer handles mixing, channels, volume natively |
| Format | `.au` (Sun audio), 8-bit | **OGG Vorbis** (or **WAV** for simplicity) | `.au` is dead. WAV is zero-dependency for short SFX; OGG for any longer clips. Batch-convert existing files |
| Driver layer | 12 platform-specific drivers, symlink swap | **Gone** — SDL2 abstracts audio backends (PulseAudio, PipeWire, ALSA) | The entire `audio/` directory with 12 drivers collapses to ~30 lines of SDL2_mixer calls |
| Volume | No-op on Linux driver | **Actually works** via `Mix_Volume` / `Mix_VolumeMusic` | Real per-channel volume control |
| Device | Hardcoded `/dev/dsp` | **Automatic** (SDL2 picks best available) | PipeWire/PulseAudio/ALSA, no user config needed |

---

## 3. Build System & Distribution

| Aspect | Current | Proposed | Rationale |
|--------|---------|----------|-----------|
| Build tool | Hand-written `Makefile` with hardcoded `gcc` flags | **Meson** (or CMake) | Native `pkg-config` integration for SDL2/SDL2_mixer/SDL2_image/SDL2_ttf. Generates install targets, pkg metadata. Meson is increasingly the Linux-native standard |
| Compile-time paths | `#define HIGH_SCORE_FILE`, `LEVEL_INSTALL_DIR`, `SOUNDS_DIR` baked in | **XDG Base Directory spec at runtime** | Data: `$XDG_DATA_DIRS/xboing/` (levels, sounds, images). Config: `$XDG_CONFIG_HOME/xboing/`. Scores: `$XDG_DATA_HOME/xboing/`. No compile-time path assumptions |
| Dependencies | `libxpm-dev`, `libx11-dev` | `libsdl2-dev`, `libsdl2-image-dev`, `libsdl2-mixer-dev`, `libsdl2-ttf-dev` | All in Ubuntu repos. Commonly available across distros |
| Installation | Run from repo root only | **FHS-compliant install** (`/usr/share/xboing/` for assets, `/usr/games/xboing` binary) | Required for distro packaging. Meson handles this natively with `install_data()` and `install_dir` |
| Packaging | None | **`.deb`** (via `dpkg-buildpackage`), **AppStream metainfo XML**, **`.desktop` file**, **icon** | Ubuntu/Debian packaging. AppStream metadata gets it into GNOME Software / KDE Discover. Desktop file for application menu |
| Portability | X11-only, Linux-only | **Linux-first, but portable for free** | SDL2 abstracts platform. macOS/Windows would work with minimal effort, but not a goal |

---

## 4. Game Loop & Timing

| Aspect | Current | Proposed | Rationale |
|--------|---------|----------|-----------|
| Loop structure | `XPending` / `XPeekEvent` polling with `usleep` | **Fixed-timestep loop** with `SDL_GetTicks64()` / `SDL_Delay()` | Deterministic physics regardless of frame rate. Classic pattern: accumulate time, step physics in fixed increments, render at display rate |
| Frame timing | `sleepSync(display, ms)` = `XSync` + `usleep(ms * 400)` — tuned by hand | **Vsync** via `SDL_RENDERER_PRESENTVSYNC` + fixed logic timestep | Eliminates the hand-tuned sleep calibration. Consistent 60Hz or monitor-native. Speed levels adjust the logic timestep, not the sleep duration |
| Speed levels | `usleep` duration scaled by `userDelay` (1-9) | **Logic tick rate** scaled by speed level | Same 9 speed levels, but implemented as "how many physics ticks per frame" or "tick interval in ms" rather than sleep hacking |
| Pause | `XPeekEvent` blocking + frame counter freeze | `SDL_WaitEvent` when paused | Same behavior, cleaner implementation |

---

## 5. State Machine & Game Logic

| Aspect | Current | Proposed | Rationale |
|--------|---------|----------|-----------|
| State machine | `switch(mode)` in `handleGameStates()`, 16 modes | **Keep as-is** (or light refactor to function pointer table) | This is actually a solid, well-proven pattern. No reason to over-engineer it. A function pointer dispatch table (`void (*modeHandlers[16])(...)`) cleans up the switch without changing semantics |
| Mode transitions | Scattered across input handlers | **Centralized transition function** with entry/exit hooks | `transitionMode(oldMode, newMode)` that calls `exitMode(old)` then `enterMode(new)`. Makes reset/setup logic explicit rather than sprinkled through key handlers |
| Input dispatch | Giant `switch` on `KeySym` across 4 handler functions | **Action mapping layer** | Map SDL scancodes to game actions (`ACTION_LEFT`, `ACTION_SHOOT`, etc.) in a config table. Same bindings, but rebindable and cleaner dispatch |
| Ball physics | Intact — frame-stepped position, trig paddle bounce, region collision, quadratic ball-ball | **Keep the math, remove X11 types** | The physics are the soul of the game. Port `XRectInRegion` to pure C point-in-triangle tests. Everything else (trajectory stepping, elastic collision, paddle angle math) is already pure C |
| Block grid | `struct aBlock blocks[18][9]` with X11 Regions | **Same grid, pure C regions** | Replace `Region` (X11 type) with 4 triangles stored as 3 vertices each. `XRectInRegion` becomes ~10 lines of point-in-triangle math |

---

## 6. Data Formats

| Aspect | Current | Proposed | Rationale |
|--------|---------|----------|-----------|
| Level files | Text: title, time, 15 rows of 9 chars | **Keep identical format** | It's simple, human-editable, works perfectly. No reason to change |
| High scores | Binary `highScoreHeader` + 10x `highScoreEntry`, `htonl` byte order, `flock` locking | **SQLite** (single file) or **JSON** | Eliminates endianness issues, struct padding issues, version migration pain. SQLite gives atomic writes and locking for free. JSON is simpler if you don't need concurrent access |
| Save files | Binary `saveGameStruct` via `fread`/`fwrite` | **JSON** or **INI** | Human-readable, no struct layout dependency, trivially extensible. Save file is tiny (~10 fields) |
| Config | Compile-time defines + env vars + CLI flags | **Config file** (`$XDG_CONFIG_HOME/xboing/config.toml`) + CLI overrides | Persistent user preferences (volume, speed, controls, nickname) without recompile. TOML or INI for simplicity |

---

## 7. Code Organization

| Aspect | Current | Proposed | Rationale |
|--------|---------|----------|-----------|
| Module pattern | Static globals per `.c` file, `extern` in headers | **Keep module pattern**, but consider opaque structs | The static-globals-per-module pattern is actually clean C encapsulation. Optionally wrap in opaque `GameState*` passed to functions rather than true globals, which helps testability |
| Prototype guards | `#if NeedFunctionPrototypes` dual K&R/ANSI | **Drop K&R support entirely** | C99+ is universal. Remove all the `#if` prototype guards |
| Error handling | `ErrorMessage()` / `WarningMessage()` to stdout/stderr | **Same, plus `SDL_GetError()`** | SDL2 provides detailed error strings. Keep simple print-based error reporting |
| String handling | `sprintf`, `strcpy` with fixed buffers | **`snprintf`, `strncpy`** throughout | Harden against buffer overflows for distro security review |

---

## 8. What Stays the Same

These aspects should be preserved exactly to maintain the original feel:

- **16-mode state machine** — proven architecture, don't over-engineer
- **Ball physics math** — trigonometric paddle bounce, 4-region block collision, quadratic ball-ball collision, velocity clamping. This IS the game feel
- **Block type catalog** — all 30 types with identical point values and behaviors
- **Level file format** — keep the character-to-block mapping identical so all 80+ levels work unchanged
- **Timing feel** — 9 speed levels mapping to equivalent tick rates
- **Bonus sequence** — same 10-state tally with same score constants
- **UI flow** — Presents -> Intro -> (cycle) -> Game. Same screen order, same transition timing
- **Pixel art assets** — convert XPM to PNG but keep identical sprites (or upscale with nearest-neighbor for HiDPI)
- **Game constants** — MAX_BALLS=5, grid 18x9, paddle sizes 40/50/70, DIST_BASE=30, all scoring values
- **Level editor** — same grid constraints, mouse controls, play-test mode
- **Key bindings** — same default mappings (now rebindable via action mapping layer)

---

## 9. What Gets Eliminated

The rewrite deletes more than it adds:

- **~3,000 lines of X11 boilerplate** — visual selection, colormap management, 6 graphics contexts, 9+ sub-windows, cursor management, XLFD font loading, XPM loading, X11 Region creation
- **12 audio drivers** — the entire `audio/` directory collapses to ~50 lines of SDL2_mixer calls
- **`fork()`/pipe audio child process** — replaced by SDL2_mixer's built-in async mixing
- **`NeedFunctionPrototypes` dual-prototype system** — K&R C support removed entirely
- **Network byte order score serialization** — `htonl`/`ntohl` gone with JSON or SQLite
- **Compile-time path `#defines`** — replaced by XDG runtime path resolution
- **Hand-tuned `sleepSync` calibration** — replaced by vsync + fixed timestep
- **`version.sh` build counter script** — replaced by version in `meson.build` or git tags
- **`.au` sound files** — batch-converted to WAV or OGG once

The result would be a **smaller codebase** (estimated ~60-70% of current line count) that builds with standard SDL2 packages on any Linux distro, installs to FHS-compliant paths, and plays identically to the original.
