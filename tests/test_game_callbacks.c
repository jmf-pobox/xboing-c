/*
 * test_game_callbacks.c — Integration tests for game_callbacks module.
 *
 * Tests the Fix 3 skull-collision bug fix (xboing-c-6x0):
 *
 *   DEATH_BLK hit must:
 *   (a) clear the block (block_system_is_occupied returns 0)
 *   (b) transition the ball at ball_index to BALL_POP
 *       (not just "some ball", the one that hit it)
 *
 * Strategy: enter GAME mode, clear the block grid, add a DEATH_BLK at a
 * known grid cell, add a ball in BALL_ACTIVE state positioned directly on
 * the block center.  ball_system_update normalizes (0, 0) velocity to
 * MIN_DX_BALL/MIN_DY_BALL before the ray-march, but since the ball is
 * already overlapping the block, block_system_check_region fires on the
 * first update regardless.  Use ball_index=2 (non-zero) to confirm it's
 * the correct slot that transitions.
 *
 * Requires: SDL_VIDEODRIVER=dummy, SDL_AUDIODRIVER=dummy
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>

#include <cmocka.h>

#include "ball_system.h"
#include "ball_types.h"
#include "block_system.h"
#include "block_types.h"
#include "game_callbacks.h"
#include "game_context.h"
#include "game_init.h"
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

    /* Enter GAME mode */
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
 * Fix 3: DEATH_BLK hit clears block and transitions correct ball to BALL_POP
 *
 * Canonical reference: original/ball.c:847-861 — DEATH_BLK calls
 * ClearBallNow(display, window, i) before the block is erased, then breaks
 * (returns False from HandleTheBlocks).
 * ========================================================================= */

static void test_death_blk_clears_block_and_kills_ball(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* Clear the entire block grid — we control placement */
    block_system_clear_all(ctx->block);
    /* Clear all balls — we control placement */
    ball_system_clear_all(ctx->ball);

    /* Place a DEATH_BLK at row=3, col=4.
     * Geometry: col_width=55, row_height=32, block width=40, height=20.
     * x = 4*55 + (55-40)/2 = 220 + 7 = 227
     * y = 3*32 + (32-20)/2 = 96  + 6 = 102
     * center: cx = 227+20 = 247, cy = 102+10 = 112 */
    block_system_status_t bst = block_system_add(ctx->block, 3, 4, DEATH_BLK, 0, 0);
    assert_int_equal(bst, BLOCK_SYS_OK);
    assert_int_equal(block_system_is_occupied(ctx->block, 3, 4), 1);

    /* Add a ball at slot index 2 (non-zero to verify the right index transitions).
     * ball_system_add fills the lowest free slot, so first call gives index 0.
     * Pad slots 0 and 1 with dummy balls at a safe out-of-grid location, then
     * add the test ball. */
    ball_system_env_t env = game_callbacks_ball_env(ctx);

    /* Slots 0 and 1: inactive positions far from any block (x=10, y=400) */
    int idx0 = ball_system_add(ctx->ball, &env, 10, 400, 0, 0, NULL);
    assert_int_equal(idx0, 0);
    int idx1 = ball_system_add(ctx->ball, &env, 10, 400, 0, 0, NULL);
    assert_int_equal(idx1, 1);

    /* Slot 2: the test ball — position at block center, dx=0, dy=0.
     * The block is at (row=3, col=4).  Ball center at (247, 112).
     * ball_system_update will normalize (0, 0) to MIN_DX_BALL/MIN_DY_BALL
     * but the ball is already on top of the block, so the 9-cell
     * neighborhood scan picks up the DEATH_BLK at (3, 4) on the first
     * check regardless of step direction. */
    int idx2 = ball_system_add(ctx->ball, &env, 247, 112, 0, 0, NULL);
    assert_int_equal(idx2, 2);

    /* Transition ball 2 to BALL_ACTIVE so ball_system_update processes it */
    ball_system_status_t st = ball_system_change_mode(ctx->ball, &env, 2, BALL_ACTIVE);
    assert_int_equal(st, BALL_SYS_OK);

    /* Tick one ball_system_update — on_block_hit fires for ball_index=2 */
    ball_system_update(ctx->ball, &env);

    /* (a) Block must be cleared */
    assert_int_equal(block_system_is_occupied(ctx->block, 3, 4), 0);

    /* (b) Ball at index 2 must be in BALL_POP (not BALL_ACTIVE) */
    enum BallStates state2 = ball_system_get_state(ctx->ball, 2);
    assert_int_equal(state2, BALL_POP);

    /* Balls at slots 0 and 1 must NOT have been affected */
    enum BallStates state0 = ball_system_get_state(ctx->ball, 0);
    enum BallStates state1 = ball_system_get_state(ctx->ball, 1);
    assert_int_not_equal(state0, BALL_POP);
    assert_int_not_equal(state1, BALL_POP);
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(
            test_death_blk_clears_block_and_kills_ball, setup, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
