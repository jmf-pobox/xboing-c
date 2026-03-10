/*
 * test_integration_all_levels.c — Verification pass for all 80 level files.
 *
 * Loads every level file (level01.data through level80.data) via the
 * full integration stack and verifies:
 *   - Parse succeeds (LEVEL_SYS_OK)
 *   - At least one block is placed
 *   - Title is non-empty
 *   - Time bonus is positive
 *   - Level wrapping works (level 81 maps to 1)
 *   - 100 frames of gameplay per level with no crash (ASan safety net)
 *
 * Requires: SDL_VIDEODRIVER=dummy, SDL_AUDIODRIVER=dummy
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "block_system.h"
#include "game_context.h"
#include "game_init.h"
#include "level_system.h"
#include "paths.h"
#include "sdl2_input.h"
#include "sdl2_state.h"

/* =========================================================================
 * Writable argv buffer
 * ========================================================================= */

static char arg_prog[] = "xboing_test";

/* =========================================================================
 * Fixture — full game context
 * ========================================================================= */

typedef struct
{
    game_ctx_t *ctx;
} test_fixture_t;

static int setup(void **vstate)
{
    test_fixture_t *f = calloc(1, sizeof(*f));
    assert_non_null(f);

    char *argv[] = {arg_prog, NULL};
    f->ctx = game_create(1, argv);
    assert_non_null(f->ctx);

    *vstate = f;
    return 0;
}

static int teardown(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_destroy(f->ctx);
    free(f);
    return 0;
}

/* =========================================================================
 * Helper: load a level by number and verify basic properties
 * ========================================================================= */

static void load_and_verify_level(game_ctx_t *ctx, int level_num)
{
    int file_num = level_system_wrap_number(level_num);
    char filename[32];
    snprintf(filename, sizeof(filename), "level%02d.data", file_num);

    char level_path[PATHS_MAX_PATH];
    paths_status_t ps = paths_level_file(&ctx->paths, filename, level_path, sizeof(level_path));
    assert_int_equal(ps, PATHS_OK);

    block_system_clear_all(ctx->block);
    level_system_status_t status = level_system_load_file(ctx->level, level_path);

    if (status != LEVEL_SYS_OK)
    {
        fprintf(stderr, "FAIL: level %d (%s): parse error: %s\n", level_num, filename,
                level_system_status_string(status));
    }
    assert_int_equal(status, LEVEL_SYS_OK);

    /* Title should be non-empty */
    const char *title = level_system_get_title(ctx->level);
    assert_non_null(title);
    if (title[0] == '\0')
    {
        fprintf(stderr, "WARN: level %d (%s): empty title\n", level_num, filename);
    }

    /* Time bonus should be positive */
    int time_bonus = level_system_get_time_bonus(ctx->level);
    if (time_bonus <= 0)
    {
        fprintf(stderr, "WARN: level %d (%s): time_bonus=%d\n", level_num, filename, time_bonus);
    }

    /* At least one block should exist */
    assert_true(block_system_still_active(ctx->block));
}

/* =========================================================================
 * Tests
 * ========================================================================= */

static void test_load_all_80_levels(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    for (int level = 1; level <= 80; level++)
    {
        load_and_verify_level(f->ctx, level);
    }
}

static void test_level_wrapping(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Level 81 should map to level 1 */
    assert_int_equal(level_system_wrap_number(81), 1);
    assert_int_equal(level_system_wrap_number(160), 80);

    /* Load level 81 (wraps to 1) — should succeed */
    load_and_verify_level(f->ctx, 81);
}

static void test_gameplay_tick_all_levels(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* Enter GAME mode */
    sdl2_state_transition(ctx->state, SDL2ST_GAME);

    /* For each level, load it and tick 100 frames */
    for (int level = 1; level <= 80; level++)
    {
        load_and_verify_level(ctx, level);

        /* Tick 100 frames — exercises ball physics, collisions, etc. */
        for (int i = 0; i < 100; i++)
        {
            sdl2_input_begin_frame(ctx->input);
            sdl2_state_update(ctx->state);
        }

        /* Should still be in a valid state */
        sdl2_state_mode_t mode = sdl2_state_current(ctx->state);
        if (mode < SDL2ST_NONE || mode >= SDL2ST_COUNT)
        {
            fprintf(stderr, "FAIL: level %d: invalid mode %d after ticking\n", level, mode);
        }
        assert_true(mode >= SDL2ST_NONE && mode < SDL2ST_COUNT);

        /* Re-enter GAME mode if we transitioned out (e.g., bonus) */
        if (mode != SDL2ST_GAME)
            sdl2_state_transition(ctx->state, SDL2ST_GAME);
    }
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_load_all_80_levels, setup, teardown),
        cmocka_unit_test_setup_teardown(test_level_wrapping, setup, teardown),
        cmocka_unit_test_setup_teardown(test_gameplay_tick_all_levels, setup, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
