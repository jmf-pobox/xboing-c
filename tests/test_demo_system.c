/*
 * test_demo_system.c — Tests for the pure C demo/preview sequencer.
 *
 * 6 groups:
 *   1. Lifecycle (3 tests)
 *   2. Demo state flow (3 tests)
 *   3. Preview state flow (3 tests)
 *   4. Data tables (3 tests)
 *   5. Sparkle and specials (2 tests)
 *   6. Null safety (1 test)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

/* cmocka must come after setjmp.h / stdarg.h / stddef.h */
#include <cmocka.h>

#include "demo_system.h"

/* =========================================================================
 * Test helpers
 * ========================================================================= */

static int g_finished_demo;
static int g_finished_preview;
static int g_loaded_level;

static void on_finished(demo_screen_mode_t mode, void *user_data)
{
    (void)user_data;
    if (mode == DEMO_MODE_DEMO)
    {
        g_finished_demo = 1;
    }
    else
    {
        g_finished_preview = 1;
    }
}

static void on_load_level(int level_num, void *user_data)
{
    (void)user_data;
    g_loaded_level = level_num;
}

static int g_rand_val;

static int test_rand(void *user_data)
{
    (void)user_data;
    return g_rand_val++;
}

static demo_system_t *make_ctx(void)
{
    demo_system_callbacks_t cb = {0};
    cb.on_finished = on_finished;
    cb.on_load_level = on_load_level;
    g_finished_demo = 0;
    g_finished_preview = 0;
    g_loaded_level = 0;
    g_rand_val = 42;
    return demo_system_create(&cb, NULL, test_rand);
}

/* =========================================================================
 * Group 1: Lifecycle
 * ========================================================================= */

static void test_create_destroy(void **state)
{
    (void)state;
    demo_system_t *ctx = make_ctx();
    assert_non_null(ctx);
    assert_int_equal(demo_system_get_state(ctx), DEMO_STATE_NONE);
    demo_system_destroy(ctx);
}

static void test_begin_demo(void **state)
{
    (void)state;
    demo_system_t *ctx = make_ctx();
    demo_system_begin(ctx, DEMO_MODE_DEMO, 0);
    assert_int_equal(demo_system_get_state(ctx), DEMO_STATE_TITLE);
    assert_int_equal(demo_system_get_mode(ctx), DEMO_MODE_DEMO);
    assert_int_equal(demo_system_is_finished(ctx), 0);
    demo_system_destroy(ctx);
}

static void test_begin_preview(void **state)
{
    (void)state;
    demo_system_t *ctx = make_ctx();
    demo_system_begin(ctx, DEMO_MODE_PREVIEW, 0);
    assert_int_equal(demo_system_get_state(ctx), DEMO_STATE_TITLE);
    assert_int_equal(demo_system_get_mode(ctx), DEMO_MODE_PREVIEW);
    demo_system_destroy(ctx);
}

/* =========================================================================
 * Group 2: Demo state flow
 * ========================================================================= */

static void test_demo_title_to_blocks(void **state)
{
    (void)state;
    demo_system_t *ctx = make_ctx();
    demo_system_begin(ctx, DEMO_MODE_DEMO, 0);
    demo_system_update(ctx, 0);
    assert_int_equal(demo_system_get_state(ctx), DEMO_STATE_BLOCKS);
    demo_system_destroy(ctx);
}

static void test_demo_blocks_to_sparkle(void **state)
{
    (void)state;
    demo_system_t *ctx = make_ctx();
    demo_system_begin(ctx, DEMO_MODE_DEMO, 0);
    demo_system_update(ctx, 0); /* TITLE -> BLOCKS */
    demo_system_update(ctx, 1); /* BLOCKS -> TEXT */
    demo_system_update(ctx, 2); /* TEXT -> SPARKLE */
    assert_int_equal(demo_system_get_state(ctx), DEMO_STATE_SPARKLE);
    demo_system_destroy(ctx);
}

static void test_demo_finishes_at_end(void **state)
{
    (void)state;
    demo_system_t *ctx = make_ctx();
    demo_system_begin(ctx, DEMO_MODE_DEMO, 0);

    /* Advance to sparkle. */
    for (int i = 0; i < 5; i++)
    {
        demo_system_update(ctx, i);
    }

    /* Jump to end. TEXT sets end_frame = current + 5000.
     * TEXT ran at frame 2, so end_frame = 5002. */
    demo_system_update(ctx, 5002);
    assert_int_equal(demo_system_get_state(ctx), DEMO_STATE_FINISH);

    demo_system_update(ctx, 5003);
    assert_int_equal(demo_system_is_finished(ctx), 1);
    assert_int_equal(g_finished_demo, 1);

    demo_sound_t snd = demo_system_get_sound(ctx);
    assert_non_null(snd.name);
    assert_string_equal(snd.name, "whizzo");
    demo_system_destroy(ctx);
}

/* =========================================================================
 * Group 3: Preview state flow
 * ========================================================================= */

static void test_preview_loads_level(void **state)
{
    (void)state;
    demo_system_t *ctx = make_ctx();
    demo_system_begin(ctx, DEMO_MODE_PREVIEW, 0);
    demo_system_update(ctx, 0); /* TITLE -> load level -> TEXT */
    assert_true(g_loaded_level > 0);
    assert_true(g_loaded_level <= 80);
    assert_int_equal(demo_system_get_preview_level(ctx), g_loaded_level);
    demo_system_destroy(ctx);
}

static void test_preview_text_to_wait(void **state)
{
    (void)state;
    demo_system_t *ctx = make_ctx();
    demo_system_begin(ctx, DEMO_MODE_PREVIEW, 0);
    demo_system_update(ctx, 0); /* TITLE -> TEXT */
    demo_system_update(ctx, 1); /* TEXT -> WAIT */
    assert_int_equal(demo_system_get_state(ctx), DEMO_STATE_WAIT);
    demo_system_destroy(ctx);
}

static void test_preview_finishes_after_wait(void **state)
{
    (void)state;
    demo_system_t *ctx = make_ctx();
    demo_system_begin(ctx, DEMO_MODE_PREVIEW, 0);
    demo_system_update(ctx, 0); /* TITLE -> TEXT */
    demo_system_update(ctx, 1); /* TEXT -> WAIT (wait_frame = 1 + 5000) */

    /* Jump past wait. */
    demo_system_update(ctx, 5001); /* WAIT -> FINISH */
    assert_int_equal(demo_system_get_state(ctx), DEMO_STATE_FINISH);

    demo_system_update(ctx, 5002);
    assert_int_equal(demo_system_is_finished(ctx), 1);
    assert_int_equal(g_finished_preview, 1);
    demo_system_destroy(ctx);
}

/* =========================================================================
 * Group 4: Data tables
 * ========================================================================= */

static void test_ball_trail_count(void **state)
{
    (void)state;
    demo_system_t *ctx = make_ctx();
    const demo_ball_pos_t *trail = NULL;
    int count = demo_system_get_ball_trail(ctx, &trail);
    assert_int_equal(count, DEMO_BALL_TRAIL_COUNT);
    assert_non_null(trail);
    demo_system_destroy(ctx);
}

static void test_ball_trail_first_position(void **state)
{
    (void)state;
    demo_system_t *ctx = make_ctx();
    const demo_ball_pos_t *trail = NULL;
    demo_system_get_ball_trail(ctx, &trail);
    /* First ball at PLAY_WIDTH - PLAY_WIDTH/3, PLAY_HEIGHT - PLAY_HEIGHT/3 */
    assert_int_equal(trail[0].x, 330);
    assert_int_equal(trail[0].y, 387);
    assert_int_equal(trail[0].frame_index, 0);
    demo_system_destroy(ctx);
}

static void test_demo_text_count(void **state)
{
    (void)state;
    demo_system_t *ctx = make_ctx();
    const demo_text_line_t *text = NULL;
    int count = demo_system_get_demo_text(ctx, &text);
    assert_int_equal(count, DEMO_TEXT_LINES);
    assert_non_null(text);
    assert_string_equal(text[0].text, "Ball hits the paddle");
    demo_system_destroy(ctx);
}

/* =========================================================================
 * Group 5: Sparkle and specials
 * ========================================================================= */

static void test_sparkle_active_in_demo(void **state)
{
    (void)state;
    demo_system_t *ctx = make_ctx();
    demo_system_begin(ctx, DEMO_MODE_DEMO, 0);

    for (int i = 0; i < 5; i++)
    {
        demo_system_update(ctx, i);
    }

    demo_sparkle_info_t info;
    demo_system_get_sparkle_info(ctx, &info);
    assert_int_equal(info.active, 1);
    demo_system_destroy(ctx);
}

static void test_specials_in_preview_wait(void **state)
{
    (void)state;
    demo_system_t *ctx = make_ctx();
    demo_system_begin(ctx, DEMO_MODE_PREVIEW, 0);
    demo_system_update(ctx, 0);
    demo_system_update(ctx, 1);
    assert_int_equal(demo_system_get_state(ctx), DEMO_STATE_WAIT);

    /* Frame 30 should trigger specials (FLASH=30). */
    demo_system_update(ctx, 30);
    assert_int_equal(demo_system_should_draw_specials(ctx), 1);

    demo_system_update(ctx, 31);
    assert_int_equal(demo_system_should_draw_specials(ctx), 0);
    demo_system_destroy(ctx);
}

/* =========================================================================
 * Group 6: Null safety
 * ========================================================================= */

static void test_null_safety(void **state)
{
    (void)state;
    demo_system_destroy(NULL);
    demo_system_begin(NULL, DEMO_MODE_DEMO, 0);
    assert_int_equal(demo_system_update(NULL, 0), 0);
    assert_int_equal(demo_system_get_state(NULL), DEMO_STATE_NONE);
    assert_int_equal(demo_system_is_finished(NULL), 1);
    assert_int_equal(demo_system_should_draw_specials(NULL), 0);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_begin_demo),
        cmocka_unit_test(test_begin_preview),
        /* Group 2: Demo state flow */
        cmocka_unit_test(test_demo_title_to_blocks),
        cmocka_unit_test(test_demo_blocks_to_sparkle),
        cmocka_unit_test(test_demo_finishes_at_end),
        /* Group 3: Preview state flow */
        cmocka_unit_test(test_preview_loads_level),
        cmocka_unit_test(test_preview_text_to_wait),
        cmocka_unit_test(test_preview_finishes_after_wait),
        /* Group 4: Data tables */
        cmocka_unit_test(test_ball_trail_count),
        cmocka_unit_test(test_ball_trail_first_position),
        cmocka_unit_test(test_demo_text_count),
        /* Group 5: Sparkle and specials */
        cmocka_unit_test(test_sparkle_active_in_demo),
        cmocka_unit_test(test_specials_in_preview_wait),
        /* Group 6: Null safety */
        cmocka_unit_test(test_null_safety),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
