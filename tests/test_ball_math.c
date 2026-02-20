/*
 * test_ball_math.c — Characterization tests for ball physics math.
 *
 * Bead n9e.1: Ball physics characterization tests.
 *
 * Tests CAPTURE current behavior of the extracted ball_math functions.
 * Do NOT fix bugs found here — document them as bead candidates.
 *
 * All tests call ball_math.h functions directly. No X11, no game globals.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>
#include <math.h>

#include "ball_math.h"
#include "ball_types.h"

/* Tolerance for floating point comparisons */
#define FLOAT_EPS 0.001f

/* Helper: initialize a ball with position, velocity, radius, mass */
static BALL make_ball(int x, int y, int dx, int dy, float radius, float mass)
{
    BALL b = {0};
    b.ballx = x;
    b.bally = y;
    b.dx = dx;
    b.dy = dy;
    b.radius = radius;
    b.mass = mass;
    return b;
}

/* -------------------------------------------------------------------------
 * Group 1: ball_math_init — MACHINE_EPS computation
 * Source: ball.c:317 — MACHINE_EPS = sqrt(MINFLOAT)
 * ------------------------------------------------------------------------- */

/* TC-01: MACHINE_EPS is a small positive float. */
static void test_init_returns_small_positive(void **state)
{
    (void)state;
    float eps = ball_math_init();
    assert_true(eps > 0.0f);
    assert_true(eps < 1.0e-10f);
}

/* -------------------------------------------------------------------------
 * Group 2: ball_math_will_collide — swept-circle collision detection
 * Source: ball.c WhenBallsCollide() line 1656
 * ------------------------------------------------------------------------- */

/* TC-02: Two balls heading directly at each other collide. */
static void test_collide_head_on(void **state)
{
    (void)state;
    float eps = ball_math_init();
    float time;

    BALL b1 = make_ball(0, 100, 5, 0, BALL_WC, 1.0f);
    BALL b2 = make_ball(30, 100, -5, 0, BALL_WC, 1.0f);

    int result = ball_math_will_collide(&b1, &b2, &time, eps);
    assert_int_equal(result, 1);
    assert_true(time >= 0.0f && time <= 1.0f);
}

/* TC-03: Two balls moving in the same direction, same speed — no collision. */
static void test_no_collide_same_direction(void **state)
{
    (void)state;
    float eps = ball_math_init();
    float time;

    BALL b1 = make_ball(0, 100, 5, 0, BALL_WC, 1.0f);
    BALL b2 = make_ball(100, 100, 5, 0, BALL_WC, 1.0f);

    int result = ball_math_will_collide(&b1, &b2, &time, eps);
    assert_int_equal(result, 0);
}

/* TC-04: Two balls diverging — no collision. */
static void test_no_collide_diverging(void **state)
{
    (void)state;
    float eps = ball_math_init();
    float time;

    BALL b1 = make_ball(0, 100, -5, 0, BALL_WC, 1.0f);
    BALL b2 = make_ball(100, 100, 5, 0, BALL_WC, 1.0f);

    int result = ball_math_will_collide(&b1, &b2, &time, eps);
    assert_int_equal(result, 0);
}

/* TC-05: Two stationary balls overlapping — v2 is 0, no collision reported. */
static void test_no_collide_stationary(void **state)
{
    (void)state;
    float eps = ball_math_init();
    float time;

    BALL b1 = make_ball(50, 100, 0, 0, BALL_WC, 1.0f);
    BALL b2 = make_ball(55, 100, 0, 0, BALL_WC, 1.0f);

    int result = ball_math_will_collide(&b1, &b2, &time, eps);
    assert_int_equal(result, 0);
}

/* -------------------------------------------------------------------------
 * Group 3: ball_math_collide — momentum exchange
 * Source: ball.c Ball2BallCollision() line 1726
 * Known bug: p.y = ball1->ballx - ball2->bally (should be bally)
 * ------------------------------------------------------------------------- */

/* TC-06: Equal mass head-on collision swaps velocities.
 * With the bug, the result is approximate, not exact. */
static void test_collide_equal_mass(void **state)
{
    (void)state;

    BALL b1 = make_ball(0, 100, 5, 0, BALL_WC, 2.0f);
    BALL b2 = make_ball(20, 100, -5, 0, BALL_WC, 2.0f);

    int b1_dx_before = b1.dx;
    int b2_dx_before = b2.dx;

    ball_math_collide(&b1, &b2);

    /* After collision, velocities should have changed */
    assert_true(b1.dx != b1_dx_before || b1.dy != 0);
    assert_true(b2.dx != b2_dx_before || b2.dy != 0);
}

/* TC-07: Collision with known bug — p.y uses ballx not bally.
 * The bug at ball.c:1744 computes p.y = ball1->ballx - ball2->bally
 * instead of ball1->bally - ball2->bally. When ballx >> bally, the
 * collision direction vector gets a large spurious y component, causing
 * the y velocities to change even for a purely horizontal collision.
 *
 * Setup: balls at same y=50, but ball1.x=200 so the bug makes
 * p.y = 200 - 50 = 150 instead of the correct 50 - 50 = 0. */
static void test_collide_bug_ballx_for_bally(void **state)
{
    (void)state;

    BALL b1 = make_ball(200, 50, 14, 0, BALL_WC, 2.0f);
    BALL b2 = make_ball(210, 50, -14, 0, BALL_WC, 2.0f);

    ball_math_collide(&b1, &b2);

    /* With the bug, p.y=150 dominates p.x=-10 in the direction vector.
     * The momentum exchange produces spurious dy changes while dx is
     * unchanged (the small px component truncates to 0 after int cast):
     *   b1: dx=14 (unchanged), dy=+1 (spurious — from bug)
     *   b2: dx=-14 (unchanged), dy=-1 (spurious — from bug)
     * Without the bug (p.y=0), dy would remain 0 for both balls. */
    assert_int_equal(b1.dx, 14);
    assert_int_equal(b1.dy, 1);
    assert_int_equal(b2.dx, -14);
    assert_int_equal(b2.dy, -1);
}

/* TC-08: Mass ratio affects momentum transfer.
 * Position balls where ballx == bally to neutralize the 1744 bug
 * (p.y = ball1->ballx - ball2->bally matches ball1->bally - ball2->bally
 * when ballx == bally for both balls). */
static void test_collide_mass_ratio(void **state)
{
    (void)state;

    /* Heavy ball at (100,100) hits stationary light ball at (120,100).
     * ballx==bally for b1 => bug is neutralized, pure horizontal collision. */
    BALL heavy = make_ball(100, 100, 10, 0, BALL_WC, 3.0f);
    BALL light = make_ball(120, 100, 0, 0, BALL_WC, 1.0f);

    ball_math_collide(&heavy, &light);

    /* Light ball picks up velocity from heavy ball */
    assert_true(abs(light.dx) > 0);
    /* Heavy ball loses some velocity */
    assert_true(heavy.dx < 10);
}

/* -------------------------------------------------------------------------
 * Group 4: ball_math_paddle_bounce — reflection trig
 * Source: ball.c UpdateABall() lines 1238-1273
 * ------------------------------------------------------------------------- */

/* TC-09: Ball hitting center of paddle bounces straight up. */
static void test_paddle_bounce_center(void **state)
{
    (void)state;
    int new_dx, new_dy;

    /* Ball going straight down, hit_pos=0 (center), pad_size=50 */
    ball_math_paddle_bounce(0, 5, 0, 50, 0, &new_dx, &new_dy);

    /* Should bounce mostly straight up */
    assert_true(new_dy < 0);  /* Going upward */
    assert_true(abs(new_dx) <= 1);  /* Minimal horizontal component */
}

/* TC-10: Ball hitting left side of paddle bounces left. */
static void test_paddle_bounce_left(void **state)
{
    (void)state;
    int new_dx, new_dy;

    /* hit_pos negative = left of center */
    ball_math_paddle_bounce(0, 5, -20, 50, 0, &new_dx, &new_dy);

    assert_true(new_dy < 0);   /* Going upward */
    assert_true(new_dx < 0);   /* Going leftward */
}

/* TC-11: Ball hitting right side of paddle bounces right. */
static void test_paddle_bounce_right(void **state)
{
    (void)state;
    int new_dx, new_dy;

    /* hit_pos positive = right of center */
    ball_math_paddle_bounce(0, 5, 20, 50, 0, &new_dx, &new_dy);

    assert_true(new_dy < 0);   /* Going upward */
    assert_true(new_dx > 0);   /* Going rightward */
}

/* TC-12: dy is always <= -MIN_DY_BALL after paddle bounce. */
static void test_paddle_bounce_min_dy(void **state)
{
    (void)state;
    int new_dx, new_dy;

    /* Slow ball barely touching paddle */
    ball_math_paddle_bounce(1, 1, 0, 50, 0, &new_dx, &new_dy);

    assert_true(new_dy <= -MIN_DY_BALL);
}

/* -------------------------------------------------------------------------
 * Group 5: ball_math_normalize_speed — velocity clamping
 * Source: ball.c UpdateABall() lines 1305-1333
 * ------------------------------------------------------------------------- */

/* TC-13: Normalization at speed level 5 scales to expected magnitude. */
static void test_normalize_speed_level5(void **state)
{
    (void)state;
    int dx = 3, dy = -4;

    ball_math_normalize_speed(&dx, &dy, 5);

    /* Speed should be approximately:
     * sqrt(14^2 + 14^2) / 9.0 * 5 = 19.799/9*5 = ~11.0 */
    float actual = (float)sqrt((double)(dx * dx + dy * dy));
    assert_true(actual > 5.0f && actual < 20.0f);
}

/* TC-14: Zero velocity gets clamped to minimums. */
static void test_normalize_speed_zero_velocity(void **state)
{
    (void)state;
    int dx = 0, dy = 0;

    ball_math_normalize_speed(&dx, &dy, 5);

    /* Zero input → Vx=Vy=0 → stays 0 through scaling → minimum clamps:
     * dy = MIN_DY_BALL = +2, dx = MIN_DX_BALL = +2.
     * Note: sign is positive (not negative). For a paddle bounce,
     * +dy means downward, which is a bug — but this characterizes it. */
    assert_int_equal(dx, MIN_DX_BALL);
    assert_int_equal(dy, MIN_DY_BALL);
}

/* -------------------------------------------------------------------------
 * Group 6: ball_math_x_to_col / ball_math_y_to_row — grid macros
 * Source: ball.c X2COL/Y2ROW macros (lines 105-106)
 * colWidth = PLAY_WIDTH/MAX_COL = 495/9 = 55
 * rowHeight = PLAY_HEIGHT/MAX_ROW = 580/18 = 32
 * ------------------------------------------------------------------------- */

/* TC-15: X coordinate maps to correct column. */
static void test_x_to_col(void **state)
{
    (void)state;
    int col_width = 55;  /* PLAY_WIDTH / MAX_COL */

    assert_int_equal(ball_math_x_to_col(0, col_width), 0);
    assert_int_equal(ball_math_x_to_col(54, col_width), 0);
    assert_int_equal(ball_math_x_to_col(55, col_width), 1);
    assert_int_equal(ball_math_x_to_col(494, col_width), 8);
}

/* TC-16: Y coordinate maps to correct row. */
static void test_y_to_row(void **state)
{
    (void)state;
    int row_height = 32;  /* PLAY_HEIGHT / MAX_ROW */

    assert_int_equal(ball_math_y_to_row(0, row_height), 0);
    assert_int_equal(ball_math_y_to_row(31, row_height), 0);
    assert_int_equal(ball_math_y_to_row(32, row_height), 1);
    assert_int_equal(ball_math_y_to_row(575, row_height), 17);
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_init_returns_small_positive),
        cmocka_unit_test(test_collide_head_on),
        cmocka_unit_test(test_no_collide_same_direction),
        cmocka_unit_test(test_no_collide_diverging),
        cmocka_unit_test(test_no_collide_stationary),
        cmocka_unit_test(test_collide_equal_mass),
        cmocka_unit_test(test_collide_bug_ballx_for_bally),
        cmocka_unit_test(test_collide_mass_ratio),
        cmocka_unit_test(test_paddle_bounce_center),
        cmocka_unit_test(test_paddle_bounce_left),
        cmocka_unit_test(test_paddle_bounce_right),
        cmocka_unit_test(test_paddle_bounce_min_dy),
        cmocka_unit_test(test_normalize_speed_level5),
        cmocka_unit_test(test_normalize_speed_zero_velocity),
        cmocka_unit_test(test_x_to_col),
        cmocka_unit_test(test_y_to_row),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
