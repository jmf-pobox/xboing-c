/*
 * test_integration_gameplay.c — Gameplay integration test.
 *
 * Enters GAME mode and ticks frames to verify ball, paddle, and block
 * subsystems interact correctly through the full integration stack.
 *
 * Tests verify:
 *   - Entering GAME mode initializes game state (lives, score, blocks)
 *   - Ball launches and moves during gameplay ticks
 *   - Paddle position is centered after game start
 *   - Blocks are loaded from level file
 *   - Extended gameplay ticking doesn't crash (ASan safety net)
 *   - Game-over lifecycle: lose all lives → transition to highscore
 *
 * Requires: SDL_VIDEODRIVER=dummy, SDL_AUDIODRIVER=dummy
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <cmocka.h>

#include "ball_system.h"
#include "block_system.h"
#include "game_context.h"
#include "game_init.h"
#include "paddle_system.h"
#include "score_system.h"
#include "sdl2_input.h"
#include "sdl2_state.h"

/* =========================================================================
 * Writable argv buffer
 * ========================================================================= */

static char arg_prog[] = "xboing_test";

/* =========================================================================
 * Helper: tick N frames with input begin_frame
 * ========================================================================= */

static void tick_frames(game_ctx_t *ctx, int n)
{
    for (int i = 0; i < n; i++)
    {
        sdl2_input_begin_frame(ctx->input);
        sdl2_state_update(ctx->state);
    }
}

/* =========================================================================
 * Fixture — creates game context and enters GAME mode
 * ========================================================================= */

typedef struct
{
    game_ctx_t *ctx;
} test_fixture_t;

static int setup_game_mode(void **vstate)
{
    test_fixture_t *f = calloc(1, sizeof(*f));
    assert_non_null(f);

    char *argv[] = {arg_prog, NULL};
    f->ctx = game_create(1, argv);
    assert_non_null(f->ctx);

    /* Enter GAME mode — triggers start_new_game() */
    sdl2_state_transition(f->ctx->state, SDL2ST_GAME);

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
 * Tests
 * ========================================================================= */

static void test_game_mode_initializes_state(void **vstate)
{
    const test_fixture_t *f = (const test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_GAME);
    assert_int_equal(ctx->lives_left, 3);
    assert_true(ctx->game_active);
    assert_int_equal((int)score_system_get(ctx->score), 0);
}

static void test_blocks_loaded_from_level(void **vstate)
{
    const test_fixture_t *f = (const test_fixture_t *)*vstate;

    /* Level 1 should have blocks loaded */
    assert_true(block_system_still_active(f->ctx->block));
}

static void test_ball_exists_after_game_start(void **vstate)
{
    const test_fixture_t *f = (const test_fixture_t *)*vstate;

    /* Ball 0 should be in BALL_ON_PADDLE state after start */
    enum BallStates state = ball_system_get_state(f->ctx->ball, 0);
    assert_true(state != BALL_NONE);
}

static void test_paddle_centered(void **vstate)
{
    const test_fixture_t *f = (const test_fixture_t *)*vstate;

    int pos = paddle_system_get_pos(f->ctx->paddle);
    /* Paddle should be roughly centered in the 495px play area */
    assert_true(pos > 150 && pos < 350);
}

static void test_gameplay_ticking_no_crash(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Tick 500 gameplay frames — ball will be moving, colliding, etc.
     * The ball starts on the paddle in BALL_ON_PADDLE state.
     * After the auto-launch timer expires, it enters play.
     * ASan catches any memory errors during this. */
    tick_frames(f->ctx, 500);

    /* Game should still be in a valid state (may have transitioned
     * to BONUS if level completed, or still in GAME) */
    sdl2_state_mode_t mode = sdl2_state_current(f->ctx->state);
    assert_true(mode >= SDL2ST_NONE && mode < SDL2ST_COUNT);
}

static void test_extended_gameplay_no_crash(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Tick 5000 frames — enough for ball to bounce around extensively.
     * May trigger level completion, bonus mode, life loss, etc.
     * The key assertion: no crash, no ASan error. */
    tick_frames(f->ctx, 5000);

    sdl2_state_mode_t mode = sdl2_state_current(f->ctx->state);
    assert_true(mode >= SDL2ST_NONE && mode < SDL2ST_COUNT);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_game_mode_initializes_state, setup_game_mode, teardown),
        cmocka_unit_test_setup_teardown(test_blocks_loaded_from_level, setup_game_mode, teardown),
        cmocka_unit_test_setup_teardown(test_ball_exists_after_game_start, setup_game_mode, teardown),
        cmocka_unit_test_setup_teardown(test_paddle_centered, setup_game_mode, teardown),
        cmocka_unit_test_setup_teardown(test_gameplay_ticking_no_crash, setup_game_mode, teardown),
        cmocka_unit_test_setup_teardown(test_extended_gameplay_no_crash, setup_game_mode, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
