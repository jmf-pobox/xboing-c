# Reading the Original XBoing Source (1996)

This directory contains the unmodified 1996 XBoing source by Justin C. Kibell.
Read it before designing any modernization — understand what was done before
deciding it was done wrong.

## Module Mapping

| Original | Modern | Purpose |
|----------|--------|---------|
| `main.c` | `game_main.c`, `game_input.c`, `game_modes.c` | Event loop, input, state machine |
| `init.c` | `game_init.c`, `sdl2_renderer.c` | Setup, window creation |
| `ball.c` | `src/ball_system.c` | Ball physics, collision |
| `blocks.c` | `src/block_system.c` | Block grid, types, explosion |
| `paddle.c` | `src/paddle_system.c` | Paddle control, sizes |
| `level.c` | `src/level_system.c` | Level file loading |
| `stage.c` | `src/sdl2_renderer.c`, `game_render.c` | Window/play area management |
| `highscore.c` | `src/highscore_system.c`, `highscore_io.c` | Score display, file I/O |
| `editor.c` | `src/editor_system.c` | Level editor |
| `dialogue.c` | `src/dialogue_system.c` | Modal input dialogues |
| `sfx.c` | `src/sfx_system.c` | Visual effects (shake, fade, glow) |
| `intro.c` | `src/intro_system.c` | Title/instruction screens |
| `keys.c` | `src/keys_system.c` | Key binding display screens |
| `presents.c` | `src/presents_system.c` | Splash/credits screen |
| `demo.c` | `src/demo_system.c` | Demo/preview screens |
| `misc.c` | Various | Helper functions (DrawShadowCentredText, YesNoDialogue, etc.) |

## Key Constants

| Constant | Value | File | Modern equivalent |
|----------|-------|------|-------------------|
| `PADDLE_VEL` | 10 | `include/paddle.h:69` | `PADDLE_VELOCITY` (4, adjusted for per-frame update) |
| `PADDLE_ANIMATE_DELAY` | 5 | `include/main.h:57` | Removed (update every frame now) |
| `DIST_BASE` | 30 | `include/paddle.h:68` | `DIST_BALL_OF_PADDLE` |
| `MAX_TILTS` | 3 | `include/main.h:85` | `GAME_MAX_TILTS` in `game_context.h` |
| `FAST_SPEED` | 5 | `include/main.h:81` | `SDL2L_DEFAULT_SPEED` |
| `MAX_BALLS` | 5 | `include/ball.h` | `MAX_BALLS` in `ball_types.h` |
| `PLAY_WIDTH` | 495 | `include/stage.h` | `PLAY_AREA_W` |
| `PLAY_HEIGHT` | 580 | `include/stage.h` | `PLAY_AREA_H` |
| `MAIN_WIDTH` | 70 | `include/stage.h` | Side panel width |
| `MAIN_HEIGHT` | 130 | `include/stage.h` | Top/bottom panel height |
| `DIALOGUE_WIDTH` | 380 | `include/dialogue.h:59` | `DIALOGUE_WIDTH` in `dialogue_system.h` |
| `DIALOGUE_HEIGHT` | 120 | `include/dialogue.h:60` | `DIALOGUE_HEIGHT` in `dialogue_system.h` |
| `BIRTH_FRAME_RATE` | 5 | `ball.c` | `BIRTH_FRAME_RATE` in `ball_types.h` |
| `BALL_FRAME_RATE` | 5 | `ball.c` | `BALL_FRAME_RATE` in `ball_types.h` |

## Coordinate Systems

- **mainWindow**: the full game window (575×720 in modern coordinates)
- **playWindow**: the play area subwindow at (35, 60), 495×580
- **inputWindow**: the dialogue overlay at (92, 295), 380×120
- All original coordinates are relative to their parent window
- Modern SDL2 uses a single renderer with logical coordinates — add the parent window offset

## Fonts

| Original variable | Font spec | Modern ID |
|-------------------|-----------|-----------|
| `titleFont` | Helvetica Bold 24pt | `SDL2F_FONT_TITLE` |
| `textFont` | Helvetica Medium 18pt | `SDL2F_FONT_TEXT` |
| `copyFont` | Helvetica Medium 12pt | `SDL2F_FONT_COPY` |

## Common Patterns

- **DrawShadowCentredText**: draws text centered within a window width, shadow at (+2, +2) in black → modern: `sdl2_font_draw_shadow_centred` or manual measure+position
- **RenderShape**: draws an XPM pixmap with mask → modern: `sdl2_texture_get` + `SDL_RenderCopy`
- **DrawStageBackground**: tiles a 32×32 background pixmap → modern: tile loop in `render_play_area_frame`
- **XPM pixmaps**: loaded via `XpmCreatePixmapFromData` → modern: PNG files in `assets/images/`, loaded via `sdl2_texture`
- **Event loop**: `XNextEvent` / `XPeekEvent` → modern: `SDL_PollEvent` in `game_main.c`

## Citation Rule

When adopting a solution from the original, cite `original/<file>.c:<line>` in the code or commit message. This makes the provenance auditable.
