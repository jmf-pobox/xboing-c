/*
 * test_sdl2_loop.c — CMocka tests for the fixed-timestep game loop module.
 *
 * Pure logic tests — no video, audio, or SDL2 runtime needed.
 * Synthetic time injection makes all tests deterministic.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* cmocka.h must come after the setjmp/stdarg/stddef includes. */
#include <cmocka.h>

#include "sdl2_loop.h"

/* =========================================================================
 * Test callback infrastructure
 * ========================================================================= */

typedef struct
{
    int tick_count;
    int render_count;
    double last_alpha;
} callback_log_t;

static void tick_callback(void *user_data)
{
    callback_log_t *log = (callback_log_t *)user_data;
    log->tick_count++;
}

static void render_callback(double alpha, void *user_data)
{
    callback_log_t *log = (callback_log_t *)user_data;
    log->render_count++;
    log->last_alpha = alpha;
}

/* =========================================================================
 * Helper: create default loop with callback log
 * ========================================================================= */

static sdl2_loop_t *create_default_loop(callback_log_t *log, sdl2_loop_status_t *status)
{
    memset(log, 0, sizeof(*log));
    return sdl2_loop_create(tick_callback, render_callback, log, status);
}

/* =========================================================================
 * Group 1: Lifecycle
 * ========================================================================= */

static void test_create_destroy(void **state)
{
    (void)state;
    sdl2_loop_status_t status;
    callback_log_t log;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);
    assert_non_null(ctx);
    assert_int_equal(status, SDL2L_OK);
    sdl2_loop_destroy(ctx);
}

static void test_destroy_null(void **state)
{
    (void)state;
    sdl2_loop_destroy(NULL); /* must not crash */
}

static void test_create_null_callbacks(void **state)
{
    (void)state;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = sdl2_loop_create(NULL, NULL, NULL, &status);
    assert_non_null(ctx);
    assert_int_equal(status, SDL2L_OK);

    /* Update with NULL callbacks should not crash. */
    int ticks = sdl2_loop_update(ctx, 100);
    assert_true(ticks > 0);

    sdl2_loop_destroy(ctx);
}

static void test_create_null_status(void **state)
{
    (void)state;
    sdl2_loop_t *ctx = sdl2_loop_create(NULL, NULL, NULL, NULL);
    assert_non_null(ctx);
    sdl2_loop_destroy(ctx);
}

static void test_defaults(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);
    assert_non_null(ctx);

    assert_int_equal(sdl2_loop_get_speed(ctx), SDL2L_DEFAULT_SPEED);
    assert_false(sdl2_loop_is_paused(ctx));
    assert_int_equal(sdl2_loop_total_ticks(ctx), 0);
    assert_float_equal(sdl2_loop_alpha(ctx), 0.0, 0.001);

    sdl2_loop_destroy(ctx);
}

/* =========================================================================
 * Group 2: Tick interval calculation
 * ========================================================================= */

static void test_tick_interval_all_speeds(void **state)
{
    (void)state;
    /* Warp 1 (slowest): 1500 * 9 = 13500 us */
    assert_int_equal(sdl2_loop_tick_interval_us(1), 13500);
    /* Warp 5 (default): 1500 * 5 = 7500 us */
    assert_int_equal(sdl2_loop_tick_interval_us(5), 7500);
    /* Warp 9 (fastest): 1500 * 1 = 1500 us */
    assert_int_equal(sdl2_loop_tick_interval_us(9), 1500);
}

static void test_tick_interval_monotonic(void **state)
{
    (void)state;
    /* Higher speed → shorter interval. */
    for (int s = SDL2L_MIN_SPEED; s < SDL2L_MAX_SPEED; s++)
    {
        assert_true(sdl2_loop_tick_interval_us(s) > sdl2_loop_tick_interval_us(s + 1));
    }
}

static void test_tick_interval_invalid(void **state)
{
    (void)state;
    assert_int_equal(sdl2_loop_tick_interval_us(0), 0);
    assert_int_equal(sdl2_loop_tick_interval_us(10), 0);
    assert_int_equal(sdl2_loop_tick_interval_us(-1), 0);
}

/* =========================================================================
 * Group 3: Speed control
 * ========================================================================= */

static void test_set_speed_valid(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);

    for (int s = SDL2L_MIN_SPEED; s <= SDL2L_MAX_SPEED; s++)
    {
        assert_int_equal(sdl2_loop_set_speed(ctx, s), SDL2L_OK);
        assert_int_equal(sdl2_loop_get_speed(ctx), s);
    }

    sdl2_loop_destroy(ctx);
}

static void test_set_speed_invalid(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);

    assert_int_equal(sdl2_loop_set_speed(ctx, 0), SDL2L_ERR_INVALID_SPEED);
    assert_int_equal(sdl2_loop_set_speed(ctx, 10), SDL2L_ERR_INVALID_SPEED);
    assert_int_equal(sdl2_loop_set_speed(ctx, -1), SDL2L_ERR_INVALID_SPEED);

    /* Speed should remain at default after invalid attempts. */
    assert_int_equal(sdl2_loop_get_speed(ctx), SDL2L_DEFAULT_SPEED);

    sdl2_loop_destroy(ctx);
}

static void test_set_speed_null(void **state)
{
    (void)state;
    assert_int_equal(sdl2_loop_set_speed(NULL, 5), SDL2L_ERR_NULL_ARG);
}

static void test_get_speed_null(void **state)
{
    (void)state;
    assert_int_equal(sdl2_loop_get_speed(NULL), SDL2L_DEFAULT_SPEED);
}

/* =========================================================================
 * Group 4: Update / tick dispatch
 * ========================================================================= */

static void test_update_single_tick(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);

    /* Default speed 5: interval = 7500 us = 7.5 ms.
     * Inject 8 ms → should produce 1 tick with 500 us leftover. */
    int ticks = sdl2_loop_update(ctx, 8);
    assert_int_equal(ticks, 1);
    assert_int_equal(log.tick_count, 1);
    assert_int_equal(log.render_count, 1);
    assert_int_equal(sdl2_loop_total_ticks(ctx), 1);

    sdl2_loop_destroy(ctx);
}

static void test_update_multiple_ticks(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);

    /* Inject 20 ms at speed 5 (7.5 ms/tick) → 2 ticks, 5000 us leftover. */
    int ticks = sdl2_loop_update(ctx, 20);
    assert_int_equal(ticks, 2);
    assert_int_equal(log.tick_count, 2);
    assert_int_equal(log.render_count, 1);

    sdl2_loop_destroy(ctx);
}

static void test_update_zero_elapsed(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);

    /* Zero elapsed — no ticks, but render still called once. */
    int ticks = sdl2_loop_update(ctx, 0);
    assert_int_equal(ticks, 0);
    assert_int_equal(log.tick_count, 0);
    assert_int_equal(log.render_count, 1);

    sdl2_loop_destroy(ctx);
}

static void test_update_sub_tick_accumulates(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);

    /* Speed 5: 7.5 ms/tick. Inject 3 ms twice → 6 ms, no tick. */
    int ticks1 = sdl2_loop_update(ctx, 3);
    assert_int_equal(ticks1, 0);
    int ticks2 = sdl2_loop_update(ctx, 3);
    assert_int_equal(ticks2, 0);

    /* Third call with 2 ms → 8 ms total, 1 tick. */
    int ticks3 = sdl2_loop_update(ctx, 2);
    assert_int_equal(ticks3, 1);
    assert_int_equal(log.tick_count, 1);

    sdl2_loop_destroy(ctx);
}

static void test_update_null(void **state)
{
    (void)state;
    int ticks = sdl2_loop_update(NULL, 100);
    assert_int_equal(ticks, 0);
}

/* =========================================================================
 * Group 5: Spiral of death prevention
 * ========================================================================= */

static void test_max_ticks_per_update(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);

    /* Speed 5: 7.5 ms/tick.  Inject 1000 ms → would be 133 ticks,
     * but clamped to SDL2L_MAX_TICKS_PER_UPDATE (10). */
    int ticks = sdl2_loop_update(ctx, 1000);
    assert_int_equal(ticks, SDL2L_MAX_TICKS_PER_UPDATE);
    assert_int_equal(log.tick_count, SDL2L_MAX_TICKS_PER_UPDATE);
    assert_int_equal(log.render_count, 1);
    assert_int_equal(sdl2_loop_total_ticks(ctx), (uint64_t)SDL2L_MAX_TICKS_PER_UPDATE);

    /* Accumulator should be reset — next frame with small delta gives 0 ticks. */
    log.tick_count = 0;
    log.render_count = 0;
    int ticks2 = sdl2_loop_update(ctx, 1);
    assert_int_equal(ticks2, 0);

    sdl2_loop_destroy(ctx);
}

/* =========================================================================
 * Group 6: Pause
 * ========================================================================= */

static void test_pause_no_ticks(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);

    sdl2_loop_set_paused(ctx, true);
    assert_true(sdl2_loop_is_paused(ctx));

    int ticks = sdl2_loop_update(ctx, 100);
    assert_int_equal(ticks, 0);
    assert_int_equal(log.tick_count, 0);
    assert_int_equal(log.render_count, 0);

    sdl2_loop_destroy(ctx);
}

static void test_unpause_clears_accumulator(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);

    /* Accumulate some time, then pause. */
    sdl2_loop_update(ctx, 5); /* 5 ms, sub-tick at speed 5 */
    sdl2_loop_set_paused(ctx, true);

    /* Unpause should clear accumulator — no burst. */
    sdl2_loop_set_paused(ctx, false);
    assert_false(sdl2_loop_is_paused(ctx));

    log.tick_count = 0;
    log.render_count = 0;

    /* 5 ms would have produced a tick if accumulator was preserved. */
    int ticks = sdl2_loop_update(ctx, 5);
    assert_int_equal(ticks, 0);

    sdl2_loop_destroy(ctx);
}

static void test_pause_null(void **state)
{
    (void)state;
    sdl2_loop_set_paused(NULL, true); /* must not crash */
    assert_false(sdl2_loop_is_paused(NULL));
}

static void test_double_pause(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);

    sdl2_loop_set_paused(ctx, true);
    sdl2_loop_set_paused(ctx, true); /* idempotent */
    assert_true(sdl2_loop_is_paused(ctx));

    sdl2_loop_set_paused(ctx, false);
    sdl2_loop_set_paused(ctx, false); /* already unpaused — no-op */
    assert_false(sdl2_loop_is_paused(ctx));

    sdl2_loop_destroy(ctx);
}

/* =========================================================================
 * Group 7: Alpha interpolation
 * ========================================================================= */

static void test_alpha_zero_on_exact_multiple(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);

    /* Speed 1: interval = 13500 us = 13.5 ms.
     * Inject exactly 27 ms → 2 ticks, 0 us leftover.
     * alpha = 0 / 13500 = 0.0 */
    sdl2_loop_set_speed(ctx, 1);
    sdl2_loop_update(ctx, 27);
    assert_float_equal(sdl2_loop_alpha(ctx), 0.0, 0.0001);

    sdl2_loop_destroy(ctx);
}

static void test_alpha_nonzero_on_remainder(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);

    /* Speed 1: interval = 13500 us = 13.5 ms.
     * Inject 14 ms → 1 tick, 500 us leftover.
     * alpha = 500 / 13500 ≈ 0.037 */
    sdl2_loop_set_speed(ctx, 1);
    sdl2_loop_update(ctx, 14);
    assert_true(sdl2_loop_alpha(ctx) > 0.0);
    assert_true(sdl2_loop_alpha(ctx) < 1.0);

    sdl2_loop_destroy(ctx);
}

static void test_alpha_within_range(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);

    /* Various elapsed values — alpha always in [0.0, 1.0). */
    uint64_t test_values[] = {0, 1, 5, 8, 15, 50};
    for (size_t i = 0; i < sizeof(test_values) / sizeof(test_values[0]); i++)
    {
        /* Reset for each test by creating a fresh context. */
        sdl2_loop_destroy(ctx);
        ctx = create_default_loop(&log, &status);
        sdl2_loop_update(ctx, test_values[i]);
        double a = sdl2_loop_alpha(ctx);
        assert_true(a >= 0.0);
        assert_true(a < 1.0);
    }

    sdl2_loop_destroy(ctx);
}

static void test_alpha_passed_to_render(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);

    sdl2_loop_update(ctx, 5);
    /* The alpha passed to render_callback should match the stored alpha. */
    assert_float_equal(log.last_alpha, sdl2_loop_alpha(ctx), 0.0001);

    sdl2_loop_destroy(ctx);
}

static void test_alpha_null(void **state)
{
    (void)state;
    assert_float_equal(sdl2_loop_alpha(NULL), 0.0, 0.001);
}

/* =========================================================================
 * Group 8: Total ticks counter
 * ========================================================================= */

static void test_total_ticks_accumulates(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);

    /* Speed 5: 7.5 ms/tick.  Three frames of 8 ms → 1 tick each. */
    sdl2_loop_update(ctx, 8);
    assert_int_equal(sdl2_loop_total_ticks(ctx), 1);
    sdl2_loop_update(ctx, 8);
    /* Second frame: 8000 + 500 leftover = 8500 us → 1 tick, 1000 leftover. */
    assert_int_equal(sdl2_loop_total_ticks(ctx), 2);
    sdl2_loop_update(ctx, 8);
    /* Third frame: 8000 + 1000 = 9000 us → 1 tick, 1500 leftover. */
    assert_int_equal(sdl2_loop_total_ticks(ctx), 3);

    sdl2_loop_destroy(ctx);
}

static void test_total_ticks_null(void **state)
{
    (void)state;
    assert_int_equal(sdl2_loop_total_ticks(NULL), 0);
}

/* =========================================================================
 * Group 9: Speed change mid-game
 * ========================================================================= */

static void test_speed_change_affects_tick_rate(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);

    /* Speed 1 (slowest): 13.5 ms/tick.  20 ms → 1 tick. */
    sdl2_loop_set_speed(ctx, 1);
    int ticks_slow = sdl2_loop_update(ctx, 20);
    assert_int_equal(ticks_slow, 1);

    /* Reset: create fresh. */
    sdl2_loop_destroy(ctx);
    ctx = create_default_loop(&log, &status);

    /* Speed 9 (fastest): 1.5 ms/tick.  20 ms → 13 ticks (20000/1500 = 13.3). */
    sdl2_loop_set_speed(ctx, 9);
    int ticks_fast = sdl2_loop_update(ctx, 20);
    assert_int_equal(ticks_fast, 10); /* clamped to max 10 */

    sdl2_loop_destroy(ctx);
}

static void test_speed_change_preserves_accumulator(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);

    /* Speed 5: 7.5 ms/tick.  Inject 5 ms (sub-tick). */
    sdl2_loop_update(ctx, 5);
    assert_int_equal(log.tick_count, 0);

    /* Switch to speed 9: 1.5 ms/tick.  Accumulator has 5000 us.
     * Next update with 0 ms → 5000/1500 = 3 ticks. */
    sdl2_loop_set_speed(ctx, 9);
    int ticks = sdl2_loop_update(ctx, 0);
    assert_int_equal(ticks, 3);

    sdl2_loop_destroy(ctx);
}

/* =========================================================================
 * Group 10: Status strings
 * ========================================================================= */

static void test_status_strings(void **state)
{
    (void)state;
    sdl2_loop_status_t codes[] = {
        SDL2L_OK,
        SDL2L_ERR_NULL_ARG,
        SDL2L_ERR_INVALID_SPEED,
        SDL2L_ERR_ALLOC_FAILED,
    };
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
    {
        const char *s = sdl2_loop_status_string(codes[i]);
        assert_non_null(s);
        assert_true(s[0] != '\0');
    }
}

static void test_status_string_unknown(void **state)
{
    (void)state;
    const char *s = sdl2_loop_status_string((sdl2_loop_status_t)99);
    assert_non_null(s);
    assert_string_equal(s, "unknown status");
}

/* =========================================================================
 * Group 11: Warp speed characterization
 * ========================================================================= */

static void test_all_speeds_produce_correct_interval(void **state)
{
    (void)state;
    /* Verify the formula: tick_interval_us = 1500 * (10 - level). */
    for (int level = SDL2L_MIN_SPEED; level <= SDL2L_MAX_SPEED; level++)
    {
        uint64_t expected = (uint64_t)1500 * (uint64_t)(10 - level);
        assert_int_equal(sdl2_loop_tick_interval_us(level), expected);
    }
}

static void test_warp9_tick_rate(void **state)
{
    (void)state;
    callback_log_t log;
    sdl2_loop_status_t status;
    sdl2_loop_t *ctx = create_default_loop(&log, &status);

    /* Warp 9: 1500 us/tick = 1.5 ms/tick.
     * Inject 15 ms → exactly 10 ticks (15000/1500).
     * This is also exactly SDL2L_MAX_TICKS_PER_UPDATE. */
    sdl2_loop_set_speed(ctx, 9);
    int ticks = sdl2_loop_update(ctx, 15);
    assert_int_equal(ticks, 10);

    sdl2_loop_destroy(ctx);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_destroy_null),
        cmocka_unit_test(test_create_null_callbacks),
        cmocka_unit_test(test_create_null_status),
        cmocka_unit_test(test_defaults),
        /* Group 2: Tick interval calculation */
        cmocka_unit_test(test_tick_interval_all_speeds),
        cmocka_unit_test(test_tick_interval_monotonic),
        cmocka_unit_test(test_tick_interval_invalid),
        /* Group 3: Speed control */
        cmocka_unit_test(test_set_speed_valid),
        cmocka_unit_test(test_set_speed_invalid),
        cmocka_unit_test(test_set_speed_null),
        cmocka_unit_test(test_get_speed_null),
        /* Group 4: Update / tick dispatch */
        cmocka_unit_test(test_update_single_tick),
        cmocka_unit_test(test_update_multiple_ticks),
        cmocka_unit_test(test_update_zero_elapsed),
        cmocka_unit_test(test_update_sub_tick_accumulates),
        cmocka_unit_test(test_update_null),
        /* Group 5: Spiral of death */
        cmocka_unit_test(test_max_ticks_per_update),
        /* Group 6: Pause */
        cmocka_unit_test(test_pause_no_ticks),
        cmocka_unit_test(test_unpause_clears_accumulator),
        cmocka_unit_test(test_pause_null),
        cmocka_unit_test(test_double_pause),
        /* Group 7: Alpha interpolation */
        cmocka_unit_test(test_alpha_zero_on_exact_multiple),
        cmocka_unit_test(test_alpha_nonzero_on_remainder),
        cmocka_unit_test(test_alpha_within_range),
        cmocka_unit_test(test_alpha_passed_to_render),
        cmocka_unit_test(test_alpha_null),
        /* Group 8: Total ticks */
        cmocka_unit_test(test_total_ticks_accumulates),
        cmocka_unit_test(test_total_ticks_null),
        /* Group 9: Speed change mid-game */
        cmocka_unit_test(test_speed_change_affects_tick_rate),
        cmocka_unit_test(test_speed_change_preserves_accumulator),
        /* Group 10: Status strings */
        cmocka_unit_test(test_status_strings),
        cmocka_unit_test(test_status_string_unknown),
        /* Group 11: Warp speed characterization */
        cmocka_unit_test(test_all_speeds_produce_correct_interval),
        cmocka_unit_test(test_warp9_tick_rate),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
