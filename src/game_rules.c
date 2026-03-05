/*
 * game_rules.c -- Game rule logic for SDL2-based XBoing.
 *
 * Checks per-frame game conditions: level completion, ball death,
 * extra lives, and game over transitions.
 *
 * Replicates the logic from legacy level.c:CheckGameRules() and
 * level.c:DeadBall().
 */

#include "game_rules.h"

#include <stdio.h>

#include "ball_system.h"
#include "block_system.h"
#include "game_callbacks.h"
#include "gun_system.h"
#include "level_system.h"
#include "message_system.h"
#include "paddle_system.h"
#include "paths.h"
#include "score_system.h"
#include "sdl2_audio.h"
#include "sdl2_state.h"
#include "special_system.h"

/* Play area constants */
#define GAME_PLAY_WIDTH 495
#define GAME_PLAY_HEIGHT 580
#define GAME_COL_WIDTH (GAME_PLAY_WIDTH / 9)
#define GAME_ROW_HEIGHT (GAME_PLAY_HEIGHT / 18)

/* =========================================================================
 * Level advancement
 * ========================================================================= */

void game_rules_next_level(game_ctx_t *ctx)
{
    ctx->level_number++;

    int file_num = level_system_wrap_number(ctx->level_number);
    char filename[32];
    snprintf(filename, sizeof(filename), "level%02d.data", file_num);

    char level_path[PATHS_MAX_PATH];
    if (paths_level_file(&ctx->paths, filename, level_path, sizeof(level_path)) != PATHS_OK)
    {
        fprintf(stderr, "Warning: could not find level file: %s\n", filename);
        return;
    }

    /* Clear state for new level */
    block_system_clear_all(ctx->block);
    ball_system_clear_all(ctx->ball);
    gun_system_clear(ctx->gun);
    special_system_turn_off(ctx->special);

    /* Load the level */
    level_system_advance_background(ctx->level);
    level_system_load_file(ctx->level, level_path);

    /* Set level title as default message */
    const char *title = level_system_get_title(ctx->level);
    if (title)
        message_system_set_default(ctx->message, title);

    /* Reset paddle to center, place new ball */
    paddle_system_reset(ctx->paddle);
    paddle_system_set_size(ctx->paddle, PADDLE_SIZE_HUGE);

    ball_system_env_t env = game_callbacks_ball_env(ctx);
    ball_system_reset_start(ctx->ball, &env);

    /* Give ammo for the new level */
    gun_system_set_ammo(ctx->gun, GUN_AMMO_PER_LEVEL);

    /* Reset bonus spawning state */
    ctx->bonus_block_active = false;
    ctx->next_bonus_frame = 0;

    if (ctx->audio)
        sdl2_audio_play(ctx->audio, "applause");
}

/* =========================================================================
 * Ball death
 * ========================================================================= */

void game_rules_ball_died(game_ctx_t *ctx)
{
    /* If there are still active balls, do nothing — multiball */
    if (ball_system_get_active_count(ctx->ball) > 0)
        return;

    /* No balls left */
    ctx->lives_left--;

    if (ctx->lives_left <= 0)
    {
        /* Game over */
        ctx->game_active = false;
        if (ctx->audio)
            sdl2_audio_play(ctx->audio, "game_over");
        message_system_set(ctx->message, "GAME OVER", 0, 0);

        /* Transition to high score screen (will be wired in phase 3) */
        sdl2_state_transition(ctx->state, SDL2ST_HIGHSCORE);
        return;
    }

    /* Still have lives — reset ball on paddle */
    if (ctx->audio)
        sdl2_audio_play(ctx->audio, "balldead");

    ball_system_env_t env = game_callbacks_ball_env(ctx);
    ball_system_reset_start(ctx->ball, &env);
}

/* =========================================================================
 * Per-frame rule check
 * ========================================================================= */

void game_rules_check(game_ctx_t *ctx)
{
    /* Level completion: no required blocks remain */
    if (!block_system_still_active(ctx->block))
    {
        game_rules_next_level(ctx);
        return;
    }

    /* Extra life check is handled by score_system's on_extra_life callback */
}
