/*
 * test_ball_system.c — Tests for the pure C ball physics system.
 *
 * Bead xboing-1ka.1: Port ball physics to pure C.
 *
 * PR 1 test groups:
 *   Group 1: Lifecycle (create/destroy, initial state, machine_eps)
 *   Group 2: Ball management (add, add-fills-slots, add-full, clear, clear_all)
 *   Group 10: Render state queries (active ball info, inactive slot, guide info)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>

#include <cmocka.h>

#include "ball_system.h"
#include "ball_types.h"

/* =========================================================================
 * Helpers
 * ========================================================================= */

static ball_system_env_t make_env(int frame)
{
    ball_system_env_t env = {0};
    env.frame = frame;
    env.speed_level = 5;
    env.paddle_pos = 247;
    env.paddle_size = 50;
    env.play_width = 495;
    env.play_height = 580;
    return env;
}

/* =========================================================================
 * Group 1: Lifecycle
 * ========================================================================= */

/* TC-01: Create returns non-NULL context with OK status. */
static void test_create_returns_context(void **state)
{
    (void)state;
    ball_system_status_t st;
    ball_system_t *ctx = ball_system_create(NULL, NULL, &st);

    assert_non_null(ctx);
    assert_int_equal(st, BALL_SYS_OK);

    ball_system_destroy(ctx);
}

/* TC-02: All ball slots start inactive after create. */
static void test_create_all_balls_inactive(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    assert_non_null(ctx);

    for (int i = 0; i < MAX_BALLS; i++)
    {
        ball_system_render_info_t info;
        ball_system_status_t st = ball_system_get_render_info(ctx, i, &info);
        assert_int_equal(st, BALL_SYS_OK);
        assert_int_equal(info.active, 0);
        assert_int_equal(info.state, BALL_CREATE);
    }

    ball_system_destroy(ctx);
}

/* TC-03: Guide starts at position 6 (middle). */
static void test_create_guide_initial_position(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    assert_non_null(ctx);

    ball_system_guide_info_t guide = ball_system_get_guide_info(ctx);
    assert_int_equal(guide.pos, 6);

    ball_system_destroy(ctx);
}

/* TC-04: Destroy with NULL is safe (no crash). */
static void test_destroy_null_safe(void **state)
{
    (void)state;
    ball_system_destroy(NULL); /* Should not crash */
}

/* =========================================================================
 * Group 2: Ball management
 * ========================================================================= */

/* TC-05: Add a ball returns slot 0 and sets it active. */
static void test_add_returns_slot_zero(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);
    ball_system_status_t st;

    int idx = ball_system_add(ctx, &env, 200, 300, 3, -3, &st);

    assert_int_equal(idx, 0);
    assert_int_equal(st, BALL_SYS_OK);

    /* Verify the ball was set up */
    ball_system_render_info_t info;
    ball_system_get_render_info(ctx, 0, &info);
    assert_int_equal(info.active, 1);
    assert_int_equal(info.x, 200);
    assert_int_equal(info.y, 300);
    assert_int_equal(info.state, BALL_CREATE);

    ball_system_destroy(ctx);
}

/* TC-06: Add fills consecutive slots. */
static void test_add_fills_consecutive_slots(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);

    for (int i = 0; i < MAX_BALLS; i++)
    {
        int idx = ball_system_add(ctx, &env, i * 10, i * 20, 3, -3, NULL);
        assert_int_equal(idx, i);
    }

    ball_system_destroy(ctx);
}

/* TC-07: Add when all slots full returns -1 and ERR_FULL. */
static void test_add_full_returns_error(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);

    /* Fill all slots */
    for (int i = 0; i < MAX_BALLS; i++)
    {
        ball_system_add(ctx, &env, 0, 0, 3, -3, NULL);
    }

    /* Next add should fail */
    ball_system_status_t st;
    int idx = ball_system_add(ctx, &env, 0, 0, 3, -3, &st);

    assert_int_equal(idx, -1);
    assert_int_equal(st, BALL_SYS_ERR_FULL);

    ball_system_destroy(ctx);
}

/* TC-08: Clear resets ball to defaults and frees the slot. */
static void test_clear_resets_to_defaults(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);

    ball_system_add(ctx, &env, 200, 300, 5, -5, NULL);
    ball_system_clear(ctx, 0);

    ball_system_render_info_t info;
    ball_system_get_render_info(ctx, 0, &info);
    assert_int_equal(info.active, 0);
    assert_int_equal(info.x, 0);
    assert_int_equal(info.y, 0);
    assert_int_equal(info.slide, 0);
    assert_int_equal(info.state, BALL_CREATE);

    ball_system_destroy(ctx);
}

/* TC-09: Clear frees slot so add can reuse it. */
static void test_clear_allows_reuse(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);

    /* Fill all slots */
    for (int i = 0; i < MAX_BALLS; i++)
    {
        ball_system_add(ctx, &env, 0, 0, 3, -3, NULL);
    }

    /* Clear slot 2 and re-add */
    ball_system_clear(ctx, 2);
    int idx = ball_system_add(ctx, &env, 99, 88, 1, -1, NULL);
    assert_int_equal(idx, 2);

    int x, y;
    ball_system_get_position(ctx, 2, &x, &y);
    assert_int_equal(x, 99);
    assert_int_equal(y, 88);

    ball_system_destroy(ctx);
}

/* TC-10: Clear all resets every slot. */
static void test_clear_all(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);

    for (int i = 0; i < MAX_BALLS; i++)
    {
        ball_system_add(ctx, &env, i * 10, i * 20, 3, -3, NULL);
    }

    ball_system_clear_all(ctx);

    for (int i = 0; i < MAX_BALLS; i++)
    {
        ball_system_render_info_t info;
        ball_system_get_render_info(ctx, i, &info);
        assert_int_equal(info.active, 0);
    }

    ball_system_destroy(ctx);
}

/* TC-11: Add with NULL context returns -1. */
static void test_add_null_context(void **state)
{
    (void)state;
    ball_system_env_t env = make_env(100);
    ball_system_status_t st;

    int idx = ball_system_add(NULL, &env, 0, 0, 3, -3, &st);
    assert_int_equal(idx, -1);
    assert_int_equal(st, BALL_SYS_ERR_NULL_ARG);
}

/* =========================================================================
 * Group 10: Render state queries
 * ========================================================================= */

/* TC-12: Render info for active ball returns correct data. */
static void test_render_info_active_ball(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);

    ball_system_add(ctx, &env, 150, 250, 3, -3, NULL);

    ball_system_render_info_t info;
    ball_system_status_t st = ball_system_get_render_info(ctx, 0, &info);

    assert_int_equal(st, BALL_SYS_OK);
    assert_int_equal(info.active, 1);
    assert_int_equal(info.x, 150);
    assert_int_equal(info.y, 250);
    assert_int_equal(info.slide, 0);
    assert_int_equal(info.state, BALL_CREATE);

    ball_system_destroy(ctx);
}

/* TC-13: Render info for inactive slot shows active=0. */
static void test_render_info_inactive_slot(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);

    ball_system_render_info_t info;
    ball_system_status_t st = ball_system_get_render_info(ctx, 3, &info);

    assert_int_equal(st, BALL_SYS_OK);
    assert_int_equal(info.active, 0);

    ball_system_destroy(ctx);
}

/* TC-14: Render info for out-of-range index returns error. */
static void test_render_info_invalid_index(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);

    ball_system_render_info_t info;
    ball_system_status_t st = ball_system_get_render_info(ctx, MAX_BALLS, &info);
    assert_int_equal(st, BALL_SYS_ERR_INVALID_INDEX);

    st = ball_system_get_render_info(ctx, -1, &info);
    assert_int_equal(st, BALL_SYS_ERR_INVALID_INDEX);

    ball_system_destroy(ctx);
}

/* TC-15: Guide info returns initial state. */
static void test_guide_info_initial(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);

    ball_system_guide_info_t guide = ball_system_get_guide_info(ctx);
    assert_int_equal(guide.pos, 6);
    assert_true(guide.inc == 1 || guide.inc == -1);

    ball_system_destroy(ctx);
}

/* TC-16: Query functions with NULL context return safe defaults. */
static void test_queries_null_safe(void **state)
{
    (void)state;

    assert_int_equal(ball_system_get_active_count(NULL), 0);
    assert_int_equal(ball_system_get_active_index(NULL), -1);
    assert_int_equal(ball_system_is_ball_waiting(NULL), 0);
    assert_int_equal(ball_system_get_state(NULL, 0), BALL_NONE);

    ball_system_guide_info_t guide = ball_system_get_guide_info(NULL);
    assert_int_equal(guide.pos, 0);
}

/* TC-17: Status string returns meaningful text. */
static void test_status_strings(void **state)
{
    (void)state;

    assert_string_equal(ball_system_status_string(BALL_SYS_OK), "OK");
    assert_string_equal(ball_system_status_string(BALL_SYS_ERR_NULL_ARG), "NULL argument");
    assert_string_equal(ball_system_status_string(BALL_SYS_ERR_FULL), "all ball slots full");
}

/* TC-18: State name returns meaningful text. */
static void test_state_names(void **state)
{
    (void)state;

    assert_string_equal(ball_system_state_name(BALL_ACTIVE), "active");
    assert_string_equal(ball_system_state_name(BALL_POP), "pop");
    assert_string_equal(ball_system_state_name(BALL_READY), "ready");
    assert_string_equal(ball_system_state_name(BALL_NONE), "none");
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_returns_context),
        cmocka_unit_test(test_create_all_balls_inactive),
        cmocka_unit_test(test_create_guide_initial_position),
        cmocka_unit_test(test_destroy_null_safe),
        /* Group 2: Ball management */
        cmocka_unit_test(test_add_returns_slot_zero),
        cmocka_unit_test(test_add_fills_consecutive_slots),
        cmocka_unit_test(test_add_full_returns_error),
        cmocka_unit_test(test_clear_resets_to_defaults),
        cmocka_unit_test(test_clear_allows_reuse),
        cmocka_unit_test(test_clear_all),
        cmocka_unit_test(test_add_null_context),
        /* Group 10: Render state queries */
        cmocka_unit_test(test_render_info_active_ball),
        cmocka_unit_test(test_render_info_inactive_slot),
        cmocka_unit_test(test_render_info_invalid_index),
        cmocka_unit_test(test_guide_info_initial),
        cmocka_unit_test(test_queries_null_safe),
        cmocka_unit_test(test_status_strings),
        cmocka_unit_test(test_state_names),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
