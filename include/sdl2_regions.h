#ifndef SDL2_REGIONS_H
#define SDL2_REGIONS_H

/*
 * sdl2_regions.h — Logical render regions.
 *
 * Defines named SDL_Rect regions matching the original X11 sub-window layout.
 * The legacy game uses 10 child windows of mainWindow for compositing;
 * SDL2 replaces them with clip rectangles on a single renderer.
 *
 * All coordinates are in logical pixels (575x720) matching the renderer's
 * logical size (SDL2R_LOGICAL_WIDTH x SDL2R_LOGICAL_HEIGHT from sdl2_renderer.h).
 *
 * No opaque context — just pure const data and lookup functions.
 * See ADR-009 in docs/DESIGN.md.
 */

#include <SDL2/SDL.h>

/* Region identifiers — match the legacy sub-windows in stage.c:218-371. */
typedef enum
{
    SDL2RGN_PLAY = 0,    /* Main play area (legacy playWindow) */
    SDL2RGN_SCORE,       /* Score display (legacy scoreWindow) */
    SDL2RGN_LEVEL,       /* Level display (legacy levelWindow) */
    SDL2RGN_MESSAGE,     /* Message area (legacy messWindow) */
    SDL2RGN_SPECIAL,     /* Special bonus display (legacy specialWindow) */
    SDL2RGN_TIMER,       /* Timer display (legacy timeWindow) */
    SDL2RGN_EDITOR,      /* Editor tool palette (legacy blockWindow) */
    SDL2RGN_EDITOR_TYPE, /* Editor block type (legacy typeWindow) */
    SDL2RGN_DIALOGUE,    /* Modal input dialogue (legacy inputWindow) */
    SDL2RGN_COUNT
} sdl2_region_id_t;

/*
 * Get the SDL_Rect for a named region.
 * Returns a zero rect {0,0,0,0} for out-of-range IDs.
 */
SDL_Rect sdl2_region_get(sdl2_region_id_t id);

/*
 * Get the region that contains pixel (x, y) in logical coordinates.
 * Tests gameplay regions first (play, score, level, message, special, timer),
 * then editor regions.  Returns SDL2RGN_COUNT if no region contains the point.
 *
 * Useful for mapping mouse clicks to their target region.
 */
sdl2_region_id_t sdl2_region_hit_test(int x, int y);

/* Return a human-readable name for a region ID. */
const char *sdl2_region_name(sdl2_region_id_t id);

#endif /* SDL2_REGIONS_H */
