/*
 * game_input.c -- Input dispatch for SDL2-based XBoing.
 *
 * Translates sdl2_input action queries into game module calls.
 * Called once per frame after event processing, before the game tick.
 *
 * The input layer reads action state (pressed, just_pressed) and
 * dispatches to the appropriate game modules.  It does NOT modify
 * game state directly — modules handle their own state changes.
 */

#include "game_input.h"

#include "paddle_system.h"
#include "sdl2_input.h"
#include "sdl2_state.h"

/* =========================================================================
 * Paddle input — keyboard direction + mouse position
 * ========================================================================= */

static void input_update_paddle(game_ctx_t *ctx)
{
    int direction = PADDLE_DIR_NONE;

    if (sdl2_input_pressed(ctx->input, SDL2I_LEFT))
        direction = PADDLE_DIR_LEFT;
    else if (sdl2_input_pressed(ctx->input, SDL2I_RIGHT))
        direction = PADDLE_DIR_RIGHT;

    /* Mouse position for mouse-mode paddle control.
     * The paddle module handles both keyboard and mouse input —
     * keyboard takes priority when direction != NONE. */
    int mx = 0, my = 0;
    sdl2_input_get_mouse(ctx->input, &mx, &my);

    paddle_system_update(ctx->paddle, direction, mx);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void game_input_update(game_ctx_t *ctx)
{
    sdl2_state_mode_t mode = sdl2_state_current(ctx->state);

    switch (mode)
    {
        case SDL2ST_GAME:
            input_update_paddle(ctx);
            break;

        default:
            break;
    }
}
