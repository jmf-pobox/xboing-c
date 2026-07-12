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

#include <stdio.h>

#include "ball_system.h"
#include "block_sound.h"
#include "block_system.h"
#include "block_types.h"
#include "bonus_system.h"
#include "config_io.h"
#include "demo_system.h"
#include "dialogue_system.h"
#include "editor_system.h"
#include "game_context.h"
#include "game_modes.h"
#include "game_rules.h"
#include "gun_system.h"
#include "intro_system.h"
#include "keys_system.h"
#include "level_system.h"
#include "message_system.h"
#include "paddle_system.h"
#include "paths.h"
#include "presents_system.h"
#include "savegame_system.h"
#include "score_logic.h"
#include "score_system.h"
#include "sdl2_audio.h"
#include "sdl2_loop.h"
#include "sdl2_state.h"
#include "sfx_system.h"
#include "special_system.h"

/* =========================================================================
 * Attract cycle — single source of truth for screen order
 *
 * Order matches the natural callback chain (keys_cb_on_finished →
 * PREVIEW, demo_cb_on_finished(PREVIEW) → HIGHSCORE).  The original's
 * C-key handler (original/main.c:554-605) had HIGHSCORE before PREVIEW,
 * disagreeing with the natural cycle — a bug in the original that caused
 * C-key and timer-based transitions to visit screens in different order.
 * We align both paths to the natural order.
 * ========================================================================= */

static const sdl2_state_mode_t attract_cycle[] = {
    SDL2ST_INTRO,    SDL2ST_INSTRUCT, SDL2ST_DEMO,      SDL2ST_KEYS,
    SDL2ST_KEYSEDIT, SDL2ST_PREVIEW,  SDL2ST_HIGHSCORE,
};
static const int attract_cycle_len = (int)(sizeof(attract_cycle) / sizeof(attract_cycle[0]));

sdl2_state_mode_t game_callbacks_attract_next(sdl2_state_mode_t current)
{
    for (int i = 0; i < attract_cycle_len; i++)
    {
        if (attract_cycle[i] == current)
            return attract_cycle[(i + 1) % attract_cycle_len];
    }
    return SDL2ST_NONE;
}

/* Block-hit sound: delegates the type→(name, volume) mapping to the pure
 * block_sound module (testable without an audio context).  Silent for
 * types that map to NULL (intermediate hits, sentinels, gaps).  Per-call
 * volume matches the original's playSoundFile(name, volume) per
 * docs/audits/2026-06-28-audio-volume-modulation.md. */
static void play_block_hit_sound(sdl2_audio_t *audio, int block_type)
{
    block_sound_t s = block_sound_lookup(block_type);
    if (audio && s.name)
        sdl2_audio_play_at_percent(audio, s.name, s.volume);
}

/* =========================================================================
 * Ball system callbacks
 * ========================================================================= */

/*
 * Block collision check: delegates to block_system's original-faithful
 * bbox-vs-triangle classifier (port of CheckRegions in original/ball.c).
 * Returns a bitmask of BALL_REGION_TOP / BOTTOM / LEFT / RIGHT, or NONE.
 * The ball bounce switch in ball_system.c handles the single-region and
 * corner-pair cases the original handled.
 */
static int ball_cb_check_region(int row, int col, int bx, int by, int bdx, void *ud)
{
    game_ctx_t *ctx = ud;
    return block_system_check_region_bbox(row, col, bx, by, bdx, ctx->block);
}

/*
 * Block hit handler: process the hit, award points, clear the block.
 *
 * Returns nonzero if ball should NOT bounce (e.g., DEATH_BLK kills ball,
 * HYPERSPACE_BLK teleports — these absorb the hit).
 */
static block_hit_result_t ball_cb_on_block_hit(int row, int col, int ball_index, void *ud)
{
    game_ctx_t *ctx = ud;

    int block_type = block_system_get_type(ctx->block, row, col);
    if (block_type == NONE_BLK)
        return BLOCK_HIT_BOUNCE;

    int frame = (int)sdl2_state_frame(ctx->state);
    int killer = special_system_is_active(ctx->special, SPECIAL_KILLER);

    switch (block_type)
    {
        case DEATH_BLK:
            /* Match original/ball.c:847-861 ordering: kill ball first,
             * then arm explosion and play sound (the original plays the
             * sound inside DrawBlock(KILL_BLK) which is called AFTER
             * ClearBallNow). */
            {
                ball_system_env_t benv = game_callbacks_ball_env(ctx);
                ball_system_change_mode(ctx->ball, &benv, ball_index, BALL_POP);
            }
            (void)block_system_explode(ctx->block, row, col, frame);
            play_block_hit_sound(ctx->audio, DEATH_BLK);
            return BLOCK_HIT_ABSORB;

        case BOMB_BLK:
            (void)block_system_explode(ctx->block, row, col, frame);
            play_block_hit_sound(ctx->audio, BOMB_BLK);
            return killer ? BLOCK_HIT_ABSORB : BLOCK_HIT_BOUNCE;

        case REVERSE_BLK:
            (void)block_system_explode(ctx->block, row, col, frame);
            play_block_hit_sound(ctx->audio, REVERSE_BLK);
            paddle_system_toggle_reverse(ctx->paddle);
            return killer ? BLOCK_HIT_ABSORB : BLOCK_HIT_BOUNCE;

        case MULTIBALL_BLK:
        {
            (void)block_system_explode(ctx->block, row, col, frame);
            play_block_hit_sound(ctx->audio, MULTIBALL_BLK);
            ball_system_env_t env = game_callbacks_ball_env(ctx);
            ball_system_split(ctx->ball, &env);
            return killer ? BLOCK_HIT_ABSORB : BLOCK_HIT_BOUNCE;
        }

        case STICKY_BLK:
            (void)block_system_explode(ctx->block, row, col, frame);
            play_block_hit_sound(ctx->audio, STICKY_BLK);
            special_system_set(ctx->special, SPECIAL_STICKY, 1);
            paddle_system_set_sticky(ctx->paddle, 1);
            return killer ? BLOCK_HIT_ABSORB : BLOCK_HIT_BOUNCE;

        case PAD_SHRINK_BLK:
            (void)block_system_explode(ctx->block, row, col, frame);
            play_block_hit_sound(ctx->audio, PAD_SHRINK_BLK);
            paddle_system_change_size(ctx->paddle, 1);
            return killer ? BLOCK_HIT_ABSORB : BLOCK_HIT_BOUNCE;

        case PAD_EXPAND_BLK:
            (void)block_system_explode(ctx->block, row, col, frame);
            play_block_hit_sound(ctx->audio, PAD_EXPAND_BLK);
            paddle_system_change_size(ctx->paddle, 0);
            return killer ? BLOCK_HIT_ABSORB : BLOCK_HIT_BOUNCE;

        case MGUN_BLK:
            (void)block_system_explode(ctx->block, row, col, frame);
            play_block_hit_sound(ctx->audio, MGUN_BLK);
            special_system_set(ctx->special, SPECIAL_FAST_GUN, 1);
            return killer ? BLOCK_HIT_ABSORB : BLOCK_HIT_BOUNCE;

        case WALLOFF_BLK:
            (void)block_system_explode(ctx->block, row, col, frame);
            play_block_hit_sound(ctx->audio, WALLOFF_BLK);
            special_system_set(ctx->special, SPECIAL_NO_WALLS, 1);
            return killer ? BLOCK_HIT_ABSORB : BLOCK_HIT_BOUNCE;

        case EXTRABALL_BLK:
            (void)block_system_explode(ctx->block, row, col, frame);
            play_block_hit_sound(ctx->audio, EXTRABALL_BLK);
            ctx->lives_left++;
            return killer ? BLOCK_HIT_ABSORB : BLOCK_HIT_BOUNCE;

        case COUNTER_BLK:
            if (killer || block_system_ball_hit_counter(ctx->block, row, col) <= 0)
            {
                (void)block_system_explode(ctx->block, row, col, frame);
                play_block_hit_sound(ctx->audio, COUNTER_BLK);
                return killer ? BLOCK_HIT_ABSORB : BLOCK_HIT_BOUNCE;
            }
            return BLOCK_HIT_BOUNCE;

        case BLACK_BLK:
        {
            int survives = block_system_check_black_hit(ctx->block, row, col, frame);
            if (survives > 0)
                return BLOCK_HIT_BOUNCE;
            (void)block_system_explode(ctx->block, row, col, frame);
            play_block_hit_sound(ctx->audio, BLACK_BLK);
            return killer ? BLOCK_HIT_ABSORB : BLOCK_HIT_BOUNCE;
        }

        case HYPERSPACE_BLK:
            play_block_hit_sound(ctx->audio, HYPERSPACE_BLK);
            return BLOCK_HIT_TELEPORT;

        default:
            (void)block_system_explode(ctx->block, row, col, frame);
            play_block_hit_sound(ctx->audio, block_type);
            return killer ? BLOCK_HIT_ABSORB : BLOCK_HIT_BOUNCE;
    }
}

/*
 * Block explosion finalize handler — fires once per block reaching the
 * end of its KILL_BLK animation (~40 ticks after trigger).  Applies score
 * and per-type finalize-only side effects, matching the per-type switch
 * at original/blocks.c:1550-1637.
 *
 * The cell is already unoccupied when this callback fires.
 */
void game_callbacks_on_block_finalize(int row, int col, int block_type, int hit_points, void *ud)
{
    game_ctx_t *ctx = ud;
    int frame = (int)sdl2_state_frame(ctx->state);

    /* Score deferred from hit time (original/blocks.c:1547). */
    if (hit_points > 0)
    {
        score_system_env_t senv = {
            .x2_active = special_system_is_active(ctx->special, SPECIAL_X2_BONUS),
            .x4_active = special_system_is_active(ctx->special, SPECIAL_X4_BONUS),
        };
        score_system_add(ctx->score, (unsigned long)hit_points, &senv);
    }

    switch (block_type)
    {
        case BOMB_BLK:
            /* 8-neighbor chain reaction (original/blocks.c:1559-1566).
             * Return value intentionally discarded — overlapping chains
             * may try to re-arm an already-exploding neighbor and the
             * silent skip matches original/blocks.c:1825. */
            for (int dr = -1; dr <= 1; dr++)
            {
                for (int dc = -1; dc <= 1; dc++)
                {
                    if (dr == 0 && dc == 0)
                        continue;
                    (void)block_system_explode(ctx->block, row + dr, col + dc,
                                               frame + BLOCK_EXPLODE_DELAY);
                }
            }
            /* Screen shake — matches original/blocks.c:1571-1572 SFX_SHAKE. */
            if (ctx->sfx)
                sfx_system_set_mode(ctx->sfx, SFX_MODE_SHAKE);
            break;

        case BONUSX2_BLK:
            /* x2 explicitly disables x4 (original/blocks.c:1619). */
            special_system_set(ctx->special, SPECIAL_X2_BONUS, 1);
            special_system_set(ctx->special, SPECIAL_X4_BONUS, 0);
            break;

        case BONUSX4_BLK:
            /* x4 explicitly disables x2 (original/blocks.c:1628). */
            special_system_set(ctx->special, SPECIAL_X4_BONUS, 1);
            special_system_set(ctx->special, SPECIAL_X2_BONUS, 0);
            break;

        case TIMER_BLK:
            /* +20 seconds (original/blocks.c:1576, BLOCK_EXTRA_TIME=20). */
            if (ctx->time_remaining < 1000000)
                ctx->time_remaining += BLOCK_EXTRA_TIME;
            break;

        case BULLET_BLK:
            /* +4 ammo (original/blocks.c:1584-1585).  Matches the gun-hit
             * pattern at gun_cb_on_block_hit line 317-319. */
            if (!gun_system_get_unlimited(ctx->gun))
            {
                for (int i = 0; i < BLOCK_NUMBER_OF_BULLETS_NEW_LEVEL; i++)
                    gun_system_add_ammo(ctx->gun);
            }
            break;

        case MAXAMMO_BLK:
            /* Unlimited bullets (original/blocks.c:1590-1591). */
            gun_system_set_unlimited(ctx->gun, 1);
            gun_system_set_ammo(ctx->gun, GUN_MAX_AMMO + 1);
            break;

        case BONUS_BLK:
            /* Bonus counter — killer mode at exactly 10
             * (original/blocks.c:1607). */
            ctx->bonus_count++;
            if (ctx->bonus_count == 10)
                special_system_set(ctx->special, SPECIAL_KILLER, 1);
            break;

        default:
            break;
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
static void ball_cb_on_sound(const char *name, int volume, void *ud)
{
    game_ctx_t *ctx = ud;
    if (ctx->audio)
        sdl2_audio_play_at_percent(ctx->audio, name, volume);
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
                sdl2_audio_play_at_percent(ctx->audio, "paddle", 50);
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

/* Check if bullet at (bx, by) hits a block. Narrows to 3x3 neighborhood. */
static int gun_cb_check_block_hit(int bx, int by, int *out_row, int *out_col, void *ud)
{
    const game_ctx_t *ctx = ud;

    int center_col = bx / GAME_COL_WIDTH;
    int center_row = by / GAME_ROW_HEIGHT;
    int r0 = (center_row > 0) ? center_row - 1 : 0;
    int r1 = (center_row < MAX_ROW - 1) ? center_row + 1 : MAX_ROW - 1;
    int c0 = (center_col > 0) ? center_col - 1 : 0;
    int c1 = (center_col < MAX_COL - 1) ? center_col + 1 : MAX_COL - 1;

    for (int row = r0; row <= r1; row++)
    {
        for (int col = c0; col <= c1; col++)
        {
            if (!block_system_is_occupied(ctx->block, row, col))
                continue;

            block_system_render_info_t info;
            if (block_system_get_render_info(ctx->block, row, col, &info) != BLOCK_SYS_OK)
                continue;

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

/* Handle bullet-block hit: decrement/absorb per block type, award points. */
static void gun_cb_on_block_hit(int row, int col, void *ud)
{
    game_ctx_t *ctx = ud;
    int block_type = block_system_get_type(ctx->block, row, col);
    if (block_type == NONE_BLK)
        return;

    /* Delegate decrement / absorb logic to block_system —
     * original/gun.c:318-350.  Returns 1 if bullet absorbed (block still
     * occupied), 0 if the block would be destroyed by this hit. */
    int absorbed = block_system_decrement_gun_hit(ctx->block, row, col);
    if (absorbed)
        return;

    /* Block destroyed by this bullet — arm the explosion lifecycle so
     * finalize-time effects (BULLET +4 ammo, MAXAMMO unlimited, score,
     * BOMB chain, etc.) fire via game_callbacks_on_block_finalize at
     * the end of the explosion animation.  This matches original
     * blocks.c:1547-1637 where every block dies through
     * ExplodeBlocksPending regardless of whether the killing hit was
     * a bullet or a ball.  Pickup-feedback messages still fire at hit
     * time below for immediate player feedback. */
    int frame = (int)sdl2_state_frame(ctx->state);
    (void)block_system_explode(ctx->block, row, col, frame);
    play_block_hit_sound(ctx->audio, block_type);

    switch (block_type)
    {
        case BULLET_BLK:
            message_system_set(ctx->message, "More ammunition, cool!", 1, frame);
            break;

        case MAXAMMO_BLK:
            message_system_set(ctx->message, "Unlimited bullets!", 1, frame);
            break;

        default:
            break;
    }
}

/* Check if bullet hits any active ball (AABB overlap) */
static int gun_cb_check_ball_hit(int bx, int by, void *ud)
{
    const game_ctx_t *ctx = ud;
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

/* Bullet hit ball: kill the ball — original/gun.c:284 ClearBallNow. */
static void gun_cb_on_ball_hit(int ball_index, void *ud)
{
    game_ctx_t *ctx = ud;
    /* Use the existing factory — it captures no_walls, killer, and all
     * other fields that an inline construction might miss. */
    ball_system_env_t env = game_callbacks_ball_env(ctx);
    ball_system_change_mode(ctx->ball, &env, ball_index, BALL_POP);
}

/* Bullet half-dimensions for the eyedude collision check.  The bullet
 * sprite is 7x10 (original) / 7x16 (modern PNG with alpha padding); we
 * use 4x5 half-extents for a slightly forgiving AABB. */
#define EYEDUDE_BULLET_HW 4
#define EYEDUDE_BULLET_HH 5

/* Check if a bullet at (bx, by) hits the eyedude — AABB overlap when
 * the eyedude is in WALK state.  Reuses eyedude_system_check_collision
 * which already enforces the state guard. */
static int gun_cb_check_eyedude_hit(int bx, int by, void *ud)
{
    const game_ctx_t *ctx = ud;
    return eyedude_system_check_collision(ctx->eyedude, bx, by, EYEDUDE_BULLET_HW,
                                          EYEDUDE_BULLET_HH);
}

/* Bullet hit on eyedude: switch to DIE state.  The next
 * eyedude_system_update tick processes do_die() which fires the
 * on_score / on_message / on_sound callbacks (eyedude_cb_on_score
 * adds EYEDUDE_HIT_BONUS=10000 with x2/x4 multiplier env applied),
 * so we do NOT award points here — that would double-count. */
static void gun_cb_on_eyedude_hit(void *ud)
{
    game_ctx_t *ctx = ud;
    eyedude_system_set_state(ctx->eyedude, EYEDUDE_STATE_DIE);
}

static void gun_cb_on_sound(const char *name, int volume, void *ud)
{
    game_ctx_t *ctx = ud;
    if (ctx->audio)
        sdl2_audio_play_at_percent(ctx->audio, name, volume);
}

static int gun_cb_is_ball_waiting(void *ud)
{
    const game_ctx_t *ctx = ud;
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
    sdl2_state_mode_t current = (mode == INTRO_MODE_INTRO) ? SDL2ST_INTRO : SDL2ST_INSTRUCT;
    sdl2_state_transition(ctx->state, game_callbacks_attract_next(current));
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
    (void)savegame_system_autosave((game_ctx_t *)ud);
}

static void bonus_cb_on_sound(const char *name, int volume, void *ud)
{
    game_ctx_t *ctx = ud;
    if (ctx->audio)
        sdl2_audio_play_at_percent(ctx->audio, name, volume);
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
    sdl2_state_mode_t current = (mode == DEMO_MODE_DEMO) ? SDL2ST_DEMO : SDL2ST_PREVIEW;
    sdl2_state_transition(ctx->state, game_callbacks_attract_next(current));
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
    sdl2_state_mode_t current = (mode == KEYS_MODE_GAME) ? SDL2ST_KEYS : SDL2ST_KEYSEDIT;
    sdl2_state_transition(ctx->state, game_callbacks_attract_next(current));
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
    /* Sound (gate@50, original/highscore.c:492) is stored in
     * highscore_system's sound struct and relayed by
     * mode_highscore_update — matches the keys/presents/intro/etc.
     * relay pattern.  Callback only handles the state transition.
     * game_active (set on a game-over display) is cleared by
     * mode_intro_enter, the single exit every attract advance from
     * Highscore lands on (attract_next(HIGHSCORE) == INTRO).  See ADR-055. */
    sdl2_state_transition(ctx->state, game_callbacks_attract_next(SDL2ST_HIGHSCORE));
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

static void sfx_cb_on_sound(const char *name, int volume, void *ud)
{
    game_ctx_t *ctx = ud;
    if (ctx->audio)
        sdl2_audio_play_at_percent(ctx->audio, name, volume);
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
    const game_ctx_t *ctx = ud;
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

static void eyedude_cb_on_sound(const char *name, int volume, void *ud)
{
    game_ctx_t *ctx = ud;
    if (ctx->audio)
        sdl2_audio_play_at_percent(ctx->audio, name, volume);
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

/* =========================================================================
 * Editor system callbacks
 * ========================================================================= */

static void editor_cb_add_block(int row, int col, int block_type, int counter_slide, int visible,
                                void *ud)
{
    (void)visible;
    game_ctx_t *ctx = ud;
    block_system_add(ctx->block, row, col, block_type, counter_slide, 0);
}

static void editor_cb_erase_block(int row, int col, void *ud)
{
    game_ctx_t *ctx = ud;
    block_system_clear(ctx->block, row, col);
}

static void editor_cb_clear_grid(void *ud)
{
    game_ctx_t *ctx = ud;
    block_system_clear_all(ctx->block);
}

static int editor_cb_query_cell(int row, int col, editor_cell_t *cell, void *ud)
{
    const game_ctx_t *ctx = ud;
    block_system_render_info_t info;
    if (block_system_get_render_info(ctx->block, row, col, &info) != BLOCK_SYS_OK || !info.occupied)
        return 0;
    cell->occupied = 1;
    cell->block_type = info.random ? RANDOM_BLK : info.block_type;
    cell->counter_slide = info.counter_slide;
    return 1;
}

static void editor_cb_on_sound(const char *name, int volume, void *ud)
{
    game_ctx_t *ctx = ud;
    if (ctx->audio)
        sdl2_audio_play_at_percent(ctx->audio, name, volume);
}

static void editor_cb_on_message(const char *message, int sticky, void *ud)
{
    game_ctx_t *ctx = ud;
    int frame = (int)sdl2_state_frame(ctx->state);
    message_system_set(ctx->message, message, sticky ? 0 : 1, frame);
}

static int editor_cb_load_level(const char *path, void *ud)
{
    game_ctx_t *ctx = ud;
    block_system_clear_all(ctx->block);
    return level_system_load_file(ctx->level, path) == LEVEL_SYS_OK ? 1 : 0;
}

/*
 * Async yes/no dialogue request — editor_system.c cannot block (the
 * fixed-timestep loop must keep returning every frame), so this pushes
 * SDL2ST_DIALOGUE and returns immediately.  The answer arrives later via
 * editor_system_dialogue_result(), routed through mode_dialogue_exit's
 * editor_dialogue_pending flag (see game_modes.c).  Mirrors the existing
 * abort/quit/level-set push+open+flag pattern at game_input.c:321-326.
 */
static int editor_cb_request_yes_no(const char *message, void *ud)
{
    game_ctx_t *ctx = ud;
    if (sdl2_state_push_dialogue(ctx->state) != SDL2ST_OK)
        return 0;
    dialogue_system_open(ctx->dialogue, message, DIALOGUE_ICON_TEXT, DIALOGUE_VALIDATION_YES_NO);
    game_modes_set_editor_dialogue_pending();
    return 1;
}

/*
 * Reverse mapping: block type → level file character.
 * Inverse of level_system_char_to_block().
 *
 * counter_slide selects the '0'-'5' digit for COUNTER_BLK, matching
 * original/file.c:658-684.  Clamped defensively; callers pass values
 * already bounded to 0..5 by block_system.
 */
static char block_type_to_char(int block_type, int counter_slide)
{
    switch (block_type)
    {
        case RED_BLK:
            return 'r';
        case GREEN_BLK:
            return 'g';
        case BLUE_BLK:
            return 'b';
        case TAN_BLK:
            return 't';
        case PURPLE_BLK:
            return 'p';
        case YELLOW_BLK:
            return 'y';
        case BLACK_BLK:
            return 'w';
        case COUNTER_BLK:
        {
            int slide = counter_slide < 0 ? 0 : (counter_slide > 5 ? 5 : counter_slide);
            return (char)('0' + slide);
        }
        case BOMB_BLK:
            return 'X';
        case DEATH_BLK:
            return 'D';
        case HYPERSPACE_BLK:
            return 'H';
        case BULLET_BLK:
            return 'B';
        case MAXAMMO_BLK:
            return 'c';
        case ROAMER_BLK:
            return '+';
        case EXTRABALL_BLK:
            return 'L';
        case MGUN_BLK:
            return 'M';
        case WALLOFF_BLK:
            return 'W';
        case RANDOM_BLK:
            return '?';
        case DROP_BLK:
            return 'd';
        case TIMER_BLK:
            return 'T';
        case MULTIBALL_BLK:
            return 'm';
        case STICKY_BLK:
            return 's';
        case REVERSE_BLK:
            return 'R';
        case PAD_SHRINK_BLK:
            return '<';
        case PAD_EXPAND_BLK:
            return '>';
        default:
            return '.';
    }
}

/*
 * Resolve the level-file character for (row, col), reading the block's
 * random flag and counter_slide in one call so a RANDOM_BLK cell saves
 * '?' (original/file.c:562-609) and a COUNTER_BLK cell saves its real
 * '0'-'5' digit (original/file.c:658-684) instead of losing both to the
 * un-annotated resolved color / hardcoded '0'.
 */
static char resolve_save_char(const game_ctx_t *ctx, int row, int col)
{
    block_system_render_info_t info;
    if (block_system_get_render_info(ctx->block, row, col, &info) != BLOCK_SYS_OK || !info.occupied)
        return block_type_to_char(NONE_BLK, 0);

    int effective_type = info.random ? RANDOM_BLK : info.block_type;
    return block_type_to_char(effective_type, info.counter_slide);
}

static int editor_cb_save_level(const char *path, void *ud)
{
    const game_ctx_t *ctx = ud;
    FILE *fp = fopen(path, "w");
    if (!fp)
        return 0;

    /* Line 1: level title */
    const char *title = editor_system_get_level_title(ctx->editor);
    fprintf(fp, "%s\n", (title && title[0] != '\0') ? title : "Untitled");

    /* Line 2: time bonus */
    fprintf(fp, "%d\n", level_system_get_time_bonus(ctx->level));

    /* Lines 3-17: 15 rows of 9 characters */
    for (int row = 0; row < 15; row++)
    {
        for (int col = 0; col < 9; col++)
        {
            fputc(resolve_save_char(ctx, row, col), fp);
        }
        fputc('\n', fp);
    }

    fclose(fp);
    return 1;
}

/*
 * Async text/numeric input dialogue request — same non-blocking pattern
 * as editor_cb_request_yes_no.  numeric_only selects the validation mode
 * so the dialogue itself filters keystrokes to digits when appropriate
 * (e.g. level numbers, time bonus), matching the original's per-prompt
 * icon (docs/specs/2026-07-11-editor-parity.md S1.4 — TEXT_ICON
 * throughout, no DIALOGUE_ICON_DISK use in any editor flow).
 */
static int editor_cb_request_input(const char *message, int numeric_only, void *ud)
{
    game_ctx_t *ctx = ud;
    if (sdl2_state_push_dialogue(ctx->state) != SDL2ST_OK)
        return 0;
    dialogue_validation_t validation =
        numeric_only ? DIALOGUE_VALIDATION_NUMERIC : DIALOGUE_VALIDATION_TEXT;
    dialogue_system_open(ctx->dialogue, message, DIALOGUE_ICON_TEXT, validation);
    game_modes_set_editor_dialogue_pending();
    return 1;
}

/* Non-sticky transient error display — matches the original's
 * ErrorMessage dialogs (original/editor.c:163, :193, :382, :864, :918),
 * which show briefly rather than persisting. */
static void editor_cb_on_error(const char *message, void *ud)
{
    game_ctx_t *ctx = ud;
    int frame = (int)sdl2_state_frame(ctx->state);
    message_system_set(ctx->message, message, 0, frame);
}

static void editor_cb_on_set_time(int seconds, void *ud)
{
    game_ctx_t *ctx = ud;
    level_system_set_time_bonus(ctx->level, seconds);
}

static void editor_cb_on_finish(void *ud)
{
    game_ctx_t *ctx = ud;
    sdl2_state_transition(ctx->state, SDL2ST_INTRO);
}

static void editor_cb_on_playtest_start(void *ud)
{
    game_ctx_t *ctx = ud;
    sdl2_state_transition(ctx->state, SDL2ST_GAME);
}

static void editor_cb_on_playtest_end(void *ud)
{
    game_ctx_t *ctx = ud;
    sdl2_state_transition(ctx->state, SDL2ST_EDIT);
}

editor_system_callbacks_t game_callbacks_editor(void)
{
    editor_system_callbacks_t cbs = {
        .on_add_block = editor_cb_add_block,
        .on_erase_block = editor_cb_erase_block,
        .on_clear_grid = editor_cb_clear_grid,
        .query_cell = editor_cb_query_cell,
        .on_sound = editor_cb_on_sound,
        .on_message = editor_cb_on_message,
        .on_load_level = editor_cb_load_level,
        .on_save_level = editor_cb_save_level,
        .on_error = editor_cb_on_error,
        .on_request_yes_no_dialogue = editor_cb_request_yes_no,
        .on_request_input_dialogue = editor_cb_request_input,
        .on_set_time = editor_cb_on_set_time,
        .on_finish = editor_cb_on_finish,
        .on_playtest_start = editor_cb_on_playtest_start,
        .on_playtest_end = editor_cb_on_playtest_end,
    };
    return cbs;
}
