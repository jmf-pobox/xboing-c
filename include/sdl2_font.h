#ifndef SDL2_FONT_H
#define SDL2_FONT_H

/*
 * sdl2_font.h — SDL2 TTF font rendering.
 *
 * Loads Liberation Sans TTF fonts (metrically equivalent to Helvetica)
 * into four enum-indexed slots matching the legacy XLFD bitmap fonts.
 * Provides draw, shadow-draw, centred-shadow-draw, and text measurement.
 *
 * Opaque context pattern: no globals, fully testable.
 * See ADR-006 in docs/DESIGN.md.
 */

#include <SDL2/SDL.h>

/* Shadow text offset in pixels (matches legacy DrawShadowText at x+2, y+2). */
#define SDL2F_SHADOW_OFFSET 2

/* Default font directory (relative to CWD, development mode). */
#define SDL2F_DEFAULT_FONT_DIR "assets/fonts"

/* Font slot identifiers — match the four legacy XLFD fonts in init.c. */
typedef enum
{
    SDL2F_FONT_TITLE = 0, /* titleFont: Bold 24pt */
    SDL2F_FONT_TEXT = 1,  /* textFont:  Regular 18pt */
    SDL2F_FONT_DATA = 2,  /* dataFont:  Bold 14pt */
    SDL2F_FONT_COPY = 3,  /* copyFont:  Regular 12pt */
    SDL2F_FONT_COUNT = 4
} sdl2_font_id_t;

/* Status codes returned by sdl2_font functions. */
typedef enum
{
    SDL2F_OK = 0,
    SDL2F_ERR_NULL_ARG,
    SDL2F_ERR_RENDERER,
    SDL2F_ERR_TTF_INIT,
    SDL2F_ERR_FONT_LOAD,
    SDL2F_ERR_RENDER_FAILED,
    SDL2F_ERR_INVALID_FONT_ID
} sdl2_font_status_t;

/* Text dimensions returned by sdl2_font_measure(). */
typedef struct
{
    int width;
    int height;
} sdl2_font_metrics_t;

/*
 * Configuration for sdl2_font_create().
 * Use sdl2_font_config_defaults() for sane starting values,
 * then override individual fields as needed.
 */
typedef struct
{
    SDL_Renderer *renderer; /* required — borrowed, not owned */
    const char *font_dir;   /* default: SDL2F_DEFAULT_FONT_DIR */
} sdl2_font_config_t;

/* Opaque font context — allocated by create, freed by destroy. */
typedef struct sdl2_font sdl2_font_t;

/*
 * Return a config struct populated with default values:
 *   renderer = NULL  (caller must set)
 *   font_dir = SDL2F_DEFAULT_FONT_DIR
 */
sdl2_font_config_t sdl2_font_config_defaults(void);

/*
 * Create a font context.  Opens all four TTF fonts from font_dir.
 * All four fonts must load successfully (all-or-nothing).
 *
 * Returns NULL on failure; *status indicates the reason.
 * The caller owns the returned context and must call sdl2_font_destroy().
 */
sdl2_font_t *sdl2_font_create(const sdl2_font_config_t *config, sdl2_font_status_t *status);

/*
 * Destroy the font context: closes all TTF_Font handles, calls
 * TTF_Quit() if this context initialized SDL2_ttf, and frees memory.
 * Safe to call with NULL.
 */
void sdl2_font_destroy(sdl2_font_t *ctx);

/*
 * Draw text at (x, y) in the given color.
 * y is the top of the text area (same convention as legacy DrawText).
 */
sdl2_font_status_t sdl2_font_draw(sdl2_font_t *ctx, sdl2_font_id_t font_id, const char *text, int x,
                                  int y, SDL_Color color);

/*
 * Draw text with a black shadow at (x+2, y+2) behind the foreground.
 * Matches legacy DrawShadowText behavior.
 */
sdl2_font_status_t sdl2_font_draw_shadow(sdl2_font_t *ctx, sdl2_font_id_t font_id, const char *text,
                                         int x, int y, SDL_Color color);

/*
 * Draw centred shadow text: centres horizontally within the given width,
 * then draws shadow + foreground.  Matches legacy DrawShadowCentredText.
 */
sdl2_font_status_t sdl2_font_draw_shadow_centred(sdl2_font_t *ctx, sdl2_font_id_t font_id,
                                                 const char *text, int y, SDL_Color color,
                                                 int width);

/*
 * Measure text dimensions without drawing.
 * On success, fills *metrics with width and height in pixels.
 */
sdl2_font_status_t sdl2_font_measure(sdl2_font_t *ctx, sdl2_font_id_t font_id, const char *text,
                                     sdl2_font_metrics_t *metrics);

/*
 * Return the line height (ascent + descent) for a font slot.
 * Returns 0 if ctx is NULL or font_id is invalid.
 */
int sdl2_font_line_height(const sdl2_font_t *ctx, sdl2_font_id_t font_id);

/* Return a human-readable string for a status code. */
const char *sdl2_font_status_string(sdl2_font_status_t status);

#endif /* SDL2_FONT_H */
