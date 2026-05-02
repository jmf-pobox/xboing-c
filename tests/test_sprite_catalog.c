/*
 * test_sprite_catalog.c — Tests for the sprite catalog header-only helpers.
 *
 * Focus: sprite_block_animated_key sibling helper added for basket 1
 * (block animation frame selection — beads ejn, qe2, ax9, agi).
 *
 * Groups:
 *   1. NULL returns for non-animated block types
 *   2. BONUSX2/X4/BONUS 4-frame cycling (slide modulo 4)
 *   3. DEATH 5-frame cycling (slide modulo 5)
 *   4. EXTRABALL 2-frame cycling (slide modulo 2)
 *   5. ROAMER directional + out-of-range clamp
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

/* cmocka must come after setjmp.h / stdarg.h / stddef.h */
#include <cmocka.h>

#include "sprite_catalog.h"

/* =========================================================================
 * Group 1 — NULL returns for non-animated block types
 * ========================================================================= */

static void test_animated_key_red_blk_returns_null(void **state)
{
    (void)state;
    assert_null(sprite_block_animated_key(RED_BLK, 0));
    assert_null(sprite_block_animated_key(RED_BLK, 5));
}

static void test_animated_key_none_blk_returns_null(void **state)
{
    (void)state;
    assert_null(sprite_block_animated_key(NONE_BLK, 0));
}

static void test_animated_key_counter_blk_returns_null(void **state)
{
    (void)state;
    assert_null(sprite_block_animated_key(COUNTER_BLK, 0));
    assert_null(sprite_block_animated_key(COUNTER_BLK, 3));
}

static void test_animated_key_bullet_blk_returns_null(void **state)
{
    /* BULLET_BLK is a static composite (yellow + 4 bullets), not a per-tick animation. */
    (void)state;
    assert_null(sprite_block_animated_key(BULLET_BLK, 0));
}

/* =========================================================================
 * Group 2 — BONUSX2/X4/BONUS 4-frame cycling (slide modulo 4)
 * ========================================================================= */

static void test_animated_key_bonusx2(void **state)
{
    (void)state;
    assert_string_equal(sprite_block_animated_key(BONUSX2_BLK, 0), SPR_BLOCK_X2_1);
    assert_string_equal(sprite_block_animated_key(BONUSX2_BLK, 1), SPR_BLOCK_X2_2);
    assert_string_equal(sprite_block_animated_key(BONUSX2_BLK, 2), SPR_BLOCK_X2_3);
    assert_string_equal(sprite_block_animated_key(BONUSX2_BLK, 3), SPR_BLOCK_X2_4);
    /* slide modulo 4 for cycling */
    assert_string_equal(sprite_block_animated_key(BONUSX2_BLK, 4), SPR_BLOCK_X2_1);
    assert_string_equal(sprite_block_animated_key(BONUSX2_BLK, 7), SPR_BLOCK_X2_4);
}

static void test_animated_key_bonusx4(void **state)
{
    (void)state;
    assert_string_equal(sprite_block_animated_key(BONUSX4_BLK, 0), SPR_BLOCK_X4_1);
    assert_string_equal(sprite_block_animated_key(BONUSX4_BLK, 1), SPR_BLOCK_X4_2);
    assert_string_equal(sprite_block_animated_key(BONUSX4_BLK, 2), SPR_BLOCK_X4_3);
    assert_string_equal(sprite_block_animated_key(BONUSX4_BLK, 3), SPR_BLOCK_X4_4);
    assert_string_equal(sprite_block_animated_key(BONUSX4_BLK, 4), SPR_BLOCK_X4_1);
}

static void test_animated_key_bonus(void **state)
{
    (void)state;
    assert_string_equal(sprite_block_animated_key(BONUS_BLK, 0), SPR_BLOCK_BONUS_1);
    assert_string_equal(sprite_block_animated_key(BONUS_BLK, 1), SPR_BLOCK_BONUS_2);
    assert_string_equal(sprite_block_animated_key(BONUS_BLK, 2), SPR_BLOCK_BONUS_3);
    assert_string_equal(sprite_block_animated_key(BONUS_BLK, 3), SPR_BLOCK_BONUS_4);
    assert_string_equal(sprite_block_animated_key(BONUS_BLK, 4), SPR_BLOCK_BONUS_1);
}

/* =========================================================================
 * Group 3 — DEATH 5-frame cycling (slide modulo 5)
 * ========================================================================= */

static void test_animated_key_death(void **state)
{
    (void)state;
    assert_string_equal(sprite_block_animated_key(DEATH_BLK, 0), SPR_BLOCK_DEATH_1);
    assert_string_equal(sprite_block_animated_key(DEATH_BLK, 1), SPR_BLOCK_DEATH_2);
    assert_string_equal(sprite_block_animated_key(DEATH_BLK, 2), SPR_BLOCK_DEATH_3);
    assert_string_equal(sprite_block_animated_key(DEATH_BLK, 3), SPR_BLOCK_DEATH_4);
    assert_string_equal(sprite_block_animated_key(DEATH_BLK, 4), SPR_BLOCK_DEATH_5);
    /* slide modulo 5 for cycling */
    assert_string_equal(sprite_block_animated_key(DEATH_BLK, 5), SPR_BLOCK_DEATH_1);
    assert_string_equal(sprite_block_animated_key(DEATH_BLK, 9), SPR_BLOCK_DEATH_5);
}

/* =========================================================================
 * Group 4 — EXTRABALL 2-frame cycling (slide modulo 2)
 * ========================================================================= */

static void test_animated_key_extraball(void **state)
{
    (void)state;
    assert_string_equal(sprite_block_animated_key(EXTRABALL_BLK, 0), SPR_BLOCK_EXTRABALL);
    assert_string_equal(sprite_block_animated_key(EXTRABALL_BLK, 1), SPR_BLOCK_EXTRABALL_2);
    assert_string_equal(sprite_block_animated_key(EXTRABALL_BLK, 2), SPR_BLOCK_EXTRABALL);
    assert_string_equal(sprite_block_animated_key(EXTRABALL_BLK, 3), SPR_BLOCK_EXTRABALL_2);
}

/* =========================================================================
 * Group 5 — ROAMER directional + out-of-range clamp to neutral
 * ========================================================================= */

static void test_animated_key_roamer_directions(void **state)
{
    (void)state;
    assert_string_equal(sprite_block_animated_key(ROAMER_BLK, 0), SPR_BLOCK_ROAMER);
    assert_string_equal(sprite_block_animated_key(ROAMER_BLK, 1), SPR_BLOCK_ROAMER_L);
    assert_string_equal(sprite_block_animated_key(ROAMER_BLK, 2), SPR_BLOCK_ROAMER_R);
    assert_string_equal(sprite_block_animated_key(ROAMER_BLK, 3), SPR_BLOCK_ROAMER_U);
    assert_string_equal(sprite_block_animated_key(ROAMER_BLK, 4), SPR_BLOCK_ROAMER_D);
}

static void test_animated_key_roamer_out_of_range_clamps_to_neutral(void **state)
{
    (void)state;
    /* Out-of-range slide clamps to index 0 (neutral). Matches the original's
     * rand() % 5 at original/blocks.c:1372 which never exceeds 4, but
     * defensive against future slide-mutation bugs. */
    assert_string_equal(sprite_block_animated_key(ROAMER_BLK, 5), SPR_BLOCK_ROAMER);
    assert_string_equal(sprite_block_animated_key(ROAMER_BLK, 100), SPR_BLOCK_ROAMER);
    assert_string_equal(sprite_block_animated_key(ROAMER_BLK, -1), SPR_BLOCK_ROAMER);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1 — non-animated NULL */
        cmocka_unit_test(test_animated_key_red_blk_returns_null),
        cmocka_unit_test(test_animated_key_none_blk_returns_null),
        cmocka_unit_test(test_animated_key_counter_blk_returns_null),
        cmocka_unit_test(test_animated_key_bullet_blk_returns_null),

        /* Group 2 — BONUSX2/X4/BONUS 4-frame */
        cmocka_unit_test(test_animated_key_bonusx2),
        cmocka_unit_test(test_animated_key_bonusx4),
        cmocka_unit_test(test_animated_key_bonus),

        /* Group 3 — DEATH 5-frame */
        cmocka_unit_test(test_animated_key_death),

        /* Group 4 — EXTRABALL 2-frame */
        cmocka_unit_test(test_animated_key_extraball),

        /* Group 5 — ROAMER directional + clamp */
        cmocka_unit_test(test_animated_key_roamer_directions),
        cmocka_unit_test(test_animated_key_roamer_out_of_range_clamps_to_neutral),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
