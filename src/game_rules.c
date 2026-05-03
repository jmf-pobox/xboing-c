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
#include <stdlib.h>

#include "ball_system.h"
#include "block_system.h"
#include "block_types.h"
#include "eyedude_system.h"
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

/* Legacy bonus spawning constants */
#define BONUS_SEED 2000

/* =========================================================================
 * Find a random empty cell in the block grid
 * ========================================================================= */

static int find_random_empty_cell(const block_system_t *block, int *out_row, int *out_col)
{
    /* Try random positions up to 100 times.
     * Row range: 1 to MAX_ROW-7 (rows 1-11) — matches legacy
     * AddBonusBlock/AddSpecialBlock: r = (rand() % (MAX_ROW - 7)) + 1
     * This keeps bonus blocks in the upper half, away from the paddle. */
    for (int attempt = 0; attempt < 100; attempt++)
    {
        int row = (rand() % (MAX_ROW - 7)) + 1;
        int col = rand() % MAX_COL;
        if (!block_system_is_occupied(block, row, col))
        {
            *out_row = row;
            *out_col = col;
            return 1;
        }
    }
    return 0; /* Grid too full */
}

/* =========================================================================
 * Bonus block spawning — port of main.c:handleGameMode() switch
 * ========================================================================= */

static void try_spawn_bonus(game_ctx_t *ctx, int frame)
{
    /* Schedule next bonus if not yet scheduled */
    if (ctx->next_bonus_frame == 0)
    {
        ctx->next_bonus_frame = frame + (rand() % BONUS_SEED);
        return;
    }

    /* Not time yet */
    if (frame < ctx->next_bonus_frame || ctx->bonus_block_active)
        return;

    /* Find an empty cell */
    int row, col;
    if (!find_random_empty_cell(ctx->block, &row, &col))
    {
        ctx->next_bonus_frame = 0;
        return;
    }

    /* Pick a bonus type — exact probability distribution from legacy */
    int roll = rand() % 27;

    if (roll <= 7)
    {
        /* Cases 0-7: normal bonus block (8/27 chance) */
        block_system_add(ctx->block, row, col, BONUS_BLK, 0, frame);
    }
    else if (roll <= 11)
    {
        /* Cases 8-11: x2 bonus (4/27 chance) */
        if (!special_system_is_active(ctx->special, SPECIAL_X2_BONUS))
            block_system_add(ctx->block, row, col, BONUSX2_BLK, 0, frame);
    }
    else if (roll <= 13)
    {
        /* Cases 12-13: x4 bonus (2/27 chance) */
        if (!special_system_is_active(ctx->special, SPECIAL_X4_BONUS))
            block_system_add(ctx->block, row, col, BONUSX4_BLK, 0, frame);
    }
    else if (roll <= 15)
    {
        /* Cases 14-15: paddle shrink (2/27) */
        block_system_add(ctx->block, row, col, PAD_SHRINK_BLK, 3, frame);
    }
    else if (roll <= 17)
    {
        /* Cases 16-17: paddle expand (2/27) */
        block_system_add(ctx->block, row, col, PAD_EXPAND_BLK, 3, frame);
    }
    else if (roll == 18)
    {
        /* Case 18: multiball (1/27) */
        block_system_add(ctx->block, row, col, MULTIBALL_BLK, 3, frame);
    }
    else if (roll == 19)
    {
        /* Case 19: reverse (1/27) */
        block_system_add(ctx->block, row, col, REVERSE_BLK, 3, frame);
    }
    else if (roll <= 21)
    {
        /* Cases 20-21: machine gun (2/27) */
        block_system_add(ctx->block, row, col, MGUN_BLK, 3, frame);
    }
    else if (roll == 22)
    {
        /* Case 22: wall off (1/27) */
        block_system_add(ctx->block, row, col, WALLOFF_BLK, 3, frame);
    }
    else if (roll == 23)
    {
        /* Case 23: extra ball (1/27) */
        block_system_add(ctx->block, row, col, EXTRABALL_BLK, 0, frame);
    }
    else if (roll == 24)
    {
        /* Case 24: death block (1/27) */
        block_system_add(ctx->block, row, col, DEATH_BLK, 3, frame);
    }
    else if (roll == 25)
    {
        /* Case 25: dynamite — clear all blocks of a random color */
        static const int dyn_types[] = {YELLOW_BLK, BLUE_BLK,    RED_BLK,  PURPLE_BLK,
                                        TAN_BLK,    COUNTER_BLK, GREEN_BLK};
        int target = dyn_types[rand() % 7];
        for (int r = 0; r < MAX_ROW; r++)
        {
            for (int c = 0; c < MAX_COL; c++)
            {
                if (block_system_get_type(ctx->block, r, c) == target)
                    block_system_clear(ctx->block, r, c);
            }
        }
        if (ctx->audio)
            sdl2_audio_play(ctx->audio, "bomb");
    }
    else
    {
        /* Case 26 (final): start eyedude */
        eyedude_system_set_state(ctx->eyedude, EYEDUDE_STATE_RESET);
    }

    ctx->next_bonus_frame = 0;
}

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

    /* Reset timer for new level */
    ctx->time_bonus_total = level_system_get_time_bonus(ctx->level);
    ctx->time_remaining = ctx->time_bonus_total;
    ctx->timer_frame_acc = 0;

    /* Set level title as default message */
    const char *title = level_system_get_title(ctx->level);
    if (title)
        message_system_set_default(ctx->message, title);

    /* Reset paddle to center, clear reverse, place new ball.
     * Matches original/file.c:122 — SetReverseOff() inside SetupStage. */
    paddle_system_reset(ctx->paddle);
    paddle_system_set_reverse(ctx->paddle, 0);
    paddle_system_set_size(ctx->paddle, PADDLE_SIZE_HUGE);

    ball_system_env_t env = game_callbacks_ball_env(ctx);
    ball_system_reset_start(ctx->ball, &env);

    /* Reset unlimited and give ammo for the new level.
     * original/file.c:115 — SetUnlimitedBullets(False) before SetNumberBullets. */
    gun_system_set_unlimited(ctx->gun, 0);
    gun_system_set_ammo(ctx->gun, GUN_AMMO_PER_LEVEL);

    /* Reset bonus spawning state */
    ctx->bonus_block_active = false;
    ctx->next_bonus_frame = 0;

    /* Reset BONUS_BLK pickup counter — original calls ResetNumberBonus
     * during level load (original/file.c:328-333).  Without this the
     * killer-mode-at-10 threshold could be tripped by carrying BONUS_BLK
     * pickups across multiple levels. */
    ctx->bonus_count = 0;

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

    /* Still have lives — reset ball on paddle.
     * Clear reverse here: matches original/level.c:492 — SetReverseOff()
     * inside DeadBall, before ResetBallStart. */
    if (ctx->audio)
        sdl2_audio_play(ctx->audio, "balllost");

    /* Grant +2 ammo as consolation — original/ball.c:1803-1805.
     * Skip when unlimited is active: MAXAMMO_BLK sets ammo to GUN_MAX_AMMO+1
     * as a sentinel, but gun_system_add_ammo clamps to GUN_MAX_AMMO, so the
     * +2 here would silently reduce 21→20. */
    if (!gun_system_get_unlimited(ctx->gun))
    {
        gun_system_add_ammo(ctx->gun);
        gun_system_add_ammo(ctx->gun);
    }

    paddle_system_set_reverse(ctx->paddle, 0);
    ball_system_env_t env = game_callbacks_ball_env(ctx);
    ball_system_reset_start(ctx->ball, &env);
}

/* =========================================================================
 * Per-frame rule check
 * ========================================================================= */

void game_rules_check(game_ctx_t *ctx)
{
    /* Level completion: no required blocks remain → go to bonus screen */
    if (!block_system_still_active(ctx->block))
    {
        special_system_turn_off(ctx->special);
        if (ctx->audio)
            sdl2_audio_play(ctx->audio, "applause");
        sdl2_state_transition(ctx->state, SDL2ST_BONUS);
        return;
    }

    /* Bonus block spawning */
    int frame = (int)sdl2_state_frame(ctx->state);
    try_spawn_bonus(ctx, frame);
}
