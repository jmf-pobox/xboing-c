#ifndef SDL2_CURSOR_H
#define SDL2_CURSOR_H

/*
 * sdl2_cursor.h — SDL2 cursor management.
 *
 * Replaces legacy X11 cursor handling (XCreateFontCursor, XCreatePixmapCursor,
 * XDefineCursor) with SDL2 system cursors.  Five cursor types match the
 * original CURSOR_* constants in init.h.
 *
 * Opaque context pattern: cursors are created once and cached.
 * See ADR-008 in docs/DESIGN.md.
 */

#include <SDL2/SDL.h>

/* Cursor identifiers — correspond to the legacy CURSOR_* defines in init.h.
 * Note: legacy values are 1-based (CURSOR_WAIT=1..CURSOR_SKULL=5);
 * these are 0-based for array indexing.  Migration code must map. */
typedef enum
{
    SDL2CUR_WAIT = 0,  /* Watch/hourglass (legacy XC_watch) */
    SDL2CUR_PLUS = 1,  /* Crosshair (legacy XC_plus) */
    SDL2CUR_NONE = 2,  /* Invisible/hidden cursor */
    SDL2CUR_POINT = 3, /* Hand/pointer (legacy XC_hand2) */
    SDL2CUR_SKULL = 4, /* Crosshair, skull substitute (legacy XC_pirate) */
    SDL2CUR_COUNT = 5
} sdl2_cursor_id_t;

/* Status codes returned by sdl2_cursor functions. */
typedef enum
{
    SDL2CUR_OK = 0,
    SDL2CUR_ERR_NULL_ARG,
    SDL2CUR_ERR_CREATE_FAILED,
    SDL2CUR_ERR_INVALID_ID
} sdl2_cursor_status_t;

/* Opaque cursor context — allocated by create, freed by destroy. */
typedef struct sdl2_cursor sdl2_cursor_t;

/*
 * Create a cursor context.  Attempts to pre-create all 5 cursor types.
 * System cursor creation is best-effort: some video drivers (e.g. dummy)
 * don't support it.  Missing cursors are tracked as NULL and set() will
 * gracefully skip SDL_SetCursor for them while still tracking the active ID.
 *
 * Returns NULL only on memory allocation failure; *status indicates the reason.
 * The caller owns the returned context and must call sdl2_cursor_destroy().
 */
sdl2_cursor_t *sdl2_cursor_create(sdl2_cursor_status_t *status);

/*
 * Destroy the cursor context: frees all SDL_Cursor handles and memory.
 * Safe to call with NULL.
 */
void sdl2_cursor_destroy(sdl2_cursor_t *ctx);

/*
 * Set the active cursor.  Equivalent to legacy ChangePointer().
 * For SDL2CUR_NONE, hides the cursor entirely via SDL_ShowCursor(SDL_DISABLE).
 * For all others, shows the cursor and sets the appropriate shape.
 */
sdl2_cursor_status_t sdl2_cursor_set(sdl2_cursor_t *ctx, sdl2_cursor_id_t id);

/*
 * Query the currently active cursor ID.
 * Returns SDL2CUR_COUNT if ctx is NULL.
 */
sdl2_cursor_id_t sdl2_cursor_current(const sdl2_cursor_t *ctx);

/* Return a human-readable string for a cursor ID. */
const char *sdl2_cursor_name(sdl2_cursor_id_t id);

/* Return a human-readable string for a status code. */
const char *sdl2_cursor_status_string(sdl2_cursor_status_t status);

#endif /* SDL2_CURSOR_H */
