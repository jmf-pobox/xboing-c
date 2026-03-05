/*
 * test_integration_smoke.c — Integration smoke test for the full game lifecycle.
 *
 * Creates the complete game context via game_create() with SDL dummy drivers,
 * verifies all modules initialized, checks initial state, then destroys
 * cleanly.  ASan catches any leaks or memory errors during teardown.
 *
 * Requires: SDL_VIDEODRIVER=dummy, SDL_AUDIODRIVER=dummy environment variables.
 * Set via CMake set_tests_properties(ENVIRONMENT ...).
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <cmocka.h>

#include "game_context.h"
#include "game_init.h"
#include "sdl2_state.h"

/* =========================================================================
 * Writable argv buffers — CLI parsing may mutate argv strings, so we
 * must not pass string literals (UB if written to).
 * ========================================================================= */

static char arg_prog[] = "xboing_test";
static char arg_startlevel[] = "-startlevel";
static char arg_five[] = "5";

/* =========================================================================
 * Smoke tests
 * ========================================================================= */

static void test_game_create_returns_non_null(void **vstate)
{
    (void)vstate;

    char *argv[] = {arg_prog, NULL};
    int argc = 1;

    game_ctx_t *ctx = game_create(argc, argv);
    assert_non_null(ctx);

    game_destroy(ctx);
}

static void test_all_modules_initialized(void **vstate)
{
    (void)vstate;

    char *argv[] = {arg_prog, NULL};
    int argc = 1;

    game_ctx_t *ctx = game_create(argc, argv);
    assert_non_null(ctx);

    /* SDL2 platform modules */
    assert_non_null(ctx->renderer);
    assert_non_null(ctx->texture);
    assert_non_null(ctx->font);
    /* audio may be NULL if dummy driver doesn't support it — that's OK */
    assert_non_null(ctx->input);
    assert_non_null(ctx->cursor);
    assert_non_null(ctx->state);
    assert_non_null(ctx->loop);

    /* Game systems */
    assert_non_null(ctx->ball);
    assert_non_null(ctx->block);
    assert_non_null(ctx->paddle);
    assert_non_null(ctx->gun);
    assert_non_null(ctx->score);
    assert_non_null(ctx->level);
    assert_non_null(ctx->special);
    assert_non_null(ctx->bonus);
    assert_non_null(ctx->sfx);
    assert_non_null(ctx->eyedude);
    assert_non_null(ctx->message);
    assert_non_null(ctx->editor);

    /* UI sequencers */
    assert_non_null(ctx->presents);
    assert_non_null(ctx->intro);
    assert_non_null(ctx->demo);
    assert_non_null(ctx->keys);
    assert_non_null(ctx->dialogue);
    assert_non_null(ctx->highscore_display);

    game_destroy(ctx);
}

static void test_initial_state_machine_mode(void **vstate)
{
    (void)vstate;

    char *argv[] = {arg_prog, NULL};
    int argc = 1;

    game_ctx_t *ctx = game_create(argc, argv);
    assert_non_null(ctx);

    /* State machine should be in initial mode (PRESENTS or NONE) */
    sdl2_state_mode_t mode = sdl2_state_current(ctx->state);
    /* After game_modes_register, initial mode is PRESENTS (10) */
    assert_true(mode == SDL2ST_PRESENTS || mode == SDL2ST_NONE);

    game_destroy(ctx);
}

static void test_initial_game_state(void **vstate)
{
    (void)vstate;

    char *argv[] = {arg_prog, NULL};
    int argc = 1;

    game_ctx_t *ctx = game_create(argc, argv);
    assert_non_null(ctx);

    /* Default game state */
    assert_int_equal(ctx->lives_left, 3);
    assert_int_equal(ctx->level_number, 1);
    assert_false(ctx->game_active);

    game_destroy(ctx);
}

static void test_destroy_null_is_safe(void **vstate)
{
    (void)vstate;
    game_destroy(NULL); /* Must not crash */
}

static void test_create_with_start_level(void **vstate)
{
    (void)vstate;

    char *argv[] = {arg_prog, arg_startlevel, arg_five, NULL};
    int argc = 3;

    game_ctx_t *ctx = game_create(argc, argv);
    assert_non_null(ctx);

    assert_int_equal(ctx->level_number, 5);
    assert_int_equal(ctx->start_level, 5);

    game_destroy(ctx);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_game_create_returns_non_null),
        cmocka_unit_test(test_all_modules_initialized),
        cmocka_unit_test(test_initial_state_machine_mode),
        cmocka_unit_test(test_initial_game_state),
        cmocka_unit_test(test_destroy_null_is_safe),
        cmocka_unit_test(test_create_with_start_level),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
