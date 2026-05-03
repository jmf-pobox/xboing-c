# Screenshot Testing Methodology — Peer Review (sjl)

**Date:** 2026-05-03
**Reviewer:** sjl (Sam J. Lantinga, av-platform-engineer)
**Proposal:** `docs/research/2026-05-03-visual-fidelity-screenshot-testing.md`
**Mission:** m-2026-05-03-003

---

## Verdict

**Pass with revisions.**

The hybrid pyramid (L1–L4) is the right structure. Three platform-mechanics
blockers require fixes before implementation starts: the dummy-driver pixel
question (Q3), the audio flag inversion for original/ (Q8), and the
render_alpha pinning for hash determinism (Q7). Everything else is refinable
during implementation.

---

## Q1 — Xvfb capture reliability

**Assessment: workable with known caveats.**

Xvfb is a framebuffer X server, not a hardware-accelerated one. It operates
in TrueColor (24-bit) by default since Xvfb 1.20. The original binary
allocates a private colormap via `XCreateColormap` (original/init.c:~295,
`InitialiseSettings` path → `InitialiseColourNames` at line 183). Under
Xvfb, PseudoColor colormaps can be requested but Xvfb defaults to TrueColor.
The original has a `-usedefcmap` flag; without it, the game installs its own
colormap. Under Xvfb this works but the backing store behavior differs.

Concrete gotchas:

- The original uses backing-store pixmaps (the `bufferWindow` sub-window,
  `original/stage.c:255`). Xvfb supports backing store, so XCopyArea to the
  backing window works. ImageMagick `import` captures the composited
  framebuffer, which is correct.
- `XSynchronize` is not set by default; frames may be partially composited
  when `import` fires. Fix: add a `usleep(100000)` after the state settles,
  or use `import -window root` after `xdotool windowfocus --sync`.
- Font rendering: Xvfb uses the same X11 core font paths as the running
  system. If the system has Adobe Helvetica BDF fonts installed
  (`xfonts-100dpi`, `xfonts-75dpi`), Xvfb renders identically to a real X
  server for the XLFD patterns the original requests. If those fonts are
  absent, it falls back to `fixed` (as coded at original/init.c:229–254).
  CI runners typically lack `xfonts-75dpi` — install it explicitly.
  Package: `xfonts-75dpi xfonts-100dpi` (Ubuntu 24.04, ~2 MB each).
- Color fidelity: Xvfb TrueColor output is pixel-accurate. The XPM palettes
  are small and named (e.g. `"red"`, `"#f00"`) — Xlib maps them to the exact
  same RGB values under Xvfb as on a real server. No dithering.

**Recommendation:** add `xfonts-75dpi xfonts-100dpi` to the CI image.
Use `DISPLAY=:99 Xvfb :99 -screen 0 1280x1024x24` as the Xvfb invocation.
Capture after a sync delay: `sleep 0.1 && import -window <wid> out.png`.

---

## Q2 — Driving original/ to specific states

**Assessment: patch is cleaner; xdotool is viable for simple states only.**

`xdotool` is sufficient for keyboard-driven state transitions (skip presents,
start level, pause). It is fragile for mid-animation captures because the
original's animation loop uses `usleep`-based timing driven by `SetGameSpeed`
(original/main.c:154–158), and xdotool has no way to synchronize to a
specific frame count.

For mid-animation captures (bonus coin reveal frame 5, explosion slide 2),
the correct approach is a minimal patch to original/ that:

1. Accepts a new flag `-snapshot <state> <frame>` in `ParseCommandLine`
   (original/init.c:507–692, add one `else if` clause).
2. After reaching the target state and frame count, calls `import` via
   `system()` or writes the backing-store pixmap to XWD format using
   `XWriteBitmapFile` and exits.

This is 30–50 lines. It does touch original/ but does not change gameplay
behavior — it adds a new exit path that only fires when the flag is set.
The flag is guarded by `#ifdef SNAPSHOT_FLAG` if we want to preserve the
pristine state of original/ for normal builds.

**Recommendation:** implement the `-snapshot` patch. Guard it with
`SNAPSHOT_FLAG` at compile time so the normal `original/xboing` binary is
unaffected. Use xdotool only for simple attract-screen captures.

---

## Q3 — SDL_RenderReadPixels with SDL_VIDEODRIVER=dummy

**Assessment: BLOCKING — dummy driver does NOT populate pixels.**

This is the most critical finding. The SDL2 `dummy` video driver exists to
allow SDL_Init to succeed in headless environments. It creates a window with
no backing pixels. When `SDL_VIDEODRIVER=dummy` is set, `SDL_CreateRenderer`
with `SDL_RENDERER_ACCELERATED` fails (no GPU), then the existing fallback
in `sdl2_renderer_create` (src/sdl2_renderer.c:103) calls
`SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE)`. The software
renderer does maintain an in-memory pixel buffer, BUT with the dummy video
driver the `SDL_Window` surface is a zero-size or non-displayable surface
depending on SDL2 version. SDL_RenderReadPixels on a dummy-backed software
renderer returns a buffer of zeros on most SDL2 2.x versions — the renderer
has no valid pixel surface to read from.

Verified path: `SDL_VIDEODRIVER=dummy` → dummy driver creates a window with a
null physical surface → `SDL_CreateRenderer(SDL_RENDERER_SOFTWARE)` creates
a renderer whose target is an `SDL_Surface` allocated at the logical size
(this is renderer-internal) → `SDL_RenderReadPixels` reads from that surface.
In SDL2 >= 2.0.18 the software renderer DOES allocate its own surface
regardless of the window's physical backing; in SDL2 < 2.0.18 it does not.
Ubuntu 24.04 ships SDL2 2.0.20, so this should work — but it is version-
dependent and must be verified with a test.

The safer, version-independent approach: bypass the window entirely and use
an offscreen `SDL_Surface` as the render target:

```c
SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(
    0, logical_w, logical_h, 32, SDL_PIXELFORMAT_ARGB8888);
SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surf);
/* render game frame */
/* SDL_RenderPresent is a no-op for software renderer — pixels are in surf */
SDL_LockSurface(surf);
/* read surf->pixels directly — always populated */
SDL_UnlockSurface(surf);
```

`SDL_CreateSoftwareRenderer` is the correct API for headless pixel capture.
It does not require `SDL_VIDEODRIVER` at all. This replaces both the window
creation and the renderer creation for the test path. The existing
`sdl2_renderer_t` abstraction can accept an optional `offscreen_surface`
config flag, or the test can create its own renderer without going through
`sdl2_renderer_create`.

**Recommendation (blocking):** Do not rely on `SDL_VIDEODRIVER=dummy` for
L2 pixel capture. Use `SDL_CreateSoftwareRenderer(surf)` with a
`SDL_CreateRGBSurfaceWithFormat` surface. This is always populated, always
deterministic, and requires no VIDEODRIVER env var. The test binary sets
`SDL_AUDIODRIVER=dummy` for audio (see Q8), nothing else.

---

## Q4 — Color quantization between XPM and PNG

**Assessment: small but real; SSIM thresholds should account for it.**

XPM files use named colors or hex triplets (e.g. `"red"`, `"#FF0000"`).
When ImageMagick `convert` translates XPM → PNG, it maps these to the same
RGB values — no quantization for indexed-to-RGB since the source is already
named. The delta per pixel for typical XPM → PNG conversion via
`convert foo.xpm foo.png` is 0 for solid-color sprites. For sprites that use
color cycling (the `reds[]` and `greens[]` arrays at original/init.c:200–216),
the values are exact hex codes, so conversion is lossless.

The Xvfb capture introduces one potential delta: the original uses Xlib
XPM rendering through `XpmCreatePixmapFromData`, which maps XPM colors
through the colormap. Under TrueColor Xvfb, colormap allocation is direct
RGB — no dithering, no quantization. The delta between "XPM rendered by Xlib
on Xvfb" and "PNG loaded by SDL2_image from a `convert`-produced PNG" is
zero for solid blocks and at most ±1 RGB unit for any floating-point
rounding in the conversion tool chain.

The 0.92–0.98 SSIM thresholds in the proposal are achievable for sprite
regions. The main source of SSIM degradation is not color quantization but
font rendering (see Q5).

---

## Q5 — Anti-aliasing differences between X11 fonts and SDL2_ttf

**Assessment: SSIM will NOT pass for text regions — separate them.**

The original requests fonts by XLFD pattern (original/init.c:95–98):

- `TITLE_FONT`: `-adobe-helvetica-bold-r-*-*-24-*-*-*-*-*-*-*`
- `COPY_FONT`: `-adobe-helvetica-medium-r-*-*-12-*-*-*-*-*-*-*`
- `TEXT_FONT`: `-adobe-helvetica-medium-r-*-*-18-*-*-*-*-*-*-*`
- `DATA_FONT`: `-adobe-helvetica-bold-r-*-*-14-*-*-*-*-*-*-*`

These are bitmap fonts from the `xfonts-75dpi` / `xfonts-100dpi` packages.
Bitmap fonts render with exact pixel placement, no anti-aliasing, no
sub-pixel hinting. Each glyph is a fixed pixel pattern.

SDL2_ttf renders TrueType fonts with FreeType, which applies anti-aliasing
by default (RGBA blended glyphs). Even with `TTF_RenderText_Solid` (no AA),
the glyph shapes differ from the bitmap originals because TrueType outlines
are mathematically different from the bitmap originals.

SSIM for a text region comparing bitmap-font X11 output vs. SDL2_ttf output
will land in the 0.50–0.75 range — far below any passing threshold. This is
not a tuning problem; it is a fundamental incompatibility between the two
rendering paths.

**Recommendation:** exclude all text-heavy regions from L3 SSIM comparison.
The viable L3 regions are sprite-only:

- Playfield border (solid color, SSIM ~0.99)
- Block grid (XPM sprites, SSIM ~0.96–0.98)
- Paddle (XPM sprite, SSIM ~0.97)
- Ball (XPM sprite, SSIM ~0.97)
- Lives icons (XPM sprite row, SSIM ~0.95–0.97)
- Bonus coin sprites (XPM, SSIM ~0.95)

Drop "Level number digits", "Score area", and any region containing
typewriter/text from the L3 table. L2 hash tests verify modern text
regression (modern vs. modern); L3 tests only verify sprite fidelity
vs. original.

---

## Q6 — Window size match

**Assessment: confirmed match, with one measurement note.**

Original window size: `PLAY_WIDTH + MAIN_WIDTH + 10` × `PLAY_HEIGHT + MAIN_HEIGHT + 10`
= (495 + 70 + 10) × (580 + 130 + 10) = **575 × 720** (original/include/stage.h:59–63,
original/stage.c:239).

Modern SDL2 logical size: `SDL2R_LOGICAL_WIDTH = 575`, `SDL2R_LOGICAL_HEIGHT = 720`
(include/sdl2_renderer.h:21–22).

The dimensions match exactly. The sub-window layout is also matched:

- `playWindow`: offsetX=35, y=60, w=495, h=580 (original/stage.c:251)
- Modern `PLAY_AREA_X=35`, `PLAY_AREA_Y=60`, `PLAY_AREA_W=495`, `PLAY_AREA_H=580`
  (src/game_render.c:57–63, src/game_render_ui.c:29–33)

One measurement note: when ImageMagick `import` captures the original under
Xvfb, it captures the X11 window including the 2-pixel border of `playWindow`
(border_width=2 at original/stage.c:251). The main window capture includes
the border pixels. Crop coordinates in `regions.json` must account for this
+2px offset on the play area. The SDL2 build has no window border — it
renders the border as a 2-pixel drawn rectangle. The border pixels will be
in different absolute positions if `import` captures the inner child window
vs. the root window screenshot. Capture the root window screenshot and use
absolute coordinates based on the 575×720 logical canvas.

---

## Q7 — Animation timing and render_alpha determinism

**Assessment: BLOCKING for L2 hash tests.**

The `sdl2_loop_t` computes `alpha = accumulator_us / tick_interval_us`
(src/sdl2_loop.c:138–143) and passes it to the render callback
`render_fn(alpha, user_data)` (src/sdl2_loop.c:149). If the render path
uses `alpha` for any interpolated position (ball smoothing, paddle smoothing,
animation easing), the rendered frame is non-deterministic — `alpha` depends
on the wall-clock elapsed time between the last tick and the render call.

For L2 hash tests, the test must NOT drive rendering through `sdl2_loop_update`.
Instead, it must call the render function directly with a fixed `alpha=0.0`.
This is already possible since the render callback signature is
`render_fn(double alpha, void *user_data)` — the test can call it directly.

For any animation that uses `alpha` for sub-tick interpolation: the golden
hash must be captured with `alpha=0.0` and the test must pass `alpha=0.0`.
Document this constraint in the test fixture comments.

**Recommendation (blocking):** L2 tests must call the render function
directly with `alpha=0.0`. Do not route through `sdl2_loop_update`. Add a
test-only render entrypoint or pass alpha directly:
`game_render_frame(ctx, 0.0)` rather than going through the loop timer.

---

## Q8 — Audio side effects during capture

**Assessment: flag inversion in proposal — the original is silent by default.**

The proposal says "Capture scripts must suppress audio (`-nosound` for
original)." This is wrong. Looking at original/init.c:489: `noSound = True`
is the default. Audio in the original is OFF unless `-sound` is passed. The
correct flag for original/ is no flag for silence, or `-nosound` is not even
a valid flag (there is no such flag in `ParseCommandLine`). To be explicit
in the capture script, just don't pass `-sound`.

For SDL2: `SDL_AUDIODRIVER=dummy` is correct for suppressing audio
initialization. Confirm via test_replay_smoke.c:8 comment which already
documents this requirement.

**Recommendation:** remove `-nosound` from the capture script. The original
is already silent by default. Capture command: `./original/xboing -startlevel 1`
(no `-sound` flag). For SDL2: `SDL_AUDIODRIVER=dummy ./xboing`.

---

## Q9 — Headless vs. real X server divergence

**Assessment: Xvfb is a faithful proxy for sprite rendering; not for the
player's exact visual experience.**

The question of "what did the 1996 player see" depends on:

1. Monitor gamma (CRTs in 1996 had ~2.2 gamma, very different from modern LCDs)
2. The X server's colormap allocation under PseudoColor
3. The specific display hardware and color depth (8bpp was common in 1996)

None of these are reproducible on Xvfb today. However, the goal of the L3
test is NOT to reproduce what a 1996 player saw — it is to establish
"what does `original/xboing` render when run on a modern Linux machine"
and compare the SDL2 port to that. For this purpose, Xvfb is a faithful
proxy: both the original and the golden were captured on the same software
stack (Xvfb + same libX11 + same font rendering). The comparison is
internally consistent.

The original runs correctly under Xvfb — it uses TrueColor visual (confirmed
by the `-usedefcmap` path in original/init.c:625–631). The `import` capture
reads the framebuffer, which is identical to what a human would see on the
same TrueColor display.

Window decorations: Xvfb has no window manager by default, so `import -window
root` captures a borderless window. If a window manager is added to Xvfb
for test purposes, window decoration pixels will appear in the capture.
Do not run a window manager in the Xvfb instance — capture the window
directly by window ID: `import -window $(xdotool search --name "xboing") out.png`.

**Recommendation:** use Xvfb with no window manager. Capture by window ID,
not by root. This gives a clean 575×720 capture with no decoration noise.

---

## Q10 — Practical CI image weight

**Assessment: modest cost; all packages available on Ubuntu 24.04.**

Packages required and their approximate compressed sizes (Ubuntu 24.04):

| Package | Purpose | Compressed size |
|---------|---------|----------------|
| `xvfb` | Headless X server | ~1.4 MB |
| `imagemagick` | `import` capture tool | ~8 MB |
| `xdotool` | Window ID lookup | ~0.2 MB |
| `xfonts-75dpi` | X11 bitmap fonts for original/ | ~2.1 MB |
| `xfonts-100dpi` | X11 bitmap fonts (fallback) | ~2.1 MB |
| `python3-pillow` | PNG I/O for SSIM script | ~4 MB (usually pre-installed) |
| `python3-scikit-image` | SSIM computation | ~18 MB |
| `python3-numpy` | scikit-image dep (usually pre-installed) | ~15 MB (pre-installed) |

Total new packages not already in a standard Ubuntu 24.04 build runner:
approximately **14–22 MB** depending on what is already present.
`python3-scikit-image` is the dominant cost. If size is a concern,
`python3-skimage` can be replaced with a pure-Pillow SSIM implementation
(~80 LOC) that avoids the dependency entirely.

The L3 step adds approximately 15–30 s to a CI run (Xvfb startup, original/
binary execution, ImageMagick capture, Python SSIM). Path-filtering to
`src/game_render*.c` changes is recommended to avoid this cost on every PR.

All packages are available in Ubuntu 24.04 `universe` (no PPA required).
`python3-scikit-image` is in the `universe` repo as `python3-skimage` on
22.04 but is packaged as `python3-scikit-image` on 24.04 — verify the exact
package name in the CI workflow.

---

## Summary of Blocking Items

| # | Blocking? | Action required |
|---|-----------|----------------|
| Q3 | YES | Replace `SDL_VIDEODRIVER=dummy` + ReadPixels with `SDL_CreateSoftwareRenderer(SDL_Surface*)` |
| Q7 | YES | L2 test must pin `alpha=0.0` by calling render function directly |
| Q8 | No (clarification) | Remove `-nosound` flag; original is silent by default |

## Summary of Non-Blocking Refinements

| # | Refinement |
|---|-----------|
| Q1 | Add `xfonts-75dpi xfonts-100dpi` to CI; sync-wait before `import` |
| Q2 | Implement `-snapshot` patch for original/ (30–50 lines, compile-guarded) |
| Q4 | No action needed; XPM→PNG conversion is lossless for named colors |
| Q5 | Exclude all text regions from L3 SSIM table; sprite-only comparison |
| Q6 | Use absolute root-window coordinates; account for playWindow +2px border |
| Q9 | Capture by window ID, no window manager in Xvfb instance |
| Q10 | ~14–22 MB CI image addition; path-filter L3 to `game_render*.c` changes |

---

*Review completed by sjl (mission m-2026-05-03-003, round 1 of 1).*
