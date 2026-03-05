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
#include "bonus_system.h"
#include "config_io.h"
#include "demo_system.h"
#include "game_context.h"
#include "game_rules.h"
#include "intro_system.h"
#include "keys_system.h"
#include "level_system.h"
#include "message_system.h"
#include "paddle_system.h"
#include "paths.h"
#include "presents_system.h"
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
            block_system_clear(ctx->block, row, col);
            return 1; /* Kill the ball */

        case BOMB_BLK:
            block_system_clear(ctx->block, row, col);
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++)
                    if (!(dr == 0 && dc == 0) &&
                        block_system_is_occupied(ctx->block, row + dr, col + dc))
                        block_system_clear(ctx->block, row + dr, col + dc);
            if (ctx->audio)
                sdl2_audio_play(ctx->audio, "explosion");
            return 0;

        case REVERSE_BLK:
            block_system_clear(ctx->block, row, col);
            paddle_system_toggle_reverse(ctx->paddle);
            return 0;

        case MULTIBALL_BLK:
        {
            block_system_clear(ctx->block, row, col);
            ball_system_env_t env = game_callbacks_ball_env(ctx);
            ball_system_split(ctx->ball, &env);
            return 0;
        }

        case STICKY_BLK:
            block_system_clear(ctx->block, row, col);
            special_system_set(ctx->special, SPECIAL_STICKY, 1);
            paddle_system_set_sticky(ctx->paddle, 1);
            return 0;

        case PAD_SHRINK_BLK:
            block_system_clear(ctx->block, row, col);
            paddle_system_change_size(ctx->paddle, 1); /* shrink */
            return 0;

        case PAD_EXPAND_BLK:
            block_system_clear(ctx->block, row, col);
            paddle_system_change_size(ctx->paddle, 0); /* expand */
            return 0;

        case MGUN_BLK:
            block_system_clear(ctx->block, row, col);
            special_system_set(ctx->special, SPECIAL_FAST_GUN, 1);
            gun_system_set_unlimited(ctx->gun, 1);
            return 0;

        case WALLOFF_BLK:
            block_system_clear(ctx->block, row, col);
            special_system_set(ctx->special, SPECIAL_NO_WALLS, 1);
            return 0;

        case EXTRABALL_BLK:
            block_system_clear(ctx->block, row, col);
            ctx->lives_left++;
            return 0;

        case BONUSX2_BLK:
            block_system_clear(ctx->block, row, col);
            special_system_set(ctx->special, SPECIAL_X2_BONUS, 1);
            return 0;

        case BONUSX4_BLK:
            block_system_clear(ctx->block, row, col);
            special_system_set(ctx->special, SPECIAL_X4_BONUS, 1);
            return 0;

        case HYPERSPACE_BLK:
            block_system_clear(ctx->block, row, col);
            return 1; /* Absorb ball (teleport in full impl) */

        default:
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

/* =========================================================================
 * Presents system callbacks
 * ========================================================================= */

static void presents_cb_on_finished(void *ud)
{
    game_ctx_t *ctx = ud;
    sdl2_state_transition(ctx->state, SDL2ST_INTRO);
}

static const char *presents_cb_get_nickname(void *ud)
{
    game_ctx_t *ctx = ud;
    return ctx->config.nickname;
}

static const char *presents_cb_get_fullname(void *ud)
{
    (void)ud;
    return "Player";
}

presents_system_callbacks_t game_callbacks_presents(void)
{
    presents_system_callbacks_t cbs = {
        .on_finished = presents_cb_on_finished,
        .get_nickname = presents_cb_get_nickname,
        .get_fullname = presents_cb_get_fullname,
    };
    return cbs;
}

/* =========================================================================
 * Intro system callbacks
 * ========================================================================= */

static void intro_cb_on_finished(intro_screen_mode_t mode, void *ud)
{
    game_ctx_t *ctx = ud;
    if (mode == INTRO_MODE_INTRO)
        sdl2_state_transition(ctx->state, SDL2ST_INSTRUCT);
    else
        sdl2_state_transition(ctx->state, SDL2ST_DEMO);
}

intro_system_callbacks_t game_callbacks_intro(void)
{
    intro_system_callbacks_t cbs = {
        .on_finished = intro_cb_on_finished,
    };
    return cbs;
}

/* =========================================================================
 * Bonus system callbacks
 * ========================================================================= */

static void bonus_cb_on_score_add(unsigned long points, void *ud)
{
    game_ctx_t *ctx = ud;
    score_system_add_raw(ctx->score, points);
}

static void bonus_cb_on_bullet_consumed(void *ud)
{
    game_ctx_t *ctx = ud;
    gun_system_use_ammo(ctx->gun);
}

static void bonus_cb_on_save_triggered(void *ud)
{
    (void)ud;
    /* TODO: auto-save game state (bead 4.3) */
}

static void bonus_cb_on_sound(const char *name, void *ud)
{
    game_ctx_t *ctx = ud;
    if (ctx->audio)
        sdl2_audio_play(ctx->audio, name);
}

static void bonus_cb_on_finished(int next_level, void *ud)
{
    (void)next_level;
    game_ctx_t *ctx = ud;
    /* Advance to the next level */
    game_rules_next_level(ctx);
    sdl2_state_transition(ctx->state, SDL2ST_GAME);
}

bonus_system_callbacks_t game_callbacks_bonus(void)
{
    bonus_system_callbacks_t cbs = {
        .on_score_add = bonus_cb_on_score_add,
        .on_bullet_consumed = bonus_cb_on_bullet_consumed,
        .on_save_triggered = bonus_cb_on_save_triggered,
        .on_sound = bonus_cb_on_sound,
        .on_finished = bonus_cb_on_finished,
    };
    return cbs;
}

/* =========================================================================
 * Demo system callbacks
 * ========================================================================= */

static void demo_cb_on_finished(demo_screen_mode_t mode, void *ud)
{
    game_ctx_t *ctx = ud;
    /* Attract cycle: demo → keys → keysedit → preview → highscore → intro */
    if (mode == DEMO_MODE_DEMO)
        sdl2_state_transition(ctx->state, SDL2ST_KEYS);
    else /* DEMO_MODE_PREVIEW */
        sdl2_state_transition(ctx->state, SDL2ST_HIGHSCORE);
}

static void demo_cb_on_load_level(int level_num, void *ud)
{
    game_ctx_t *ctx = ud;
    int file_num = level_system_wrap_number(level_num);
    char filename[32];
    snprintf(filename, sizeof(filename), "level%02d.data", file_num);

    char level_path[PATHS_MAX_PATH];
    if (paths_level_file(&ctx->paths, filename, level_path, sizeof(level_path)) == PATHS_OK)
    {
        block_system_clear_all(ctx->block);
        level_system_load_file(ctx->level, level_path);
    }
}

demo_system_callbacks_t game_callbacks_demo(void)
{
    demo_system_callbacks_t cbs = {
        .on_finished = demo_cb_on_finished,
        .on_load_level = demo_cb_on_load_level,
    };
    return cbs;
}

/* =========================================================================
 * Keys system callbacks
 * ========================================================================= */

static void keys_cb_on_finished(keys_screen_mode_t mode, void *ud)
{
    game_ctx_t *ctx = ud;
    /* Attract cycle: keys → keysedit → preview → highscore → intro */
    if (mode == KEYS_MODE_GAME)
        sdl2_state_transition(ctx->state, SDL2ST_KEYSEDIT);
    else /* KEYS_MODE_EDITOR */
        sdl2_state_transition(ctx->state, SDL2ST_PREVIEW);
}

keys_system_callbacks_t game_callbacks_keys(void)
{
    keys_system_callbacks_t cbs = {
        .on_finished = keys_cb_on_finished,
    };
    return cbs;
}

/* =========================================================================
 * Highscore system callbacks
 * ========================================================================= */

static void highscore_cb_on_finished(highscore_type_t type, void *ud)
{
    (void)type;
    game_ctx_t *ctx = ud;
    /* After highscore display, cycle back to intro */
    sdl2_state_transition(ctx->state, SDL2ST_INTRO);
}

highscore_system_callbacks_t game_callbacks_highscore(void)
{
    highscore_system_callbacks_t cbs = {
        .on_finished = highscore_cb_on_finished,
    };
    return cbs;
}

/* =========================================================================
 * SFX system callbacks
 * ========================================================================= */

static void sfx_cb_on_move_window(int x, int y, void *ud)
{
    (void)x;
    (void)y;
    (void)ud;
    /* Shake offset is applied during rendering via sfx_system_get_shake_pos() */
}

static void sfx_cb_on_sound(const char *name, void *ud)
{
    game_ctx_t *ctx = ud;
    if (ctx->audio)
        sdl2_audio_play(ctx->audio, name);
}

sfx_system_callbacks_t game_callbacks_sfx(void)
{
    sfx_system_callbacks_t cbs = {
        .on_move_window = sfx_cb_on_move_window,
        .on_sound = sfx_cb_on_sound,
    };
    return cbs;
}

/* =========================================================================
 * EyeDude system callbacks
 * ========================================================================= */

static int eyedude_cb_is_path_clear(void *ud)
{
    game_ctx_t *ctx = ud;
    /* Check if top row has blocks */
    for (int col = 0; col < MAX_COL; col++)
    {
        if (block_system_is_occupied(ctx->block, 0, col))
            return 0;
    }
    return 1;
}

static void eyedude_cb_on_score(unsigned long points, void *ud)
{
    game_ctx_t *ctx = ud;
    score_system_env_t senv = {
        .x2_active = special_system_is_active(ctx->special, SPECIAL_X2_BONUS),
        .x4_active = special_system_is_active(ctx->special, SPECIAL_X4_BONUS),
    };
    score_system_add(ctx->score, points, &senv);
}

static void eyedude_cb_on_sound(const char *name, void *ud)
{
    game_ctx_t *ctx = ud;
    if (ctx->audio)
        sdl2_audio_play(ctx->audio, name);
}

static void eyedude_cb_on_message(const char *msg, void *ud)
{
    game_ctx_t *ctx = ud;
    int frame = (int)sdl2_state_frame(ctx->state);
    message_system_set(ctx->message, msg, 1, frame);
}

eyedude_system_callbacks_t game_callbacks_eyedude(void)
{
    eyedude_system_callbacks_t cbs = {
        .is_path_clear = eyedude_cb_is_path_clear,
        .on_score = eyedude_cb_on_score,
        .on_sound = eyedude_cb_on_sound,
        .on_message = eyedude_cb_on_message,
    };
    return cbs;
}
