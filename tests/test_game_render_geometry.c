/*
 * test_game_render_geometry.c — Tests for pure geometry helpers in
 * include/game_render.h.
 *
 * Currently covers block_overlay_text_pos (basket 2 composite text centering).
 * Added in response to Copilot review F1 on PR #103: composite math had no
 * automated coverage, leaving one-pixel regressions invisible to CI.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

/* cmocka must come after setjmp.h / stdarg.h / stddef.h */
#include <cmocka.h>

#include "game_render.h"

/* =========================================================================
 * block_overlay_text_pos — center text within a block bounding box
 * ========================================================================= */

static void test_overlay_pos_centered_text_smaller_than_block(void **state)
{
    (void)state;
    /* Text 10x6 centered in 40x20 block at origin (100, 200).
     * Center of block: (120, 210). Text top-left to center: (120-5, 210-3) = (115, 207). */
    int x = 0, y = 0;
    block_overlay_text_pos(100, 200, 40, 20, 10, 6, &x, &y);
    assert_int_equal(x, 115);
    assert_int_equal(y, 207);
}

static void test_overlay_pos_text_exactly_block_size(void **state)
{
    (void)state;
    /* Text matching block dimensions lands flush at the block origin. */
    int x = 0, y = 0;
    block_overlay_text_pos(50, 50, 40, 20, 40, 20, &x, &y);
    assert_int_equal(x, 50);
    assert_int_equal(y, 50);
}

static void test_overlay_pos_text_larger_than_block_negative_offset(void **state)
{
    (void)state;
    /* Text wider than block: top-left goes negative relative to block origin
     * (text overhangs both sides). The function does not clip — it returns
     * the centering position, callers handle clipping if needed. */
    int x = 0, y = 0;
    block_overlay_text_pos(0, 0, 40, 20, 60, 30, &x, &y);
    assert_int_equal(x, -10); /* (40/2) - (60/2) = 20 - 30 = -10 */
    assert_int_equal(y, -5);  /* (20/2) - (30/2) = 10 - 15 = -5 */
}

static void test_overlay_pos_at_arbitrary_origin(void **state)
{
    (void)state;
    /* Block at (300, 400), text 14x10 centered. */
    int x = 0, y = 0;
    block_overlay_text_pos(300, 400, 40, 20, 14, 10, &x, &y);
    assert_int_equal(x, 300 + 20 - 7); /* 313 */
    assert_int_equal(y, 400 + 10 - 5); /* 405 */
}

static void test_overlay_pos_null_out_x_skipped(void **state)
{
    (void)state;
    /* NULL out_x is a partial-use pattern: only y is computed. */
    int y = 0;
    block_overlay_text_pos(100, 200, 40, 20, 10, 6, NULL, &y);
    assert_int_equal(y, 207);
}

static void test_overlay_pos_null_out_y_skipped(void **state)
{
    (void)state;
    int x = 0;
    block_overlay_text_pos(100, 200, 40, 20, 10, 6, &x, NULL);
    assert_int_equal(x, 115);
}

static void test_overlay_pos_zero_text(void **state)
{
    (void)state;
    /* Zero-size text centers at block center (text top-left = block center). */
    int x = 0, y = 0;
    block_overlay_text_pos(100, 200, 40, 20, 0, 0, &x, &y);
    assert_int_equal(x, 120);
    assert_int_equal(y, 210);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_overlay_pos_centered_text_smaller_than_block),
        cmocka_unit_test(test_overlay_pos_text_exactly_block_size),
        cmocka_unit_test(test_overlay_pos_text_larger_than_block_negative_offset),
        cmocka_unit_test(test_overlay_pos_at_arbitrary_origin),
        cmocka_unit_test(test_overlay_pos_null_out_x_skipped),
        cmocka_unit_test(test_overlay_pos_null_out_y_skipped),
        cmocka_unit_test(test_overlay_pos_zero_text),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
