/*
 * game_callbacks.c -- Callback implementations that wire game modules together.
 *
 * Each callback receives game_ctx_t* as its user_data pointer and
 * dispatches to the appropriate module(s).
 *
 * Callbacks are grouped by the module that invokes them:
 *   - Ball system callbacks (check_region, on_block_hit, etc.)
 *   - Gun system callbacks (added in bead 2.5)
 */

#include "game_callbacks.h"

#include "ball_system.h"
#include "block_system.h"
#include "block_types.h"
#include "game_context.h"
#include "game_rules.h"
#include "message_system.h"
#include "paddle_system.h"
#include "score_logic.h"
#include "score_system.h"
#include "sdl2_audio.h"
#include "sdl2_loop.h"
#include "sdl2_state.h"
#include "special_system.h"

/* Play area constants */
#define GAME_PLAY_WIDTH 495
#define GAME_PLAY_HEIGHT 580
#define GAME_COL_WIDTH (GAME_PLAY_WIDTH / 9)
#define GAME_ROW_HEIGHT (GAME_PLAY_HEIGHT / 18)

/* =========================================================================
 * Ball system callbacks
 * ========================================================================= */

/*
 * Block collision check: delegates to block_system's pure C geometry.
 * Returns BALL_REGION_NONE/TOP/BOTTOM/LEFT/RIGHT.
 */
static int ball_cb_check_region(int row, int col, int bx, int by, int bdx, void *ud)
{
    game_ctx_t *ctx = ud;
    /* block_system_check_region expects (row, col, bx, by, bdx, block_system_t*) */
    return block_system_check_region(row, col, bx, by, bdx, ctx->block);
}

/*
 * Block hit handler: process the hit, award points, clear the block.
 *
 * Returns nonzero if ball should NOT bounce (e.g., DEATH_BLK kills ball,
 * HYPERSPACE_BLK teleports — these absorb the hit).
 */
static int ball_cb_on_block_hit(int row, int col, int ball_index, void *ud)
{
    (void)ball_index;
    game_ctx_t *ctx = ud;

    int block_type = block_system_get_type(ctx->block, row, col);
    if (block_type == NONE_BLK)
        return 0;

    /* Award points based on block type */
    int points = score_block_hit_points(block_type, row);
    if (points > 0)
    {
        score_system_env_t senv = {
            .x2_active = special_system_is_active(ctx->special, SPECIAL_X2_BONUS),
            .x4_active = special_system_is_active(ctx->special, SPECIAL_X4_BONUS),
        };
        score_system_add(ctx->score, (unsigned long)points, &senv);
    }

    /* Handle block-type-specific behavior */
    switch (block_type)
    {
        case DEATH_BLK:
            /* Death block kills the ball — return nonzero to suppress bounce */
            block_system_clear(ctx->block, row, col);
            return 1;

        case BOMB_BLK:
            /* Bomb: clear this block and neighbors */
            block_system_clear(ctx->block, row, col);
            /* Clear adjacent blocks (simplified — full chain explosion in bead 6.2) */
            for (int dr = -1; dr <= 1; dr++)
            {
                for (int dc = -1; dc <= 1; dc++)
                {
                    if (dr == 0 && dc == 0)
                        continue;
                    int nr = row + dr, nc = col + dc;
                    if (block_system_is_occupied(ctx->block, nr, nc))
                        block_system_clear(ctx->block, nr, nc);
                }
            }
            if (ctx->audio)
                sdl2_audio_play(ctx->audio, "explosion");
            return 0;

        case BLACK_BLK:
            /* Black blocks take 2 hits — just clear on hit (simplified) */
            block_system_clear(ctx->block, row, col);
            return 0;

        case COUNTER_BLK:
            /* Counter blocks decrement — simplified: just clear */
            block_system_clear(ctx->block, row, col);
            return 0;

        default:
            /* Standard block: just clear it */
            block_system_clear(ctx->block, row, col);
            return 0;
    }
}

/*
 * Cell availability for teleport: delegates to block_system.
 */
static int ball_cb_cell_available(int row, int col, void *ud)
{
    game_ctx_t *ctx = ud;
    return block_system_cell_available(row, col, ctx->block);
}

/*
 * Sound playback: delegates to sdl2_audio.
 */
static void ball_cb_on_sound(const char *name, void *ud)
{
    game_ctx_t *ctx = ud;
    if (ctx->audio)
        sdl2_audio_play(ctx->audio, name);
}

/*
 * Score addition from ball events (paddle hit bonus, etc.).
 * Uses raw add — no multiplier (multiplier applied in on_block_hit).
 */
static void ball_cb_on_score(unsigned long points, void *ud)
{
    game_ctx_t *ctx = ud;
    score_system_add_raw(ctx->score, points);
}

/*
 * Message display: delegates to message_system.
 */
static void ball_cb_on_message(const char *msg, void *ud)
{
    game_ctx_t *ctx = ud;
    unsigned long frame = sdl2_state_frame(ctx->state);
    message_system_set(ctx->message, msg, 1, (int)frame);
}

/*
 * Ball lifecycle events: handles DIED, ACTIVATED, PADDLE_HIT, etc.
 * Game rule consequences (lives, game over) handled in game_rules.c (bead 2.4).
 */
static void ball_cb_on_event(ball_system_event_t event, int ball_index, void *ud)
{
    (void)ball_index;
    game_ctx_t *ctx = ud;

    switch (event)
    {
        case BALL_EVT_DIED:
            game_rules_ball_died(ctx);
            break;

        case BALL_EVT_PADDLE_HIT:
            if (ctx->audio)
                sdl2_audio_play(ctx->audio, "paddle");
            break;

        default:
            break;
    }
}

/* =========================================================================
 * Public: build callback tables
 * ========================================================================= */

/* =========================================================================
 * Gun system callbacks
 * ========================================================================= */

/*
 * Check if bullet at (bx, by) hits any block in the grid.
 * Iterates all occupied blocks and checks AABB overlap.
 */
static int gun_cb_check_block_hit(int bx, int by, int *out_row, int *out_col, void *ud)
{
    game_ctx_t *ctx = ud;

    for (int row = 0; row < MAX_ROW; row++)
    {
        for (int col = 0; col < MAX_COL; col++)
        {
            if (!block_system_is_occupied(ctx->block, row, col))
                continue;

            block_system_render_info_t info;
            if (block_system_get_render_info(ctx->block, row, col, &info) != BLOCK_SYS_OK)
                continue;

            /* AABB overlap: bullet center vs block rectangle */
            if (bx >= info.x && bx < info.x + info.width && by >= info.y &&
                by < info.y + info.height)
            {
                *out_row = row;
                *out_col = col;
                return 1;
            }
        }
    }
    return 0;
}

/* Handle bullet-block hit: clear block and award points */
static void gun_cb_on_block_hit(int row, int col, void *ud)
{
    game_ctx_t *ctx = ud;
    int block_type = block_system_get_type(ctx->block, row, col);
    if (block_type == NONE_BLK)
        return;

    int points = score_block_hit_points(block_type, row);
    if (points > 0)
    {
        score_system_env_t senv = {
            .x2_active = special_system_is_active(ctx->special, SPECIAL_X2_BONUS),
            .x4_active = special_system_is_active(ctx->special, SPECIAL_X4_BONUS),
        };
        score_system_add(ctx->score, (unsigned long)points, &senv);
    }
    block_system_clear(ctx->block, row, col);
}

/* Check if bullet hits any active ball (AABB overlap) */
static int gun_cb_check_ball_hit(int bx, int by, void *ud)
{
    game_ctx_t *ctx = ud;
    for (int i = 0; i < MAX_BALLS; i++)
    {
        ball_system_render_info_t info;
        if (ball_system_get_render_info(ctx->ball, i, &info) != BALL_SYS_OK)
            continue;
        if (!info.active || info.state != BALL_ACTIVE)
            continue;
        /* Simple distance check */
        int dx = bx - info.x;
        int dy = by - info.y;
        if (dx * dx + dy * dy < 15 * 15)
            return i;
    }
    return -1;
}

/* Bullet hit ball: activate it (wake up from stun) */
static void gun_cb_on_ball_hit(int ball_index, void *ud)
{
    (void)ball_index;
    (void)ud;
    /* Ball activation on bullet hit — simplified for now */
}

/* Check if bullet hits eyedude — stub until bead 4.2 */
static int gun_cb_check_eyedude_hit(int bx, int by, void *ud)
{
    (void)bx;
    (void)by;
    (void)ud;
    return 0;
}

static void gun_cb_on_eyedude_hit(void *ud)
{
    (void)ud;
}

static void gun_cb_on_sound(const char *name, void *ud)
{
    game_ctx_t *ctx = ud;
    if (ctx->audio)
        sdl2_audio_play(ctx->audio, name);
}

static int gun_cb_is_ball_waiting(void *ud)
{
    game_ctx_t *ctx = ud;
    return ball_system_is_ball_waiting(ctx->ball);
}

/* =========================================================================
 * Public: build callback tables
 * ========================================================================= */

ball_system_callbacks_t game_callbacks_ball(void)
{
    ball_system_callbacks_t cbs = {
        .check_region = ball_cb_check_region,
        .on_block_hit = ball_cb_on_block_hit,
        .cell_available = ball_cb_cell_available,
        .on_sound = ball_cb_on_sound,
        .on_score = ball_cb_on_score,
        .on_message = ball_cb_on_message,
        .on_event = ball_cb_on_event,
    };
    return cbs;
}

gun_system_callbacks_t game_callbacks_gun(void)
{
    gun_system_callbacks_t cbs = {
        .check_block_hit = gun_cb_check_block_hit,
        .on_block_hit = gun_cb_on_block_hit,
        .check_ball_hit = gun_cb_check_ball_hit,
        .on_ball_hit = gun_cb_on_ball_hit,
        .check_eyedude_hit = gun_cb_check_eyedude_hit,
        .on_eyedude_hit = gun_cb_on_eyedude_hit,
        .on_sound = gun_cb_on_sound,
        .is_ball_waiting = gun_cb_is_ball_waiting,
    };
    return cbs;
}

gun_system_env_t game_callbacks_gun_env(const game_ctx_t *ctx)
{
    gun_system_env_t env = {
        .frame = (int)sdl2_state_frame(ctx->state),
        .paddle_pos = paddle_system_get_pos(ctx->paddle),
        .paddle_size = paddle_system_get_size(ctx->paddle),
        .fast_gun = special_system_is_active(ctx->special, SPECIAL_FAST_GUN),
    };
    return env;
}

ball_system_env_t game_callbacks_ball_env(const game_ctx_t *ctx)
{
    ball_system_env_t env = {
        .frame = (int)sdl2_state_frame(ctx->state),
        .speed_level = sdl2_loop_get_speed(ctx->loop),
        .paddle_pos = paddle_system_get_pos(ctx->paddle),
        .paddle_dx = paddle_system_get_dx(ctx->paddle),
        .paddle_size = paddle_system_get_size(ctx->paddle),
        .play_width = GAME_PLAY_WIDTH,
        .play_height = GAME_PLAY_HEIGHT,
        .no_walls = special_system_is_active(ctx->special, SPECIAL_NO_WALLS),
        .killer = special_system_is_active(ctx->special, SPECIAL_KILLER),
        .sticky_bat = special_system_is_active(ctx->special, SPECIAL_STICKY),
        .col_width = GAME_COL_WIDTH,
        .row_height = GAME_ROW_HEIGHT,
    };
    return env;
}
