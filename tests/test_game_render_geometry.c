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
 * level_life_position — right-to-left lives in level info panel
 * (basket 4, xboing-c-f9i)
 * ========================================================================= */

static void test_life_pos_rightmost_at_anchor(void **state)
{
    (void)state;
    /* Modern LEVEL_AREA_X=284, LEVEL_AREA_Y=5; sprite 25x24 (asset).
     * Life i=0 center at (284+175, 5+21) = (459, 26).
     * Top-left at (459 - 25/2, 26 - 24/2) = (447, 14). */
    int x = 0, y = 0;
    level_life_position(284, 5, 0, 25, 24, &x, &y);
    assert_int_equal(x, 447);
    assert_int_equal(y, 14);
}

static void test_life_pos_stride_30_left(void **state)
{
    (void)state;
    /* Life i=1 center 30px left of i=0 → (459-30, 26) = (429, 26). */
    int x = 0, y = 0;
    level_life_position(284, 5, 1, 25, 24, &x, &y);
    assert_int_equal(x, 429 - 25 / 2);
    assert_int_equal(y, 26 - 24 / 2);
}

static void test_life_pos_three_lives_progression(void **state)
{
    (void)state;
    /* Lives 0..2 should march left at 30px stride. */
    int x0 = 0, x1 = 0, x2 = 0;
    level_life_position(284, 5, 0, 25, 24, &x0, NULL);
    level_life_position(284, 5, 1, 25, 24, &x1, NULL);
    level_life_position(284, 5, 2, 25, 24, &x2, NULL);
    assert_int_equal(x0 - x1, 30);
    assert_int_equal(x1 - x2, 30);
}

static void test_life_pos_null_out_skipped(void **state)
{
    (void)state;
    int y = 0;
    level_life_position(284, 5, 0, 25, 24, NULL, &y);
    assert_int_equal(y, 14);
    int x = 0;
    level_life_position(284, 5, 0, 25, 24, &x, NULL);
    assert_int_equal(x, 447);
}

static void test_life_pos_at_origin(void **state)
{
    (void)state;
    /* level_area at (0,0) — centers at (175, 21).
     * Sprite 25x24, integer-half = 12 (matches original/level.c:204
     * RenderShape(... x - 12, y - 12, 25, 24)).  Top-left at (163, 9). */
    int x = 0, y = 0;
    level_life_position(0, 0, 0, 25, 24, &x, &y);
    assert_int_equal(x, 163);
    assert_int_equal(y, 9);
}

/* =========================================================================
 * level_number_digit_position — right-anchored level number digits
 * (basket 4, xboing-c-82v)
 * ========================================================================= */

static void test_level_num_rightmost_digit(void **state)
{
    (void)state;
    /* Modern LEVEL_AREA_X=284.  Anchor is window-local x=260; rightmost
     * digit (index 0) drawn at anchor - 32 = 228.  Absolute: 284 + 228 = 512. */
    int x = 0, y = 0;
    level_number_digit_position(284, 5, 0, &x, &y);
    assert_int_equal(x, 512);
    assert_int_equal(y, 10);
}

static void test_level_num_tens_digit(void **state)
{
    (void)state;
    /* Tens digit (index 1) at anchor - 64 = 196.  Absolute: 284 + 196 = 480. */
    int x = 0, y = 0;
    level_number_digit_position(284, 5, 1, &x, &y);
    assert_int_equal(x, 480);
    assert_int_equal(y, 10);
}

static void test_level_num_hundreds_digit(void **state)
{
    (void)state;
    /* Hundreds (index 2) at anchor - 96 = 164.  Absolute: 284 + 164 = 448. */
    int x = 0, y = 0;
    level_number_digit_position(284, 5, 2, &x, &y);
    assert_int_equal(x, 448);
    assert_int_equal(y, 10);
}

static void test_level_num_stride_32(void **state)
{
    (void)state;
    /* Each successive digit is 32 px to the left of the prior (more
     * significant digit further left).  Difference between index 0 and
     * index 1 is 32. */
    int x0 = 0, x1 = 0;
    level_number_digit_position(284, 5, 0, &x0, NULL);
    level_number_digit_position(284, 5, 1, &x1, NULL);
    assert_int_equal(x0 - x1, 32);
}

static void test_level_num_at_origin(void **state)
{
    (void)state;
    /* level_area at (0,0): rightmost digit at anchor-32 = 228, y=5. */
    int x = 0, y = 0;
    level_number_digit_position(0, 0, 0, &x, &y);
    assert_int_equal(x, 228);
    assert_int_equal(y, 5);
}

/* =========================================================================
 * bonus_row_item_x — coin/bullet centred row for bonus screen
 * (basket 5, xboing-c-tp4)
 * ========================================================================= */

static void test_bonus_row_first_item_leftmost(void **state)
{
    (void)state;
    /* total=5 coins at stride 37, center_x=500.
     * Item 0 (leftmost, first to appear):
     *   max_len = 5*37 + 5 = 190; max_len/2 = 95
     *   x = 500 + 95 - (5 - 0) * 37 = 500 + 95 - 185 = 410. */
    assert_int_equal(bonus_row_item_x(500, 5, 37, 0), 410);
}

static void test_bonus_row_last_item_rightmost(void **state)
{
    (void)state;
    /* Item N-1 (rightmost):
     *   x = 500 + 95 - (5 - 4) * 37 = 500 + 95 - 37 = 558. */
    assert_int_equal(bonus_row_item_x(500, 5, 37, 4), 558);
}

static void test_bonus_row_stride_between_items(void **state)
{
    (void)state;
    int x0 = bonus_row_item_x(500, 5, 37, 0);
    int x1 = bonus_row_item_x(500, 5, 37, 1);
    int x2 = bonus_row_item_x(500, 5, 37, 2);
    /* Successive items differ by exactly stride. */
    assert_int_equal(x1 - x0, 37);
    assert_int_equal(x2 - x1, 37);
}

static void test_bonus_row_bullet_stride(void **state)
{
    (void)state;
    /* Bullet row: stride=10, total=20 bullets, center=500.
     *   max_len = 20*10 + 5 = 205; max_len/2 = 102 (int)
     *   item 0:  500 + 102 - 20*10 = 500 + 102 - 200 = 402
     *   item 19: 500 + 102 - 1*10  = 592 */
    assert_int_equal(bonus_row_item_x(500, 20, 10, 0), 402);
    assert_int_equal(bonus_row_item_x(500, 20, 10, 19), 592);
}

static void test_bonus_row_single_item_centred(void **state)
{
    (void)state;
    /* Single item at total=1, stride=37:
     *   max_len = 37 + 5 = 42; max_len/2 = 21
     *   x = center + 21 - 1*37 = center - 16. */
    assert_int_equal(bonus_row_item_x(500, 1, 37, 0), 484);
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
        cmocka_unit_test(test_life_pos_rightmost_at_anchor),
        cmocka_unit_test(test_life_pos_stride_30_left),
        cmocka_unit_test(test_life_pos_three_lives_progression),
        cmocka_unit_test(test_life_pos_null_out_skipped),
        cmocka_unit_test(test_life_pos_at_origin),
        cmocka_unit_test(test_level_num_rightmost_digit),
        cmocka_unit_test(test_level_num_tens_digit),
        cmocka_unit_test(test_level_num_hundreds_digit),
        cmocka_unit_test(test_level_num_stride_32),
        cmocka_unit_test(test_level_num_at_origin),
        cmocka_unit_test(test_bonus_row_first_item_leftmost),
        cmocka_unit_test(test_bonus_row_last_item_rightmost),
        cmocka_unit_test(test_bonus_row_stride_between_items),
        cmocka_unit_test(test_bonus_row_bullet_stride),
        cmocka_unit_test(test_bonus_row_single_item_centred),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
