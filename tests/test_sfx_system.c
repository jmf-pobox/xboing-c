/*
 * test_sfx_system.c — Tests for the pure C visual SFX state machine.
 *
 * 7 groups:
 *   1. Lifecycle (3 tests)
 *   2. Enable/disable (2 tests)
 *   3. Shake effect (4 tests)
 *   4. Fade effect (3 tests)
 *   5. Blind and Shatter (3 tests)
 *   6. Static effect (2 tests)
 *   7. BorderGlow (3 tests)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

/* cmocka must come after setjmp.h / stdarg.h / stddef.h */
#include <cmocka.h>

#include "sfx_system.h"

/* =========================================================================
 * Test helpers
 * ========================================================================= */

static int g_move_count;
static int g_last_move_x;
static int g_last_move_y;

static void reset_tracking(void)
{
    g_move_count = 0;
    g_last_move_x = 0;
    g_last_move_y = 0;
}

static void on_move_window(int x, int y, void *ud)
{
    (void)ud;
    g_move_count++;
    g_last_move_x = x;
    g_last_move_y = y;
}

static sfx_system_callbacks_t make_callbacks(void)
{
    sfx_system_callbacks_t cbs;
    cbs.on_move_window = on_move_window;
    cbs.on_sound = NULL;
    return cbs;
}

/* Deterministic random: returns values from a static sequence */
static int g_rand_seq[64];
static int g_rand_idx;

static int test_rand(void)
{
    return g_rand_seq[g_rand_idx++ % 64];
}

static void set_rand_seq(int val)
{
    for (int i = 0; i < 64; i++)
    {
        g_rand_seq[i] = val;
    }
    g_rand_idx = 0;
}

/* =========================================================================
 * Group 1: Lifecycle
 * ========================================================================= */

static void test_create_destroy(void **state)
{
    (void)state;
    sfx_system_t *ctx = sfx_system_create(NULL, NULL, NULL);
    assert_non_null(ctx);
    sfx_system_destroy(ctx);
}

static void test_create_with_callbacks(void **state)
{
    (void)state;
    sfx_system_callbacks_t cbs = make_callbacks();
    sfx_system_t *ctx = sfx_system_create(&cbs, NULL, test_rand);
    assert_non_null(ctx);
    sfx_system_destroy(ctx);
}

static void test_initial_state(void **state)
{
    (void)state;
    sfx_system_t *ctx = sfx_system_create(NULL, NULL, NULL);

    assert_int_equal(sfx_system_get_mode(ctx), SFX_MODE_NONE);
    assert_int_equal(sfx_system_get_enabled(ctx), 1);

    sfx_system_destroy(ctx);
}

/* =========================================================================
 * Group 2: Enable / disable
 * ========================================================================= */

static void test_disable_effects(void **state)
{
    (void)state;
    sfx_system_t *ctx = sfx_system_create(NULL, NULL, NULL);

    sfx_system_set_enabled(ctx, 0);
    assert_int_equal(sfx_system_get_enabled(ctx), 0);

    sfx_system_set_enabled(ctx, 1);
    assert_int_equal(sfx_system_get_enabled(ctx), 1);

    sfx_system_destroy(ctx);
}

static void test_disabled_effect_resets(void **state)
{
    (void)state;
    reset_tracking();
    sfx_system_callbacks_t cbs = make_callbacks();
    sfx_system_t *ctx = sfx_system_create(&cbs, NULL, NULL);

    /* Activate shake then disable */
    sfx_system_set_mode(ctx, SFX_MODE_SHAKE);
    sfx_system_set_end_frame(ctx, 100);
    sfx_system_set_enabled(ctx, 0);

    /* Update should reset the effect immediately */
    int result = sfx_system_update(ctx, 0);
    assert_int_equal(result, 0);
    assert_int_equal(sfx_system_get_mode(ctx), SFX_MODE_NONE);

    /* on_move_window should have been called to restore position */
    assert_int_equal(g_last_move_x, SFX_WINDOW_X);
    assert_int_equal(g_last_move_y, SFX_WINDOW_Y);

    sfx_system_destroy(ctx);
}

/* =========================================================================
 * Group 3: Shake effect
 * ========================================================================= */

static void test_shake_moves_window(void **state)
{
    (void)state;
    reset_tracking();
    set_rand_seq(3); /* rand() % 6 = 3, offset = 3 - 3 = 0 */
    sfx_system_callbacks_t cbs = make_callbacks();
    sfx_system_t *ctx = sfx_system_create(&cbs, NULL, test_rand);

    sfx_system_set_mode(ctx, SFX_MODE_SHAKE);
    sfx_system_set_end_frame(ctx, 100);

    /* Frame 0: 0 % 5 == 0 → should move */
    int result = sfx_system_update(ctx, 0);
    assert_int_equal(result, 1);
    assert_true(g_move_count > 0);

    sfx_system_destroy(ctx);
}

static void test_shake_throttle(void **state)
{
    (void)state;
    reset_tracking();
    set_rand_seq(3);
    sfx_system_callbacks_t cbs = make_callbacks();
    sfx_system_t *ctx = sfx_system_create(&cbs, NULL, test_rand);

    sfx_system_set_mode(ctx, SFX_MODE_SHAKE);
    sfx_system_set_end_frame(ctx, 100);

    /* Frame 1: 1 % 5 != 0 → should NOT move */
    int result = sfx_system_update(ctx, 1);
    assert_int_equal(result, 1);
    assert_int_equal(g_move_count, 0);

    /* Frame 5: 5 % 5 == 0 → should move */
    result = sfx_system_update(ctx, 5);
    assert_int_equal(result, 1);
    assert_int_equal(g_move_count, 1);

    sfx_system_destroy(ctx);
}

static void test_shake_expiry(void **state)
{
    (void)state;
    reset_tracking();
    set_rand_seq(3);
    sfx_system_callbacks_t cbs = make_callbacks();
    sfx_system_t *ctx = sfx_system_create(&cbs, NULL, test_rand);

    sfx_system_set_mode(ctx, SFX_MODE_SHAKE);
    sfx_system_set_end_frame(ctx, 10);

    /* Frame 10: expired → reset */
    int result = sfx_system_update(ctx, 10);
    assert_int_equal(result, 0);
    assert_int_equal(sfx_system_get_mode(ctx), SFX_MODE_NONE);

    /* Window restored to canonical position */
    assert_int_equal(g_last_move_x, SFX_WINDOW_X);
    assert_int_equal(g_last_move_y, SFX_WINDOW_Y);

    sfx_system_destroy(ctx);
}

static void test_shake_position_range(void **state)
{
    (void)state;
    reset_tracking();
    /* rand() returns 0 → 0%6=0, offset = 0-3 = -3 → position = 32, 57 */
    set_rand_seq(0);
    sfx_system_callbacks_t cbs = make_callbacks();
    sfx_system_t *ctx = sfx_system_create(&cbs, NULL, test_rand);

    sfx_system_set_mode(ctx, SFX_MODE_SHAKE);
    sfx_system_set_end_frame(ctx, 100);

    /* First update at frame 0 moves to initial position (35, 60) */
    sfx_system_update(ctx, 0);

    /* After the move, next position is computed from rand */
    sfx_shake_pos_t pos = sfx_system_get_shake_pos(ctx);
    /* rand()=0 → offset = 0-3 = -3, so position = 35-3=32, 60-3=57 */
    assert_int_equal(pos.x, SFX_WINDOW_X - 3);
    assert_int_equal(pos.y, SFX_WINDOW_Y - 3);

    sfx_system_destroy(ctx);
}

/* =========================================================================
 * Group 4: Fade effect
 * ========================================================================= */

static void test_fade_completes_in_13_frames(void **state)
{
    (void)state;
    reset_tracking();
    sfx_system_callbacks_t cbs = make_callbacks();
    sfx_system_t *ctx = sfx_system_create(&cbs, NULL, NULL);

    sfx_system_set_mode(ctx, SFX_MODE_FADE);

    int running = 1;
    int frame_count = 0;
    while (running)
    {
        running = sfx_system_update(ctx, frame_count);
        frame_count++;
    }

    /* Fade takes SFX_FADE_STEPS + 1 calls: 13 drawing frames + 1 termination.
     * This matches legacy behavior where the function draws on calls 1-13
     * (steps 0-12) and returns False on call 14 when done==True. */
    assert_int_equal(frame_count, SFX_FADE_STEPS + 1);
    assert_int_equal(sfx_system_get_mode(ctx), SFX_MODE_NONE);

    sfx_system_destroy(ctx);
}

static void test_fade_step_progresses(void **state)
{
    (void)state;
    sfx_system_t *ctx = sfx_system_create(NULL, NULL, NULL);

    sfx_system_set_mode(ctx, SFX_MODE_FADE);

    /* First update processes step 0 */
    sfx_system_update(ctx, 0);
    sfx_fade_frame_t info = sfx_system_get_fade_frame(ctx);
    assert_int_equal(info.step, 0);
    assert_int_equal(info.stride, SFX_FADE_STRIDE);

    /* Second update processes step 1 */
    sfx_system_update(ctx, 1);
    info = sfx_system_get_fade_frame(ctx);
    assert_int_equal(info.step, 1);

    sfx_system_destroy(ctx);
}

static void test_fade_disabled_resets(void **state)
{
    (void)state;
    reset_tracking();
    sfx_system_callbacks_t cbs = make_callbacks();
    sfx_system_t *ctx = sfx_system_create(&cbs, NULL, NULL);

    sfx_system_set_mode(ctx, SFX_MODE_FADE);
    sfx_system_set_enabled(ctx, 0);

    int result = sfx_system_update(ctx, 0);
    assert_int_equal(result, 0);
    assert_int_equal(sfx_system_get_mode(ctx), SFX_MODE_NONE);

    sfx_system_destroy(ctx);
}

/* =========================================================================
 * Group 5: Blind and Shatter (synchronous effects)
 * ========================================================================= */

static void test_blind_completes_immediately(void **state)
{
    (void)state;
    sfx_system_t *ctx = sfx_system_create(NULL, NULL, NULL);

    sfx_system_set_mode(ctx, SFX_MODE_BLIND);

    int result = sfx_system_update(ctx, 0);
    assert_int_equal(result, 0); /* Completes in one call */
    assert_int_equal(sfx_system_get_mode(ctx), SFX_MODE_NONE);

    sfx_system_destroy(ctx);
}

static void test_blind_strips_generated(void **state)
{
    (void)state;
    sfx_system_t *ctx = sfx_system_create(NULL, NULL, NULL);

    sfx_system_set_mode(ctx, SFX_MODE_BLIND);

    /* 495/8 = 61 strip_w, (61+1) * 8 = 496 strips total */
    sfx_blind_strip_t strips[512];
    int count = sfx_system_get_blind_strips(ctx, strips, 512, 495, 580);

    /* 62 outer iterations * 8 inner iterations = 496 strips */
    assert_true(count > 0);
    assert_true(count <= 512);

    /* First strip should be at x=0 */
    assert_int_equal(strips[0].x, 0);
    assert_int_equal(strips[0].h, 580);

    sfx_system_destroy(ctx);
}

static void test_shatter_completes_immediately(void **state)
{
    (void)state;
    set_rand_seq(0);
    sfx_system_t *ctx = sfx_system_create(NULL, NULL, test_rand);

    sfx_system_set_mode(ctx, SFX_MODE_SHATTER);

    int result = sfx_system_update(ctx, 0);
    assert_int_equal(result, 0); /* Completes in one call */
    assert_int_equal(sfx_system_get_mode(ctx), SFX_MODE_NONE);

    sfx_system_destroy(ctx);
}

/* =========================================================================
 * Group 6: Static effect (placeholder)
 * ========================================================================= */

static void test_static_runs_50_frames(void **state)
{
    (void)state;
    sfx_system_t *ctx = sfx_system_create(NULL, NULL, NULL);

    sfx_system_set_mode(ctx, SFX_MODE_STATIC);

    /* First update at frame 0: sets end_frame to 0+50=50 */
    int result = sfx_system_update(ctx, 0);
    assert_int_equal(result, 1);

    /* Frame 49: still running */
    result = sfx_system_update(ctx, 49);
    assert_int_equal(result, 1);

    /* Frame 50: expired */
    result = sfx_system_update(ctx, 50);
    assert_int_equal(result, 0);
    assert_int_equal(sfx_system_get_mode(ctx), SFX_MODE_NONE);

    sfx_system_destroy(ctx);
}

static void test_static_no_drawing(void **state)
{
    (void)state;
    reset_tracking();
    sfx_system_callbacks_t cbs = make_callbacks();
    sfx_system_t *ctx = sfx_system_create(&cbs, NULL, NULL);

    sfx_system_set_mode(ctx, SFX_MODE_STATIC);

    /* Static is a stub — should not move window during effect */
    sfx_system_update(ctx, 0);
    sfx_system_update(ctx, 5);
    sfx_system_update(ctx, 10);

    /* No moves during the effect */
    assert_int_equal(g_move_count, 0);

    /* But when it expires, reset_effect moves window */
    sfx_system_update(ctx, 50);
    assert_int_equal(g_move_count, 1);
    assert_int_equal(g_last_move_x, SFX_WINDOW_X);
    assert_int_equal(g_last_move_y, SFX_WINDOW_Y);

    sfx_system_destroy(ctx);
}

/* =========================================================================
 * Group 7: BorderGlow
 * ========================================================================= */

static void test_glow_initial_state(void **state)
{
    (void)state;
    sfx_system_t *ctx = sfx_system_create(NULL, NULL, NULL);

    sfx_glow_state_t glow = sfx_system_update_glow(ctx, 1);
    /* Not on interval frame → returns current state */
    assert_int_equal(glow.color_index, 0);
    assert_int_equal(glow.use_green, 0); /* Red phase */

    sfx_system_destroy(ctx);
}

static void test_glow_advances_on_interval(void **state)
{
    (void)state;
    sfx_system_t *ctx = sfx_system_create(NULL, NULL, NULL);

    /* Frame 40 is the first update interval */
    sfx_glow_state_t glow = sfx_system_update_glow(ctx, 40);
    assert_int_equal(glow.color_index, 1);
    assert_int_equal(glow.use_green, 0); /* Still red */

    sfx_system_destroy(ctx);
}

static void test_glow_reverses_at_peak(void **state)
{
    (void)state;
    sfx_system_t *ctx = sfx_system_create(NULL, NULL, NULL);

    /* Advance through 7 steps (0→6) to reach peak */
    sfx_glow_state_t glow;
    for (int i = 1; i <= SFX_GLOW_STEPS; i++)
    {
        glow = sfx_system_update_glow(ctx, i * SFX_GLOW_FRAME_INTERVAL);
    }

    /* At step 7 (index reaches SFX_GLOW_STEPS), direction reverses
     * and phase toggles to green.  Index clamps to SFX_GLOW_STEPS-1. */
    assert_int_equal(glow.color_index, SFX_GLOW_STEPS - 1);
    assert_int_equal(glow.use_green, 1); /* Now green */

    /* Next step goes back down */
    glow = sfx_system_update_glow(ctx, (SFX_GLOW_STEPS + 1) * SFX_GLOW_FRAME_INTERVAL);
    assert_int_equal(glow.color_index, SFX_GLOW_STEPS - 2);
    assert_int_equal(glow.use_green, 1); /* Still green */

    sfx_system_destroy(ctx);
}

/* =========================================================================
 * Group 8: Null safety and utilities
 * ========================================================================= */

static void test_null_safety(void **state)
{
    (void)state;
    /* None of these should crash */
    sfx_system_destroy(NULL);
    sfx_system_set_enabled(NULL, 1);
    sfx_system_set_mode(NULL, SFX_MODE_SHAKE);
    sfx_system_set_end_frame(NULL, 100);

    assert_int_equal(sfx_system_get_enabled(NULL), 0);
    assert_int_equal(sfx_system_get_mode(NULL), SFX_MODE_NONE);
    assert_int_equal(sfx_system_update(NULL, 0), 0);

    sfx_shake_pos_t pos = sfx_system_get_shake_pos(NULL);
    assert_int_equal(pos.x, SFX_WINDOW_X);
    assert_int_equal(pos.y, SFX_WINDOW_Y);
}

static void test_fadeaway_steps(void **state)
{
    (void)state;
    /* 495 / 15 + 1 = 34 */
    assert_int_equal(sfx_system_fadeaway_steps(495), 34);
    assert_int_equal(sfx_system_fadeaway_steps(0), 0);
    assert_int_equal(sfx_system_fadeaway_steps(15), 2);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_create_with_callbacks),
        cmocka_unit_test(test_initial_state),

        /* Group 2: Enable/disable */
        cmocka_unit_test(test_disable_effects),
        cmocka_unit_test(test_disabled_effect_resets),

        /* Group 3: Shake effect */
        cmocka_unit_test(test_shake_moves_window),
        cmocka_unit_test(test_shake_throttle),
        cmocka_unit_test(test_shake_expiry),
        cmocka_unit_test(test_shake_position_range),

        /* Group 4: Fade effect */
        cmocka_unit_test(test_fade_completes_in_13_frames),
        cmocka_unit_test(test_fade_step_progresses),
        cmocka_unit_test(test_fade_disabled_resets),

        /* Group 5: Blind and Shatter */
        cmocka_unit_test(test_blind_completes_immediately),
        cmocka_unit_test(test_blind_strips_generated),
        cmocka_unit_test(test_shatter_completes_immediately),

        /* Group 6: Static effect */
        cmocka_unit_test(test_static_runs_50_frames),
        cmocka_unit_test(test_static_no_drawing),

        /* Group 7: BorderGlow */
        cmocka_unit_test(test_glow_initial_state),
        cmocka_unit_test(test_glow_advances_on_interval),
        cmocka_unit_test(test_glow_reverses_at_peak),

        /* Group 8: Null safety and utilities */
        cmocka_unit_test(test_null_safety),
        cmocka_unit_test(test_fadeaway_steps),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
