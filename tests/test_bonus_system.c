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

/* Sound history buffer — captures every name passed to fire_sound
 * during a sequence so tests can assert specific names landed in
 * the right order.  Capacity 256 covers the longest realistic
 * sequence (e.g. BONUS_MAX_COINS=30 coins + 30 bullets + applause
 * + 2 Doh* paths = 63; comfortable margin above any future test
 * scenario).  Overflow is an assert (see on_sound). */
#define SOUND_HISTORY_CAP 256
static char g_sound_history[SOUND_HISTORY_CAP][64];
static int g_sound_history_len;

static int sound_history_contains(const char *name)
{
    for (int i = 0; i < g_sound_history_len; i++)
    {
        if (strcmp(g_sound_history[i], name) == 0)
            return 1;
    }
    return 0;
}

static int sound_history_count(const char *name)
{
    int n = 0;
    for (int i = 0; i < g_sound_history_len; i++)
    {
        if (strcmp(g_sound_history[i], name) == 0)
            n++;
    }
    return n;
}

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
    g_sound_history_len = 0;
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
        /* Loud failure on overflow — silent drops would mask
         * subsequent count assertions and produce confusing
         * "off by N" failures. */
        assert_true(g_sound_history_len < SOUND_HISTORY_CAP);
        strncpy(g_sound_history[g_sound_history_len], name,
                sizeof(g_sound_history[0]) - 1);
        g_sound_history[g_sound_history_len][sizeof(g_sound_history[0]) - 1] = '\0';
        g_sound_history_len++;
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

static void test_set_coins_sets_count(void **state)
{
    (void)state;
    bonus_system_t *ctx = bonus_system_create(NULL, NULL);

    bonus_system_set_coins(ctx, 5);
    assert_int_equal(bonus_system_get_coins(ctx), 5);

    /* Overwrites prior value (the canonical sync semantic). */
    bonus_system_set_coins(ctx, 2);
    assert_int_equal(bonus_system_get_coins(ctx), 2);

    bonus_system_destroy(ctx);
}

static void test_set_coins_negative_is_noop(void **state)
{
    (void)state;
    bonus_system_t *ctx = bonus_system_create(NULL, NULL);

    bonus_system_set_coins(ctx, 3);
    bonus_system_set_coins(ctx, -1);
    assert_int_equal(bonus_system_get_coins(ctx), 3);

    /* NULL ctx is also a no-op (no crash). */
    bonus_system_set_coins(NULL, 1);

    bonus_system_destroy(ctx);
}

static void test_set_coins_then_begin_uses_set_value(void **state)
{
    (void)state;
    reset_tracking();
    bonus_system_callbacks_t cbs = make_callbacks();
    bonus_system_t *ctx = bonus_system_create(&cbs, NULL);

    /* Set via setter (the canonical production path), then begin. */
    bonus_system_set_coins(ctx, 4);
    bonus_system_env_t env = make_env(10000, 3, 1, 20, 5, 0);
    bonus_system_begin(ctx, &env, 0);

    /* Storage: initial_count captures the set value. */
    assert_int_equal(bonus_system_get_initial_coins(ctx), 4);

    /* Computation: total uses the set value, not 0.
     * Per ADR-040, this is the failure mode that goes silently
     * wrong if the setter is called AFTER begin. */
    unsigned long expected =
        bonus_system_compute_total(4, env.level, env.starting_level, env.time_bonus_secs,
                                   env.bullet_count);
    assert_int_equal(g_score_added, expected);

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
    int found_supbons = 0;
    while (!bonus_system_is_finished(ctx) && safety < 2000)
    {
        frame++;
        bonus_system_update(ctx, frame);
        if (strcmp(g_last_sound, "supbons") == 0)
        {
            found_supbons = 1;
        }
        safety++;
    }

    assert_int_equal(found_supbons, 1);
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
 * Group 7: Renderer-facing accessors (basket 5, xboing-c-tp4)
 * ========================================================================= */

static void test_initial_counts_captured_at_begin(void **state)
{
    (void)state;
    bonus_system_t *ctx = bonus_system_create(NULL, NULL);

    /* Set up: 5 coins collected, 12 bullets remaining */
    for (int i = 0; i < 5; i++)
        bonus_system_inc_coins(ctx);

    bonus_system_env_t env = make_env(0, 1, 1, 30, 12, 0);
    bonus_system_begin(ctx, &env, 0);

    assert_int_equal(bonus_system_get_initial_coins(ctx), 5);
    assert_int_equal(bonus_system_get_initial_bullets(ctx), 12);
    bonus_system_destroy(ctx);
}

static void test_initial_counts_freeze_during_drain(void **state)
{
    (void)state;
    /* Initial counts must NOT change as the live counts decrement —
     * the renderer relies on (initial - live) staying meaningful. */
    bonus_system_t *ctx = bonus_system_create(NULL, NULL);
    for (int i = 0; i < 3; i++)
        bonus_system_inc_coins(ctx);

    bonus_system_env_t env = make_env(0, 1, 1, 30, 4, 0);
    bonus_system_begin(ctx, &env, 0);

    /* Decrement live coins; initial should remain 3. */
    bonus_system_dec_coins(ctx);
    bonus_system_dec_coins(ctx);
    assert_int_equal(bonus_system_get_coins(ctx), 1);
    assert_int_equal(bonus_system_get_initial_coins(ctx), 3);
    bonus_system_destroy(ctx);
}

static void test_initial_counts_refresh_on_second_begin(void **state)
{
    (void)state;
    /* Calling begin() twice with different envs must update the captured
     * initial counts — stale values across levels would mislead the
     * renderer. */
    bonus_system_t *ctx = bonus_system_create(NULL, NULL);

    bonus_system_inc_coins(ctx);
    bonus_system_inc_coins(ctx);
    bonus_system_env_t env1 = make_env(0, 1, 1, 30, 7, 0);
    bonus_system_begin(ctx, &env1, 0);
    assert_int_equal(bonus_system_get_initial_coins(ctx), 2);
    assert_int_equal(bonus_system_get_initial_bullets(ctx), 7);

    /* Second sequence — coin count drained to 0 by first sequence */
    bonus_system_reset_coins(ctx);
    for (int i = 0; i < 6; i++)
        bonus_system_inc_coins(ctx);
    bonus_system_env_t env2 = make_env(0, 2, 1, 0, 15, 0);
    bonus_system_begin(ctx, &env2, 0);
    assert_int_equal(bonus_system_get_initial_coins(ctx), 6);
    assert_int_equal(bonus_system_get_initial_bullets(ctx), 15);
    bonus_system_destroy(ctx);
}

static void test_get_bullets_returns_live_env_value(void **state)
{
    (void)state;
    /* get_bullets reads the live env field, which decrements through
     * BONUS_STATE_BULLET as bullets are consumed.  Initially equals
     * the initial value. */
    bonus_system_t *ctx = bonus_system_create(NULL, NULL);
    bonus_system_env_t env = make_env(0, 1, 1, 30, 8, 0);
    bonus_system_begin(ctx, &env, 0);
    assert_int_equal(bonus_system_get_bullets(ctx), 8);
    assert_int_equal(bonus_system_get_initial_bullets(ctx), 8);
    bonus_system_destroy(ctx);
}

static void test_get_time_bonus_secs_returns_env_value(void **state)
{
    (void)state;
    bonus_system_t *ctx = bonus_system_create(NULL, NULL);

    bonus_system_env_t env_void = make_env(0, 1, 1, 0, 5, 0);
    bonus_system_begin(ctx, &env_void, 0);
    assert_int_equal(bonus_system_get_time_bonus_secs(ctx), 0);

    bonus_system_env_t env_full = make_env(0, 2, 1, 47, 5, 0);
    bonus_system_begin(ctx, &env_full, 0);
    assert_int_equal(bonus_system_get_time_bonus_secs(ctx), 47);
    bonus_system_destroy(ctx);
}

static void test_renderer_accessors_null_safe(void **state)
{
    (void)state;
    assert_int_equal(bonus_system_get_initial_coins(NULL), 0);
    assert_int_equal(bonus_system_get_initial_bullets(NULL), 0);
    assert_int_equal(bonus_system_get_bullets(NULL), 0);
    assert_int_equal(bonus_system_get_time_bonus_secs(NULL), 0);
}

/* =========================================================================
 * Group 7: Sound mapping (original/bonus.c playSoundFile parity)
 *
 * Each test drives the state machine to a specific branch and
 * asserts the right sound fires.  Cited line numbers refer to
 * original/bonus.c playSoundFile calls.
 * ========================================================================= */

/* Helper: run a full bonus sequence to completion.  Asserts the
 * loop exited because the sequence finished, not because the
 * safety cap was hit — a regression that stalls a state (e.g.
 * WAIT that never expires) would otherwise let sound/pacing
 * tests pass with partial execution. */
static void drive_sequence(bonus_system_t *ctx)
{
    int frame = 0;
    int safety = 0;
    while (!bonus_system_is_finished(ctx) && safety < 5000)
    {
        frame++;
        bonus_system_update(ctx, frame);
        safety++;
    }
    assert_true(bonus_system_is_finished(ctx));
}

/* "bonus" sound fires once per coin animated (bonus.c:366). */
static void test_sound_per_coin_bonus(void **state)
{
    (void)state;
    reset_tracking();
    bonus_system_callbacks_t cbs = make_callbacks();
    bonus_system_t *ctx = bonus_system_create(&cbs, NULL);

    bonus_system_inc_coins(ctx);
    bonus_system_inc_coins(ctx);
    bonus_system_inc_coins(ctx);
    bonus_system_env_t env = make_env(0, 1, 1, 30, 0, 0);
    bonus_system_begin(ctx, &env, 0);
    drive_sequence(ctx);

    assert_int_equal(sound_history_count("bonus"), 3);
    bonus_system_destroy(ctx);
}

/* "Doh1" fires when no coins were collected (bonus.c:315). */
static void test_sound_no_coins_doh1(void **state)
{
    (void)state;
    reset_tracking();
    bonus_system_callbacks_t cbs = make_callbacks();
    bonus_system_t *ctx = bonus_system_create(&cbs, NULL);

    bonus_system_env_t env = make_env(0, 1, 1, 30, 0, 0);
    bonus_system_begin(ctx, &env, 0);
    drive_sequence(ctx);

    assert_true(sound_history_contains("Doh1"));
    assert_false(sound_history_contains("wzzz"));
    bonus_system_destroy(ctx);
}

/* "Doh4" fires in do_bonuses when timer ran out (bonus.c:292). */
static void test_sound_timer_void_doh4(void **state)
{
    (void)state;
    reset_tracking();
    bonus_system_callbacks_t cbs = make_callbacks();
    bonus_system_t *ctx = bonus_system_create(&cbs, NULL);

    bonus_system_inc_coins(ctx);
    bonus_system_inc_coins(ctx);
    bonus_system_env_t env = make_env(0, 1, 1, 0, 0, 0);
    bonus_system_begin(ctx, &env, 0);
    drive_sequence(ctx);

    /* Timer ran out path plays "Doh4" twice — once in do_bonuses
     * (coins voided) and once in do_time_bonus (no time bonus). */
    assert_int_equal(sound_history_count("Doh4"), 2);
    bonus_system_destroy(ctx);
}

/* "Doh2" fires in do_level when timer ran out (bonus.c:421). */
static void test_sound_no_level_bonus_doh2(void **state)
{
    (void)state;
    reset_tracking();
    bonus_system_callbacks_t cbs = make_callbacks();
    bonus_system_t *ctx = bonus_system_create(&cbs, NULL);

    bonus_system_env_t env = make_env(0, 5, 1, 0, 3, 0);
    bonus_system_begin(ctx, &env, 0);
    drive_sequence(ctx);

    assert_true(sound_history_contains("Doh2"));
    bonus_system_destroy(ctx);
}

/* "Doh3" fires in do_bullets when none remain (bonus.c:450). */
static void test_sound_no_bullets_doh3(void **state)
{
    (void)state;
    reset_tracking();
    bonus_system_callbacks_t cbs = make_callbacks();
    bonus_system_t *ctx = bonus_system_create(&cbs, NULL);

    bonus_system_env_t env = make_env(0, 1, 1, 30, 0, 0);
    bonus_system_begin(ctx, &env, 0);
    drive_sequence(ctx);

    assert_true(sound_history_contains("Doh3"));
    assert_false(sound_history_contains("wzzz"));
    bonus_system_destroy(ctx);
}

/* "key" fires once per bullet animated (bonus.c:473). */
static void test_sound_per_bullet_key(void **state)
{
    (void)state;
    reset_tracking();
    bonus_system_callbacks_t cbs = make_callbacks();
    bonus_system_t *ctx = bonus_system_create(&cbs, NULL);

    bonus_system_env_t env = make_env(0, 1, 1, 30, 5, 0);
    bonus_system_begin(ctx, &env, 0);
    drive_sequence(ctx);

    assert_int_equal(sound_history_count("key"), 5);
    bonus_system_destroy(ctx);
}

/* "applause" fires once on entering end-text (bonus.c:600). */
static void test_sound_end_text_applause(void **state)
{
    (void)state;
    reset_tracking();
    bonus_system_callbacks_t cbs = make_callbacks();
    bonus_system_t *ctx = bonus_system_create(&cbs, NULL);

    bonus_system_inc_coins(ctx);
    bonus_system_env_t env = make_env(0, 1, 1, 30, 1, 0);
    bonus_system_begin(ctx, &env, 0);
    drive_sequence(ctx);

    assert_int_equal(sound_history_count("applause"), 1);
    bonus_system_destroy(ctx);
}

/* Per-coin pacing: the BONUS_STEP_DELAY wait between coin drops
 * is exactly observed — no drop during the wait window, exactly
 * one drop when the wait expires.  This test fails loudly if
 * anyone sets BONUS_STEP_DELAY to 0 or removes the
 * self-rearming set_bonus_wait call. */
static void test_per_coin_pacing(void **state)
{
    (void)state;
    reset_tracking();
    bonus_system_callbacks_t cbs = make_callbacks();
    bonus_system_t *ctx = bonus_system_create(&cbs, NULL);

    bonus_system_inc_coins(ctx);
    bonus_system_inc_coins(ctx);
    bonus_system_inc_coins(ctx);
    bonus_system_env_t env = make_env(0, 1, 1, 30, 0, 0);
    bonus_system_begin(ctx, &env, 0);

    /* Advance past TEXT and SCORE waits until the BONUS body
     * fires its first coin.  After the first drop the state is
     * WAIT (the self-rearming wait scheduled BONUS_STEP_DELAY
     * sub-frames later). */
    int frame = 0;
    int coins_at_bonus_entry = -1;
    while (frame < 500)
    {
        frame++;
        bonus_system_update(ctx, frame);
        if (bonus_system_get_highest_reached(ctx) >= BONUS_STATE_BONUS &&
            bonus_system_get_coins(ctx) < 3)
        {
            coins_at_bonus_entry = bonus_system_get_coins(ctx);
            break;
        }
    }
    assert_int_equal(coins_at_bonus_entry, 2); /* one of the three coins dropped */

    /* During the BONUS_STEP_DELAY wait window, no further coin
     * may drop.  Advance up to (but not including) the expiry
     * tick. */
    for (int i = 0; i < BONUS_STEP_DELAY - 1; i++)
    {
        frame++;
        bonus_system_update(ctx, frame);
        assert_int_equal(bonus_system_get_coins(ctx), coins_at_bonus_entry);
    }

    /* The next sub-frame expires the wait (transitions WAIT →
     * BONUS without entering the body). */
    frame++;
    bonus_system_update(ctx, frame);
    assert_int_equal(bonus_system_get_coins(ctx), coins_at_bonus_entry);

    /* The tick after that runs do_bonuses once and drops exactly
     * one coin. */
    frame++;
    bonus_system_update(ctx, frame);
    assert_int_equal(bonus_system_get_coins(ctx), coins_at_bonus_entry - 1);

    bonus_system_destroy(ctx);
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
        cmocka_unit_test(test_set_coins_sets_count),
        cmocka_unit_test(test_set_coins_negative_is_noop),
        cmocka_unit_test(test_set_coins_then_begin_uses_set_value),

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

        /* Group 7: Renderer-facing accessors (basket 5) */
        cmocka_unit_test(test_initial_counts_captured_at_begin),
        cmocka_unit_test(test_initial_counts_freeze_during_drain),
        cmocka_unit_test(test_initial_counts_refresh_on_second_begin),
        cmocka_unit_test(test_get_bullets_returns_live_env_value),
        cmocka_unit_test(test_get_time_bonus_secs_returns_env_value),
        cmocka_unit_test(test_renderer_accessors_null_safe),

        /* Group 7: Sound mapping (original/bonus.c parity) */
        cmocka_unit_test(test_sound_per_coin_bonus),
        cmocka_unit_test(test_sound_no_coins_doh1),
        cmocka_unit_test(test_sound_timer_void_doh4),
        cmocka_unit_test(test_sound_no_level_bonus_doh2),
        cmocka_unit_test(test_sound_no_bullets_doh3),
        cmocka_unit_test(test_sound_per_bullet_key),
        cmocka_unit_test(test_sound_end_text_applause),
        cmocka_unit_test(test_per_coin_pacing),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
