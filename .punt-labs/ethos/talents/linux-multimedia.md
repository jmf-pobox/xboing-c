# Linux Multimedia

The Linux graphics and audio stack from the perspective of porting a
1990s X11 game forward to SDL2-on-modern-Linux.

## X11 / Xlib (Legacy)

- **Display model.** Connection-based, server-side rendering, GC
  (Graphics Context) state machine. Each draw uses a GC; `XCopyArea`
  is the workhorse blit.
- **Colormaps.** PseudoColor (8-bit indexed) vs TrueColor (24/32-bit
  direct). Legacy code has hacks for both — XBoing's TrueColor path
  uses `WhitePixel`/`BlackPixel` and `gccopy` instead of XOR drawing.
- **XPM pixmaps.** Client-side image format, ASCII with embedded
  palette. Compiled into binary or loaded via `XpmReadFileToPixmap`.
  Each sprite is an XPM file in `bitmaps/`.
- **Double buffering.** XBoing uses `XSetWindowAttributes.backing_store
  = Always`, not true double buffering. Tearing is possible.
- **Event model.** `XNextEvent` polling with `XPending` non-block
  check. Input + expose interleaved with logic.
- **Sub-windows.** Play area, score, level, message — separate X
  windows children of the main window. Each has its own coordinate
  origin.

## SDL2 (Target)

- **Core.** `SDL_Window` + `SDL_Renderer` + `SDL_Texture`. One window,
  hardware-accelerated renderer, GPU-resident textures.
- **SDL2_image.** PNG/JPG via `IMG_LoadTexture`. Replaces XPM.
- **SDL2_mixer.** Multi-channel audio. Replaces fork+pipe `/dev/audio`.
  WAV / OGG / FLAC support.
- **SDL2_ttf.** TrueType font rendering via `TTF_RenderText` + blit.
  Replaces `XDrawString`.
- **Pipeline.** `SDL_RenderCopy` to blit, `SDL_RenderPresent` for
  vsync'd page flip.
- **Input.** `SDL_PollEvent`, scancode/keycode for keyboard,
  motion+buttons for mouse.

## Linux Audio Stack

- **ALSA.** Kernel-level audio. Legacy code may write raw samples to
  `/dev/audio` (OSS compat) or use ALSA directly via `snd_pcm_*`.
- **PulseAudio / PipeWire.** Modern sound servers. SDL2_mixer
  abstracts which is running — the game doesn't care.
- **Format conversion.** `.au` (Sun/NeXT, mu-law or PCM) → WAV (PCM)
  via `sox` or `ffmpeg`. OGG Vorbis for any music tracks.
- **Latency.** Game sound effects need <50ms. SDL2_mixer's default
  chunk size is usually fine. Tune if needed.

## Asset Pipeline

**XPM → PNG:**

```bash
for f in bitmaps/*.xpm; do
    convert "$f" "bitmaps/$(basename "$f" .xpm).png"
done
```

Preserve `None` color → PNG alpha. Verify pixel-perfect dimensions.
Pixel art needs `SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest)` —
default linear filter blurs sprites.

**.au → WAV:**

```bash
for f in sounds/*.au; do
    sox "$f" "sounds/$(basename "$f" .au).wav"
done
```

Consider upsampling 8 kHz → 22050 / 44100 for quality. WAV is fine
for short effects; OGG for music.

## Mapping Reference

| Xlib | SDL2 | Notes |
|------|------|-------|
| `XCreateWindow` | `SDL_CreateWindow` | One window |
| `XpmReadFileToPixmap` | `IMG_LoadTexture` | XPM → PNG first |
| `XCopyArea` | `SDL_RenderCopy` | Texture blit |
| `XDrawLine` | `SDL_RenderDrawLine` | Direct |
| `XDrawString` | `TTF_RenderText` + blit | Need font file |
| `XFillRectangle` | `SDL_RenderFillRect` | Direct |
| `XNextEvent` loop | `SDL_PollEvent` loop | Similar |
| `KeyPress`/`KeyRelease` | `SDL_KEYDOWN`/`SDL_KEYUP` | Map keysyms |
| `MotionNotify` | `SDL_MOUSEMOTION` | Direct |
| fork+pipe audio | `Mix_PlayChannel` | Dramatically simpler |

## Things That Need Redesign (Not Direct Mapping)

- **Sub-windows → viewports.** XBoing's 4 child windows become one
  SDL window with logical regions. Renderer needs a viewport/camera
  abstraction.
- **GC state → per-draw state.** Xlib's GC carries colors / line
  width. SDL2 sets these per call.
- **Colormap allocation → direct color.** All colormap index
  manipulation disappears.
- **Backing store → explicit double buffer.** `SDL_RenderPresent`
  handles it.
- **`XSync`-based timing → fixed timestep.** Replace `usleep` /
  `XSync` patterns with proper delta-time or fixed-timestep loop.

## Watch-Outs

1. **Coordinate origin shift.** Sub-window-local → main-window-global.
   Audit every blit.
2. **Pixel filtering.** Default SDL2 renderer blurs pixel art. Set
   `SDL_HINT_RENDER_SCALE_QUALITY=0` or per-texture
   `SDL_ScaleModeNearest`.
3. **Audio model.** Original is fire-and-forget (fork, write samples,
   exit). SDL2_mixer is callback-based. Sound trigger feel must
   match.
4. **Vsync vs. physics.** SDL2 vsync caps frame rate at refresh.
   Game logic must not depend on frame rate (use fixed timestep).

## Reference

- `docs/SPECIFICATION.md` — sections 2 (graphics), 3 (audio), 15 (UI)
- `docs/MODERNIZATION.md` — full from/to plan
- `audio/LINUXaudio.c` — current audio driver
- `init.c` — X11 display/window/colormap setup
