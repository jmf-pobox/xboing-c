/*
 * sdl2_state.c — Game state machine with function pointer dispatch.
 *
 * See include/sdl2_state.h for API documentation.
 * See ADR-012 in docs/DESIGN.md for design rationale.
 */

#include "sdl2_state.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal data structures
 * ========================================================================= */

struct sdl2_state
{
    /* Handler table: one entry per mode. */
    sdl2_state_mode_def_t handlers[SDL2ST_COUNT];

    /* Current and previous modes. */
    sdl2_state_mode_t current;
    sdl2_state_mode_t previous;

    /* Saved mode for dialogue push/pop. */
    sdl2_state_mode_t saved;
    bool in_dialogue;

    /* Frame counter — incremented on update except pause/dialogue. */
    unsigned long frame;

    /* User data passed to all callbacks. */
    void *user_data;
};

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static bool is_valid_mode(sdl2_state_mode_t mode)
{
    return mode >= 0 && mode < SDL2ST_COUNT;
}

static void call_handler(sdl2_state_handler_fn fn, sdl2_state_mode_t mode, void *user_data)
{
    if (fn != NULL)
    {
        fn(mode, user_data);
    }
}

/* =========================================================================
 * Public API — Lifecycle
 * ========================================================================= */

sdl2_state_t *sdl2_state_create(void *user_data, sdl2_state_status_t *status)
{
    sdl2_state_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
    {
        if (status != NULL)
        {
            *status = SDL2ST_ERR_ALLOC_FAILED;
        }
        return NULL;
    }

    ctx->current = SDL2ST_NONE;
    ctx->previous = SDL2ST_NONE;
    ctx->saved = SDL2ST_NONE;
    ctx->in_dialogue = false;
    ctx->frame = 0;
    ctx->user_data = user_data;

    if (status != NULL)
    {
        *status = SDL2ST_OK;
    }
    return ctx;
}

void sdl2_state_destroy(sdl2_state_t *ctx)
{
    free(ctx);
}

/* =========================================================================
 * Public API — Mode registration
 * ========================================================================= */

sdl2_state_status_t sdl2_state_register(sdl2_state_t *ctx, sdl2_state_mode_t mode,
                                        const sdl2_state_mode_def_t *def)
{
    if (ctx == NULL)
    {
        return SDL2ST_ERR_NULL_ARG;
    }
    if (!is_valid_mode(mode))
    {
        return SDL2ST_ERR_INVALID_MODE;
    }

    if (def != NULL)
    {
        ctx->handlers[mode] = *def;
    }
    else
    {
        memset(&ctx->handlers[mode], 0, sizeof(ctx->handlers[mode]));
    }
    return SDL2ST_OK;
}

/* =========================================================================
 * Public API — Transitions
 * ========================================================================= */

sdl2_state_status_t sdl2_state_transition(sdl2_state_t *ctx, sdl2_state_mode_t new_mode)
{
    if (ctx == NULL)
    {
        return SDL2ST_ERR_NULL_ARG;
    }
    if (!is_valid_mode(new_mode))
    {
        return SDL2ST_ERR_INVALID_MODE;
    }

    /* No-op if already in this mode. */
    if (ctx->current == new_mode)
    {
        return SDL2ST_OK;
    }

    /* Call exit on old mode. */
    call_handler(ctx->handlers[ctx->current].on_exit, ctx->current, ctx->user_data);

    /* Update state. */
    ctx->previous = ctx->current;
    ctx->current = new_mode;

    /* Call enter on new mode. */
    call_handler(ctx->handlers[new_mode].on_enter, new_mode, ctx->user_data);

    return SDL2ST_OK;
}

sdl2_state_status_t sdl2_state_push_dialogue(sdl2_state_t *ctx)
{
    if (ctx == NULL)
    {
        return SDL2ST_ERR_NULL_ARG;
    }
    if (ctx->in_dialogue)
    {
        return SDL2ST_ERR_ALREADY_IN_DIALOGUE;
    }

    /* Save current mode. */
    ctx->saved = ctx->current;
    ctx->in_dialogue = true;

    /* Call exit on current mode. */
    call_handler(ctx->handlers[ctx->current].on_exit, ctx->current, ctx->user_data);

    /* Update state. */
    ctx->previous = ctx->current;
    ctx->current = SDL2ST_DIALOGUE;

    /* Call enter on dialogue. */
    call_handler(ctx->handlers[SDL2ST_DIALOGUE].on_enter, SDL2ST_DIALOGUE, ctx->user_data);

    return SDL2ST_OK;
}

sdl2_state_status_t sdl2_state_pop_dialogue(sdl2_state_t *ctx)
{
    if (ctx == NULL)
    {
        return SDL2ST_ERR_NULL_ARG;
    }
    if (!ctx->in_dialogue)
    {
        return SDL2ST_ERR_NOT_IN_DIALOGUE;
    }

    sdl2_state_mode_t restore = ctx->saved;

    /* Call exit on dialogue. */
    call_handler(ctx->handlers[SDL2ST_DIALOGUE].on_exit, SDL2ST_DIALOGUE, ctx->user_data);

    /* Update state. */
    ctx->previous = SDL2ST_DIALOGUE;
    ctx->current = restore;
    ctx->saved = SDL2ST_NONE;
    ctx->in_dialogue = false;

    /* Call enter on restored mode. */
    call_handler(ctx->handlers[restore].on_enter, restore, ctx->user_data);

    return SDL2ST_OK;
}

/* =========================================================================
 * Public API — Per-frame update
 * ========================================================================= */

void sdl2_state_update(sdl2_state_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    /* Increment frame counter unless paused or in dialogue.
     * Matches legacy: main.c:1283 — frame++ except MODE_DIALOGUE. */
    if (ctx->current != SDL2ST_PAUSE && ctx->current != SDL2ST_DIALOGUE)
    {
        ctx->frame++;
    }

    /* Dispatch the update handler. */
    call_handler(ctx->handlers[ctx->current].on_update, ctx->current, ctx->user_data);
}

/* =========================================================================
 * Public API — Queries
 * ========================================================================= */

sdl2_state_mode_t sdl2_state_current(const sdl2_state_t *ctx)
{
    if (ctx == NULL)
    {
        return SDL2ST_NONE;
    }
    return ctx->current;
}

sdl2_state_mode_t sdl2_state_previous(const sdl2_state_t *ctx)
{
    if (ctx == NULL)
    {
        return SDL2ST_NONE;
    }
    return ctx->previous;
}

sdl2_state_mode_t sdl2_state_saved_mode(const sdl2_state_t *ctx)
{
    if (ctx == NULL || !ctx->in_dialogue)
    {
        return SDL2ST_NONE;
    }
    return ctx->saved;
}

unsigned long sdl2_state_frame(const sdl2_state_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    return ctx->frame;
}

bool sdl2_state_is_paused(const sdl2_state_t *ctx)
{
    if (ctx == NULL)
    {
        return false;
    }
    return ctx->current == SDL2ST_PAUSE;
}

bool sdl2_state_is_dialogue(const sdl2_state_t *ctx)
{
    if (ctx == NULL)
    {
        return false;
    }
    return ctx->in_dialogue;
}

bool sdl2_state_is_gameplay(const sdl2_state_t *ctx)
{
    if (ctx == NULL)
    {
        return false;
    }
    switch (ctx->current)
    {
        case SDL2ST_GAME:
        case SDL2ST_PAUSE:
        case SDL2ST_BALL_WAIT:
        case SDL2ST_WAIT:
            return true;
        default:
            return false;
    }
}

/* =========================================================================
 * Public API — Utility
 * ========================================================================= */

const char *sdl2_state_mode_name(sdl2_state_mode_t mode)
{
    switch (mode)
    {
        case SDL2ST_NONE:
            return "none";
        case SDL2ST_HIGHSCORE:
            return "highscore";
        case SDL2ST_INTRO:
            return "intro";
        case SDL2ST_GAME:
            return "game";
        case SDL2ST_PAUSE:
            return "pause";
        case SDL2ST_BALL_WAIT:
            return "ball_wait";
        case SDL2ST_WAIT:
            return "wait";
        case SDL2ST_BONUS:
            return "bonus";
        case SDL2ST_INSTRUCT:
            return "instruct";
        case SDL2ST_KEYS:
            return "keys";
        case SDL2ST_PRESENTS:
            return "presents";
        case SDL2ST_DEMO:
            return "demo";
        case SDL2ST_PREVIEW:
            return "preview";
        case SDL2ST_DIALOGUE:
            return "dialogue";
        case SDL2ST_EDIT:
            return "edit";
        case SDL2ST_KEYSEDIT:
            return "keysedit";
        case SDL2ST_COUNT:
            break;
    }
    return "unknown";
}

const char *sdl2_state_status_string(sdl2_state_status_t status)
{
    switch (status)
    {
        case SDL2ST_OK:
            return "OK";
        case SDL2ST_ERR_NULL_ARG:
            return "NULL argument";
        case SDL2ST_ERR_INVALID_MODE:
            return "invalid mode ID";
        case SDL2ST_ERR_ALLOC_FAILED:
            return "allocation failed";
        case SDL2ST_ERR_ALREADY_IN_DIALOGUE:
            return "already in dialogue mode";
        case SDL2ST_ERR_NOT_IN_DIALOGUE:
            return "not in dialogue mode";
    }
    return "unknown status";
}
