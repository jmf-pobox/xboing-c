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

/*
 * Confine (grab) the mouse pointer to the window, or release it.
 * Wraps SDL_SetWindowMouseGrab — the modern equivalent of the original's
 * XGrabPointer(confine_to=window) for the -grab option.  No-op if ctx or
 * the window is NULL.
 */
void sdl2_renderer_set_mouse_grab(sdl2_renderer_t *ctx, bool grab);

/* Query whether the mouse pointer is currently grabbed to the window. */
bool sdl2_renderer_is_mouse_grabbed(const sdl2_renderer_t *ctx);

/* Access the underlying SDL_Renderer (for drawing operations). */
SDL_Renderer *sdl2_renderer_get(const sdl2_renderer_t *ctx);

/* Access the underlying SDL_Window. */
SDL_Window *sdl2_renderer_get_window(const sdl2_renderer_t *ctx);

/* Get the logical (game-coordinate) size. */
void sdl2_renderer_get_logical_size(const sdl2_renderer_t *ctx, int *w, int *h);

/* Get the current physical window size. */
void sdl2_renderer_get_window_size(const sdl2_renderer_t *ctx, int *w, int *h);

/*
 * Change the renderer's logical width while preserving the on-screen
 * pixel size of existing logical content (height is never touched).
 *
 * Windowed mode: grows/shrinks the physical window width in lockstep,
 * derived from the window's current height, so the logical->physical
 * scale factor is unchanged.  Existing content (e.g. the play area)
 * does not resize or shift; only new horizontal logical space is
 * exposed (or reclaimed).  The window is not repositioned or
 * recentred — same top-left-anchored growth as the original's
 * ResizeMainWindow (original/editor.c:162-164).
 *
 * Fullscreen mode: the physical window already spans the display and
 * cannot grow, so only the logical width changes.  SDL's own uniform
 * letterbox scaling then shrinks the whole canvas to fit the new,
 * wider aspect ratio.  This is a deliberate, documented fidelity
 * trade-off unique to fullscreen editor use — see
 * docs/specs/2026-07-11-editor-window-width.md.
 *
 * No-op if new_logical_width already equals the current logical
 * width (idempotent — safe to call on every mode-enter).
 *
 * Returns 0 on success, -1 on NULL ctx or non-positive width.
 */
int sdl2_renderer_set_logical_width(sdl2_renderer_t *ctx, int new_logical_width);

/* Save the current framebuffer to a BMP file.  Returns 0 on success. */
int sdl2_renderer_save_screenshot(const sdl2_renderer_t *ctx, const char *path);

#endif /* SDL2_RENDERER_H */
