/*
 * test_score.c — Characterization tests for scoring logic.
 *
 * Bead n9e.2: Scoring system characterization tests.
 *
 * Tests CAPTURE current behavior of the extracted score_logic functions.
 * Do NOT fix bugs found here — document them as bead candidates.
 *
 * All tests call score_logic.h functions directly. No X11, no game
 * globals. Every function is pure — no setup/teardown needed.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <sys/types.h>

#include "score_logic.h"
#include "block_types.h"

/* -------------------------------------------------------------------------
 * Group 1: Multiplier logic (score_apply_multiplier)
 * Source: score.c ComputeScore() lines 226-229
 * ------------------------------------------------------------------------- */

/* TC-01: No bonus active — score passes through unchanged. */
static void test_multiplier_no_bonus(void **state)
{
    (void)state;
    assert_int_equal((int)score_apply_multiplier(100, 0, 0), 100);
    assert_int_equal((int)score_apply_multiplier(0, 0, 0), 0);
}

/* TC-02: x2 active — score is doubled. */
static void test_multiplier_x2(void **state)
{
    (void)state;
    assert_int_equal((int)score_apply_multiplier(100, 1, 0), 200);
    assert_int_equal((int)score_apply_multiplier(150, 1, 0), 300);
}

/* TC-03: x4 active — score is quadrupled. */
static void test_multiplier_x4(void **state)
{
    (void)state;
    assert_int_equal((int)score_apply_multiplier(100, 0, 1), 400);
    assert_int_equal((int)score_apply_multiplier(50, 0, 1), 200);
}

/* TC-04: Both x2 and x4 active — x2 wins (if/else if precedence).
 * This characterizes the existing code behavior. */
static void test_multiplier_both_x2_wins(void **state)
{
    (void)state;
    assert_int_equal((int)score_apply_multiplier(100, 1, 1), 200);
}

/* -------------------------------------------------------------------------
 * Group 2: Block hit point values (score_block_hit_points)
 * Source: blocks.c AddNewBlock() switch, lines 2495-2566
 * ------------------------------------------------------------------------- */

/* TC-05: Color blocks have progressive point values. */
static void test_block_points_color_blocks(void **state)
{
    (void)state;
    assert_int_equal(score_block_hit_points(RED_BLK, 0), 100);
    assert_int_equal(score_block_hit_points(BLUE_BLK, 0), 110);
    assert_int_equal(score_block_hit_points(GREEN_BLK, 0), 120);
    assert_int_equal(score_block_hit_points(TAN_BLK, 0), 130);
    assert_int_equal(score_block_hit_points(YELLOW_BLK, 0), 140);
    assert_int_equal(score_block_hit_points(PURPLE_BLK, 0), 150);
}

/* TC-06: Special zero-point and fixed-point blocks. */
static void test_block_points_special_blocks(void **state)
{
    (void)state;
    assert_int_equal(score_block_hit_points(DEATH_BLK, 0), 0);
    assert_int_equal(score_block_hit_points(BOMB_BLK, 0), 50);
    assert_int_equal(score_block_hit_points(BULLET_BLK, 0), 50);
    assert_int_equal(score_block_hit_points(MAXAMMO_BLK, 0), 50);
    assert_int_equal(score_block_hit_points(ROAMER_BLK, 0), 400);
    assert_int_equal(score_block_hit_points(COUNTER_BLK, 0), 200);
}

/* TC-07: DROP_BLK points depend on row — higher row = less score.
 * Formula: (MAX_ROW - row) * 100.  MAX_ROW=18.
 * Row 0 (top) = 1800, Row 17 (bottom) = 100, Row 9 (middle) = 900. */
static void test_block_points_drop_block_row_dependent(void **state)
{
    (void)state;
    assert_int_equal(score_block_hit_points(DROP_BLK, 0), 1800);
    assert_int_equal(score_block_hit_points(DROP_BLK, 17), 100);
    assert_int_equal(score_block_hit_points(DROP_BLK, 9), 900);
}

/* TC-08: Special action blocks all score 100 points. */
static void test_block_points_action_blocks_score_100(void **state)
{
    (void)state;
    assert_int_equal(score_block_hit_points(EXTRABALL_BLK, 0), 100);
    assert_int_equal(score_block_hit_points(TIMER_BLK, 0), 100);
    assert_int_equal(score_block_hit_points(HYPERSPACE_BLK, 0), 100);
    assert_int_equal(score_block_hit_points(MGUN_BLK, 0), 100);
    assert_int_equal(score_block_hit_points(WALLOFF_BLK, 0), 100);
    assert_int_equal(score_block_hit_points(REVERSE_BLK, 0), 100);
    assert_int_equal(score_block_hit_points(MULTIBALL_BLK, 0), 100);
    assert_int_equal(score_block_hit_points(STICKY_BLK, 0), 100);
    assert_int_equal(score_block_hit_points(PAD_SHRINK_BLK, 0), 100);
    assert_int_equal(score_block_hit_points(PAD_EXPAND_BLK, 0), 100);
}

/* TC-09: Blocks that fall through the default case score 0.
 * DYNAMITE_BLK, BONUSX2_BLK, BONUSX4_BLK, BONUS_BLK, BLACKHIT_BLK
 * have no case in the switch — hitPoints is implicitly 0. */
static void test_block_points_default_zero(void **state)
{
    (void)state;
    assert_int_equal(score_block_hit_points(DYNAMITE_BLK, 0), 0);
    assert_int_equal(score_block_hit_points(BONUSX2_BLK, 0), 0);
    assert_int_equal(score_block_hit_points(BONUSX4_BLK, 0), 0);
    assert_int_equal(score_block_hit_points(BONUS_BLK, 0), 0);
    assert_int_equal(score_block_hit_points(BLACKHIT_BLK, 0), 0);
    assert_int_equal(score_block_hit_points(BLACK_BLK, 0), 0);
}

/* -------------------------------------------------------------------------
 * Group 3: Bonus score calculation (score_compute_bonus)
 * Source: bonus.c ComputeAndAddBonusScore(), lines 838-888
 * ------------------------------------------------------------------------- */

/* TC-10: No time bonus (time_bonus=0) suppresses coin/level/time awards.
 * Only bullet bonus is awarded unconditionally. */
static void test_bonus_no_time_bonus(void **state)
{
    (void)state;
    /* time_bonus=0, num_bonus=3, max_bonus=8, num_bullets=4, level_adj=1 */
    u_long result = score_compute_bonus(0, 3, 8, 4, 1);
    /* Expected: bullet only = 4 * 500 = 2000 */
    assert_int_equal((int)result, 2000);
}

/* TC-11: Normal bonus below MAX_BONUS threshold. */
static void test_bonus_normal_coins(void **state)
{
    (void)state;
    /* time_bonus=60, num_bonus=3, max_bonus=8, num_bullets=0, level_adj=2 */
    /* Expected: (3 * 3000) + (100 * 2) + 0 + (100 * 60)
     *         = 9000 + 200 + 0 + 6000 = 15200 */
    u_long result = score_compute_bonus(60, 3, 8, 0, 2);
    assert_int_equal((int)result, 15200);
}

/* TC-12: Super bonus kicks in when num_bonus > MAX_BONUS. */
static void test_bonus_super_bonus_above_threshold(void **state)
{
    (void)state;
    /* time_bonus=10, num_bonus=9, max_bonus=8, num_bullets=0, level_adj=1 */
    /* Expected: SUPER_BONUS_SCORE(50000) + (100*1) + (100*10)
     *         = 50000 + 100 + 1000 = 51100 */
    u_long result = score_compute_bonus(10, 9, 8, 0, 1);
    assert_int_equal((int)result, 51100);
}

/* TC-13: Exactly at MAX_BONUS threshold — super bonus does NOT trigger.
 * num_bonus > MAX_BONUS is strict greater-than. */
static void test_bonus_exactly_at_threshold_not_super(void **state)
{
    (void)state;
    /* time_bonus=1, num_bonus=8, max_bonus=8, num_bullets=0, level_adj=1 */
    /* Expected: (8 * 3000) + (100 * 1) + (100 * 1)
     *         = 24000 + 100 + 100 = 24200 */
    u_long result = score_compute_bonus(1, 8, 8, 0, 1);
    assert_int_equal((int)result, 24200);
}

/* -------------------------------------------------------------------------
 * Group 4: Extra life threshold (score_extra_life_threshold)
 * Source: level.c CheckAndAddExtraLife(), lines 504-514
 * ------------------------------------------------------------------------- */

/* TC-14: No life awarded below 100,000 points. */
static void test_extra_life_below_threshold(void **state)
{
    (void)state;
    assert_int_equal(score_extra_life_threshold(0), 0);
    assert_int_equal(score_extra_life_threshold(99999L), 0);
}

/* TC-15: Life awarded at exactly 100,000 points. */
static void test_extra_life_at_threshold(void **state)
{
    (void)state;
    int prev = score_extra_life_threshold(99999L);
    int curr = score_extra_life_threshold(100000L);
    assert_int_equal(prev, 0);
    assert_int_equal(curr, 1);
}

/* TC-16: Second life at 200,000 (not 150,000). Threshold is cumulative. */
static void test_extra_life_second_threshold(void **state)
{
    (void)state;
    assert_int_equal(score_extra_life_threshold(150000L), 1);
    assert_int_equal(score_extra_life_threshold(199999L), 1);
    assert_int_equal(score_extra_life_threshold(200000L), 2);
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_multiplier_no_bonus),
        cmocka_unit_test(test_multiplier_x2),
        cmocka_unit_test(test_multiplier_x4),
        cmocka_unit_test(test_multiplier_both_x2_wins),
        cmocka_unit_test(test_block_points_color_blocks),
        cmocka_unit_test(test_block_points_special_blocks),
        cmocka_unit_test(test_block_points_drop_block_row_dependent),
        cmocka_unit_test(test_block_points_action_blocks_score_100),
        cmocka_unit_test(test_block_points_default_zero),
        cmocka_unit_test(test_bonus_no_time_bonus),
        cmocka_unit_test(test_bonus_normal_coins),
        cmocka_unit_test(test_bonus_super_bonus_above_threshold),
        cmocka_unit_test(test_bonus_exactly_at_threshold_not_super),
        cmocka_unit_test(test_extra_life_below_threshold),
        cmocka_unit_test(test_extra_life_at_threshold),
        cmocka_unit_test(test_extra_life_second_threshold),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
