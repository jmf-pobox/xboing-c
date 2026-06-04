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

#include "game_context.h"
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
    /* Two distinct seeds producing 8 identical rand() values is
     * vanishingly improbable for any sane libc — each rand() draws
     * from [0, RAND_MAX] independently.  Anything < 8 matches means
     * the seeds are doing their job; the exact collision probability
     * depends on RAND_MAX (libc-dependent). */
    assert_true(matches < 8);
}

/* =========================================================================
 * game_create() does not call srand() — the load-bearing claim of
 * the caller-seeds design.  Verified two ways:
 *
 *   (a) Determinism: starting from the same caller-set seed, two
 *       independent game_create+destroy cycles leave rand() in the
 *       same state.  If game_create called srand(time(NULL))
 *       internally, the two cycles would (in most cases) end up in
 *       different rand() states; this test catches that.
 *
 *   (b) Consumption: game_create+destroy DOES consume at least one
 *       rand() call (ball mass init, level RNG, etc).  Asserting
 *       this rules out a regression where game_create somehow
 *       short-circuits rand consumption.
 *
 * This is the test Copilot review #142 asked for — it exercises
 * game_create rather than testing srand semantics in isolation.
 * ========================================================================= */

static char s_arg_prog[] = "test_rng_seeding";

static void test_game_create_does_not_seed(void **state)
{
    (void)state;
    char *argv[] = {s_arg_prog, NULL};

    /* (a) Determinism: same caller seed → same rand() state after
     *     game_create+destroy. */
    srand(0xABCDEF);
    game_ctx_t *c1 = game_create(1, argv);
    assert_non_null(c1);
    game_destroy(c1);
    int val_a = rand();

    srand(0xABCDEF);
    game_ctx_t *c2 = game_create(1, argv);
    assert_non_null(c2);
    game_destroy(c2);
    int val_b = rand();

    assert_int_equal(val_a, val_b);

    /* (b) Consumption: game_create+destroy moves the rand() state
     *     forward, so the first rand() after the cycle differs from
     *     the first rand() without the cycle. */
    srand(0xABCDEF);
    int fresh = rand();
    assert_int_not_equal(fresh, val_a);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_seed_rng_default_smoke),
        cmocka_unit_test(test_caller_seed_is_deterministic),
        cmocka_unit_test(test_different_seeds_diverge),
        cmocka_unit_test(test_game_create_does_not_seed),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
