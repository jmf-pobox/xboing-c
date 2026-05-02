/*
 * test_game_rules.c — Integration tests for game_rules module.
 *
 * Tests the two reverse-persistence bug fixes (xboing-c-qnk):
 *
 *   Fix 2a: game_rules_next_level clears reverse
 *     (matches original/file.c:122 — SetReverseOff() inside SetupStage)
 *   Fix 2b: game_rules_ball_died clears reverse in the still-have-lives branch
 *     (matches original/level.c:492 — SetReverseOff() inside DeadBall)
 *
 * Each test sets reverse_on=1 BEFORE the transition, then asserts it is 0
 * after.  Testing without the pre-set would be vacuous (default is already 0).
 *
 * Requires: SDL_VIDEODRIVER=dummy, SDL_AUDIODRIVER=dummy
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>

#include <cmocka.h>

#include "ball_system.h"
#include "game_context.h"
#include "game_init.h"
#include "game_rules.h"
#include "paddle_system.h"
#include "sdl2_state.h"

/* =========================================================================
 * Writable argv buffer
 * ========================================================================= */

static char s_arg_prog[] = "xboing_test";

/* =========================================================================
 * Fixture — creates game context in GAME mode
 * ========================================================================= */

typedef struct
{
    game_ctx_t *ctx;
} fixture_t;

static int setup(void **vstate)
{
    fixture_t *f = calloc(1, sizeof(*f));
    assert_non_null(f);

    char *argv[] = {s_arg_prog, NULL};
    f->ctx = game_create(1, argv);
    assert_non_null(f->ctx);

    /* Enter GAME mode — calls start_new_game, loads level 1, resets paddle */
    sdl2_state_transition(f->ctx->state, SDL2ST_GAME);

    *vstate = f;
    return 0;
}

static int teardown(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_destroy(f->ctx);
    free(f);
    return 0;
}

/* =========================================================================
 * Fix 2a: game_rules_next_level clears reverse
 *
 * Canonical reference: original/file.c:122 — SetReverseOff() inside
 * SetupStage, which is called on every level transition.
 * ========================================================================= */

static void test_next_level_clears_reverse(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* Pre-condition: set reverse on.  Without this the test passes vacuously. */
    paddle_system_set_reverse(ctx->paddle, 1);
    assert_int_equal(paddle_system_get_reverse(ctx->paddle), 1);

    /* Calling game_rules_next_level should clear reverse */
    game_rules_next_level(ctx);

    assert_int_equal(paddle_system_get_reverse(ctx->paddle), 0);
}

/* =========================================================================
 * Fix 2b: game_rules_ball_died clears reverse (still-have-lives branch)
 *
 * Canonical reference: original/level.c:492 — SetReverseOff() inside
 * DeadBall, fires only when GetAnActiveBall() == -1 && livesLeft > 0.
 * ========================================================================= */

static void test_ball_died_clears_reverse(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* Ensure we have lives left so we take the still-have-lives branch */
    ctx->lives_left = 3;

    /* Ensure no active balls (so game_rules_ball_died doesn't early-return
     * from the multiball guard) */
    ball_system_clear_all(ctx->ball);

    /* Pre-condition: set reverse on */
    paddle_system_set_reverse(ctx->paddle, 1);
    assert_int_equal(paddle_system_get_reverse(ctx->paddle), 1);

    /* game_rules_ball_died: lives_left > 0, no active balls → still-have-lives branch */
    game_rules_ball_died(ctx);

    assert_int_equal(paddle_system_get_reverse(ctx->paddle), 0);
    /* Lives should have been decremented */
    assert_int_equal(ctx->lives_left, 2);
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_next_level_clears_reverse, setup, teardown),
        cmocka_unit_test_setup_teardown(test_ball_died_clears_reverse, setup, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
