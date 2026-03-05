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
#include "game_rules.h"

#include <stdio.h>

#include "ball_system.h"
#include "block_system.h"
#include "eyedude_system.h"
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
#include "sfx_system.h"
#include "special_system.h"

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
 * Debug cheat codes — only active when ctx->debug_mode is true
 *
 * All cheats require Shift held:
 *   Shift+N  — skip level (clear blocks → bonus → next)
 *   Shift+D  — kill ball (force ball death)
 *   Shift+G  — game over (set lives=0, kill ball)
 *   Shift+L  — add 5 lives
 *   Shift+A  — max ammo (20 bullets)
 *   Shift+E  — spawn EyeDude
 *   Shift+S  — trigger screen shake
 *   Shift+1..9 — jump to level 10/20/30/.../90
 * ========================================================================= */

static void input_debug_cheats(game_ctx_t *ctx)
{
    if (!ctx->debug_mode)
        return;
    if (!sdl2_input_shift_held(ctx->input))
        return;

    int frame = (int)sdl2_state_frame(ctx->state);

    /* Shift+N: skip level */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_NEXT_LEVEL))
    {
        /* Clear all blocks to trigger level completion on next rules check */
        for (int r = 0; r < 18; r++)
            for (int c = 0; c < 9; c++)
                block_system_clear(ctx->block, r, c);
        message_system_set(ctx->message, "[DEBUG] Level skipped", 1, frame);
    }

    /* Shift+D: kill ball */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_KILL_BALL))
    {
        ball_system_env_t env = game_callbacks_ball_env(ctx);
        for (int i = 0; i < 5; i++)
        {
            if (ball_system_get_state(ctx->ball, i) == BALL_ACTIVE ||
                ball_system_get_state(ctx->ball, i) == BALL_READY)
            {
                ball_system_change_mode(ctx->ball, &env, i, BALL_POP);
            }
        }
        message_system_set(ctx->message, "[DEBUG] Ball killed", 1, frame);
    }

    /* Shift+G: game over */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_TOGGLE_CONTROL))
    {
        ctx->lives_left = 0;
        ball_system_env_t env = game_callbacks_ball_env(ctx);
        for (int i = 0; i < 5; i++)
        {
            if (ball_system_get_state(ctx->ball, i) != BALL_NONE)
                ball_system_change_mode(ctx->ball, &env, i, BALL_POP);
        }
        message_system_set(ctx->message, "[DEBUG] Game over triggered", 1, frame);
    }

    /* Shift+L: add 5 lives */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_LOAD))
    {
        ctx->lives_left += 5;
        char buf[64];
        snprintf(buf, sizeof(buf), "[DEBUG] Lives: %d", ctx->lives_left);
        message_system_set(ctx->message, buf, 1, frame);
    }

    /* Shift+A: max ammo */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_TOGGLE_AUDIO))
    {
        gun_system_set_ammo(ctx->gun, GUN_MAX_AMMO);
        gun_system_set_unlimited(ctx->gun, 1);
        message_system_set(ctx->message, "[DEBUG] Max ammo + unlimited", 1, frame);
    }

    /* Shift+E: spawn EyeDude */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_ENTER_EDITOR))
    {
        eyedude_system_set_state(ctx->eyedude, EYEDUDE_STATE_RESET);
        message_system_set(ctx->message, "[DEBUG] EyeDude spawned", 1, frame);
    }

    /* Shift+S: trigger shake */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_TOGGLE_SFX))
    {
        sfx_system_set_mode(ctx->sfx, SFX_MODE_SHAKE);
        sfx_system_set_end_frame(ctx->sfx, frame + 100);
        message_system_set(ctx->message, "[DEBUG] Shake!", 1, frame);
    }

    /* Shift+1..9: jump to level 10/20/30/.../90 */
    for (int s = 1; s <= 9; s++)
    {
        sdl2_input_action_t action = (sdl2_input_action_t)(SDL2I_SPEED_1 + s - 1);
        if (sdl2_input_just_pressed(ctx->input, action))
        {
            ctx->level_number = s * 10;
            game_rules_next_level(ctx);
            sdl2_state_transition(ctx->state, SDL2ST_GAME);
            char buf[64];
            snprintf(buf, sizeof(buf), "[DEBUG] Jump to level %d", ctx->level_number);
            message_system_set(ctx->message, buf, 1, frame);
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

            /* Speed keys 1-9 (only without Shift — Shift+1..9 are debug cheats) */
            if (!sdl2_input_shift_held(ctx->input))
                input_check_speed(ctx);

            /* Debug cheat codes (Shift+key, --debug only) */
            input_debug_cheats(ctx);
            break;

        default:
            break;
    }
}
