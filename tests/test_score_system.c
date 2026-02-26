/*
 * test_score_system.c — CMocka tests for score_system module.
 *
 * Characterization tests for the pure C score system port.
 * All tests are deterministic — no randomness, no I/O.
 *
 * Test groups:
 *   1. Lifecycle (3 tests)
 *   2. Score operations (5 tests)
 *   3. Multiplier application (3 tests)
 *   4. Extra life tracking (4 tests)
 *   5. Digit layout (5 tests)
 *   6. Callbacks (3 tests)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

/* CMocka must come after setjmp.h */
#include <cmocka.h>

#include "score_system.h"

/* =========================================================================
 * Stub callback state
 * ========================================================================= */

typedef struct
{
    int score_changed_count;
    u_long last_score_changed;

    int extra_life_count;
    u_long last_extra_life_score;
} stub_state_t;

static void reset_stub(stub_state_t *s)
{
    memset(s, 0, sizeof(*s));
}

static void stub_on_score_changed(u_long new_score, void *ud)
{
    stub_state_t *s = ud;
    s->score_changed_count++;
    s->last_score_changed = new_score;
}

static void stub_on_extra_life(u_long score_value, void *ud)
{
    stub_state_t *s = ud;
    s->extra_life_count++;
    s->last_extra_life_score = score_value;
}

/* =========================================================================
 * Helper
 * ========================================================================= */

static score_system_t *create_test_ctx(stub_state_t *s)
{
    reset_stub(s);

    score_system_callbacks_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.on_score_changed = stub_on_score_changed;
    cb.on_extra_life = stub_on_extra_life;

    score_system_status_t status;
    score_system_t *ctx = score_system_create(&cb, s, &status);
    assert_non_null(ctx);
    assert_int_equal(status, SCORE_SYS_OK);
    return ctx;
}

/* =========================================================================
 * Group 1: Lifecycle
 * ========================================================================= */

static void test_create_destroy(void **state)
{
    (void)state;
    score_system_status_t st;
    score_system_t *ctx = score_system_create(NULL, NULL, &st);
    assert_non_null(ctx);
    assert_int_equal(st, SCORE_SYS_OK);
    score_system_destroy(ctx);
}

static void test_create_initial_state(void **state)
{
    (void)state;
    stub_state_t s;
    score_system_t *ctx = create_test_ctx(&s);

    assert_true(score_system_get(ctx) == 0);
    assert_int_equal(score_system_get_life_threshold(ctx), 0);

    score_system_destroy(ctx);
}

static void test_destroy_null_safe(void **state)
{
    (void)state;
    score_system_destroy(NULL); /* Should not crash */
}

/* =========================================================================
 * Group 2: Score operations
 * ========================================================================= */

static void test_set_score(void **state)
{
    (void)state;
    stub_state_t s;
    score_system_t *ctx = create_test_ctx(&s);

    score_system_set(ctx, 50000);
    assert_true(score_system_get(ctx) == 50000);

    score_system_set(ctx, 0);
    assert_true(score_system_get(ctx) == 0);

    score_system_destroy(ctx);
}

static void test_add_score_no_multiplier(void **state)
{
    (void)state;
    stub_state_t s;
    score_system_t *ctx = create_test_ctx(&s);

    score_system_env_t env = {0, 0};
    u_long result = score_system_add(ctx, 100, &env);

    assert_true(result == 100);
    assert_true(score_system_get(ctx) == 100);

    score_system_destroy(ctx);
}

static void test_add_score_accumulates(void **state)
{
    (void)state;
    stub_state_t s;
    score_system_t *ctx = create_test_ctx(&s);

    score_system_env_t env = {0, 0};
    score_system_add(ctx, 100, &env);
    score_system_add(ctx, 200, &env);
    score_system_add(ctx, 300, &env);

    assert_true(score_system_get(ctx) == 600);

    score_system_destroy(ctx);
}

static void test_add_raw_no_multiplier(void **state)
{
    (void)state;
    stub_state_t s;
    score_system_t *ctx = create_test_ctx(&s);

    score_system_set(ctx, 1000);
    u_long result = score_system_add_raw(ctx, 500);

    assert_true(result == 1500);
    assert_true(score_system_get(ctx) == 1500);

    score_system_destroy(ctx);
}

static void test_add_with_null_env(void **state)
{
    (void)state;
    stub_state_t s;
    score_system_t *ctx = create_test_ctx(&s);

    /* NULL env means no multiplier */
    u_long result = score_system_add(ctx, 100, NULL);
    assert_true(result == 100);

    score_system_destroy(ctx);
}

/* =========================================================================
 * Group 3: Multiplier application
 * ========================================================================= */

static void test_x2_multiplier(void **state)
{
    (void)state;
    stub_state_t s;
    score_system_t *ctx = create_test_ctx(&s);

    score_system_env_t env = {1, 0}; /* x2 active */
    score_system_add(ctx, 100, &env);

    assert_true(score_system_get(ctx) == 200);

    score_system_destroy(ctx);
}

static void test_x4_multiplier(void **state)
{
    (void)state;
    stub_state_t s;
    score_system_t *ctx = create_test_ctx(&s);

    score_system_env_t env = {0, 1}; /* x4 active */
    score_system_add(ctx, 100, &env);

    assert_true(score_system_get(ctx) == 400);

    score_system_destroy(ctx);
}

static void test_x2_wins_over_x4(void **state)
{
    (void)state;
    stub_state_t s;
    score_system_t *ctx = create_test_ctx(&s);

    /* Both active: x2 takes precedence (legacy bug preserved) */
    score_system_env_t env = {1, 1};
    score_system_add(ctx, 100, &env);

    assert_true(score_system_get(ctx) == 200);

    score_system_destroy(ctx);
}

/* =========================================================================
 * Group 4: Extra life tracking
 * ========================================================================= */

static void test_extra_life_at_100k(void **state)
{
    (void)state;
    stub_state_t s;
    score_system_t *ctx = create_test_ctx(&s);

    score_system_env_t env = {0, 0};
    score_system_add(ctx, 100000, &env);

    assert_int_equal(s.extra_life_count, 1);
    assert_true(s.last_extra_life_score == 100000);
    assert_int_equal(score_system_get_life_threshold(ctx), 1);

    score_system_destroy(ctx);
}

static void test_no_extra_life_below_threshold(void **state)
{
    (void)state;
    stub_state_t s;
    score_system_t *ctx = create_test_ctx(&s);

    score_system_env_t env = {0, 0};
    score_system_add(ctx, 99999, &env);

    assert_int_equal(s.extra_life_count, 0);
    assert_int_equal(score_system_get_life_threshold(ctx), 0);

    score_system_destroy(ctx);
}

static void test_multiple_extra_lives(void **state)
{
    (void)state;
    stub_state_t s;
    score_system_t *ctx = create_test_ctx(&s);

    score_system_env_t env = {0, 0};

    /* Reach 100k — first extra life */
    score_system_add(ctx, 100000, &env);
    assert_int_equal(s.extra_life_count, 1);

    /* Reach 200k — second extra life */
    score_system_add(ctx, 100000, &env);
    assert_int_equal(s.extra_life_count, 2);

    /* Reach 300k — third extra life */
    score_system_add(ctx, 100000, &env);
    assert_int_equal(s.extra_life_count, 3);

    score_system_destroy(ctx);
}

static void test_set_resets_life_tracking(void **state)
{
    (void)state;
    stub_state_t s;
    score_system_t *ctx = create_test_ctx(&s);

    /* Set to 250k — should set threshold to 2 without awarding lives */
    score_system_set(ctx, 250000);
    assert_int_equal(score_system_get_life_threshold(ctx), 2);
    assert_int_equal(s.extra_life_count, 0); /* set doesn't award lives */

    /* Adding 50k reaches 300k — should award one life (threshold 2→3) */
    score_system_env_t env = {0, 0};
    score_system_add(ctx, 50000, &env);
    assert_int_equal(s.extra_life_count, 1);
    assert_int_equal(score_system_get_life_threshold(ctx), 3);

    score_system_destroy(ctx);
}

/* =========================================================================
 * Group 5: Digit layout
 * ========================================================================= */

static void test_digit_layout_zero(void **state)
{
    (void)state;
    score_system_digit_layout_t layout;
    score_system_get_digit_layout(0, &layout);

    assert_int_equal(layout.count, 1);
    assert_int_equal(layout.digits[0], 0);
    assert_int_equal(layout.x_positions[0], 192); /* 224 - 32 */
    assert_int_equal(layout.y, 0);
}

static void test_digit_layout_single_digit(void **state)
{
    (void)state;
    score_system_digit_layout_t layout;
    score_system_get_digit_layout(7, &layout);

    assert_int_equal(layout.count, 1);
    assert_int_equal(layout.digits[0], 7);
    assert_int_equal(layout.x_positions[0], 192);
}

static void test_digit_layout_multi_digit(void **state)
{
    (void)state;
    score_system_digit_layout_t layout;
    score_system_get_digit_layout(12345, &layout);

    assert_int_equal(layout.count, 5);

    /* Most-significant first */
    assert_int_equal(layout.digits[0], 1);
    assert_int_equal(layout.digits[1], 2);
    assert_int_equal(layout.digits[2], 3);
    assert_int_equal(layout.digits[3], 4);
    assert_int_equal(layout.digits[4], 5);

    /* Right-aligned: rightmost at 192, each stride left */
    assert_int_equal(layout.x_positions[4], 192); /* 5 */
    assert_int_equal(layout.x_positions[3], 160); /* 4 */
    assert_int_equal(layout.x_positions[2], 128); /* 3 */
    assert_int_equal(layout.x_positions[1], 96);  /* 2 */
    assert_int_equal(layout.x_positions[0], 64);  /* 1 */
}

static void test_digit_layout_max_digits(void **state)
{
    (void)state;
    score_system_digit_layout_t layout;
    score_system_get_digit_layout(9999999, &layout);

    assert_int_equal(layout.count, 7);

    /* All nines */
    for (int i = 0; i < 7; i++)
    {
        assert_int_equal(layout.digits[i], 9);
    }

    /* Leftmost digit at x = 224 - 32*7 = 0 */
    assert_int_equal(layout.x_positions[0], 0);
    /* Rightmost at 192 */
    assert_int_equal(layout.x_positions[6], 192);
}

static void test_digit_layout_typical_score(void **state)
{
    (void)state;
    score_system_digit_layout_t layout;
    score_system_get_digit_layout(100, &layout);

    assert_int_equal(layout.count, 3);
    assert_int_equal(layout.digits[0], 1);
    assert_int_equal(layout.digits[1], 0);
    assert_int_equal(layout.digits[2], 0);

    assert_int_equal(layout.x_positions[2], 192); /* Rightmost "0" */
    assert_int_equal(layout.x_positions[1], 160); /* Middle "0" */
    assert_int_equal(layout.x_positions[0], 128); /* Leading "1" */
}

/* =========================================================================
 * Group 6: Callbacks
 * ========================================================================= */

static void test_score_changed_fires_on_add(void **state)
{
    (void)state;
    stub_state_t s;
    score_system_t *ctx = create_test_ctx(&s);

    score_system_env_t env = {0, 0};
    score_system_add(ctx, 100, &env);

    assert_int_equal(s.score_changed_count, 1);
    assert_true(s.last_score_changed == 100);

    score_system_destroy(ctx);
}

static void test_score_changed_fires_on_set(void **state)
{
    (void)state;
    stub_state_t s;
    score_system_t *ctx = create_test_ctx(&s);

    score_system_set(ctx, 50000);

    assert_int_equal(s.score_changed_count, 1);
    assert_true(s.last_score_changed == 50000);

    score_system_destroy(ctx);
}

static void test_no_callbacks_safe(void **state)
{
    (void)state;
    score_system_t *ctx = score_system_create(NULL, NULL, NULL);
    assert_non_null(ctx);

    /* Operations with no callbacks should not crash */
    score_system_set(ctx, 100000);
    score_system_env_t env = {1, 0};
    score_system_add(ctx, 50000, &env);
    score_system_add_raw(ctx, 25000);

    assert_true(score_system_get(ctx) == 225000);

    score_system_destroy(ctx);
}

/* =========================================================================
 * Main — register all test groups
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_create_initial_state),
        cmocka_unit_test(test_destroy_null_safe),

        /* Group 2: Score operations */
        cmocka_unit_test(test_set_score),
        cmocka_unit_test(test_add_score_no_multiplier),
        cmocka_unit_test(test_add_score_accumulates),
        cmocka_unit_test(test_add_raw_no_multiplier),
        cmocka_unit_test(test_add_with_null_env),

        /* Group 3: Multiplier application */
        cmocka_unit_test(test_x2_multiplier),
        cmocka_unit_test(test_x4_multiplier),
        cmocka_unit_test(test_x2_wins_over_x4),

        /* Group 4: Extra life tracking */
        cmocka_unit_test(test_extra_life_at_100k),
        cmocka_unit_test(test_no_extra_life_below_threshold),
        cmocka_unit_test(test_multiple_extra_lives),
        cmocka_unit_test(test_set_resets_life_tracking),

        /* Group 5: Digit layout */
        cmocka_unit_test(test_digit_layout_zero),
        cmocka_unit_test(test_digit_layout_single_digit),
        cmocka_unit_test(test_digit_layout_multi_digit),
        cmocka_unit_test(test_digit_layout_max_digits),
        cmocka_unit_test(test_digit_layout_typical_score),

        /* Group 6: Callbacks */
        cmocka_unit_test(test_score_changed_fires_on_add),
        cmocka_unit_test(test_score_changed_fires_on_set),
        cmocka_unit_test(test_no_callbacks_safe),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
