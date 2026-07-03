/*
 * sdl2_cursor.c — SDL2 cursor management.
 *
 * See include/sdl2_cursor.h for API documentation.
 * See ADR-008 in docs/DESIGN.md for design rationale.
 */

#include "sdl2_cursor.h"

#include <stdlib.h>
#include <string.h>

#include "sdl2_cursor_bitmaps.h" /* generated XC_plus / XC_pirate bitmaps */

/* =========================================================================
 * Internal data structures
 * ========================================================================= */

struct sdl2_cursor
{
    SDL_Cursor *cursors[SDL2CUR_COUNT];
    sdl2_cursor_id_t current;
};

/*
 * Fallback system cursors, used when a custom bitmap cursor can't be built.
 * SDL2CUR_NONE uses SDL_SYSTEM_CURSOR_ARROW as a placeholder — the actual
 * hiding is done via SDL_ShowCursor(SDL_DISABLE).  PLUS and SKULL are built
 * from the real X11 cursor-font bitmaps (see create_custom_cursor); the
 * crosshair here is only their fallback when SDL_CreateCursor fails.
 */
static const SDL_SystemCursor system_cursor_map[SDL2CUR_COUNT] = {
    [SDL2CUR_WAIT] = SDL_SYSTEM_CURSOR_WAIT,       [SDL2CUR_PLUS] = SDL_SYSTEM_CURSOR_CROSSHAIR,
    [SDL2CUR_NONE] = SDL_SYSTEM_CURSOR_ARROW,      [SDL2CUR_POINT] = SDL_SYSTEM_CURSOR_HAND,
    [SDL2CUR_SKULL] = SDL_SYSTEM_CURSOR_CROSSHAIR,
};

/*
 * Build the exact original editor cursor from the X11 cursor-font bitmaps:
 * XC_plus (draw) and XC_pirate (skull, erase).  Returns NULL for ids with
 * no custom bitmap (the caller then falls back to the system cursor).
 */
static SDL_Cursor *create_custom_cursor(sdl2_cursor_id_t id)
{
    switch (id)
    {
        case SDL2CUR_PLUS:
            return SDL_CreateCursor(cursor_plus_data, cursor_plus_mask, CURSOR_PLUS_W,
                                    CURSOR_PLUS_H, CURSOR_PLUS_HOTX, CURSOR_PLUS_HOTY);
        case SDL2CUR_SKULL:
            return SDL_CreateCursor(cursor_pirate_data, cursor_pirate_mask, CURSOR_PIRATE_W,
                                    CURSOR_PIRATE_H, CURSOR_PIRATE_HOTX, CURSOR_PIRATE_HOTY);
        default:
            return NULL;
    }
}

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static int is_valid_id(sdl2_cursor_id_t id)
{
    return id >= 0 && id < SDL2CUR_COUNT;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

sdl2_cursor_t *sdl2_cursor_create(sdl2_cursor_status_t *status)
{
    sdl2_cursor_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
    {
        if (status != NULL)
        {
            *status = SDL2CUR_ERR_CREATE_FAILED;
        }
        return NULL;
    }

    ctx->current = SDL2CUR_COUNT; /* no cursor set yet */

    /*
     * Pre-create all system cursors (best-effort).
     * Some video drivers (e.g. dummy) don't support system cursors.
     * NULL handles are acceptable — set() gracefully skips SDL_SetCursor
     * for missing cursors while still tracking the current ID.
     */
    for (int i = 0; i < SDL2CUR_COUNT; i++)
    {
        /* Custom X11-font bitmap for PLUS/SKULL; system cursor otherwise or
         * as a fallback if the bitmap cursor can't be created. */
        ctx->cursors[i] = create_custom_cursor((sdl2_cursor_id_t)i);
        if (ctx->cursors[i] == NULL)
        {
            ctx->cursors[i] = SDL_CreateSystemCursor(system_cursor_map[i]);
        }
        if (ctx->cursors[i] == NULL)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "sdl2_cursor: cursor %d (SDL_SystemCursor %d) unavailable: %s", i,
                        (int)system_cursor_map[i], SDL_GetError());
        }
    }

    if (status != NULL)
    {
        *status = SDL2CUR_OK;
    }
    return ctx;
}

void sdl2_cursor_destroy(sdl2_cursor_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    for (int i = 0; i < SDL2CUR_COUNT; i++)
    {
        if (ctx->cursors[i] != NULL)
        {
            SDL_FreeCursor(ctx->cursors[i]);
        }
    }

    free(ctx);
}

sdl2_cursor_status_t sdl2_cursor_set(sdl2_cursor_t *ctx, sdl2_cursor_id_t id)
{
    if (ctx == NULL)
    {
        return SDL2CUR_ERR_NULL_ARG;
    }
    if (!is_valid_id(id))
    {
        return SDL2CUR_ERR_INVALID_ID;
    }

    if (id == SDL2CUR_NONE)
    {
        SDL_ShowCursor(SDL_DISABLE);
    }
    else
    {
        if (ctx->cursors[id] != NULL)
        {
            SDL_SetCursor(ctx->cursors[id]);
        }
        SDL_ShowCursor(SDL_ENABLE);
    }

    ctx->current = id;
    return SDL2CUR_OK;
}

sdl2_cursor_id_t sdl2_cursor_current(const sdl2_cursor_t *ctx)
{
    if (ctx == NULL)
    {
        return SDL2CUR_COUNT;
    }
    return ctx->current;
}

const char *sdl2_cursor_name(sdl2_cursor_id_t id)
{
    switch (id)
    {
        case SDL2CUR_WAIT:
            return "wait";
        case SDL2CUR_PLUS:
            return "plus";
        case SDL2CUR_NONE:
            return "none";
        case SDL2CUR_POINT:
            return "point";
        case SDL2CUR_SKULL:
            return "skull";
        case SDL2CUR_COUNT:
            break;
    }
    return "unknown";
}

const char *sdl2_cursor_status_string(sdl2_cursor_status_t status)
{
    switch (status)
    {
        case SDL2CUR_OK:
            return "OK";
        case SDL2CUR_ERR_NULL_ARG:
            return "NULL argument";
        case SDL2CUR_ERR_CREATE_FAILED:
            return "cursor creation failed";
        case SDL2CUR_ERR_INVALID_ID:
            return "invalid cursor ID";
    }
    return "unknown status";
}
