/*
 * test_block_sound.c — exhaustive table test for the block-type →
 * sound-name mapping.  Pure function, no audio context needed.
 *
 * Spec: docs/specs/2026-06-03-sfx-testability.md (Component 3a).
 * Peer review B2: every block type defined in block_types.h gets an
 * explicit assertion.  A vacuous `(void)block_sound_name(t)` loop
 * would let a NULL-returning regression pass.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "block_sound.h"
#include "block_types.h"

static void test_block_sound_exhaustive(void **state)
{
    (void)state;
    /* Asserts the current block-type → sound-name mapping for every
     * type defined in block_types.h.  Regressions on these entries
     * (renames, deletions, wrong names) fail here.  Note: a NEW type
     * added to block_types.h won't fail this test — additions must
     * be explicitly registered below.  Component 4's
     * audio-literals-check provides the additional guard. */
    assert_string_equal(block_sound_name(BOMB_BLK), "bomb");
    assert_string_equal(block_sound_name(BULLET_BLK), "ammo");
    assert_string_equal(block_sound_name(MAXAMMO_BLK), "ammo");
    assert_string_equal(block_sound_name(RED_BLK), "touch");
    assert_string_equal(block_sound_name(GREEN_BLK), "touch");
    assert_string_equal(block_sound_name(BLUE_BLK), "touch");
    assert_string_equal(block_sound_name(TAN_BLK), "touch");
    assert_string_equal(block_sound_name(PURPLE_BLK), "touch");
    assert_string_equal(block_sound_name(YELLOW_BLK), "touch");
    assert_string_equal(block_sound_name(COUNTER_BLK), "touch");
    assert_string_equal(block_sound_name(RANDOM_BLK), "touch");
    assert_string_equal(block_sound_name(DROP_BLK), "touch");
    assert_string_equal(block_sound_name(ROAMER_BLK), "ouch");
    assert_string_equal(block_sound_name(EXTRABALL_BLK), "ddloo");
    assert_string_equal(block_sound_name(MGUN_BLK), "mgun");
    assert_string_equal(block_sound_name(WALLOFF_BLK), "wallsoff");
    assert_string_equal(block_sound_name(BONUSX2_BLK), "gate");
    assert_string_equal(block_sound_name(BONUSX4_BLK), "gate");
    assert_string_equal(block_sound_name(BONUS_BLK), "gate");
    assert_string_equal(block_sound_name(REVERSE_BLK), "warp");
    assert_string_equal(block_sound_name(PAD_SHRINK_BLK), "wzzz2");
    assert_string_equal(block_sound_name(PAD_EXPAND_BLK), "wzzz");
    assert_string_equal(block_sound_name(MULTIBALL_BLK), "spring");
    assert_string_equal(block_sound_name(TIMER_BLK), "bonus");
    assert_string_equal(block_sound_name(STICKY_BLK), "sticky");
    assert_string_equal(block_sound_name(DEATH_BLK), "evillaugh");
    assert_string_equal(block_sound_name(BLACK_BLK), "metal");
    assert_string_equal(block_sound_name(HYPERSPACE_BLK), "hypspc");
    /* Explicitly silent — see comments in src/block_sound.c. */
    assert_null(block_sound_name(DYNAMITE_BLK));
    assert_null(block_sound_name(BLACKHIT_BLK));
}

static void test_block_sound_sentinels_and_invalid(void **state)
{
    (void)state;
    /* Sentinels: not destructible blocks. */
    assert_null(block_sound_name(NONE_BLK));
    assert_null(block_sound_name(KILL_BLK));
    /* Out-of-range values: defensively silent, not crash. */
    assert_null(block_sound_name(-99));
    assert_null(block_sound_name(MAX_BLOCKS));
    assert_null(block_sound_name(9999));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_block_sound_exhaustive),
        cmocka_unit_test(test_block_sound_sentinels_and_invalid),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
