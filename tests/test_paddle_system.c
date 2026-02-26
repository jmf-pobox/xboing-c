/*
 * test_paddle_system.c — CMocka tests for paddle_system module.
 *
 * Characterization tests for the pure C paddle system port.
 * All tests are deterministic — no randomness, no I/O.
 *
 * Test groups:
 *   1. Lifecycle (3 tests)
 *   2. Keyboard movement (4 tests)
 *   3. Mouse movement (4 tests)
 *   4. Reverse controls (4 tests)
 *   5. Size changes (5 tests)
 *   6. Reset (2 tests)
 *   7. Flags and queries (3 tests)
 *   8. Delta and motion tracking (3 tests)
 *   9. Render info (2 tests)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

/* CMocka must come after setjmp.h */
#include <cmocka.h>

#include "paddle_system.h"

/* Production values matching legacy constants */
#define PLAY_WIDTH 495
#define PLAY_HEIGHT 580
#define MAIN_WIDTH 70

/* =========================================================================
 * Group 1: Lifecycle
 * ========================================================================= */

static void test_create_destroy(void **state)
{
    (void)state;
    paddle_system_status_t st;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, &st);
    assert_non_null(ctx);
    assert_int_equal(st, PADDLE_SYS_OK);
    paddle_system_destroy(ctx);
}

static void test_create_initial_state(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);
    assert_non_null(ctx);

    /* Initial position: centered */
    assert_int_equal(paddle_system_get_pos(ctx), PLAY_WIDTH / 2);

    /* Initial size: HUGE */
    assert_int_equal(paddle_system_get_size_type(ctx), PADDLE_SIZE_HUGE);
    assert_int_equal(paddle_system_get_size(ctx), PADDLE_WIDTH_HUGE);

    /* Flags off */
    assert_int_equal(paddle_system_get_reverse(ctx), 0);
    assert_int_equal(paddle_system_get_sticky(ctx), 0);

    /* No motion */
    assert_int_equal(paddle_system_get_dx(ctx), 0);
    assert_int_equal(paddle_system_is_moving(ctx), 0);

    paddle_system_destroy(ctx);
}

static void test_destroy_null_safe(void **state)
{
    (void)state;
    paddle_system_destroy(NULL); /* Should not crash */
}

/* =========================================================================
 * Group 2: Keyboard movement
 * ========================================================================= */

static void test_keyboard_move_right(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);
    int start = paddle_system_get_pos(ctx);

    paddle_system_update(ctx, PADDLE_DIR_RIGHT, 0);

    assert_int_equal(paddle_system_get_pos(ctx), start + PADDLE_VELOCITY);
    assert_int_equal(paddle_system_is_moving(ctx), 1);

    paddle_system_destroy(ctx);
}

static void test_keyboard_move_left(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);
    int start = paddle_system_get_pos(ctx);

    paddle_system_update(ctx, PADDLE_DIR_LEFT, 0);

    assert_int_equal(paddle_system_get_pos(ctx), start - PADDLE_VELOCITY);
    assert_int_equal(paddle_system_is_moving(ctx), 1);

    paddle_system_destroy(ctx);
}

static void test_keyboard_clamp_left_wall(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);

    /* Move paddle to left wall by repeated left moves */
    for (int i = 0; i < 100; i++)
    {
        paddle_system_update(ctx, PADDLE_DIR_LEFT, 0);
    }

    /* HUGE half-width = 35, so minimum position is 35 */
    assert_int_equal(paddle_system_get_pos(ctx), 35);

    paddle_system_destroy(ctx);
}

static void test_keyboard_clamp_right_wall(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);

    /* Move paddle to right wall by repeated right moves */
    for (int i = 0; i < 100; i++)
    {
        paddle_system_update(ctx, PADDLE_DIR_RIGHT, 0);
    }

    /* HUGE half-width = 35, so maximum position is PLAY_WIDTH - 35 = 460 */
    assert_int_equal(paddle_system_get_pos(ctx), PLAY_WIDTH - 35);

    paddle_system_destroy(ctx);
}

/* =========================================================================
 * Group 3: Mouse movement
 * ========================================================================= */

static void test_mouse_position_huge(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);

    /* Legacy formula: pos = mouse_x - (MAIN_WIDTH/2) + half_width
     * HUGE half = 35, MAIN_WIDTH/2 = 35
     * So pos = mouse_x - 35 + 35 = mouse_x */
    paddle_system_update(ctx, PADDLE_DIR_NONE, 200);

    assert_int_equal(paddle_system_get_pos(ctx), 200);

    paddle_system_destroy(ctx);
}

static void test_mouse_position_medium(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);
    paddle_system_set_size(ctx, PADDLE_SIZE_MEDIUM);

    /* MEDIUM half = 25, MAIN_WIDTH/2 = 35
     * pos = mouse_x - 35 + 25 = mouse_x - 10 */
    paddle_system_update(ctx, PADDLE_DIR_NONE, 200);

    assert_int_equal(paddle_system_get_pos(ctx), 190);

    paddle_system_destroy(ctx);
}

static void test_mouse_position_small(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);
    paddle_system_set_size(ctx, PADDLE_SIZE_SMALL);

    /* SMALL half = 20, MAIN_WIDTH/2 = 35
     * pos = mouse_x - 35 + 20 = mouse_x - 15 */
    paddle_system_update(ctx, PADDLE_DIR_NONE, 200);

    assert_int_equal(paddle_system_get_pos(ctx), 185);

    paddle_system_destroy(ctx);
}

static void test_mouse_clamp_at_walls(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);
    paddle_system_set_size(ctx, PADDLE_SIZE_SMALL);

    /* Mouse at far left — should clamp to half_width=20 */
    paddle_system_update(ctx, PADDLE_DIR_NONE, 1);
    assert_int_equal(paddle_system_get_pos(ctx), 20);

    /* Mouse at far right — should clamp to PLAY_WIDTH - 20 = 475 */
    paddle_system_update(ctx, PADDLE_DIR_NONE, 600);
    assert_int_equal(paddle_system_get_pos(ctx), PLAY_WIDTH - 20);

    paddle_system_destroy(ctx);
}

/* =========================================================================
 * Group 4: Reverse controls
 * ========================================================================= */

static void test_reverse_keyboard_left_moves_right(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);
    paddle_system_set_reverse(ctx, 1);
    int start = paddle_system_get_pos(ctx);

    paddle_system_update(ctx, PADDLE_DIR_LEFT, 0);

    /* Reversed: LEFT adds PADDLE_VEL */
    assert_int_equal(paddle_system_get_pos(ctx), start + PADDLE_VELOCITY);

    paddle_system_destroy(ctx);
}

static void test_reverse_keyboard_right_moves_left(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);
    paddle_system_set_reverse(ctx, 1);
    int start = paddle_system_get_pos(ctx);

    paddle_system_update(ctx, PADDLE_DIR_RIGHT, 0);

    /* Reversed: RIGHT subtracts PADDLE_VEL */
    assert_int_equal(paddle_system_get_pos(ctx), start - PADDLE_VELOCITY);

    paddle_system_destroy(ctx);
}

static void test_reverse_mouse_mirrors_x(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);
    paddle_system_set_reverse(ctx, 1);

    /* HUGE: pos = mirrored_x - 35 + 35 = mirrored_x
     * mirrored_x = PLAY_WIDTH - mouse_x = 495 - 100 = 395 */
    paddle_system_update(ctx, PADDLE_DIR_NONE, 100);

    assert_int_equal(paddle_system_get_pos(ctx), 395);

    paddle_system_destroy(ctx);
}

static void test_toggle_reverse(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);

    assert_int_equal(paddle_system_get_reverse(ctx), 0);
    paddle_system_toggle_reverse(ctx);
    assert_int_equal(paddle_system_get_reverse(ctx), 1);
    paddle_system_toggle_reverse(ctx);
    assert_int_equal(paddle_system_get_reverse(ctx), 0);

    paddle_system_destroy(ctx);
}

/* =========================================================================
 * Group 5: Size changes
 * ========================================================================= */

static void test_shrink_huge_to_medium(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);

    assert_int_equal(paddle_system_get_size_type(ctx), PADDLE_SIZE_HUGE);
    paddle_system_change_size(ctx, 1); /* shrink */
    assert_int_equal(paddle_system_get_size_type(ctx), PADDLE_SIZE_MEDIUM);
    assert_int_equal(paddle_system_get_size(ctx), PADDLE_WIDTH_MEDIUM);

    paddle_system_destroy(ctx);
}

static void test_shrink_medium_to_small(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);
    paddle_system_change_size(ctx, 1); /* HUGE → MEDIUM */
    paddle_system_change_size(ctx, 1); /* MEDIUM → SMALL */

    assert_int_equal(paddle_system_get_size_type(ctx), PADDLE_SIZE_SMALL);
    assert_int_equal(paddle_system_get_size(ctx), PADDLE_WIDTH_SMALL);

    paddle_system_destroy(ctx);
}

static void test_shrink_small_stays_small(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);
    paddle_system_change_size(ctx, 1); /* HUGE → MEDIUM */
    paddle_system_change_size(ctx, 1); /* MEDIUM → SMALL */
    paddle_system_change_size(ctx, 1); /* SMALL → SMALL (clamped) */

    assert_int_equal(paddle_system_get_size_type(ctx), PADDLE_SIZE_SMALL);

    paddle_system_destroy(ctx);
}

static void test_expand_small_to_huge(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);
    paddle_system_set_size(ctx, PADDLE_SIZE_SMALL);

    paddle_system_change_size(ctx, 0); /* SMALL → MEDIUM */
    assert_int_equal(paddle_system_get_size_type(ctx), PADDLE_SIZE_MEDIUM);

    paddle_system_change_size(ctx, 0); /* MEDIUM → HUGE */
    assert_int_equal(paddle_system_get_size_type(ctx), PADDLE_SIZE_HUGE);

    paddle_system_destroy(ctx);
}

static void test_expand_huge_stays_huge(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);

    assert_int_equal(paddle_system_get_size_type(ctx), PADDLE_SIZE_HUGE);
    paddle_system_change_size(ctx, 0); /* HUGE → HUGE (clamped) */
    assert_int_equal(paddle_system_get_size_type(ctx), PADDLE_SIZE_HUGE);

    paddle_system_destroy(ctx);
}

/* =========================================================================
 * Group 6: Reset
 * ========================================================================= */

static void test_reset_centers_position(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);

    /* Move away from center */
    for (int i = 0; i < 10; i++)
    {
        paddle_system_update(ctx, PADDLE_DIR_RIGHT, 0);
    }
    assert_true(paddle_system_get_pos(ctx) != PLAY_WIDTH / 2);

    paddle_system_reset(ctx);

    assert_int_equal(paddle_system_get_pos(ctx), PLAY_WIDTH / 2);
    assert_int_equal(paddle_system_get_dx(ctx), 0);
    assert_int_equal(paddle_system_is_moving(ctx), 0);

    paddle_system_destroy(ctx);
}

static void test_reset_preserves_size_and_flags(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);

    paddle_system_change_size(ctx, 1); /* HUGE → MEDIUM */
    paddle_system_set_reverse(ctx, 1);
    paddle_system_set_sticky(ctx, 1);

    paddle_system_reset(ctx);

    /* Size and flags preserved across reset */
    assert_int_equal(paddle_system_get_size_type(ctx), PADDLE_SIZE_MEDIUM);
    assert_int_equal(paddle_system_get_reverse(ctx), 1);
    assert_int_equal(paddle_system_get_sticky(ctx), 1);

    paddle_system_destroy(ctx);
}

/* =========================================================================
 * Group 7: Flags and queries
 * ========================================================================= */

static void test_sticky_flag(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);

    assert_int_equal(paddle_system_get_sticky(ctx), 0);
    paddle_system_set_sticky(ctx, 1);
    assert_int_equal(paddle_system_get_sticky(ctx), 1);
    paddle_system_set_sticky(ctx, 0);
    assert_int_equal(paddle_system_get_sticky(ctx), 0);

    paddle_system_destroy(ctx);
}

static void test_set_size_directly(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);

    paddle_system_set_size(ctx, PADDLE_SIZE_SMALL);
    assert_int_equal(paddle_system_get_size_type(ctx), PADDLE_SIZE_SMALL);
    assert_int_equal(paddle_system_get_size(ctx), PADDLE_WIDTH_SMALL);

    paddle_system_set_size(ctx, PADDLE_SIZE_MEDIUM);
    assert_int_equal(paddle_system_get_size_type(ctx), PADDLE_SIZE_MEDIUM);
    assert_int_equal(paddle_system_get_size(ctx), PADDLE_WIDTH_MEDIUM);

    paddle_system_destroy(ctx);
}

static void test_null_queries_safe(void **state)
{
    (void)state;

    assert_int_equal(paddle_system_get_pos(NULL), 0);
    assert_int_equal(paddle_system_get_dx(NULL), 0);
    assert_int_equal(paddle_system_get_size(NULL), 0);
    assert_int_equal(paddle_system_get_size_type(NULL), 0);
    assert_int_equal(paddle_system_is_moving(NULL), 0);
    assert_int_equal(paddle_system_get_reverse(NULL), 0);
    assert_int_equal(paddle_system_get_sticky(NULL), 0);

    paddle_system_render_info_t info;
    assert_int_equal(paddle_system_get_render_info(NULL, &info), PADDLE_SYS_ERR_NULL_ARG);

    paddle_system_destroy(NULL);
}

/* =========================================================================
 * Group 8: Delta and motion tracking
 * ========================================================================= */

static void test_keyboard_dx_always_zero(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);

    paddle_system_update(ctx, PADDLE_DIR_RIGHT, 0);

    /* Legacy behavior: paddleDx not set for keyboard mode */
    assert_int_equal(paddle_system_get_dx(ctx), 0);
    assert_int_equal(paddle_system_is_moving(ctx), 1);

    paddle_system_destroy(ctx);
}

static void test_mouse_dx_computed(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);

    /* First mouse update — dx should be 0 (no previous position) */
    paddle_system_update(ctx, PADDLE_DIR_NONE, 200);
    assert_int_equal(paddle_system_get_dx(ctx), 0);

    /* Second mouse update — dx = 250 - 200 = 50 */
    paddle_system_update(ctx, PADDLE_DIR_NONE, 250);
    assert_int_equal(paddle_system_get_dx(ctx), 50);

    /* Third mouse update — dx = 230 - 250 = -20 */
    paddle_system_update(ctx, PADDLE_DIR_NONE, 230);
    assert_int_equal(paddle_system_get_dx(ctx), -20);

    paddle_system_destroy(ctx);
}

static void test_no_movement_clears_motion(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);

    /* Move once */
    paddle_system_update(ctx, PADDLE_DIR_RIGHT, 0);
    assert_int_equal(paddle_system_is_moving(ctx), 1);

    /* No-op update at same position (no direction, no mouse) */
    paddle_system_update(ctx, PADDLE_DIR_NONE, 0);
    assert_int_equal(paddle_system_is_moving(ctx), 0);

    paddle_system_destroy(ctx);
}

/* =========================================================================
 * Group 9: Render info
 * ========================================================================= */

static void test_render_info_values(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);
    paddle_system_render_info_t info;

    paddle_system_status_t st = paddle_system_get_render_info(ctx, &info);

    assert_int_equal(st, PADDLE_SYS_OK);
    assert_int_equal(info.pos, PLAY_WIDTH / 2);
    assert_int_equal(info.y, PLAY_HEIGHT - PADDLE_DIST_BASE);
    assert_int_equal(info.width, PADDLE_WIDTH_HUGE);
    assert_int_equal(info.height, PADDLE_RENDER_HEIGHT);
    assert_int_equal(info.size_type, PADDLE_SIZE_HUGE);

    paddle_system_destroy(ctx);
}

static void test_render_info_after_size_change(void **state)
{
    (void)state;
    paddle_system_t *ctx = paddle_system_create(PLAY_WIDTH, PLAY_HEIGHT, MAIN_WIDTH, NULL);
    paddle_system_render_info_t info;

    paddle_system_change_size(ctx, 1); /* HUGE → MEDIUM */
    paddle_system_get_render_info(ctx, &info);

    assert_int_equal(info.width, PADDLE_WIDTH_MEDIUM);
    assert_int_equal(info.size_type, PADDLE_SIZE_MEDIUM);

    paddle_system_destroy(ctx);
}

/* =========================================================================
 * Test registration
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_create_initial_state),
        cmocka_unit_test(test_destroy_null_safe),

        /* Group 2: Keyboard movement */
        cmocka_unit_test(test_keyboard_move_right),
        cmocka_unit_test(test_keyboard_move_left),
        cmocka_unit_test(test_keyboard_clamp_left_wall),
        cmocka_unit_test(test_keyboard_clamp_right_wall),

        /* Group 3: Mouse movement */
        cmocka_unit_test(test_mouse_position_huge),
        cmocka_unit_test(test_mouse_position_medium),
        cmocka_unit_test(test_mouse_position_small),
        cmocka_unit_test(test_mouse_clamp_at_walls),

        /* Group 4: Reverse controls */
        cmocka_unit_test(test_reverse_keyboard_left_moves_right),
        cmocka_unit_test(test_reverse_keyboard_right_moves_left),
        cmocka_unit_test(test_reverse_mouse_mirrors_x),
        cmocka_unit_test(test_toggle_reverse),

        /* Group 5: Size changes */
        cmocka_unit_test(test_shrink_huge_to_medium),
        cmocka_unit_test(test_shrink_medium_to_small),
        cmocka_unit_test(test_shrink_small_stays_small),
        cmocka_unit_test(test_expand_small_to_huge),
        cmocka_unit_test(test_expand_huge_stays_huge),

        /* Group 6: Reset */
        cmocka_unit_test(test_reset_centers_position),
        cmocka_unit_test(test_reset_preserves_size_and_flags),

        /* Group 7: Flags and queries */
        cmocka_unit_test(test_sticky_flag),
        cmocka_unit_test(test_set_size_directly),
        cmocka_unit_test(test_null_queries_safe),

        /* Group 8: Delta and motion tracking */
        cmocka_unit_test(test_keyboard_dx_always_zero),
        cmocka_unit_test(test_mouse_dx_computed),
        cmocka_unit_test(test_no_movement_clears_motion),

        /* Group 9: Render info */
        cmocka_unit_test(test_render_info_values),
        cmocka_unit_test(test_render_info_after_size_change),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
