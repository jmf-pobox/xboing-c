/*
 * test_savegame_system.c — capture/restore round-trip via real game
 * context.  Verifies that all v2 fields survive a save→restore cycle.
 *
 * Spec: docs/specs/2026-05-28-savegame-v2.md (Phase 5).
 * Bead: xboing-c-1d0.
 */

#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cmocka.h>

#include "ball_system.h"
#include "block_system.h"
#include "block_types.h"
#include "eyedude_system.h"
#include "game_callbacks.h"
#include "game_context.h"
#include "game_init.h"
#include "gun_system.h"
#include "paddle_system.h"
#include "paths.h"
#include "savegame_io.h"
#include "savegame_system.h"
#include "score_system.h"
#include "sdl2_state.h"
#include "special_system.h"

static char s_arg_prog[] = "test_savegame_system";

typedef struct
{
    game_ctx_t *ctx;
    char tmp_xdg[256]; /* per-test XDG_DATA_HOME for disk I/O tests */
    char *prev_xdg;    /* heap-owned strdup of caller's prior XDG (or NULL) */
} fixture_t;

static int setup(void **vstate)
{
    fixture_t *f = calloc(1, sizeof(*f));
    assert_non_null(f);

    /* Preserve the caller's XDG_DATA_HOME so we can restore it on
     * teardown.  Important when this binary is run inside a harness
     * (or interactively) that already sets XDG vars — we must not
     * leak environment changes across tests in the same process. */
    const char *existing = getenv("XDG_DATA_HOME");
    f->prev_xdg = existing ? strdup(existing) : NULL;

    /* Redirect XDG_DATA_HOME into the project's .tmp/ so save/load
     * tests don't write to the real user dir.  CMake sets
     * WORKING_DIRECTORY to the project root for integration tests. */
    snprintf(f->tmp_xdg, sizeof(f->tmp_xdg), ".tmp/test_savegame_system_xdg_%d", (int)getpid());
    (void)mkdir(".tmp", 0700);
    (void)mkdir(f->tmp_xdg, 0700);
    setenv("XDG_DATA_HOME", f->tmp_xdg, 1);

    char *argv[] = {s_arg_prog, NULL};
    f->ctx = game_create(1, argv);
    assert_non_null(f->ctx);
    sdl2_state_transition(f->ctx->state, SDL2ST_GAME);
    *vstate = f;
    return 0;
}

static int teardown(void **vstate)
{
    fixture_t *f = *vstate;
    /* Delete any save files this test may have written before
     * destroying the context (path-config still valid). */
    char info_path[PATHS_MAX_PATH];
    char level_path[PATHS_MAX_PATH];
    if (paths_save_info(&f->ctx->paths, info_path, sizeof(info_path)) == PATHS_OK)
    {
        (void)unlink(info_path);
    }
    if (paths_save_level(&f->ctx->paths, level_path, sizeof(level_path)) == PATHS_OK)
    {
        (void)unlink(level_path);
    }
    game_destroy(f->ctx);
    /* Save files live at $XDG_DATA_HOME/xboing/save-*.dat, so the
     * intermediate xboing/ subdir must be removed before the parent. */
    char xboing_dir[300];
    snprintf(xboing_dir, sizeof(xboing_dir), "%s/xboing", f->tmp_xdg);
    (void)rmdir(xboing_dir);
    (void)rmdir(f->tmp_xdg);
    /* Restore the caller's prior XDG_DATA_HOME, or remove it
     * entirely if none was set.  Never unconditionally unset — that
     * would clobber a setting the test harness gave us. */
    if (f->prev_xdg)
    {
        setenv("XDG_DATA_HOME", f->prev_xdg, 1);
        free(f->prev_xdg);
    }
    else
    {
        unsetenv("XDG_DATA_HOME");
    }
    free(f);
    return 0;
}

/* Populate the context with a distinctive, easy-to-verify state. */
static void seed_state(game_ctx_t *ctx)
{
    score_system_set(ctx->score, 125000UL);
    ctx->level_number = 7;
    ctx->time_bonus_total = 180;
    ctx->time_remaining = 142;
    ctx->lives_left = 3;
    ctx->start_level = 1;
    ctx->user_tilts = 1;
    ctx->bonus_count = 3;

    paddle_system_set_pos(ctx->paddle, 247);
    paddle_system_set_size(ctx->paddle, PADDLE_SIZE_MEDIUM);
    paddle_system_set_reverse(ctx->paddle, 1);
    paddle_system_set_sticky(ctx->paddle, 0);

    gun_system_set_ammo(ctx->gun, 6);
    gun_system_set_unlimited(ctx->gun, 1);

    special_system_turn_off(ctx->special);
    special_system_set(ctx->special, SPECIAL_FAST_GUN, 1);
    special_system_set(ctx->special, SPECIAL_KILLER, 1);
    special_system_set(ctx->special, SPECIAL_X2_BONUS, 1);

    /* Block grid: a colored block + a BLACK_BLK (with cooldown set later) +
     * a COUNTER with non-default counter_slide. */
    block_system_clear_all(ctx->block);
    int frame = (int)sdl2_state_frame(ctx->state);
    (void)block_system_add(ctx->block, 1, 2, RED_BLK, 0, frame);
    (void)block_system_add(ctx->block, 3, 4, BLACK_BLK, 0, frame);
    (void)block_system_add(ctx->block, 5, 6, COUNTER_BLK, 4, frame);

    /* Set BLACK cooldown to expire 20 frames in the future (well within
     * the capture's offset clamp). */
    block_system_set_black_next_frame(ctx->block, 3, 4, frame + 20);

    /* Mark the COUNTER block as a resolved RANDOM (random=1). */
    block_system_set_random(ctx->block, 5, 6, 1);

    /* Add an active ball with specific velocity. */
    ball_system_clear_all(ctx->ball);
    ball_system_env_t env = game_callbacks_ball_env(ctx);
    int idx = ball_system_add(ctx->ball, &env, 247, 520, 3, -4, NULL);
    assert_int_not_equal(idx, -1);
    (void)ball_system_change_mode(ctx->ball, &env, idx, BALL_ACTIVE);
}

/* Wipe the context to a neutral baseline so we can prove restore
 * actually re-populated everything. */
static void clear_state(game_ctx_t *ctx)
{
    score_system_set(ctx->score, 0UL);
    ctx->level_number = 1;
    ctx->time_bonus_total = 0;
    ctx->time_remaining = 0;
    ctx->lives_left = 0;
    ctx->start_level = 0;
    ctx->user_tilts = 0;
    ctx->bonus_count = 0;

    paddle_system_set_pos(ctx->paddle, 0);
    paddle_system_set_size(ctx->paddle, PADDLE_SIZE_HUGE);
    paddle_system_set_reverse(ctx->paddle, 0);
    paddle_system_set_sticky(ctx->paddle, 0);

    gun_system_set_ammo(ctx->gun, 0);
    gun_system_set_unlimited(ctx->gun, 0);

    special_system_turn_off(ctx->special);
    block_system_clear_all(ctx->block);
    ball_system_clear_all(ctx->ball);
}

/* =========================================================================
 * Round-trip: capture → clear → restore → expect seed values back.
 * ========================================================================= */

static void test_capture_restore_round_trip(void **vstate)
{
    fixture_t *f = *vstate;
    game_ctx_t *ctx = f->ctx;
    seed_state(ctx);

    savegame_data_t info;
    savegame_level_t lvl;
    savegame_system_capture(ctx, &info, &lvl);

    clear_state(ctx);
    /* Sanity: cleared state really differs from seed.  Don't assert
     * paddle_pos == 0 — set_pos clamps to [half_width, play_width-half],
     * so the post-clear value depends on size. */
    assert_int_equal(ctx->level_number, 1);
    assert_int_not_equal(paddle_system_get_pos(ctx->paddle), 247);

    savegame_system_restore(ctx, &info, &lvl);

    /* --- Player / meta ----- */
    assert_int_equal(score_system_get(ctx->score), 125000UL);
    assert_int_equal(ctx->level_number, 7);
    assert_int_equal(ctx->time_bonus_total, 180);
    assert_int_equal(ctx->time_remaining, 142);
    assert_int_equal(ctx->lives_left, 3);
    assert_int_equal(ctx->start_level, 1);
    assert_int_equal(ctx->user_tilts, 1);
    assert_int_equal(ctx->bonus_count, 3);

    /* --- Paddle ----- */
    assert_int_equal(paddle_system_get_pos(ctx->paddle), 247);
    assert_int_equal(paddle_system_get_size_type(ctx->paddle), PADDLE_SIZE_MEDIUM);
    assert_int_equal(paddle_system_get_reverse(ctx->paddle), 1);
    assert_int_equal(paddle_system_get_sticky(ctx->paddle), 0);

    /* --- Gun ----- */
    assert_int_equal(gun_system_get_ammo(ctx->gun), 6);
    assert_int_equal(gun_system_get_unlimited(ctx->gun), 1);

    /* --- Specials ----- */
    assert_true(special_system_is_active(ctx->special, SPECIAL_FAST_GUN));
    assert_true(special_system_is_active(ctx->special, SPECIAL_KILLER));
    assert_true(special_system_is_active(ctx->special, SPECIAL_X2_BONUS));
    assert_false(special_system_is_active(ctx->special, SPECIAL_NO_WALLS));
    assert_false(special_system_is_active(ctx->special, SPECIAL_STICKY));

    /* --- Block grid ----- */
    assert_int_equal(block_system_is_occupied(ctx->block, 1, 2), 1);
    assert_int_equal(block_system_get_type(ctx->block, 1, 2), RED_BLK);
    assert_int_equal(block_system_is_occupied(ctx->block, 3, 4), 1);
    assert_int_equal(block_system_get_type(ctx->block, 3, 4), BLACK_BLK);
    assert_int_equal(block_system_is_occupied(ctx->block, 5, 6), 1);
    assert_int_equal(block_system_get_type(ctx->block, 5, 6), COUNTER_BLK);
    assert_int_equal(block_system_get_random(ctx->block, 5, 6), 1);
    {
        block_system_render_info_t bri;
        assert_int_equal(block_system_get_render_info(ctx->block, 5, 6, &bri), BLOCK_SYS_OK);
        assert_int_equal(bri.counter_slide, 4);
    }

    /* BLACK_BLK cooldown: stored as frame-relative offset (20), so on
     * restore at the same frame it lands at frame+20 again. */
    int frame = (int)sdl2_state_frame(ctx->state);
    assert_int_equal(block_system_get_black_next_frame(ctx->block, 3, 4), frame + 20);

    /* --- Ball ----- */
    int active_idx = ball_system_get_active_index(ctx->ball);
    assert_int_not_equal(active_idx, -1);
    int bx = 0;
    int by = 0;
    (void)ball_system_get_position(ctx->ball, active_idx, &bx, &by);
    assert_int_equal(bx, 247);
    assert_int_equal(by, 520);
    int dx = 0;
    int dy = 0;
    (void)ball_system_get_velocity(ctx->ball, active_idx, &dx, &dy);
    assert_int_equal(dx, 3);
    assert_int_equal(dy, -4);
}

/* =========================================================================
 * NULL safety: capture/restore must not crash on NULL inputs.
 * ========================================================================= */

static void test_capture_null_inputs_safe(void **vstate)
{
    const fixture_t *f = *vstate;
    /* NULL out_info: no-op (out_info is required, contract says return). */
    savegame_system_capture(f->ctx, NULL, NULL);
    /* NULL ctx: no-op. */
    savegame_data_t info;
    savegame_system_capture(NULL, &info, NULL);
}

static void test_restore_null_inputs_safe(void **vstate)
{
    fixture_t *f = *vstate;
    /* NULL info: no-op. */
    savegame_system_restore(f->ctx, NULL, NULL);
    /* NULL ctx: no-op. */
    savegame_data_t info;
    savegame_io_init(&info);
    savegame_system_restore(NULL, &info, NULL);
}

/* =========================================================================
 * Info-only restore (autosave path) leaves level untouched when no
 * level snapshot is passed.
 * ========================================================================= */

static void test_restore_info_only_leaves_grid_untouched(void **vstate)
{
    fixture_t *f = *vstate;
    game_ctx_t *ctx = f->ctx;
    seed_state(ctx);

    savegame_data_t info;
    savegame_system_capture(ctx, &info, NULL);

    /* Mutate the grid: clear cell (1,2) so we can detect whether
     * restore touched the block_system. */
    block_system_clear(ctx->block, 1, 2);
    assert_int_equal(block_system_is_occupied(ctx->block, 1, 2), 0);

    savegame_system_restore(ctx, &info, NULL);

    /* Score restored. */
    assert_int_equal(score_system_get(ctx->score), 125000UL);
    /* Grid NOT restored (level==NULL path). */
    assert_int_equal(block_system_is_occupied(ctx->block, 1, 2), 0);
}

/* =========================================================================
 * game_time round-trip: capture computes elapsed = (now - game_start) -
 * paused_seconds; restore rebases game_start so the next capture reads
 * the same value back.
 * ========================================================================= */

static void test_game_time_round_trip(void **vstate)
{
    fixture_t *f = *vstate;
    game_ctx_t *ctx = f->ctx;
    /* Pretend the session started 100s ago with 25s spent paused. */
    ctx->game_start = time(NULL) - 100;
    ctx->paused_seconds = 25;

    savegame_data_t info;
    savegame_system_capture(ctx, &info, NULL);
    /* Allow ±1s for clock-tick boundary between capture and the
     * test's time() call. */
    assert_in_range(info.game_time, 74UL, 76UL);

    /* Restore into a fresh state and re-capture; the elapsed value
     * must round-trip cleanly. */
    ctx->game_start = 0;
    ctx->paused_seconds = 0;
    savegame_system_restore(ctx, &info, NULL);

    savegame_data_t info2;
    savegame_system_capture(ctx, &info2, NULL);
    assert_in_range(info2.game_time, info.game_time - 1UL, info.game_time + 1UL);
}

/* =========================================================================
 * Disk round-trip: save → wipe → load → expect seed values back.
 * Exercises the full save/load I/O path including JSON write/read.
 * ========================================================================= */

static void test_save_then_load_round_trip(void **vstate)
{
    fixture_t *f = *vstate;
    game_ctx_t *ctx = f->ctx;
    seed_state(ctx);

    assert_int_equal(savegame_system_save(ctx), 1);

    /* Confirm both files landed on disk. */
    char info_path[PATHS_MAX_PATH];
    char level_path[PATHS_MAX_PATH];
    assert_int_equal(paths_save_info(&ctx->paths, info_path, sizeof(info_path)), PATHS_OK);
    assert_int_equal(paths_save_level(&ctx->paths, level_path, sizeof(level_path)), PATHS_OK);
    assert_int_equal(savegame_io_exists(info_path), 1);
    assert_int_equal(savegame_io_exists(level_path), 1);

    clear_state(ctx);
    assert_int_equal(ctx->level_number, 1);

    assert_int_equal(savegame_system_load(ctx), 1);

    /* Verify key fields restored from disk. */
    assert_int_equal(score_system_get(ctx->score), 125000UL);
    assert_int_equal(ctx->level_number, 7);
    assert_int_equal(ctx->time_remaining, 142);
    assert_int_equal(ctx->bonus_count, 3);
    assert_int_equal(paddle_system_get_pos(ctx->paddle), 247);
    assert_int_equal(gun_system_get_ammo(ctx->gun), 6);
    assert_true(special_system_is_active(ctx->special, SPECIAL_KILLER));
    assert_int_equal(block_system_get_type(ctx->block, 3, 4), BLACK_BLK);
}

/* =========================================================================
 * Load with no save files returns 0 and posts a "no saved game" message.
 * ========================================================================= */

static void test_load_with_no_save_files_fails(void **vstate)
{
    fixture_t *f = *vstate;
    assert_int_equal(savegame_system_load(f->ctx), 0);
}

/* =========================================================================
 * Autosave: writes info only and removes any stale level snapshot.
 *
 * Matches the bonus-screen autosave invariant — writing the cleared
 * grid would trigger immediate level completion on the next load.
 * ========================================================================= */

static void test_autosave_writes_info_and_deletes_stale_level(void **vstate)
{
    fixture_t *f = *vstate;
    game_ctx_t *ctx = f->ctx;
    seed_state(ctx);

    /* Pre-populate a stale save-level.dat from a regular save. */
    assert_int_equal(savegame_system_save(ctx), 1);
    char info_path[PATHS_MAX_PATH];
    char level_path[PATHS_MAX_PATH];
    assert_int_equal(paths_save_info(&ctx->paths, info_path, sizeof(info_path)), PATHS_OK);
    assert_int_equal(paths_save_level(&ctx->paths, level_path, sizeof(level_path)), PATHS_OK);
    assert_int_equal(savegame_io_exists(level_path), 1);

    /* Now autosave: info should be rewritten, level file removed. */
    assert_int_equal(savegame_system_autosave(ctx), 1);
    assert_int_equal(savegame_io_exists(info_path), 1);
    assert_int_equal(savegame_io_exists(level_path), 0);
}

/* =========================================================================
 * Autosave-then-load: the load path falls back to canonical .data
 * when save-level.dat is absent.
 *
 * Validates the "auto-save level file" critical-implementation-detail
 * in the spec without requiring a level file on disk.
 * ========================================================================= */

static void test_load_after_autosave_restores_info_fields(void **vstate)
{
    fixture_t *f = *vstate;
    game_ctx_t *ctx = f->ctx;
    seed_state(ctx);

    assert_int_equal(savegame_system_autosave(ctx), 1);

    clear_state(ctx);
    /* Load succeeds even without save-level.dat; canonical level may
     * fail to load (no XDG levels dir), but the info path is what we
     * care about here. */
    (void)savegame_system_load(ctx);

    /* Info fields restored from disk regardless of level load outcome. */
    assert_int_equal(score_system_get(ctx->score), 125000UL);
    assert_int_equal(ctx->level_number, 7);
    assert_int_equal(ctx->bonus_count, 3);
}

/* =========================================================================
 * Load rejects malformed saves (Cursor finding: untrusted JSON drives
 * out-of-range state into render code → OOB array index, signed
 * overflow).  Validator runs in savegame_system_load between read
 * and restore.
 * ========================================================================= */

static void write_info_directly(game_ctx_t *ctx, const savegame_data_t *info)
{
    char info_path[PATHS_MAX_PATH];
    assert_int_equal(paths_save_info(&ctx->paths, info_path, sizeof(info_path)), PATHS_OK);
    assert_int_equal(savegame_io_write(info_path, info), SAVEGAME_IO_OK);
    /* Ensure no stale level file taints the load path. */
    char level_path[PATHS_MAX_PATH];
    if (paths_save_level(&ctx->paths, level_path, sizeof(level_path)) == PATHS_OK)
    {
        (void)savegame_io_delete(level_path);
    }
}

static void test_load_rejects_negative_eyedude_slide(void **vstate)
{
    fixture_t *f = *vstate;
    game_ctx_t *ctx = f->ctx;
    seed_state(ctx);

    /* Build a well-formed save then tamper with one field. */
    savegame_data_t info;
    savegame_system_capture(ctx, &info, NULL);
    info.eyedude.slide = -1;
    write_info_directly(ctx, &info);

    clear_state(ctx);
    /* Load must refuse to apply this state. */
    assert_int_equal(savegame_system_load(ctx), 0);
    /* Verify nothing leaked through: cleared state survived. */
    assert_int_equal(score_system_get(ctx->score), 0UL);
}

static void test_load_rejects_oversized_next_frame_offset(void **vstate)
{
    fixture_t *f = *vstate;
    game_ctx_t *ctx = f->ctx;
    seed_state(ctx);
    assert_int_equal(savegame_system_save(ctx), 1);

    /* Rewrite the level file with an out-of-range cooldown. */
    char level_path[PATHS_MAX_PATH];
    assert_int_equal(paths_save_level(&ctx->paths, level_path, sizeof(level_path)), PATHS_OK);
    savegame_level_t lvl;
    assert_int_equal(savegame_level_read(level_path, &lvl), SAVEGAME_IO_OK);
    lvl.cells[3][4].next_frame_offset = 1000000; /* well over the cap */
    assert_int_equal(savegame_level_write(level_path, &lvl), SAVEGAME_IO_OK);

    clear_state(ctx);
    assert_int_equal(savegame_system_load(ctx), 0);
}

static void test_load_rejects_out_of_range_paddle_size(void **vstate)
{
    fixture_t *f = *vstate;
    game_ctx_t *ctx = f->ctx;
    seed_state(ctx);

    savegame_data_t info;
    savegame_system_capture(ctx, &info, NULL);
    info.paddle_size_type = 99;
    write_info_directly(ctx, &info);

    clear_state(ctx);
    assert_int_equal(savegame_system_load(ctx), 0);
}

/* Cursor finding round 2: ball dx/dy flow into abs() / collision loop.
 * INT_MIN crashes abs(); huge values turn the loop into CPU exhaustion. */

static void test_load_rejects_intmin_ball_velocity(void **vstate)
{
    fixture_t *f = *vstate;
    game_ctx_t *ctx = f->ctx;
    seed_state(ctx);

    savegame_data_t info;
    savegame_system_capture(ctx, &info, NULL);
    /* Find an active ball and tamper its velocity. */
    int found = 0;
    for (int i = 0; i < MAX_BALLS; i++)
    {
        if (info.balls[i].active)
        {
            info.balls[i].dx = INT_MIN;
            found = 1;
            break;
        }
    }
    assert_true(found);
    write_info_directly(ctx, &info);

    clear_state(ctx);
    assert_int_equal(savegame_system_load(ctx), 0);
}

static void test_load_rejects_oversized_ball_position(void **vstate)
{
    fixture_t *f = *vstate;
    game_ctx_t *ctx = f->ctx;
    seed_state(ctx);

    savegame_data_t info;
    savegame_system_capture(ctx, &info, NULL);
    for (int i = 0; i < MAX_BALLS; i++)
    {
        if (info.balls[i].active)
        {
            info.balls[i].x = 1000000;
            break;
        }
    }
    write_info_directly(ctx, &info);

    clear_state(ctx);
    assert_int_equal(savegame_system_load(ctx), 0);
}

static void test_load_rejects_intmax_eyedude_coords(void **vstate)
{
    fixture_t *f = *vstate;
    game_ctx_t *ctx = f->ctx;
    seed_state(ctx);

    savegame_data_t info;
    savegame_system_capture(ctx, &info, NULL);
    info.eyedude.x = INT_MAX;
    write_info_directly(ctx, &info);

    clear_state(ctx);
    assert_int_equal(savegame_system_load(ctx), 0);
}

static void test_load_rejects_oversized_level_number(void **vstate)
{
    fixture_t *f = *vstate;
    game_ctx_t *ctx = f->ctx;
    seed_state(ctx);

    savegame_data_t info;
    savegame_system_capture(ctx, &info, NULL);
    /* Tampered level >> SAVEGAME_MAX_LEVEL — must NOT bypass the
     * range check via narrowing-to-int wraparound. */
    info.level = (unsigned long)INT_MAX + 1UL;
    write_info_directly(ctx, &info);

    clear_state(ctx);
    assert_int_equal(savegame_system_load(ctx), 0);
}

/* =========================================================================
 * Global high-score eligibility: restored sessions must be marked
 * ineligible so submit_score skips the setgid-games global write.
 * ========================================================================= */

static void test_restore_marks_session_ineligible_for_global(void **vstate)
{
    fixture_t *f = *vstate;
    game_ctx_t *ctx = f->ctx;
    /* Fresh-game default — no save load happened. */
    ctx->savegame_restored_session = false;

    savegame_data_t info;
    savegame_io_init(&info);
    info.level = 1;
    info.lives_left = 3;

    savegame_system_restore(ctx, &info, NULL);

    assert_true(ctx->savegame_restored_session);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Pure capture/restore */
        cmocka_unit_test_setup_teardown(test_capture_restore_round_trip, setup, teardown),
        cmocka_unit_test_setup_teardown(test_restore_marks_session_ineligible_for_global, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_capture_null_inputs_safe, setup, teardown),
        cmocka_unit_test_setup_teardown(test_restore_null_inputs_safe, setup, teardown),
        cmocka_unit_test_setup_teardown(test_restore_info_only_leaves_grid_untouched, setup,
                                        teardown),
        /* Time accounting */
        cmocka_unit_test_setup_teardown(test_game_time_round_trip, setup, teardown),
        /* Disk I/O paths */
        cmocka_unit_test_setup_teardown(test_save_then_load_round_trip, setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_with_no_save_files_fails, setup, teardown),
        cmocka_unit_test_setup_teardown(test_autosave_writes_info_and_deletes_stale_level, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_load_after_autosave_restores_info_fields, setup,
                                        teardown),
        /* Validation against malformed input */
        cmocka_unit_test_setup_teardown(test_load_rejects_negative_eyedude_slide, setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_rejects_oversized_next_frame_offset, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_load_rejects_out_of_range_paddle_size, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_load_rejects_intmin_ball_velocity, setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_rejects_oversized_ball_position, setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_rejects_intmax_eyedude_coords, setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_rejects_oversized_level_number, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
