/*
 * test_block_sound.c — exhaustive table test for the block-type →
 * (sound name, volume) mapping.  Pure function, no audio context needed.
 *
 * Spec: docs/specs/2026-06-03-sfx-testability.md (Component 3a).
 * Peer review B2: every block type defined in block_types.h gets an
 * explicit assertion.  A vacuous `(void)block_sound_lookup(t)` loop
 * would let a NULL-returning regression pass.
 *
 * Volume column added by docs/audits/2026-06-28-audio-volume-modulation.md.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "block_sound.h"
#include "block_types.h"

/* Compile-time guard: if MAX_BLOCKS changes (new block type added),
 * this static_assert fails to force the author to audit the
 * exhaustive table below and the switch in src/block_sound.c. */
_Static_assert(MAX_BLOCKS == 30,
               "MAX_BLOCKS changed — audit test_block_sound_exhaustive() and "
               "block_sound_lookup() to cover the new type(s).");

static void assert_sound(int block_type, const char *name, int volume)
{
    block_sound_t s = block_sound_lookup(block_type);
    assert_string_equal(s.name, name);
    assert_int_equal(s.volume, volume);
}

static void test_block_sound_exhaustive(void **state)
{
    (void)state;
    /* Asserts the current block-type → (name, volume) mapping for every
     * type defined in block_types.h.  Regressions on these entries
     * (renames, deletions, wrong names, wrong volumes) fail here.
     * Additions are caught at compile time by the _Static_assert above. */
    assert_sound(BOMB_BLK, "bomb", 50);
    assert_sound(BULLET_BLK, "ammo", 30);
    assert_sound(MAXAMMO_BLK, "ammo", 70);
    assert_sound(RED_BLK, "touch", 99);
    assert_sound(GREEN_BLK, "touch", 99);
    assert_sound(BLUE_BLK, "touch", 99);
    assert_sound(TAN_BLK, "touch", 99);
    assert_sound(PURPLE_BLK, "touch", 99);
    assert_sound(YELLOW_BLK, "touch", 99);
    assert_sound(COUNTER_BLK, "touch", 99);
    assert_sound(RANDOM_BLK, "touch", 99);
    assert_sound(DROP_BLK, "touch", 99);
    assert_sound(ROAMER_BLK, "ouch", 99);
    assert_sound(EXTRABALL_BLK, "ddloo", 99);
    assert_sound(MGUN_BLK, "mgun", 99);
    assert_sound(WALLOFF_BLK, "wallsoff", 99);
    assert_sound(BONUSX2_BLK, "gate", 99);
    assert_sound(BONUSX4_BLK, "gate", 99);
    assert_sound(BONUS_BLK, "gate", 99);
    assert_sound(REVERSE_BLK, "warp", 99);
    assert_sound(PAD_SHRINK_BLK, "wzzz2", 99);
    assert_sound(PAD_EXPAND_BLK, "wzzz", 99);
    assert_sound(MULTIBALL_BLK, "spring", 80);
    assert_sound(TIMER_BLK, "bonus", 50);
    assert_sound(STICKY_BLK, "sticky", 90);
    assert_sound(DEATH_BLK, "evillaugh", 99);
    assert_sound(BLACK_BLK, "metal", 99);
    assert_sound(HYPERSPACE_BLK, "hypspc", 99);
    /* Explicitly silent — see comments in src/block_sound.c. */
    assert_null(block_sound_lookup(DYNAMITE_BLK).name);
    assert_null(block_sound_lookup(BLACKHIT_BLK).name);
}

static void test_block_sound_sentinels_and_invalid(void **state)
{
    (void)state;
    /* Sentinels: not destructible blocks. */
    assert_null(block_sound_lookup(NONE_BLK).name);
    assert_null(block_sound_lookup(KILL_BLK).name);
    /* Out-of-range values: defensively silent, not crash. */
    assert_null(block_sound_lookup(-99).name);
    assert_null(block_sound_lookup(MAX_BLOCKS).name);
    assert_null(block_sound_lookup(9999).name);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_block_sound_exhaustive),
        cmocka_unit_test(test_block_sound_sentinels_and_invalid),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
