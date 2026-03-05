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
#include "game_callbacks.h"

#include <stdio.h>

#include "ball_system.h"
#include "block_system.h"
#include "gun_system.h"
#include "level_system.h"
#include "message_system.h"
#include "paddle_system.h"
#include "paths.h"
#include "savegame_io.h"
#include "score_system.h"
#include "sdl2_input.h"
#include "sdl2_loop.h"
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
 * Ball launch — space bar or mouse click fires the ball
 * ========================================================================= */

static void input_launch_ball(game_ctx_t *ctx)
{
    /* Space activates a waiting ball (legacy also uses mouse click —
     * that will be wired when mouse edge triggers are available) */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_START))
    {
        ball_system_env_t env = game_callbacks_ball_env(ctx);
        ball_system_activate_waiting(ctx->ball, &env);
    }
}

/* =========================================================================
 * Save/load game state — Z saves, X loads
 * ========================================================================= */

static void input_save_game(game_ctx_t *ctx)
{
    char save_path[PATHS_MAX_PATH];
    if (paths_save_info(&ctx->paths, save_path, sizeof(save_path)) != PATHS_OK)
        return;

    savegame_data_t data = {
        .score = score_system_get(ctx->score),
        .level = (unsigned long)ctx->level_number,
        .lives_left = ctx->lives_left,
        .start_level = ctx->start_level,
        .paddle_size = paddle_system_get_size(ctx->paddle),
        .num_bullets = gun_system_get_ammo(ctx->gun),
    };

    if (savegame_io_write(save_path, &data) == SAVEGAME_IO_OK)
    {
        int frame = (int)sdl2_state_frame(ctx->state);
        message_system_set(ctx->message, "Game Saved!", 1, frame);
    }
}

static void input_load_game(game_ctx_t *ctx)
{
    char save_path[PATHS_MAX_PATH];
    if (paths_save_info(&ctx->paths, save_path, sizeof(save_path)) != PATHS_OK)
        return;

    savegame_data_t data;
    if (savegame_io_read(save_path, &data) != SAVEGAME_IO_OK)
    {
        int frame = (int)sdl2_state_frame(ctx->state);
        message_system_set(ctx->message, "No saved game found", 1, frame);
        return;
    }

    /* Restore game state */
    score_system_set(ctx->score, data.score);
    ctx->level_number = (int)data.level;
    ctx->lives_left = data.lives_left;
    ctx->start_level = data.start_level;
    paddle_system_set_size(ctx->paddle, data.paddle_size <= 40   ? PADDLE_SIZE_SMALL
                                        : data.paddle_size <= 50 ? PADDLE_SIZE_MEDIUM
                                                                 : PADDLE_SIZE_HUGE);
    gun_system_set_ammo(ctx->gun, data.num_bullets);

    /* Reload the level */
    int file_num = level_system_wrap_number(ctx->level_number);
    char filename[32];
    snprintf(filename, sizeof(filename), "level%02d.data", file_num);

    char level_path[PATHS_MAX_PATH];
    block_system_clear_all(ctx->block);
    if (paths_level_file(&ctx->paths, filename, level_path, sizeof(level_path)) == PATHS_OK)
        level_system_load_file(ctx->level, level_path);

    /* Reset ball on paddle */
    ball_system_clear_all(ctx->ball);
    ball_system_env_t env = game_callbacks_ball_env(ctx);
    ball_system_reset_start(ctx->ball, &env);

    int frame = (int)sdl2_state_frame(ctx->state);
    message_system_set(ctx->message, "Game Restored!", 1, frame);
}

/* =========================================================================
 * Speed keys — 1-9 change game speed
 * ========================================================================= */

static void input_check_speed(game_ctx_t *ctx)
{
    for (int s = 1; s <= 9; s++)
    {
        sdl2_input_action_t action = (sdl2_input_action_t)(SDL2I_SPEED_1 + s - 1);
        if (sdl2_input_just_pressed(ctx->input, action))
        {
            sdl2_loop_set_speed(ctx->loop, s);
            break;
        }
    }
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
            input_launch_ball(ctx);

            /* K key shoots a bullet */
            if (sdl2_input_just_pressed(ctx->input, SDL2I_SHOOT))
            {
                gun_system_env_t genv = game_callbacks_gun_env(ctx);
                gun_system_shoot(ctx->gun, &genv);
            }

            /* P key pauses */
            if (sdl2_input_just_pressed(ctx->input, SDL2I_PAUSE))
                sdl2_state_transition(ctx->state, SDL2ST_PAUSE);

            /* Z saves, X loads */
            if (sdl2_input_just_pressed(ctx->input, SDL2I_SAVE))
                input_save_game(ctx);
            if (sdl2_input_just_pressed(ctx->input, SDL2I_LOAD))
                input_load_game(ctx);

            /* Speed keys 1-9 */
            input_check_speed(ctx);
            break;

        default:
            break;
    }
}
