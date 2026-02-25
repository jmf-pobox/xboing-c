#ifndef SDL2_RENDERER_H
#define SDL2_RENDERER_H

/*
 * sdl2_renderer.h — SDL2 window and renderer lifecycle.
 *
 * Creates a single SDL2 window with a GPU-accelerated 2D renderer.
 * The game renders at a fixed logical resolution (575x720) and
 * SDL_RenderSetLogicalSize() handles letterboxing/scaling to the
 * physical window size.
 *
 * Opaque context pattern: no globals, fully testable.
 * See ADR-004 in docs/DESIGN.md.
 */

#include <stdbool.h>

#include <SDL2/SDL.h>

/* Logical resolution — matches the original XBoing window (stage.c:240-242). */
#define SDL2R_LOGICAL_WIDTH 575
#define SDL2R_LOGICAL_HEIGHT 720

/* Default physical window scale factor (2x). */
#define SDL2R_DEFAULT_SCALE 2

/*
 * Configuration for sdl2_renderer_create().
 * Use sdl2_renderer_config_defaults() to get sane starting values,
 * then override individual fields as needed.
 */
typedef struct
{
    int logical_width;
    int logical_height;
    int scale;
    bool fullscreen;
    bool vsync;
    const char *title;
} sdl2_renderer_config_t;

/* Opaque renderer context — allocated by create, freed by destroy. */
typedef struct sdl2_renderer sdl2_renderer_t;

/*
 * Return a config struct populated with default values:
 *   logical_width  = SDL2R_LOGICAL_WIDTH
 *   logical_height = SDL2R_LOGICAL_HEIGHT
 *   scale          = SDL2R_DEFAULT_SCALE
 *   fullscreen     = false
 *   vsync          = true
 *   title          = "XBoing"
 */
sdl2_renderer_config_t sdl2_renderer_config_defaults(void);

/*
 * Create a window and renderer.  Returns NULL on failure.
 * The caller owns the returned context and must call sdl2_renderer_destroy().
 */
sdl2_renderer_t *sdl2_renderer_create(const sdl2_renderer_config_t *config);

/*
 * Destroy the renderer, window, and (if we own it) the SDL video subsystem.
 * Safe to call with NULL.
 */
void sdl2_renderer_destroy(sdl2_renderer_t *ctx);

/* Clear the render target to black. */
void sdl2_renderer_clear(sdl2_renderer_t *ctx);

/* Present the current render target to the screen. */
void sdl2_renderer_present(sdl2_renderer_t *ctx);

/*
 * Toggle between windowed and fullscreen-desktop mode.
 * Returns the new fullscreen state.
 */
bool sdl2_renderer_toggle_fullscreen(sdl2_renderer_t *ctx);

/* Query the current fullscreen state. */
bool sdl2_renderer_is_fullscreen(const sdl2_renderer_t *ctx);

/* Access the underlying SDL_Renderer (for drawing operations). */
SDL_Renderer *sdl2_renderer_get(const sdl2_renderer_t *ctx);

/* Access the underlying SDL_Window. */
SDL_Window *sdl2_renderer_get_window(const sdl2_renderer_t *ctx);

/* Get the logical (game-coordinate) size. */
void sdl2_renderer_get_logical_size(const sdl2_renderer_t *ctx, int *w, int *h);

/* Get the current physical window size. */
void sdl2_renderer_get_window_size(const sdl2_renderer_t *ctx, int *w, int *h);

#endif /* SDL2_RENDERER_H */
