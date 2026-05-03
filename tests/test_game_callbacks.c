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
#include "eyedude_system.h"
#include "game_callbacks.h"
#include "score_system.h"
#include "game_context.h"
#include "game_init.h"
#include "gun_system.h"
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

    /* (a) Block must be in explosion lifecycle (exploding=1, occupied=1
     * for ~40 ticks).  Basket 3: blocks no longer vanish instantly. */
    block_system_render_info_t bri;
    assert_int_equal(block_system_get_render_info(ctx->block, 3, 4, &bri), BLOCK_SYS_OK);
    assert_int_equal(bri.exploding, 1);

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
 * Gap 1: BULLET_BLK bullet-hit adds 4 ammo and plays sound
 *
 * original/blocks.c:1581-1585 — AddABullet × NUMBER_OF_BULLETS_NEW_LEVEL (4).
 * ========================================================================= */

static void test_bullet_blk_gun_hit_adds_ammo(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    block_system_clear_all(ctx->block);

    /* Place BULLET_BLK — counterSlide=0 means no multi-hit protection */
    block_system_status_t bst = block_system_add(ctx->block, 2, 3, BULLET_BLK, 0, 0);
    assert_int_equal(bst, BLOCK_SYS_OK);

    /* Start at 2 ammo */
    gun_system_set_ammo(ctx->gun, 2);
    gun_system_set_unlimited(ctx->gun, 0);

    /* Simulate bullet hit via gun_cb_on_block_hit (indirectly through game_callbacks_gun) */
    gun_system_callbacks_t cbs = game_callbacks_gun();
    cbs.on_block_hit(2, 3, ctx);

    /* Basket 3: bullet kill arms explosion lifecycle.  Block stays
     * occupied through the animation; pickup effects fire at finalize. */
    assert_int_equal(block_system_is_occupied(ctx->block, 2, 3), 1);
    /* Drive explosion to finalize (4 update_explosions calls at tick boundaries). */
    int frame = (int)sdl2_state_frame(ctx->state);
    for (int i = 0; i <= 3; i++)
        block_system_update_explosions(ctx->block, frame + i * BLOCK_EXPLODE_DELAY,
                                       game_callbacks_on_block_finalize, ctx);
    /* Block now cleared by finalize; pickup effect applied: 2 + 4 = 6. */
    assert_int_equal(block_system_is_occupied(ctx->block, 2, 3), 0);
    assert_int_equal(gun_system_get_ammo(ctx->gun), 6);
}

/* =========================================================================
 * Gap 2: MAXAMMO_BLK bullet-hit sets unlimited + GUN_MAX_AMMO+1 ammo
 *
 * original/blocks.c:1588-1593 — SetUnlimitedBullets + SetNumberBullets(MAX+1).
 * ========================================================================= */

static void test_maxammo_blk_gun_hit_sets_unlimited(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    block_system_clear_all(ctx->block);

    block_system_status_t bst = block_system_add(ctx->block, 1, 0, MAXAMMO_BLK, 0, 0);
    assert_int_equal(bst, BLOCK_SYS_OK);

    gun_system_set_ammo(ctx->gun, 0);
    gun_system_set_unlimited(ctx->gun, 0);

    gun_system_callbacks_t cbs = game_callbacks_gun();
    cbs.on_block_hit(1, 0, ctx);

    /* Basket 3: bullet kill arms explosion; pickup effects fire at finalize. */
    int frame = (int)sdl2_state_frame(ctx->state);
    for (int i = 0; i <= 3; i++)
        block_system_update_explosions(ctx->block, frame + i * BLOCK_EXPLODE_DELAY,
                                       game_callbacks_on_block_finalize, ctx);
    /* Block now cleared by finalize. */
    assert_int_equal(block_system_is_occupied(ctx->block, 1, 0), 0);
    assert_int_equal(gun_system_get_unlimited(ctx->gun), 1);
    assert_int_equal(gun_system_get_ammo(ctx->gun), GUN_MAX_AMMO + 1);
}

/* =========================================================================
 * Gap 3/4: HYPERSPACE_BLK and BLACK_BLK absorb bullets — remain occupied
 *
 * original/gun.c:341-350 — just redraws, never killed by bullet.
 * ========================================================================= */

static void test_hyperspace_blk_absorbs_bullet(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    block_system_clear_all(ctx->block);
    block_system_add(ctx->block, 0, 4, HYPERSPACE_BLK, 0, 0);

    gun_system_callbacks_t cbs = game_callbacks_gun();
    cbs.on_block_hit(0, 4, ctx);

    /* Block must remain occupied after bullet hit */
    assert_int_equal(block_system_is_occupied(ctx->block, 0, 4), 1);
}

static void test_black_blk_absorbs_bullet(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    block_system_clear_all(ctx->block);
    block_system_add(ctx->block, 1, 5, BLACK_BLK, 0, 0);

    gun_system_callbacks_t cbs = game_callbacks_gun();
    cbs.on_block_hit(1, 5, ctx);

    assert_int_equal(block_system_is_occupied(ctx->block, 1, 5), 1);
}

/* =========================================================================
 * Gap 5: Multi-hit special block requires 3 bullet hits before clear
 *
 * original/gun.c:325-340 — counterSlide decrements per hit.
 * ========================================================================= */

static void test_mgun_blk_requires_three_gun_hits(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    block_system_clear_all(ctx->block);
    /* counterSlide=3 = BLOCK_SHOTS_TO_KILL_SPECIAL */
    block_system_add(ctx->block, 3, 3, MGUN_BLK, 3, 0);

    gun_system_callbacks_t cbs = game_callbacks_gun();

    /* Hit 1 — still occupied */
    cbs.on_block_hit(3, 3, ctx);
    assert_int_equal(block_system_is_occupied(ctx->block, 3, 3), 1);

    /* Hit 2 — still occupied */
    cbs.on_block_hit(3, 3, ctx);
    assert_int_equal(block_system_is_occupied(ctx->block, 3, 3), 1);

    /* Hit 3 — destroyed by this hit; basket 3 arms explosion lifecycle.
     * Cell stays occupied through the animation, then finalize clears. */
    cbs.on_block_hit(3, 3, ctx);
    assert_int_equal(block_system_is_occupied(ctx->block, 3, 3), 1);
    int frame = (int)sdl2_state_frame(ctx->state);
    for (int i = 0; i <= 3; i++)
        block_system_update_explosions(ctx->block, frame + i * BLOCK_EXPLODE_DELAY,
                                       game_callbacks_on_block_finalize, ctx);
    assert_int_equal(block_system_is_occupied(ctx->block, 3, 3), 0);
}

/* =========================================================================
 * Gap 6: MGUN_BLK ball-hit must NOT set unlimited
 *
 * original/blocks.c MGUN_BLK case — only fastGun; unlimited is MAXAMMO only.
 * ========================================================================= */

static void test_mgun_blk_ball_hit_no_unlimited(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    block_system_clear_all(ctx->block);
    ball_system_clear_all(ctx->ball);

    /* Place MGUN_BLK */
    block_system_add(ctx->block, 5, 2, MGUN_BLK, 0, 0);

    gun_system_set_unlimited(ctx->gun, 0);
    gun_system_set_ammo(ctx->gun, 4);

    /* Add a ball positioned on the MGUN_BLK center so ball_system_update fires on_block_hit.
     * MGUN_BLK at (5,2): col_width=55, row_height=32, block width=35, height=15.
     * x = 2*55 + (55-35)/2 = 110 + 10 = 120
     * y = 5*32 + (32-15)/2 = 160 + 8 = 168
     * center: (120+17, 168+7) = (137, 175) */
    ball_system_env_t env = game_callbacks_ball_env(ctx);
    int idx = ball_system_add(ctx->ball, &env, 137, 175, 0, 0, NULL);
    assert_int_not_equal(idx, -1);
    ball_system_change_mode(ctx->ball, &env, idx, BALL_ACTIVE);

    ball_system_update(ctx->ball, &env);

    /* Block now in explosion lifecycle — exploding=1 but still occupied
     * (basket 3: blocks animate before disappearing). */
    block_system_render_info_t bri;
    assert_int_equal(block_system_get_render_info(ctx->block, 5, 2, &bri), BLOCK_SYS_OK);
    assert_int_equal(bri.exploding, 1);
    /* unlimited must NOT have been set */
    assert_int_equal(gun_system_get_unlimited(ctx->gun), 0);
}

/* =========================================================================
 * Gap 7: Bullet hitting active ball transitions it to BALL_POP
 *
 * original/gun.c:284 — ClearBallNow → BALL_POP → BALL_EVT_DIED.
 * ========================================================================= */

static void test_bullet_kills_ball_transitions_to_ball_pop(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    block_system_clear_all(ctx->block);
    ball_system_clear_all(ctx->ball);

    ball_system_env_t env = game_callbacks_ball_env(ctx);
    int idx = ball_system_add(ctx->ball, &env, 200, 300, 1, -3, NULL);
    assert_int_not_equal(idx, -1);
    ball_system_change_mode(ctx->ball, &env, idx, BALL_ACTIVE);

    /* Invoke on_ball_hit directly */
    gun_system_callbacks_t cbs = game_callbacks_gun();
    cbs.on_ball_hit(idx, ctx);

    enum BallStates st = ball_system_get_state(ctx->ball, idx);
    assert_int_equal(st, BALL_POP);
}

/* =========================================================================
 * Basket 6: Eyedude bullet collision (xboing-m0y)
 *
 * gun_cb_check_eyedude_hit returns 1 only when bullet AABB overlaps
 * eyedude in WALK state.  gun_cb_on_eyedude_hit transitions to DIE
 * state and awards 10000 points (with x2/x4 multiplier).
 * ========================================================================= */

static void test_eyedude_bullet_hit_kills_and_scores(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* Force eyedude into WALK state at a known position so the AABB
     * collision check fires.  WALK is the only state in which
     * eyedude_system_check_collision returns 1. */
    eyedude_system_set_state(ctx->eyedude, EYEDUDE_STATE_WALK);
    int eye_x = 0, eye_y = 0;
    eyedude_system_get_position(ctx->eyedude, &eye_x, &eye_y);

    unsigned long score_before = score_system_get(ctx->score);

    gun_system_callbacks_t cbs = game_callbacks_gun();
    /* Bullet positioned at the eyedude's center triggers the collision. */
    assert_int_equal(cbs.check_eyedude_hit(eye_x, eye_y, ctx), 1);

    cbs.on_eyedude_hit(ctx);

    /* State transitioned to DIE */
    assert_int_equal(eyedude_system_get_state(ctx->eyedude), EYEDUDE_STATE_DIE);
    /* Score incremented by 10000 (with multipliers off in fixture, no scaling) */
    assert_int_equal(score_system_get(ctx->score), score_before + 10000);
}

static void test_eyedude_bullet_miss_when_far_away(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    eyedude_system_set_state(ctx->eyedude, EYEDUDE_STATE_WALK);
    int eye_x = 0, eye_y = 0;
    eyedude_system_get_position(ctx->eyedude, &eye_x, &eye_y);

    gun_system_callbacks_t cbs = game_callbacks_gun();
    /* A bullet 1000 pixels away should not register a hit. */
    assert_int_equal(cbs.check_eyedude_hit(eye_x + 1000, eye_y, ctx), 0);
}

static void test_eyedude_bullet_check_returns_0_when_not_walking(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* eyedude_system_check_collision only returns 1 in WALK state.
     * Default state after game_create is NONE — even a bullet at the
     * exact position must not register. */
    eyedude_system_set_state(ctx->eyedude, EYEDUDE_STATE_NONE);
    int eye_x = 0, eye_y = 0;
    eyedude_system_get_position(ctx->eyedude, &eye_x, &eye_y);

    gun_system_callbacks_t cbs = game_callbacks_gun();
    assert_int_equal(cbs.check_eyedude_hit(eye_x, eye_y, ctx), 0);
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(
            test_death_blk_clears_block_and_kills_ball, setup, teardown),
        /* Gap 1 */
        cmocka_unit_test_setup_teardown(test_bullet_blk_gun_hit_adds_ammo, setup, teardown),
        /* Gap 2 */
        cmocka_unit_test_setup_teardown(test_maxammo_blk_gun_hit_sets_unlimited, setup, teardown),
        /* Gaps 3, 4 */
        cmocka_unit_test_setup_teardown(test_hyperspace_blk_absorbs_bullet, setup, teardown),
        cmocka_unit_test_setup_teardown(test_black_blk_absorbs_bullet, setup, teardown),
        /* Gap 5 */
        cmocka_unit_test_setup_teardown(test_mgun_blk_requires_three_gun_hits, setup, teardown),
        /* Gap 6 */
        cmocka_unit_test_setup_teardown(test_mgun_blk_ball_hit_no_unlimited, setup, teardown),
        /* Gap 7 */
        cmocka_unit_test_setup_teardown(
            test_bullet_kills_ball_transitions_to_ball_pop, setup, teardown),
        /* Basket 6 — eyedude bullet collision (xboing-m0y) */
        cmocka_unit_test_setup_teardown(
            test_eyedude_bullet_hit_kills_and_scores, setup, teardown),
        cmocka_unit_test_setup_teardown(test_eyedude_bullet_miss_when_far_away, setup, teardown),
        cmocka_unit_test_setup_teardown(
            test_eyedude_bullet_check_returns_0_when_not_walking, setup, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
