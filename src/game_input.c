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

#include <SDL2/SDL.h>

#include "ball_system.h"
#include "block_system.h"
#include "gun_system.h"
#include "level_system.h"
#include "message_system.h"
#include "paddle_system.h"
#include "paths.h"
#include "savegame_io.h"
#include "score_system.h"
#include "sdl2_audio.h"
#include "sdl2_input.h"
#include "sdl2_loop.h"
#include "sdl2_renderer.h"
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

            /* K key: dual-use — activate waiting ball first, then shoot.
             * original/main.c:490-494: ActivateWaitingBall first; if no
             * ball is waiting (returns -1), shootBullet. */
            if (sdl2_input_just_pressed(ctx->input, SDL2I_SHOOT))
            {
                ball_system_env_t benv = game_callbacks_ball_env(ctx);
                if (ball_system_activate_waiting(ctx->ball, &benv) == -1)
                {
                    gun_system_env_t genv = game_callbacks_gun_env(ctx);
                    gun_system_shoot(ctx->gun, &genv);
                }
            }

            /* Mouse click: same dual-use as K — original/main.c:357-366.
             * Uses edge-trigger to fire once per click, not once per frame. */
            if (sdl2_input_mouse_just_pressed(ctx->input, SDL_BUTTON_LEFT) ||
                sdl2_input_mouse_just_pressed(ctx->input, SDL_BUTTON_MIDDLE) ||
                sdl2_input_mouse_just_pressed(ctx->input, SDL_BUTTON_RIGHT))
            {
                ball_system_env_t benv = game_callbacks_ball_env(ctx);
                if (ball_system_activate_waiting(ctx->ball, &benv) == -1)
                {
                    gun_system_env_t genv = game_callbacks_gun_env(ctx);
                    gun_system_shoot(ctx->gun, &genv);
                }
            }

            /* P key pauses */
            if (sdl2_input_just_pressed(ctx->input, SDL2I_PAUSE))
                sdl2_state_transition(ctx->state, SDL2ST_PAUSE);

            /* Z saves, X loads */
            if (sdl2_input_just_pressed(ctx->input, SDL2I_SAVE))
                input_save_game(ctx);
            if (sdl2_input_just_pressed(ctx->input, SDL2I_LOAD))
                input_load_game(ctx);

            /* E key enters editor */
            if (sdl2_input_just_pressed(ctx->input, SDL2I_ENTER_EDITOR))
                sdl2_state_transition(ctx->state, SDL2ST_EDIT);

            /* Escape returns to editor if play-testing */
            if (sdl2_input_just_pressed(ctx->input, SDL2I_ABORT))
            {
                sdl2_state_mode_t prev = sdl2_state_previous(ctx->state);
                if (prev == SDL2ST_EDIT)
                    sdl2_state_transition(ctx->state, SDL2ST_EDIT);
            }
            break;

        default:
            /* E key enters editor from any attract screen */
            if (sdl2_input_just_pressed(ctx->input, SDL2I_ENTER_EDITOR))
                sdl2_state_transition(ctx->state, SDL2ST_EDIT);
            break;
    }
}

/* =========================================================================
 * Global input — mode-independent keys
 *
 * Matches original/main.c handleMiscKeys (lines 814-872) +
 * handleSpeedKeys (lines 741-803).  Called from ALL modes via
 * default-case fallthrough in the original.
 *
 * Called once per visual frame from game_main.c, NOT from stub_tick
 * (which fires multiple times per frame at high speed levels,
 * causing toggle keys to multi-fire and net to zero).
 * ========================================================================= */

static const char *warp_message(int speed)
{
    switch (speed)
    {
        case 1:
            return "Warp 1 - Slow";
        case 5:
            return "Warp 5 - Medium";
        case 9:
            return "Warp 9 - Fast";
        default:
        {
            static char buf[16];
            snprintf(buf, sizeof(buf), "Warp %d", speed);
            return buf;
        }
    }
}

void game_input_global(game_ctx_t *ctx)
{
    int frame = (int)sdl2_state_frame(ctx->state);
    sdl2_state_mode_t mode = sdl2_state_current(ctx->state);

    /* Attract-mode allowlist — matches original/main.c:898-905.
     * S key (SFX) and 1-9 (speed) only fire from these modes. */
    int is_attract = (mode == SDL2ST_INTRO || mode == SDL2ST_INSTRUCT || mode == SDL2ST_DEMO ||
                      mode == SDL2ST_PREVIEW || mode == SDL2ST_KEYS || mode == SDL2ST_KEYSEDIT ||
                      mode == SDL2ST_HIGHSCORE || mode == SDL2ST_BONUS);

    /* S: toggle visual SFX — original/main.c:639 handleIntroKeys */
    if (is_attract && sdl2_input_just_pressed(ctx->input, SDL2I_TOGGLE_SFX))
    {
        int was = sfx_system_get_enabled(ctx->sfx);
        sfx_system_set_enabled(ctx->sfx, !was);
        message_system_set(ctx->message, was ? "- SFX OFF -" : "- SFX ON -", 1, frame);
    }

    /* 1-9: set warp speed — original/main.c:741 handleSpeedKeys */
    if (is_attract)
    {
        for (int s = 1; s <= 9; s++)
        {
            sdl2_input_action_t action = (sdl2_input_action_t)(SDL2I_SPEED_1 + s - 1);
            if (sdl2_input_just_pressed(ctx->input, action))
            {
                sdl2_loop_set_speed(ctx->loop, s);
                message_system_set(ctx->message, warp_message(s), 1, frame);
                if (ctx->audio)
                    sdl2_audio_play(ctx->audio, "tone");
                break;
            }
        }
    }

    /* +/-: volume up/down — original/main.c:822 */
    if (ctx->audio)
    {
        if (sdl2_input_just_pressed(ctx->input, SDL2I_VOLUME_UP))
        {
            int vol = sdl2_audio_volume_up(ctx->audio);
            char str[32];
            snprintf(str, sizeof(str), "Maximum volume: %d%%", vol);
            message_system_set(ctx->message, str, 1, frame);
        }
        else if (sdl2_input_just_pressed(ctx->input, SDL2I_VOLUME_DOWN))
        {
            int vol = sdl2_audio_volume_down(ctx->audio);
            char str[32];
            snprintf(str, sizeof(str), "Maximum volume: %d%%", vol);
            message_system_set(ctx->message, str, 1, frame);
        }
    }

    /* I: fullscreen toggle — original/main.c:853 (XIconifyWindow).
     * Modernized as fullscreen toggle since SDL2 window management
     * differs from X11 iconify semantics. See ADR in docs/DESIGN.md. */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_ICONIFY))
        sdl2_renderer_toggle_fullscreen(ctx->renderer);

    /* G: toggle keyboard/mouse control — original/main.c:859 */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_TOGGLE_CONTROL))
    {
        ctx->config.use_keys = !ctx->config.use_keys;
        message_system_set(ctx->message, ctx->config.use_keys ? "Control: Keys" : "Control: Mouse",
                           1, frame);
    }

    /* Q: quit — original/main.c:864.  Skip in editor mode (original
     * routes editor to handleEditorKeys, not handleMiscKeys).
     * Original shows YesNoDialogue confirmation first; we quit
     * immediately as a first pass.  Dialogue integration deferred. */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_QUIT) &&
        sdl2_state_current(ctx->state) != SDL2ST_EDIT)
    {
        SDL_Event quit_event = {0};
        quit_event.type = SDL_QUIT;
        SDL_PushEvent(&quit_event);
    }

    /* C: cycle attract screens — original/main.c:554-605.
     * Each mode transitions to the next in the attract sequence. */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_CYCLE))
    {
        sdl2_state_mode_t next = SDL2ST_NONE;
        switch (mode)
        {
            case SDL2ST_INTRO:
                next = SDL2ST_INSTRUCT;
                break;
            case SDL2ST_INSTRUCT:
                next = SDL2ST_DEMO;
                break;
            case SDL2ST_DEMO:
                next = SDL2ST_KEYS;
                break;
            case SDL2ST_KEYS:
                next = SDL2ST_KEYSEDIT;
                break;
            case SDL2ST_KEYSEDIT:
                next = SDL2ST_HIGHSCORE;
                break;
            case SDL2ST_HIGHSCORE:
                next = SDL2ST_PREVIEW;
                break;
            case SDL2ST_PREVIEW:
                next = SDL2ST_INTRO;
                break;
            default:
                break;
        }
        if (next != SDL2ST_NONE)
            sdl2_state_transition(ctx->state, next);
    }

    /* A: audio toggle — deferred, no sdl2_audio enable/disable API */
}
