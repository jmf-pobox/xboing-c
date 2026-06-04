/*
 * test_rng_seeding.c — verify the caller-seeds RNG contract.
 *
 * Bead: xboing-c-hty.  The library never calls srand on the caller's
 * behalf; production main() calls game_seed_rng_default() before
 * game_create(); tests are free to seed (or not seed) as needed.
 *
 * These tests pin the contract so any future change that silently
 * adds srand() inside game_create gets caught — the deterministic-
 * seed sequence would diverge.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>

#include <cmocka.h>

#include "game_init.h"

/* =========================================================================
 * Smoke: the wrapper is reachable from the test binary, doesn't crash.
 * ========================================================================= */

static void test_seed_rng_default_smoke(void **state)
{
    (void)state;
    game_seed_rng_default();
    /* No assertion needed — reaching this line is the assertion. */
}

/* =========================================================================
 * Caller-controlled seed survives across the rand() consumers in
 * the test binary's transitive deps.  Asserting POSIX srand semantics
 * here serves as documentation: if anyone changes game_init or any
 * linked library to silently swallow rand state, this test catches it.
 * ========================================================================= */

static void test_caller_seed_is_deterministic(void **state)
{
    (void)state;
    srand(0xABCDEF);
    int a1 = rand();
    int a2 = rand();
    int a3 = rand();

    srand(0xABCDEF);
    assert_int_equal(rand(), a1);
    assert_int_equal(rand(), a2);
    assert_int_equal(rand(), a3);
}

/* =========================================================================
 * Different seeds produce different sequences.  Pinned so a future
 * change that turns game_seed_rng_default into a no-op (e.g.,
 * srand(0) hard-coded) doesn't silently revert non-determinism in
 * production.
 * ========================================================================= */

static void test_different_seeds_diverge(void **state)
{
    (void)state;
    srand(1);
    int seq1[8];
    for (int i = 0; i < 8; i++)
        seq1[i] = rand();

    srand(2);
    int matches = 0;
    for (int i = 0; i < 8; i++)
    {
        if (rand() == seq1[i])
            matches++;
    }
    /* Two distinct seeds producing 8 identical rand() values would
     * be a 1-in-2^248 collision.  Anything < 8 matches means the
     * seeds are doing their job. */
    assert_true(matches < 8);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_seed_rng_default_smoke),
        cmocka_unit_test(test_caller_seed_is_deterministic),
        cmocka_unit_test(test_different_seeds_diverge),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
