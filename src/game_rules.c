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
#include <unistd.h>

#include "ball_system.h"
#include "ball_types.h"
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
            sdl2_audio_play_at_percent(ctx->audio, "bomb", 50);
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
    /* Resolve the next level's file before mutating ctx->level_number
     * or clearing the block grid.  A failed load mid-mutation would
     * leave the grid empty and level_number incremented — the next
     * game_rules_check tick would see no required blocks and re-enter
     * BONUS, looping forever. */
    int next_level = ctx->level_number + 1;
    int file_num = level_system_wrap_number(next_level);
    char filename[32];
    snprintf(filename, sizeof(filename), "level%02d.data", file_num);

    char level_path[PATHS_MAX_PATH];
    if (paths_level_file(&ctx->paths, filename, level_path, sizeof(level_path)) != PATHS_OK ||
        access(level_path, R_OK) != 0)
    {
        /* Resolve failed or file unreadable — original/level.c calls
         * ShutDown which exits the process.  Modernized: end the game
         * cleanly via the highscore screen so the player's score is
         * still submitted (matches game_rules_ball_died semantics).
         * Probe BEFORE clearing the grid so we don't strand the
         * player on an empty board if the transition is delayed. */
        fprintf(stderr, "xboing: cannot advance past level %d (no readable file for %s)\n",
                ctx->level_number, filename);
        if (ctx->audio)
            sdl2_audio_play_at_percent(ctx->audio, "game_over", 99);
        message_system_set(ctx->message, "- Level data missing -", 0,
                           (int)sdl2_state_frame(ctx->state));
        sdl2_state_transition(ctx->state, SDL2ST_HIGHSCORE);
        return;
    }

    /* Clear state for new level */
    block_system_clear_all(ctx->block);
    ball_system_clear_all(ctx->ball);
    gun_system_clear(ctx->gun);
    special_system_turn_off(ctx->special);

    /* Load the level — even after access() probe a parse failure can
     * still occur on truncated/corrupt files.  Same end-the-game
     * semantic as the unreadable path: the grid is already cleared,
     * so we must move out of GAME mode to avoid the infinite
     * BONUS-loop game_rules_check would otherwise trigger. */
    if (level_system_load_file(ctx->level, level_path) != LEVEL_SYS_OK)
    {
        fprintf(stderr, "xboing: failed to parse level file %s; ending game on level %d\n",
                level_path, ctx->level_number);
        if (ctx->audio)
            sdl2_audio_play_at_percent(ctx->audio, "game_over", 99);
        message_system_set(ctx->message, "- Level data corrupt -", 0,
                           (int)sdl2_state_frame(ctx->state));
        sdl2_state_transition(ctx->state, SDL2ST_HIGHSCORE);
        return;
    }
    level_system_advance_background(ctx->level);
    ctx->level_number = next_level;

    /* Reset timer for new level */
    ctx->time_bonus_total = level_system_get_time_bonus(ctx->level);
    ctx->time_remaining = ctx->time_bonus_total;
    ctx->timer_frame_acc = 0;

    /* Display new level title in message bar and register as default.
     * Mirrors original/file.c:SetupStage:148-150. */
    const char *title = level_system_get_title(ctx->level);
    if (title)
    {
        char msg[80];
        snprintf(msg, sizeof(msg), "- %s -", title);
        message_system_set_default(ctx->message, msg);
        int frame = (int)sdl2_state_frame(ctx->state);
        message_system_set(ctx->message, msg, 0, frame);
    }

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

    /* No applause on level entry — original plays "applause" only at
     * level-complete (original/level.c:413) and at the end of the bonus
     * tally (original/bonus.c:600, mirrored in bonus_system.c::do_end_text).
     * A third applause here would double-fire on every level transition. */
}

/* =========================================================================
 * Ball death
 * ========================================================================= */

void game_rules_ball_died(game_ctx_t *ctx)
{
    /* If there are still active balls, do nothing — multiball */
    if (ball_system_get_active_count(ctx->ball) > 0)
        return;

    /* No balls left.
     *
     * Play-test: lives never deplete, matching DecExtraLife's
     * `if (mode != MODE_EDIT) livesLeft--;` no-op (original/level.c:
     * 346-357).  The original never needed a dedicated flag for this
     * because `mode` stays MODE_EDIT for the whole editor session,
     * play-test included (original/main.c:680, editor.c:386) -- so
     * DeadBall's `livesLeft <= 0` game-over check (original/level.c:
     * 474-505) can never trip.  The modern port re-enters a genuinely
     * distinct SDL2ST_GAME mode for play-test, so it needs
     * ctx->play_test_active to recover the same fact. */
    if (!ctx->play_test_active)
    {
        ctx->lives_left--;

        if (ctx->lives_left <= 0)
        {
            /* Game over.  Don't clear ctx->game_active here — the highscore
             * mode's on_enter uses it to distinguish real game-over from
             * attract-cycle entry.  It is cleared by mode_intro_enter when
             * the game-over highscore returns to the title (ADR-055). */
            if (ctx->audio)
                sdl2_audio_play_at_percent(ctx->audio, "game_over", 99);
            message_system_set(ctx->message, "GAME OVER", 0, 0);

            sdl2_state_transition(ctx->state, SDL2ST_HIGHSCORE);
            return;
        }
    }

    /* Still have lives (or play-testing) — reset ball on paddle.
     * Clear reverse here: matches original/level.c:492 — SetReverseOff()
     * inside DeadBall, before ResetBallStart. */
    if (ctx->audio)
        sdl2_audio_play_at_percent(ctx->audio, "balllost", 99);

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
    /* Level completion: no required blocks remain → go to bonus screen.
     * Guarded the same way as game_rules_ball_died's game-over transition
     * above: clearing the board during play-test must not reach the real
     * bonus sequence, matching CheckGameRules's mode==MODE_GAME-only call
     * site (original/main.c:1140-1141) -- CheckGameRules itself is
     * original/level.c:398-419. */
    if (!block_system_still_active(ctx->block) && !ctx->play_test_active)
    {
        special_system_turn_off(ctx->special);
        if (ctx->audio)
            sdl2_audio_play_at_percent(ctx->audio, "applause", 70);
        sdl2_state_transition(ctx->state, SDL2ST_BONUS);
        return;
    }

    /* Bonus block spawning */
    int frame = (int)sdl2_state_frame(ctx->state);
    try_spawn_bonus(ctx, frame);
}

void game_rules_check_ball_eyedude(game_ctx_t *ctx)
{
    if (!ctx || !ctx->eyedude || !ctx->ball)
        return;

    if (eyedude_system_get_state(ctx->eyedude) != EYEDUDE_STATE_WALK)
        return;

    for (int i = 0; i < MAX_BALLS; i++)
    {
        enum BallStates bs = ball_system_get_state(ctx->ball, i);
        if (bs != BALL_ACTIVE)
            continue;

        int bx = 0;
        int by = 0;
        if (ball_system_get_position(ctx->ball, i, &bx, &by) != BALL_SYS_OK)
            continue;

        if (eyedude_system_check_collision(ctx->eyedude, bx, by, BALL_WC, BALL_HC))
        {
            eyedude_system_set_state(ctx->eyedude, EYEDUDE_STATE_DIE);
            return;
        }
    }
}
