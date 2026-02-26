/*
 * test_highscore_system.c — Tests for the pure C high score display sequencer.
 *
 * 6 groups:
 *   1. Lifecycle (3 tests)
 *   2. State flow (3 tests)
 *   3. Score table data (3 tests)
 *   4. Title sparkle (2 tests)
 *   5. Row sparkle and specials (2 tests)
 *   6. Null safety (1 test)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* cmocka must come after setjmp.h / stdarg.h / stddef.h */
#include <cmocka.h>

#include "highscore_system.h"

/* =========================================================================
 * Test helpers
 * ========================================================================= */

static int g_finished;
static highscore_type_t g_finished_type;

static void on_finished(highscore_type_t type, void *user_data)
{
    (void)user_data;
    g_finished = 1;
    g_finished_type = type;
}

static highscore_table_t make_table(void)
{
    highscore_table_t t;
    memset(&t, 0, sizeof(t));
    strncpy(t.master_name, "TestMaster", HIGHSCORE_NAME_LEN - 1);
    strncpy(t.master_text, "Test quote", HIGHSCORE_NAME_LEN - 1);
    for (int i = 0; i < 5; i++)
    {
        t.entries[i].score = (unsigned long)(50000 - i * 5000);
        t.entries[i].level = (unsigned long)(10 - i);
        t.entries[i].game_time = (unsigned long)(3600 + i * 600);
        t.entries[i].timestamp = 1700000000UL;
        snprintf(t.entries[i].name, HIGHSCORE_NAME_LEN, "Player %d", i + 1);
    }
    return t;
}

static highscore_system_t *make_ctx(void)
{
    highscore_system_callbacks_t cb = {0};
    cb.on_finished = on_finished;
    g_finished = 0;
    g_finished_type = HIGHSCORE_TYPE_GLOBAL;
    return highscore_system_create(&cb, NULL);
}

/* =========================================================================
 * Group 1: Lifecycle
 * ========================================================================= */

static void test_create_destroy(void **state)
{
    (void)state;
    highscore_system_t *ctx = make_ctx();
    assert_non_null(ctx);
    assert_int_equal(highscore_system_get_state(ctx), HIGHSCORE_STATE_NONE);
    highscore_system_destroy(ctx);
}

static void test_begin_global(void **state)
{
    (void)state;
    highscore_system_t *ctx = make_ctx();
    highscore_system_begin(ctx, HIGHSCORE_TYPE_GLOBAL, 0);
    assert_int_equal(highscore_system_get_state(ctx), HIGHSCORE_STATE_TITLE);
    assert_int_equal(highscore_system_get_type(ctx), HIGHSCORE_TYPE_GLOBAL);
    assert_int_equal(highscore_system_is_finished(ctx), 0);
    highscore_system_destroy(ctx);
}

static void test_begin_personal(void **state)
{
    (void)state;
    highscore_system_t *ctx = make_ctx();
    highscore_system_begin(ctx, HIGHSCORE_TYPE_PERSONAL, 0);
    assert_int_equal(highscore_system_get_type(ctx), HIGHSCORE_TYPE_PERSONAL);
    highscore_system_destroy(ctx);
}

/* =========================================================================
 * Group 2: State flow
 * ========================================================================= */

static void test_title_to_show(void **state)
{
    (void)state;
    highscore_system_t *ctx = make_ctx();
    highscore_system_begin(ctx, HIGHSCORE_TYPE_GLOBAL, 0);
    highscore_system_update(ctx, 0);  /* TITLE -> WAIT(SHOW, +10) */
    highscore_system_update(ctx, 10); /* WAIT -> SHOW */
    assert_int_equal(highscore_system_get_state(ctx), HIGHSCORE_STATE_SHOW);
    highscore_system_destroy(ctx);
}

static void test_show_to_sparkle(void **state)
{
    (void)state;
    highscore_system_t *ctx = make_ctx();
    highscore_table_t t = make_table();
    highscore_system_set_table(ctx, &t);
    highscore_system_begin(ctx, HIGHSCORE_TYPE_GLOBAL, 0);
    highscore_system_update(ctx, 0);  /* TITLE -> WAIT(SHOW) */
    highscore_system_update(ctx, 10); /* WAIT -> SHOW */
    highscore_system_update(ctx, 11); /* SHOW -> WAIT(SPARKLE, +2) */
    highscore_system_update(ctx, 13); /* WAIT -> SPARKLE */
    assert_int_equal(highscore_system_get_state(ctx), HIGHSCORE_STATE_SPARKLE);
    highscore_system_destroy(ctx);
}

static void test_finishes_at_end(void **state)
{
    (void)state;
    highscore_system_t *ctx = make_ctx();
    highscore_table_t t = make_table();
    highscore_system_set_table(ctx, &t);
    highscore_system_begin(ctx, HIGHSCORE_TYPE_GLOBAL, 0);

    /* Advance to sparkle. */
    highscore_system_update(ctx, 0);
    highscore_system_update(ctx, 10);
    highscore_system_update(ctx, 11);
    highscore_system_update(ctx, 13);

    /* Jump to end (end_frame = 0 + 4000). */
    highscore_system_update(ctx, 4000); /* SPARKLE -> WAIT(FINISH, +1) */
    highscore_system_update(ctx, 4001); /* WAIT -> FINISH */
    highscore_system_update(ctx, 4002); /* do_finish */
    assert_int_equal(highscore_system_is_finished(ctx), 1);
    assert_int_equal(g_finished, 1);

    highscore_sound_t snd = highscore_system_get_sound(ctx);
    assert_non_null(snd.name);
    assert_string_equal(snd.name, "gate");
    highscore_system_destroy(ctx);
}

/* =========================================================================
 * Group 3: Score table data
 * ========================================================================= */

static void test_set_and_get_table(void **state)
{
    (void)state;
    highscore_system_t *ctx = make_ctx();
    highscore_table_t t = make_table();
    highscore_system_set_table(ctx, &t);

    const highscore_table_t *got = highscore_system_get_table(ctx);
    assert_non_null(got);
    assert_string_equal(got->master_name, "TestMaster");
    assert_string_equal(got->master_text, "Test quote");
    assert_int_equal((int)got->entries[0].score, 50000);
    assert_string_equal(got->entries[0].name, "Player 1");
    highscore_system_destroy(ctx);
}

static void test_current_score_highlight(void **state)
{
    (void)state;
    highscore_system_t *ctx = make_ctx();
    highscore_system_set_current_score(ctx, 35000);
    assert_int_equal((int)highscore_system_get_current_score(ctx), 35000);
    highscore_system_destroy(ctx);
}

static void test_empty_entries(void **state)
{
    (void)state;
    highscore_system_t *ctx = make_ctx();
    /* Default table has all zeros. */
    const highscore_table_t *got = highscore_system_get_table(ctx);
    assert_non_null(got);
    assert_int_equal((int)got->entries[0].score, 0);
    highscore_system_destroy(ctx);
}

/* =========================================================================
 * Group 4: Title sparkle
 * ========================================================================= */

static void test_title_sparkle_fires(void **state)
{
    (void)state;
    highscore_system_t *ctx = make_ctx();
    highscore_table_t t = make_table();
    highscore_system_set_table(ctx, &t);
    highscore_system_begin(ctx, HIGHSCORE_TYPE_GLOBAL, 0);

    /* Advance to sparkle state. */
    highscore_system_update(ctx, 0);
    highscore_system_update(ctx, 10);
    highscore_system_update(ctx, 11);
    highscore_system_update(ctx, 13);
    assert_int_equal(highscore_system_get_state(ctx), HIGHSCORE_STATE_SPARKLE);

    /* Frame 30 should trigger title sparkle (delay=30). */
    highscore_system_update(ctx, 30);
    highscore_title_sparkle_t ts;
    highscore_system_get_title_sparkle(ctx, &ts);
    assert_int_equal(ts.active, 1);
    highscore_system_destroy(ctx);
}

static void test_title_sparkle_clears(void **state)
{
    (void)state;
    highscore_system_t *ctx = make_ctx();
    highscore_table_t t = make_table();
    highscore_system_set_table(ctx, &t);
    highscore_system_begin(ctx, HIGHSCORE_TYPE_GLOBAL, 0);

    /* Advance to sparkle. */
    highscore_system_update(ctx, 0);
    highscore_system_update(ctx, 10);
    highscore_system_update(ctx, 11);
    highscore_system_update(ctx, 13);

    /* Fire 11 sparkle frames (at multiples of 30). */
    int clear_seen = 0;
    for (int f = 30; f <= 330; f += 30)
    {
        highscore_system_update(ctx, f);
        highscore_title_sparkle_t ts;
        highscore_system_get_title_sparkle(ctx, &ts);
        if (ts.clear)
        {
            clear_seen = 1;
        }
    }
    assert_int_equal(clear_seen, 1);
    highscore_system_destroy(ctx);
}

/* =========================================================================
 * Group 5: Row sparkle and specials
 * ========================================================================= */

static void test_row_sparkle_active(void **state)
{
    (void)state;
    highscore_system_t *ctx = make_ctx();
    highscore_table_t t = make_table();
    highscore_system_set_table(ctx, &t);
    highscore_system_begin(ctx, HIGHSCORE_TYPE_GLOBAL, 0);

    /* Advance to sparkle. */
    highscore_system_update(ctx, 0);
    highscore_system_update(ctx, 10);
    highscore_system_update(ctx, 11);
    highscore_system_update(ctx, 13);

    highscore_row_sparkle_t rs;
    highscore_system_get_row_sparkle(ctx, &rs);
    assert_int_equal(rs.active, 1);
    assert_int_equal(rs.row, 0);
    highscore_system_destroy(ctx);
}

static void test_specials_at_flash(void **state)
{
    (void)state;
    highscore_system_t *ctx = make_ctx();
    highscore_table_t t = make_table();
    highscore_system_set_table(ctx, &t);
    highscore_system_begin(ctx, HIGHSCORE_TYPE_GLOBAL, 0);

    /* Advance to sparkle. */
    highscore_system_update(ctx, 0);
    highscore_system_update(ctx, 10);
    highscore_system_update(ctx, 11);
    highscore_system_update(ctx, 13);

    /* Frame 30 should trigger specials (FLASH=30). */
    highscore_system_update(ctx, 30);
    assert_int_equal(highscore_system_should_draw_specials(ctx), 1);

    highscore_system_update(ctx, 31);
    assert_int_equal(highscore_system_should_draw_specials(ctx), 0);
    highscore_system_destroy(ctx);
}

/* =========================================================================
 * Group 6: Null safety
 * ========================================================================= */

static void test_null_safety(void **state)
{
    (void)state;
    highscore_system_destroy(NULL);
    highscore_system_begin(NULL, HIGHSCORE_TYPE_GLOBAL, 0);
    assert_int_equal(highscore_system_update(NULL, 0), 0);
    assert_int_equal(highscore_system_get_state(NULL), HIGHSCORE_STATE_NONE);
    assert_int_equal(highscore_system_is_finished(NULL), 1);
    assert_int_equal(highscore_system_should_draw_specials(NULL), 0);
    assert_null(highscore_system_get_table(NULL));
    highscore_system_set_table(NULL, NULL);
    highscore_system_set_current_score(NULL, 0);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_begin_global),
        cmocka_unit_test(test_begin_personal),
        /* Group 2: State flow */
        cmocka_unit_test(test_title_to_show),
        cmocka_unit_test(test_show_to_sparkle),
        cmocka_unit_test(test_finishes_at_end),
        /* Group 3: Score table data */
        cmocka_unit_test(test_set_and_get_table),
        cmocka_unit_test(test_current_score_highlight),
        cmocka_unit_test(test_empty_entries),
        /* Group 4: Title sparkle */
        cmocka_unit_test(test_title_sparkle_fires),
        cmocka_unit_test(test_title_sparkle_clears),
        /* Group 5: Row sparkle and specials */
        cmocka_unit_test(test_row_sparkle_active),
        cmocka_unit_test(test_specials_at_flash),
        /* Group 6: Null safety */
        cmocka_unit_test(test_null_safety),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
