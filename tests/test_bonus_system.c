/*
 * test_bonus_system.c — Tests for the pure C bonus tally sequence system.
 *
 * 6 groups:
 *   1. Lifecycle (3 tests)
 *   2. Score computation (5 tests)
 *   3. Coin management (3 tests)
 *   4. Save trigger (3 tests)
 *   5. State machine transitions (6 tests)
 *   6. Skip and queries (3 tests)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

/* cmocka must come after setjmp.h / stdarg.h / stddef.h */
#include <cmocka.h>

#include "bonus_system.h"

/* =========================================================================
 * Test helpers
 * ========================================================================= */

/* Callback tracking */
static unsigned long g_score_added;
static int g_score_add_count;
static int g_bullets_consumed;
static int g_save_triggered;
static int g_finished;
static int g_finished_level;
static int g_sound_count;
static char g_last_sound[64];

static void reset_tracking(void)
{
    g_score_added = 0;
    g_score_add_count = 0;
    g_bullets_consumed = 0;
    g_save_triggered = 0;
    g_finished = 0;
    g_finished_level = 0;
    g_sound_count = 0;
    g_last_sound[0] = '\0';
}

static void on_score_add(unsigned long pts, void *ud)
{
    (void)ud;
    g_score_added += pts;
    g_score_add_count++;
}

static void on_bullet_consumed(void *ud)
{
    (void)ud;
    g_bullets_consumed++;
}

static void on_save_triggered(void *ud)
{
    (void)ud;
    g_save_triggered = 1;
}

static void on_sound(const char *name, void *ud)
{
    (void)ud;
    g_sound_count++;
    if (name)
    {
        strncpy(g_last_sound, name, sizeof(g_last_sound) - 1);
        g_last_sound[sizeof(g_last_sound) - 1] = '\0';
    }
}

static void on_finished(int next_level, void *ud)
{
    (void)ud;
    g_finished = 1;
    g_finished_level = next_level;
}

static bonus_system_callbacks_t make_callbacks(void)
{
    bonus_system_callbacks_t cbs;
    cbs.on_score_add = on_score_add;
    cbs.on_bullet_consumed = on_bullet_consumed;
    cbs.on_save_triggered = on_save_triggered;
    cbs.on_sound = on_sound;
    cbs.on_finished = on_finished;
    return cbs;
}

static bonus_system_env_t make_env(unsigned long score, int level, int starting_level,
                                   int time_secs, int bullets, int rank)
{
    bonus_system_env_t env;
    env.score = score;
    env.level = level;
    env.starting_level = starting_level;
    env.time_bonus_secs = time_secs;
    env.bullet_count = bullets;
    env.highscore_rank = rank;
    return env;
}

/* =========================================================================
 * Group 1: Lifecycle
 * ========================================================================= */

static void test_create_destroy(void **state)
{
    (void)state;
    bonus_system_t *ctx = bonus_system_create(NULL, NULL);
    assert_non_null(ctx);
    bonus_system_destroy(ctx);
}

static void test_create_with_callbacks(void **state)
{
    (void)state;
    bonus_system_callbacks_t cbs = make_callbacks();
    bonus_system_t *ctx = bonus_system_create(&cbs, NULL);
    assert_non_null(ctx);
    bonus_system_destroy(ctx);
}

static void test_initial_state(void **state)
{
    (void)state;
    bonus_system_t *ctx = bonus_system_create(NULL, NULL);

    assert_int_equal(bonus_system_get_state(ctx), BONUS_STATE_TEXT);
    assert_int_equal(bonus_system_get_coins(ctx), 0);
    assert_int_equal(bonus_system_is_finished(ctx), 0);
    assert_int_equal(bonus_system_get_display_score(ctx), 0);

    bonus_system_destroy(ctx);
}

/* =========================================================================
 * Group 2: Score computation (stateless)
 * ========================================================================= */

/* Test: full bonus with coins, level, bullets, time */
static void test_compute_full_bonus(void **state)
{
    (void)state;
    /* 3 coins * 3000 + level_adj(5-1+1=5) * 100 + 4 bullets * 500 + 30 secs * 100 */
    unsigned long total = bonus_system_compute_total(3, 5, 1, 30, 4);
    unsigned long expected = 3 * 3000 + 5 * 100 + 4 * 500 + 30 * 100;
    assert_int_equal(total, expected); /* 9000 + 500 + 2000 + 3000 = 14500 */
}

/* Test: super bonus when coins > MAX_BONUS */
static void test_compute_super_bonus(void **state)
{
    (void)state;
    /* 10 coins (> 8) → 50000 + level_adj(1)*100 + 0 bullets + 10 secs*100 */
    unsigned long total = bonus_system_compute_total(10, 1, 1, 10, 0);
    unsigned long expected = 50000 + 1 * 100 + 10 * 100;
    assert_int_equal(total, expected); /* 50000 + 100 + 1000 = 51100 */
}

/* Test: timer expired — no coins, no level, no time; only bullets */
static void test_compute_timer_expired(void **state)
{
    (void)state;
    /* time=0 → no coins, no level bonus, no time bonus; bullets still count */
    unsigned long total = bonus_system_compute_total(5, 3, 1, 0, 2);
    unsigned long expected = 2 * 500;
    assert_int_equal(total, expected); /* 1000 */
}

/* Test: zero everything */
static void test_compute_zero(void **state)
{
    (void)state;
    unsigned long total = bonus_system_compute_total(0, 1, 1, 0, 0);
    assert_int_equal(total, 0);
}

/* Test: coins at exactly MAX_BONUS threshold (not super) */
static void test_compute_at_max_coins(void **state)
{
    (void)state;
    /* Exactly 8 coins → per-coin (not super), with time > 0 */
    unsigned long total = bonus_system_compute_total(8, 1, 1, 1, 0);
    unsigned long expected = 8 * 3000 + 1 * 100 + 1 * 100;
    assert_int_equal(total, expected); /* 24000 + 100 + 100 = 24200 */
}

/* =========================================================================
 * Group 3: Coin management
 * ========================================================================= */

static void test_inc_dec_coins(void **state)
{
    (void)state;
    bonus_system_t *ctx = bonus_system_create(NULL, NULL);

    bonus_system_inc_coins(ctx);
    bonus_system_inc_coins(ctx);
    bonus_system_inc_coins(ctx);
    assert_int_equal(bonus_system_get_coins(ctx), 3);

    bonus_system_dec_coins(ctx);
    assert_int_equal(bonus_system_get_coins(ctx), 2);

    bonus_system_destroy(ctx);
}

static void test_dec_coins_floor(void **state)
{
    (void)state;
    bonus_system_t *ctx = bonus_system_create(NULL, NULL);

    /* Decrement below 0 should stay at 0 */
    bonus_system_dec_coins(ctx);
    assert_int_equal(bonus_system_get_coins(ctx), 0);

    bonus_system_destroy(ctx);
}

static void test_reset_coins(void **state)
{
    (void)state;
    bonus_system_t *ctx = bonus_system_create(NULL, NULL);

    bonus_system_inc_coins(ctx);
    bonus_system_inc_coins(ctx);
    bonus_system_reset_coins(ctx);
    assert_int_equal(bonus_system_get_coins(ctx), 0);

    bonus_system_destroy(ctx);
}

/* =========================================================================
 * Group 4: Save trigger
 * ========================================================================= */

static void test_save_trigger_at_5(void **state)
{
    (void)state;
    /* Starting at level 1: save at level 5 (5-1+1=5, 5%5==0) */
    assert_int_equal(bonus_system_should_save(5, 1), 1);
}

static void test_save_trigger_not_at_3(void **state)
{
    (void)state;
    /* Starting at level 1: level 3 → 3-1+1=3, 3%5!=0 */
    assert_int_equal(bonus_system_should_save(3, 1), 0);
}

static void test_save_trigger_offset_start(void **state)
{
    (void)state;
    /* Starting at level 6: save at level 10 (10-6+1=5, 5%5==0) */
    assert_int_equal(bonus_system_should_save(10, 6), 1);
    /* Level 9: 9-6+1=4, 4%5!=0 */
    assert_int_equal(bonus_system_should_save(9, 6), 0);
}

/* =========================================================================
 * Group 5: State machine transitions
 * ========================================================================= */

/* Test: begin fires score add callback with correct total */
static void test_begin_fires_score(void **state)
{
    (void)state;
    reset_tracking();
    bonus_system_callbacks_t cbs = make_callbacks();
    bonus_system_t *ctx = bonus_system_create(&cbs, NULL);

    /* 2 coins, level 3 from start 1, 20 secs, 5 bullets */
    bonus_system_inc_coins(ctx);
    bonus_system_inc_coins(ctx);

    bonus_system_env_t env = make_env(10000, 3, 1, 20, 5, 0);
    bonus_system_begin(ctx, &env, 0);

    unsigned long expected = 2 * 3000 + 3 * 100 + 5 * 500 + 20 * 100;
    assert_int_equal(g_score_added, expected); /* 6000+300+2500+2000=10800 */
    assert_int_equal(g_score_add_count, 1);

    bonus_system_destroy(ctx);
}

/* Test: begin fires save trigger at correct level */
static void test_begin_fires_save(void **state)
{
    (void)state;
    reset_tracking();
    bonus_system_callbacks_t cbs = make_callbacks();
    bonus_system_t *ctx = bonus_system_create(&cbs, NULL);

    /* Level 5 from start 1 → 5-1+1=5, triggers save */
    bonus_system_env_t env = make_env(0, 5, 1, 10, 0, 0);
    bonus_system_begin(ctx, &env, 0);
    assert_int_equal(g_save_triggered, 1);

    bonus_system_destroy(ctx);
}

/* Test: full sequence runs to completion */
static void test_full_sequence(void **state)
{
    (void)state;
    reset_tracking();
    bonus_system_callbacks_t cbs = make_callbacks();
    bonus_system_t *ctx = bonus_system_create(&cbs, NULL);

    /* 1 coin, level 1, 10 secs, 1 bullet, no rank */
    bonus_system_inc_coins(ctx);
    bonus_system_env_t env = make_env(5000, 1, 1, 10, 1, 0);
    bonus_system_begin(ctx, &env, 0);

    /* Drive the state machine forward until finished */
    int frame = 0;
    int safety = 0;
    while (!bonus_system_is_finished(ctx) && safety < 2000)
    {
        frame++;
        bonus_system_update(ctx, frame);
        safety++;
    }

    /* Should have reached FINISH and fired on_finished */
    assert_int_equal(g_finished, 1);
    assert_int_equal(g_finished_level, 2); /* level 1 + 1 */

    /* Should have consumed 1 bullet */
    assert_int_equal(g_bullets_consumed, 1);

    bonus_system_destroy(ctx);
}

/* Test: timer expired skips coin/level/time bonuses */
static void test_sequence_timer_expired(void **state)
{
    (void)state;
    reset_tracking();
    bonus_system_callbacks_t cbs = make_callbacks();
    bonus_system_t *ctx = bonus_system_create(&cbs, NULL);

    /* 3 coins but timer=0 → only bullet bonus counts */
    bonus_system_inc_coins(ctx);
    bonus_system_inc_coins(ctx);
    bonus_system_inc_coins(ctx);
    bonus_system_env_t env = make_env(0, 1, 1, 0, 2, 0);
    bonus_system_begin(ctx, &env, 0);

    /* Only bullets * 500 should be added */
    assert_int_equal(g_score_added, 2 * 500);

    /* Drive to completion */
    int frame = 0;
    int safety = 0;
    while (!bonus_system_is_finished(ctx) && safety < 2000)
    {
        frame++;
        bonus_system_update(ctx, frame);
        safety++;
    }

    assert_int_equal(g_finished, 1);
    assert_int_equal(g_bullets_consumed, 2);

    bonus_system_destroy(ctx);
}

/* Test: no coins, no bullets, timer > 0 → only level + time bonus */
static void test_sequence_no_coins_no_bullets(void **state)
{
    (void)state;
    reset_tracking();
    bonus_system_callbacks_t cbs = make_callbacks();
    bonus_system_t *ctx = bonus_system_create(&cbs, NULL);

    bonus_system_env_t env = make_env(0, 2, 1, 5, 0, 3);
    bonus_system_begin(ctx, &env, 0);

    /* level bonus: (2-1+1)*100 = 200, time: 5*100 = 500, total = 700 */
    assert_int_equal(g_score_added, 700);

    /* Drive to completion */
    int frame = 0;
    int safety = 0;
    while (!bonus_system_is_finished(ctx) && safety < 2000)
    {
        frame++;
        bonus_system_update(ctx, frame);
        safety++;
    }

    assert_int_equal(g_finished, 1);
    assert_int_equal(g_bullets_consumed, 0);

    bonus_system_destroy(ctx);
}

/* Test: super bonus when coins > MAX_BONUS */
static void test_sequence_super_bonus(void **state)
{
    (void)state;
    reset_tracking();
    bonus_system_callbacks_t cbs = make_callbacks();
    bonus_system_t *ctx = bonus_system_create(&cbs, NULL);

    /* 10 coins (> 8) → super bonus */
    for (int i = 0; i < 10; i++)
    {
        bonus_system_inc_coins(ctx);
    }
    bonus_system_env_t env = make_env(0, 1, 1, 10, 0, 0);
    bonus_system_begin(ctx, &env, 0);

    /* 50000 + 1*100 + 10*100 = 51100 */
    assert_int_equal(g_score_added, 51100);

    /* Drive to completion — super bonus should play "supbons" sound */
    int frame = 0;
    int safety = 0;
    int found_supbns = 0;
    while (!bonus_system_is_finished(ctx) && safety < 2000)
    {
        frame++;
        bonus_system_update(ctx, frame);
        if (strcmp(g_last_sound, "supbons") == 0)
        {
            found_supbns = 1;
        }
        safety++;
    }

    assert_int_equal(found_supbns, 1);
    assert_int_equal(g_finished, 1);

    bonus_system_destroy(ctx);
}

/* =========================================================================
 * Group 6: Skip and queries
 * ========================================================================= */

/* Test: skip jumps directly to finish */
static void test_skip_to_finish(void **state)
{
    (void)state;
    reset_tracking();
    bonus_system_callbacks_t cbs = make_callbacks();
    bonus_system_t *ctx = bonus_system_create(&cbs, NULL);

    bonus_system_env_t env = make_env(0, 1, 1, 10, 0, 0);
    bonus_system_begin(ctx, &env, 0);

    /* Skip immediately */
    int frame = 1;
    bonus_system_skip(ctx, frame);

    /* Next update should transition through WAIT to FINISH */
    frame++;
    bonus_system_update(ctx, frame);
    /* May need one more update to process FINISH */
    if (bonus_system_get_state(ctx) == BONUS_STATE_FINISH)
    {
        bonus_system_update(ctx, frame);
    }

    assert_int_equal(g_finished, 1);
    assert_int_equal(g_finished_level, 2);

    bonus_system_destroy(ctx);
}

/* Test: display score tracks running total */
static void test_display_score(void **state)
{
    (void)state;
    bonus_system_callbacks_t cbs = make_callbacks();
    bonus_system_t *ctx = bonus_system_create(&cbs, NULL);

    bonus_system_env_t env = make_env(5000, 1, 1, 0, 0, 0);
    bonus_system_begin(ctx, &env, 0);

    /* Display score starts at the score value passed in env */
    assert_int_equal(bonus_system_get_display_score(ctx), 5000);

    bonus_system_destroy(ctx);
}

/* Test: null context safety */
static void test_null_safety(void **state)
{
    (void)state;
    /* None of these should crash */
    bonus_system_destroy(NULL);
    bonus_system_begin(NULL, NULL, 0);
    bonus_system_update(NULL, 0);
    bonus_system_skip(NULL, 0);
    bonus_system_inc_coins(NULL);
    bonus_system_dec_coins(NULL);
    bonus_system_reset_coins(NULL);

    assert_int_equal(bonus_system_get_state(NULL), BONUS_STATE_TEXT);
    assert_int_equal(bonus_system_get_coins(NULL), 0);
    assert_int_equal(bonus_system_is_finished(NULL), 0);
    assert_int_equal(bonus_system_get_display_score(NULL), 0);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_create_with_callbacks),
        cmocka_unit_test(test_initial_state),

        /* Group 2: Score computation */
        cmocka_unit_test(test_compute_full_bonus),
        cmocka_unit_test(test_compute_super_bonus),
        cmocka_unit_test(test_compute_timer_expired),
        cmocka_unit_test(test_compute_zero),
        cmocka_unit_test(test_compute_at_max_coins),

        /* Group 3: Coin management */
        cmocka_unit_test(test_inc_dec_coins),
        cmocka_unit_test(test_dec_coins_floor),
        cmocka_unit_test(test_reset_coins),

        /* Group 4: Save trigger */
        cmocka_unit_test(test_save_trigger_at_5),
        cmocka_unit_test(test_save_trigger_not_at_3),
        cmocka_unit_test(test_save_trigger_offset_start),

        /* Group 5: State machine transitions */
        cmocka_unit_test(test_begin_fires_score),
        cmocka_unit_test(test_begin_fires_save),
        cmocka_unit_test(test_full_sequence),
        cmocka_unit_test(test_sequence_timer_expired),
        cmocka_unit_test(test_sequence_no_coins_no_bullets),
        cmocka_unit_test(test_sequence_super_bonus),

        /* Group 6: Skip and queries */
        cmocka_unit_test(test_skip_to_finish),
        cmocka_unit_test(test_display_score),
        cmocka_unit_test(test_null_safety),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
