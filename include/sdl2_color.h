#ifndef SDL2_COLOR_H
#define SDL2_COLOR_H

/*
 * sdl2_color.h — SDL2 RGBA color system.
 *
 * Replaces the legacy PseudoColor colormap with direct RGBA colors.
 * Named colors match the X11 color database values used by the original
 * ColourNameToPixel() calls in init.c.  Gradient arrays match the
 * legacy reds[7] and greens[7] used for border glow and animations.
 *
 * No opaque context — just pure data and lookup functions.
 * See ADR-007 in docs/DESIGN.md.
 */

#include <stdbool.h>

#include <SDL2/SDL.h>

/* Number of steps in each gradient array (matches legacy reds[7], greens[7]). */
#define SDL2C_GRADIENT_STEPS 7

/* Named color identifiers — match the legacy globals in init.c:123. */
typedef enum
{
    SDL2C_RED = 0,
    SDL2C_TAN,
    SDL2C_YELLOW,
    SDL2C_GREEN,
    SDL2C_WHITE,
    SDL2C_BLACK,
    SDL2C_BLUE,
    SDL2C_PURPLE,
    SDL2C_COLOR_COUNT
} sdl2_color_id_t;

/*
 * Get a named color by enum ID.
 * Returns black {0,0,0,255} for out-of-range IDs.
 */
SDL_Color sdl2_color_get(sdl2_color_id_t id);

/*
 * Get a red gradient color by index (0 = brightest, 6 = darkest).
 * Matches legacy reds[7] from init.c: #f00, #d00, #b00, #900, #700, #500, #300.
 * Returns black for out-of-range indices.
 */
SDL_Color sdl2_color_red_gradient(int index);

/*
 * Get a green gradient color by index (0 = brightest, 6 = darkest).
 * Matches legacy greens[7] from init.c: #0f0, #0d0, #0b0, #090, #070, #050, #030.
 * Returns black for out-of-range indices.
 */
SDL_Color sdl2_color_green_gradient(int index);

/*
 * Look up a color by X11 color name string.
 * Supports the 8 named colors used by the game: "red", "tan", "yellow",
 * "green", "white", "black", "blue", "purple".
 * Also supports 3-digit hex shorthand: "#f00", "#0f0", etc.
 *
 * Returns true and fills *color on success; returns false on unknown name.
 * Case-insensitive for named colors; hex is case-insensitive for A-F.
 */
bool sdl2_color_by_name(const char *name, SDL_Color *color);

/*
 * Return a human-readable name for a color ID.
 * Returns "unknown" for out-of-range IDs.
 */
const char *sdl2_color_name(sdl2_color_id_t id);

#endif /* SDL2_COLOR_H */
