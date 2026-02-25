/*
 * sdl2_cursor.c — SDL2 cursor management.
 *
 * See include/sdl2_cursor.h for API documentation.
 * See ADR-008 in docs/DESIGN.md for design rationale.
 */

#include "sdl2_cursor.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal data structures
 * ========================================================================= */

struct sdl2_cursor
{
    SDL_Cursor *cursors[SDL2CUR_COUNT];
    sdl2_cursor_id_t current;
};

/*
 * Map cursor IDs to SDL2 system cursor constants.
 * SDL2CUR_NONE uses SDL_SYSTEM_CURSOR_ARROW as a placeholder — the
 * actual hiding is done via SDL_ShowCursor(SDL_DISABLE).
 * SDL2CUR_SKULL maps to SDL_SYSTEM_CURSOR_CROSSHAIR since SDL2 has
 * no pirate/skull cursor; crosshair is the closest match and is also
 * used in the editor context where skull appears.
 */
static const SDL_SystemCursor system_cursor_map[SDL2CUR_COUNT] = {
    [SDL2CUR_WAIT] = SDL_SYSTEM_CURSOR_WAIT,       [SDL2CUR_PLUS] = SDL_SYSTEM_CURSOR_CROSSHAIR,
    [SDL2CUR_NONE] = SDL_SYSTEM_CURSOR_ARROW,      [SDL2CUR_POINT] = SDL_SYSTEM_CURSOR_HAND,
    [SDL2CUR_SKULL] = SDL_SYSTEM_CURSOR_CROSSHAIR,
};

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
        ctx->cursors[i] = SDL_CreateSystemCursor(system_cursor_map[i]);
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
