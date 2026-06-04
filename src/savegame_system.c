/*
 * savegame_system.c — full mid-level state capture/restore.
 *
 * Phase 3 of savegame v2.  See include/savegame_system.h for the API
 * contract and docs/specs/2026-05-28-savegame-v2.md for design.
 */

#include "savegame_system.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ball_system.h"
#include "block_system.h"
#include "block_types.h"
#include "eyedude_system.h"
#include "game_callbacks.h" /* game_callbacks_ball_env */
#include "gun_system.h"
#include "level_system.h"
#include "message_system.h"
#include "paddle_system.h"
#include "paths.h"
#include "score_system.h"
#include "sdl2_state.h"
#include "special_system.h"

/* =========================================================================
 * Validation — reject malformed save data before it reaches the
 * subsystems.  Save files are user-editable; restoring untrusted
 * out-of-range enum or count values has caused real crashes
 * (eyedude.slide negative → negative modulo → OOB index into
 * left_keys[]; large next_frame_offset → signed-int overflow when
 * added to current frame).
 *
 * Bounds chosen for defense-in-depth, not for accuracy: a tampered
 * file far outside these ranges is unambiguously invalid, while
 * any legitimately produced save sits well within them.
 * ========================================================================= */

#define SAVEGAME_MAX_LEVEL 999
#define SAVEGAME_MAX_LIVES 99
#define SAVEGAME_MAX_BULLETS 9999
#define SAVEGAME_MAX_TILTS 99
#define SAVEGAME_MAX_BONUS_COUNT 99
/* Original BLACK cooldown is 30 frames; cap at ~10s at 60fps to
 * absorb any reasonable per-tick rate without enabling overflow. */
#define SAVEGAME_MAX_FRAME_OFFSET 600
#define SAVEGAME_MAX_COUNTER_SLIDE 9
/* Generous bounds for animation/state indices so legitimate ranges
 * (~0-127) pass while preventing negative-modulo OOB. */
#define SAVEGAME_MAX_ANIM_INDEX 4095

static int in_range(int v, int lo, int hi)
{
    return v >= lo && v <= hi;
}

static int validate_info(const savegame_data_t *info)
{
    if (!in_range((int)info->level, 1, SAVEGAME_MAX_LEVEL) ||
        !in_range(info->lives_left, 0, SAVEGAME_MAX_LIVES) ||
        !in_range(info->num_bullets, 0, SAVEGAME_MAX_BULLETS) ||
        !in_range(info->user_tilts, 0, SAVEGAME_MAX_TILTS) ||
        !in_range(info->bonus_count, 0, SAVEGAME_MAX_BONUS_COUNT))
    {
        return 0;
    }
    if (!in_range(info->paddle_size_type, PADDLE_SIZE_SMALL, PADDLE_SIZE_HUGE))
    {
        return 0;
    }
    /* Eyedude: state/dir are small enums; slide drives a modulo
     * sprite-table index and MUST be non-negative. */
    if (!in_range(info->eyedude.state, EYEDUDE_STATE_NONE, EYEDUDE_STATE_DIE) ||
        !in_range(info->eyedude.dir, 0, EYEDUDE_DIR_DEAD) ||
        !in_range(info->eyedude.slide, 0, SAVEGAME_MAX_ANIM_INDEX))
    {
        return 0;
    }
    /* Ball state/wait_mode are small enums.  Position bounds are
     * lenient — the gameplay layer clamps offscreen balls. */
    for (int i = 0; i < MAX_BALLS; i++)
    {
        const savegame_ball_t *b = &info->balls[i];
        if (!in_range(b->state, BALL_POP, BALL_NONE) ||
            !in_range(b->wait_mode, BALL_POP, BALL_NONE))
        {
            return 0;
        }
    }
    return 1;
}

static int validate_level(const savegame_level_t *level)
{
    for (int r = 0; r < MAX_ROW; r++)
    {
        for (int c = 0; c < MAX_COL; c++)
        {
            const savegame_cell_t *cell = &level->cells[r][c];
            if (!cell->occupied)
            {
                continue;
            }
            /* block_type accepts NONE_BLK/KILL_BLK negative sentinels
             * via the existing block_types.h range. */
            if (!in_range(cell->block_type, NONE_BLK, MAX_BLOCKS - 1) ||
                !in_range(cell->counter_slide, 0, SAVEGAME_MAX_COUNTER_SLIDE) ||
                !in_range(cell->next_frame_offset, 0, SAVEGAME_MAX_FRAME_OFFSET))
            {
                return 0;
            }
        }
    }
    return 1;
}

/* =========================================================================
 * Pure capture — no I/O, no message
 * ========================================================================= */

void savegame_system_capture(const game_ctx_t *ctx, savegame_data_t *out_info,
                             savegame_level_t *out_level)
{
    if (ctx == NULL || out_info == NULL)
    {
        return;
    }

    savegame_io_init(out_info);

    /* --- Player / meta --------------------------------------------------- */
    out_info->score = score_system_get(ctx->score);
    out_info->level = (unsigned long)ctx->level_number;
    out_info->level_time = ctx->time_bonus_total;
    out_info->time_remaining = ctx->time_remaining;
    /* game_time = elapsed session seconds, net of paused.  Matches the
     * formula in game_modes.c when posting a highscore. */
    {
        time_t now = time(NULL);
        unsigned long elapsed = (ctx->game_start > 0 && now >= ctx->game_start)
                                    ? (unsigned long)(now - ctx->game_start)
                                    : 0UL;
        unsigned long paused = (unsigned long)ctx->paused_seconds;
        out_info->game_time = (elapsed > paused) ? (elapsed - paused) : 0UL;
    }
    out_info->lives_left = ctx->lives_left;
    out_info->start_level = ctx->start_level;
    out_info->user_tilts = ctx->user_tilts;
    out_info->bonus_count = ctx->bonus_count;

    /* --- Paddle ---------------------------------------------------------- */
    out_info->paddle_pos = paddle_system_get_pos(ctx->paddle);
    out_info->paddle_size = paddle_system_get_size(ctx->paddle);
    out_info->paddle_size_type = paddle_system_get_size_type(ctx->paddle);
    out_info->paddle_reverse = paddle_system_get_reverse(ctx->paddle);
    out_info->paddle_sticky = paddle_system_get_sticky(ctx->paddle);

    /* --- Gun ------------------------------------------------------------- */
    out_info->num_bullets = gun_system_get_ammo(ctx->gun);
    out_info->gun_unlimited = gun_system_get_unlimited(ctx->gun);

    /* --- Specials -------------------------------------------------------- */
    out_info->specials.sticky = special_system_is_active(ctx->special, SPECIAL_STICKY);
    out_info->specials.saving = special_system_is_active(ctx->special, SPECIAL_SAVING);
    out_info->specials.fast_gun = special_system_is_active(ctx->special, SPECIAL_FAST_GUN);
    out_info->specials.no_walls = special_system_is_active(ctx->special, SPECIAL_NO_WALLS);
    out_info->specials.killer = special_system_is_active(ctx->special, SPECIAL_KILLER);
    out_info->specials.x2 = special_system_is_active(ctx->special, SPECIAL_X2_BONUS);
    out_info->specials.x4 = special_system_is_active(ctx->special, SPECIAL_X4_BONUS);

    /* --- Eyedude --------------------------------------------------------- */
    eyedude_save_state_t es = eyedude_system_get_save_state(ctx->eyedude);
    out_info->eyedude.state = (int)es.state;
    out_info->eyedude.dir = (int)es.dir;
    out_info->eyedude.x = es.x;
    out_info->eyedude.y = es.y;
    out_info->eyedude.slide = es.slide;
    out_info->eyedude.inc = es.inc;
    out_info->eyedude.turn = es.turn;

    /* --- Balls ----------------------------------------------------------- */
    for (int i = 0; i < MAX_BALLS; i++)
    {
        savegame_ball_t *b = &out_info->balls[i];
        ball_system_render_info_t info;
        if (ball_system_get_render_info(ctx->ball, i, &info) != BALL_SYS_OK || !info.active)
        {
            b->active = 0;
            b->state = (int)BALL_NONE;
            continue;
        }
        b->active = 1;
        b->state = (int)info.state;
        b->x = info.x;
        b->y = info.y;
        int dx = 0;
        int dy = 0;
        (void)ball_system_get_velocity(ctx->ball, i, &dx, &dy);
        b->dx = dx;
        b->dy = dy;
        b->wait_mode = (int)ball_system_get_wait_mode(ctx->ball, i);
    }

    /* --- Level grid ------------------------------------------------------ */
    if (out_level != NULL)
    {
        savegame_level_init(out_level);
        const char *title = level_system_get_title(ctx->level);
        if (title)
        {
            strncpy(out_level->title, title, LEVEL_TITLE_MAX - 1);
            out_level->title[LEVEL_TITLE_MAX - 1] = '\0';
        }
        out_level->time_bonus = level_system_get_time_bonus(ctx->level);

        int frame = (int)sdl2_state_frame(ctx->state);
        for (int r = 0; r < MAX_ROW; r++)
        {
            for (int c = 0; c < MAX_COL; c++)
            {
                if (!block_system_is_occupied(ctx->block, r, c))
                {
                    continue;
                }
                savegame_cell_t *cell = &out_level->cells[r][c];
                cell->occupied = 1;
                cell->block_type = block_system_get_type(ctx->block, r, c);
                cell->hit_points = block_system_get_hit_points(ctx->block, r, c);
                cell->random = block_system_get_random(ctx->block, r, c);
                /* counter_slide isn't exposed by a getter (it's part of
                 * the render info); use the render info path. */
                block_system_render_info_t bri;
                if (block_system_get_render_info(ctx->block, r, c, &bri) == BLOCK_SYS_OK)
                {
                    cell->counter_slide = bri.counter_slide;
                }
                /* BLACK_BLK cooldown: store as offset from current
                 * frame so the value survives a save/reload cycle. */
                if (cell->block_type == BLACK_BLK)
                {
                    int nf = block_system_get_black_next_frame(ctx->block, r, c);
                    cell->next_frame_offset = nf > frame ? nf - frame : 0;
                }
            }
        }
    }
}

/* =========================================================================
 * Pure restore — no I/O, no message
 * ========================================================================= */

void savegame_system_restore(game_ctx_t *ctx, const savegame_data_t *info,
                             const savegame_level_t *level)
{
    if (ctx == NULL || info == NULL)
    {
        return;
    }

    /* --- Player / meta --------------------------------------------------- */
    score_system_set(ctx->score, info->score);
    ctx->level_number = (int)info->level;
    ctx->time_bonus_total = info->level_time;
    ctx->time_remaining = info->time_remaining;
    /* Rebase game_start so subsequent reads of elapsed game_time
     * match the saved value.  Capture's formula will then yield
     * info->game_time at the moment of restore. */
    {
        time_t now = time(NULL);
        ctx->game_start = now - (time_t)info->game_time;
        ctx->paused_seconds = 0;
    }
    ctx->lives_left = info->lives_left;
    ctx->start_level = info->start_level;
    ctx->user_tilts = info->user_tilts;
    ctx->bonus_count = info->bonus_count;

    /* --- Paddle ---------------------------------------------------------- */
    paddle_system_set_size(ctx->paddle, info->paddle_size_type);
    paddle_system_set_pos(ctx->paddle, info->paddle_pos);
    paddle_system_set_reverse(ctx->paddle, info->paddle_reverse);
    paddle_system_set_sticky(ctx->paddle, info->paddle_sticky);

    /* --- Gun ------------------------------------------------------------- */
    gun_system_set_ammo(ctx->gun, info->num_bullets);
    gun_system_set_unlimited(ctx->gun, info->gun_unlimited);

    /* --- Specials -------------------------------------------------------- */
    /* Turn off first so any flag-clear side effects fire cleanly, then
     * set each from the save (matches the spec's restore order). */
    special_system_turn_off(ctx->special);
    special_system_set(ctx->special, SPECIAL_STICKY, info->specials.sticky);
    special_system_set(ctx->special, SPECIAL_SAVING, info->specials.saving);
    special_system_set(ctx->special, SPECIAL_FAST_GUN, info->specials.fast_gun);
    special_system_set(ctx->special, SPECIAL_NO_WALLS, info->specials.no_walls);
    special_system_set(ctx->special, SPECIAL_KILLER, info->specials.killer);
    special_system_set(ctx->special, SPECIAL_X2_BONUS, info->specials.x2);
    special_system_set(ctx->special, SPECIAL_X4_BONUS, info->specials.x4);

    /* --- Eyedude --------------------------------------------------------- */
    eyedude_save_state_t es = {
        .state = (eyedude_state_t)info->eyedude.state,
        .dir = (eyedude_dir_t)info->eyedude.dir,
        .x = info->eyedude.x,
        .y = info->eyedude.y,
        .slide = info->eyedude.slide,
        .inc = info->eyedude.inc,
        .turn = info->eyedude.turn,
    };
    eyedude_system_restore(ctx->eyedude, &es);

    /* --- Balls ----------------------------------------------------------- */
    int frame = (int)sdl2_state_frame(ctx->state);
    ball_system_clear_all(ctx->ball);
    for (int i = 0; i < MAX_BALLS; i++)
    {
        const savegame_ball_t *b = &info->balls[i];
        if (!b->active)
        {
            continue;
        }
        (void)ball_system_restore(ctx->ball, i, frame, b->active, (enum BallStates)b->state, b->x,
                                  b->y, b->dx, b->dy, (enum BallStates)b->wait_mode);
    }

    /* --- Level grid (optional) ------------------------------------------- */
    if (level != NULL)
    {
        block_system_clear_all(ctx->block);
        for (int r = 0; r < MAX_ROW; r++)
        {
            for (int c = 0; c < MAX_COL; c++)
            {
                const savegame_cell_t *cell = &level->cells[r][c];
                if (!cell->occupied)
                {
                    continue;
                }
                (void)block_system_add(ctx->block, r, c, cell->block_type, cell->counter_slide,
                                       frame);
                if (cell->random)
                {
                    block_system_set_random(ctx->block, r, c, 1);
                }
                if (cell->block_type == BLACK_BLK && cell->next_frame_offset > 0)
                {
                    block_system_set_black_next_frame(ctx->block, r, c,
                                                      frame + cell->next_frame_offset);
                }
            }
        }
    }
}

/* =========================================================================
 * Disk save — info + level (atomic per file, not across files)
 * ========================================================================= */

int savegame_system_save(game_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }

    char info_path[PATHS_MAX_PATH];
    char level_path[PATHS_MAX_PATH];
    if (paths_save_info(&ctx->paths, info_path, sizeof(info_path)) != PATHS_OK ||
        paths_save_level(&ctx->paths, level_path, sizeof(level_path)) != PATHS_OK)
    {
        return 0;
    }

    savegame_data_t info;
    savegame_level_t lvl;
    savegame_system_capture(ctx, &info, &lvl);

    /* Write level first, then info: a partial save (level OK, info
     * failed) is recoverable on next attempt; the reverse leaves an
     * info file pointing at a missing level. */
    int frame = (int)sdl2_state_frame(ctx->state);
    if (savegame_level_write(level_path, &lvl) != SAVEGAME_IO_OK ||
        savegame_io_write(info_path, &info) != SAVEGAME_IO_OK)
    {
        message_system_set(ctx->message, "Save failed", 1, frame);
        return 0;
    }

    message_system_set(ctx->message, "Game Saved!", 1, frame);
    return 1;
}

/* =========================================================================
 * Disk load — info first, level if present, else canonical fallback
 * ========================================================================= */

static int reload_canonical_level(game_ctx_t *ctx, int level_number)
{
    int file_num = level_system_wrap_number(level_number);
    char filename[32];
    snprintf(filename, sizeof(filename), "level%02d.data", file_num);

    char level_path[PATHS_MAX_PATH];
    if (paths_level_file(&ctx->paths, filename, level_path, sizeof(level_path)) != PATHS_OK)
    {
        return 0;
    }
    block_system_clear_all(ctx->block);
    return level_system_load_file(ctx->level, level_path) == 0;
}

int savegame_system_load(game_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }

    char info_path[PATHS_MAX_PATH];
    char level_path[PATHS_MAX_PATH];
    if (paths_save_info(&ctx->paths, info_path, sizeof(info_path)) != PATHS_OK ||
        paths_save_level(&ctx->paths, level_path, sizeof(level_path)) != PATHS_OK)
    {
        return 0;
    }

    savegame_data_t info;
    if (savegame_io_read(info_path, &info) != SAVEGAME_IO_OK)
    {
        int frame = (int)sdl2_state_frame(ctx->state);
        message_system_set(ctx->message, "No saved game found", 1, frame);
        return 0;
    }
    if (!validate_info(&info))
    {
        int frame = (int)sdl2_state_frame(ctx->state);
        message_system_set(ctx->message, "Save file corrupted", 1, frame);
        return 0;
    }

    /* Try the level snapshot; fall back to canonical .data file when
     * the autosave path skipped writing one. */
    savegame_level_t lvl;
    savegame_level_init(&lvl);
    int has_level_snapshot = (savegame_level_read(level_path, &lvl) == SAVEGAME_IO_OK);
    if (has_level_snapshot && !validate_level(&lvl))
    {
        int frame = (int)sdl2_state_frame(ctx->state);
        message_system_set(ctx->message, "Save file corrupted", 1, frame);
        return 0;
    }

    int level_ok = 1;
    if (has_level_snapshot)
    {
        savegame_system_restore(ctx, &info, &lvl);
    }
    else
    {
        savegame_system_restore(ctx, &info, NULL);
        level_ok = reload_canonical_level(ctx, (int)info.level);
    }

    int frame = (int)sdl2_state_frame(ctx->state);
    if (level_ok)
    {
        message_system_set(ctx->message, "Game Restored!", 1, frame);
    }
    else
    {
        /* Info restored but the canonical level file couldn't be
         * loaded — the player is in a no-grid state.  Surface this
         * rather than claim success. */
        message_system_set(ctx->message, "Game Restored (no level data)", 1, frame);
    }
    return 1;
}

/* =========================================================================
 * Auto-save (bonus screen) — info only, no level
 * ========================================================================= */

int savegame_system_autosave(game_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }

    char info_path[PATHS_MAX_PATH];
    char level_path[PATHS_MAX_PATH];
    if (paths_save_info(&ctx->paths, info_path, sizeof(info_path)) != PATHS_OK ||
        paths_save_level(&ctx->paths, level_path, sizeof(level_path)) != PATHS_OK)
    {
        return 0;
    }

    /* Auto-save fires after a level is cleared.  Writing the empty
     * grid would trigger immediate level-complete on the next load,
     * so capture info only and delete any stale level snapshot from
     * an earlier manual save. */
    savegame_data_t info;
    savegame_system_capture(ctx, &info, NULL);

    if (savegame_io_write(info_path, &info) != SAVEGAME_IO_OK)
    {
        return 0;
    }
    (void)savegame_io_delete(level_path);
    return 1;
}
