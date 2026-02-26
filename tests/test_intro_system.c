/*
 * test_intro_system.c — Tests for the pure C intro/instructions sequencer.
 *
 * 7 groups:
 *   1. Lifecycle (3 tests)
 *   2. Intro state flow (3 tests)
 *   3. Instruct state flow (3 tests)
 *   4. Block description table (2 tests)
 *   5. Instruction text (2 tests)
 *   6. Sparkle and blink (3 tests)
 *   7. Null safety (1 test)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

/* cmocka must come after setjmp.h / stdarg.h / stddef.h */
#include <cmocka.h>

#include "intro_system.h"

/* =========================================================================
 * Test helpers
 * ========================================================================= */

static int g_finished_intro;
static int g_finished_instruct;

static void on_finished(intro_screen_mode_t mode, void *user_data)
{
    (void)user_data;
    if (mode == INTRO_MODE_INTRO)
    {
        g_finished_intro = 1;
    }
    else
    {
        g_finished_instruct = 1;
    }
}

static int g_rand_val;

static int test_rand(void *user_data)
{
    (void)user_data;
    return g_rand_val++;
}

static intro_system_t *make_ctx(void)
{
    intro_system_callbacks_t cb = {0};
    cb.on_finished = on_finished;
    g_finished_intro = 0;
    g_finished_instruct = 0;
    g_rand_val = 42;
    return intro_system_create(&cb, NULL, test_rand);
}

/* =========================================================================
 * Group 1: Lifecycle
 * ========================================================================= */

static void test_create_destroy(void **state)
{
    (void)state;
    intro_system_t *ctx = make_ctx();
    assert_non_null(ctx);
    assert_int_equal(intro_system_get_state(ctx), INTRO_STATE_NONE);
    intro_system_destroy(ctx);
}

static void test_begin_intro_sets_title(void **state)
{
    (void)state;
    intro_system_t *ctx = make_ctx();
    intro_system_begin(ctx, INTRO_MODE_INTRO, 0);
    assert_int_equal(intro_system_get_state(ctx), INTRO_STATE_TITLE);
    assert_int_equal(intro_system_get_mode(ctx), INTRO_MODE_INTRO);
    assert_int_equal(intro_system_is_finished(ctx), 0);
    intro_system_destroy(ctx);
}

static void test_begin_instruct_sets_title(void **state)
{
    (void)state;
    intro_system_t *ctx = make_ctx();
    intro_system_begin(ctx, INTRO_MODE_INSTRUCT, 0);
    assert_int_equal(intro_system_get_state(ctx), INTRO_STATE_TITLE);
    assert_int_equal(intro_system_get_mode(ctx), INTRO_MODE_INSTRUCT);
    intro_system_destroy(ctx);
}

/* =========================================================================
 * Group 2: Intro state flow
 * ========================================================================= */

static void test_intro_title_to_blocks(void **state)
{
    (void)state;
    intro_system_t *ctx = make_ctx();
    intro_system_begin(ctx, INTRO_MODE_INTRO, 0);
    intro_system_update(ctx, 0);
    /* After TITLE, should advance to BLOCKS. */
    assert_int_equal(intro_system_get_state(ctx), INTRO_STATE_BLOCKS);
    intro_system_destroy(ctx);
}

static void test_intro_blocks_to_text_to_sparkle(void **state)
{
    (void)state;
    intro_system_t *ctx = make_ctx();
    intro_system_begin(ctx, INTRO_MODE_INTRO, 0);
    intro_system_update(ctx, 0); /* TITLE -> BLOCKS */
    intro_system_update(ctx, 1); /* BLOCKS -> TEXT */
    assert_int_equal(intro_system_get_state(ctx), INTRO_STATE_TEXT);
    intro_system_update(ctx, 2); /* TEXT -> SPARKLE */
    assert_int_equal(intro_system_get_state(ctx), INTRO_STATE_SPARKLE);
    intro_system_destroy(ctx);
}

static void test_intro_finishes_at_end_frame(void **state)
{
    (void)state;
    intro_system_t *ctx = make_ctx();
    intro_system_begin(ctx, INTRO_MODE_INTRO, 0);

    /* Fast-forward to sparkle state. */
    for (int i = 0; i < 5; i++)
    {
        intro_system_update(ctx, i);
    }
    assert_int_equal(intro_system_get_state(ctx), INTRO_STATE_SPARKLE);

    /* Jump to end frame. */
    intro_system_update(ctx, INTRO_END_FRAME_OFFSET);
    assert_int_equal(intro_system_get_state(ctx), INTRO_STATE_FINISH);

    intro_system_update(ctx, INTRO_END_FRAME_OFFSET + 1);
    assert_int_equal(intro_system_is_finished(ctx), 1);
    assert_int_equal(g_finished_intro, 1);
    intro_system_destroy(ctx);
}

/* =========================================================================
 * Group 3: Instruct state flow
 * ========================================================================= */

static void test_instruct_title_to_text(void **state)
{
    (void)state;
    intro_system_t *ctx = make_ctx();
    intro_system_begin(ctx, INTRO_MODE_INSTRUCT, 0);
    intro_system_update(ctx, 0); /* TITLE -> TEXT (skips BLOCKS) */
    assert_int_equal(intro_system_get_state(ctx), INTRO_STATE_TEXT);
    intro_system_destroy(ctx);
}

static void test_instruct_text_to_sparkle(void **state)
{
    (void)state;
    intro_system_t *ctx = make_ctx();
    intro_system_begin(ctx, INTRO_MODE_INSTRUCT, 0);
    intro_system_update(ctx, 0); /* TITLE -> TEXT */
    intro_system_update(ctx, 1); /* TEXT -> SPARKLE */
    assert_int_equal(intro_system_get_state(ctx), INTRO_STATE_SPARKLE);
    intro_system_destroy(ctx);
}

static void test_instruct_finishes_with_shark_sound(void **state)
{
    (void)state;
    intro_system_t *ctx = make_ctx();
    intro_system_begin(ctx, INTRO_MODE_INSTRUCT, 0);

    /* Fast-forward to sparkle. */
    for (int i = 0; i < 5; i++)
    {
        intro_system_update(ctx, i);
    }

    /* Jump past end frame. */
    intro_system_update(ctx, INSTRUCT_END_FRAME_OFFSET);
    intro_system_update(ctx, INSTRUCT_END_FRAME_OFFSET + 1);

    assert_int_equal(intro_system_is_finished(ctx), 1);
    assert_int_equal(g_finished_instruct, 1);

    intro_sound_t snd = intro_system_get_sound(ctx);
    assert_non_null(snd.name);
    assert_string_equal(snd.name, "shark");
    intro_system_destroy(ctx);
}

/* =========================================================================
 * Group 4: Block description table
 * ========================================================================= */

static void test_block_table_has_22_entries(void **state)
{
    (void)state;
    intro_system_t *ctx = make_ctx();
    const intro_block_entry_t *table = NULL;
    int count = intro_system_get_block_table(ctx, &table);
    assert_int_equal(count, INTRO_BLOCK_TOTAL);
    assert_non_null(table);
    intro_system_destroy(ctx);
}

static void test_block_table_first_and_last(void **state)
{
    (void)state;
    intro_system_t *ctx = make_ctx();
    const intro_block_entry_t *table = NULL;
    intro_system_get_block_table(ctx, &table);

    /* First entry: RED block at (40, 120). */
    assert_int_equal(table[0].type, INTRO_BLK_RED);
    assert_int_equal(table[0].x, 40);
    assert_int_equal(table[0].y, 120);
    assert_string_equal(table[0].description, "- Normal block");

    /* Last entry: STICKY block at (260, 520). */
    assert_int_equal(table[INTRO_BLOCK_TOTAL - 1].type, INTRO_BLK_STICKY);
    assert_int_equal(table[INTRO_BLOCK_TOTAL - 1].x, 260);
    assert_int_equal(table[INTRO_BLOCK_TOTAL - 1].y, 520);
    assert_string_equal(table[INTRO_BLOCK_TOTAL - 1].description, "- Sticky Ball");
    intro_system_destroy(ctx);
}

/* =========================================================================
 * Group 5: Instruction text
 * ========================================================================= */

static void test_instruct_text_has_20_lines(void **state)
{
    (void)state;
    intro_system_t *ctx = make_ctx();
    const intro_instruct_line_t *lines = NULL;
    int count = intro_system_get_instruct_text(ctx, &lines);
    assert_int_equal(count, INSTRUCT_TEXT_LINES);
    assert_non_null(lines);
    intro_system_destroy(ctx);
}

static void test_instruct_text_spacers(void **state)
{
    (void)state;
    intro_system_t *ctx = make_ctx();
    const intro_instruct_line_t *lines = NULL;
    intro_system_get_instruct_text(ctx, &lines);

    /* Line 0 is text, line 3 is spacer (NULL). */
    assert_int_equal(lines[0].is_spacer, 0);
    assert_non_null(lines[0].text);
    assert_int_equal(lines[3].is_spacer, 1);
    assert_null(lines[3].text);
    intro_system_destroy(ctx);
}

/* =========================================================================
 * Group 6: Sparkle and blink
 * ========================================================================= */

static void test_sparkle_active_during_sparkle_state(void **state)
{
    (void)state;
    intro_system_t *ctx = make_ctx();
    intro_system_begin(ctx, INTRO_MODE_INTRO, 0);

    /* Advance to SPARKLE. */
    for (int i = 0; i < 5; i++)
    {
        intro_system_update(ctx, i);
    }
    assert_int_equal(intro_system_get_state(ctx), INTRO_STATE_SPARKLE);

    intro_sparkle_info_t info;
    intro_system_get_sparkle_info(ctx, &info);
    assert_int_equal(info.active, 1);
    intro_system_destroy(ctx);
}

static void test_blink_fires_in_intro(void **state)
{
    (void)state;
    intro_system_t *ctx = make_ctx();
    intro_system_begin(ctx, INTRO_MODE_INTRO, 0);

    /* Advance to sparkle. */
    for (int i = 0; i < 5; i++)
    {
        intro_system_update(ctx, i);
    }

    /* Blink should fire at next_blink (frame 10). */
    intro_system_update(ctx, 10);
    assert_int_equal(intro_system_should_blink(ctx), 1);
    intro_system_destroy(ctx);
}

static void test_specials_draw_at_flash_interval(void **state)
{
    (void)state;
    intro_system_t *ctx = make_ctx();
    intro_system_begin(ctx, INTRO_MODE_INTRO, 0);

    /* Advance to sparkle. */
    for (int i = 0; i < 5; i++)
    {
        intro_system_update(ctx, i);
    }

    /* Frame 30 should trigger specials (FLASH=30). */
    intro_system_update(ctx, 30);
    assert_int_equal(intro_system_should_draw_specials(ctx), 1);

    /* Frame 31 should not. */
    intro_system_update(ctx, 31);
    assert_int_equal(intro_system_should_draw_specials(ctx), 0);
    intro_system_destroy(ctx);
}

/* =========================================================================
 * Group 7: Null safety
 * ========================================================================= */

static void test_null_safety(void **state)
{
    (void)state;
    intro_system_destroy(NULL);
    intro_system_begin(NULL, INTRO_MODE_INTRO, 0);
    assert_int_equal(intro_system_update(NULL, 0), 0);
    assert_int_equal(intro_system_get_state(NULL), INTRO_STATE_NONE);
    assert_int_equal(intro_system_is_finished(NULL), 1);
    assert_int_equal(intro_system_should_blink(NULL), 0);
    assert_int_equal(intro_system_should_draw_specials(NULL), 0);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_begin_intro_sets_title),
        cmocka_unit_test(test_begin_instruct_sets_title),
        /* Group 2: Intro state flow */
        cmocka_unit_test(test_intro_title_to_blocks),
        cmocka_unit_test(test_intro_blocks_to_text_to_sparkle),
        cmocka_unit_test(test_intro_finishes_at_end_frame),
        /* Group 3: Instruct state flow */
        cmocka_unit_test(test_instruct_title_to_text),
        cmocka_unit_test(test_instruct_text_to_sparkle),
        cmocka_unit_test(test_instruct_finishes_with_shark_sound),
        /* Group 4: Block description table */
        cmocka_unit_test(test_block_table_has_22_entries),
        cmocka_unit_test(test_block_table_first_and_last),
        /* Group 5: Instruction text */
        cmocka_unit_test(test_instruct_text_has_20_lines),
        cmocka_unit_test(test_instruct_text_spacers),
        /* Group 6: Sparkle and blink */
        cmocka_unit_test(test_sparkle_active_during_sparkle_state),
        cmocka_unit_test(test_blink_fires_in_intro),
        cmocka_unit_test(test_specials_draw_at_flash_interval),
        /* Group 7: Null safety */
        cmocka_unit_test(test_null_safety),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
