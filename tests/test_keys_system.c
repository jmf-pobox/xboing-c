/*
 * test_keys_system.c — Tests for the pure C keys/editor controls sequencer.
 *
 * 7 groups:
 *   1. Lifecycle (3 tests)
 *   2. Game state flow (3 tests)
 *   3. Editor state flow (2 tests)
 *   4. Data tables (3 tests)
 *   5. Sparkle and specials (2 tests)
 *   6. Blink (2 tests)
 *   7. Null safety (1 test)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

/* cmocka must come after setjmp.h / stdarg.h / stddef.h */
#include <cmocka.h>

#include "keys_system.h"

/* =========================================================================
 * Test helpers
 * ========================================================================= */

static int g_finished_game;
static int g_finished_editor;

static void on_finished(keys_screen_mode_t mode, void *user_data)
{
    (void)user_data;
    if (mode == KEYS_MODE_GAME)
    {
        g_finished_game = 1;
    }
    else
    {
        g_finished_editor = 1;
    }
}

static int g_rand_val;

static int test_rand(void *user_data)
{
    (void)user_data;
    return g_rand_val++;
}

static keys_system_t *make_ctx(void)
{
    keys_system_callbacks_t cb = {0};
    cb.on_finished = on_finished;
    g_finished_game = 0;
    g_finished_editor = 0;
    g_rand_val = 42;
    return keys_system_create(&cb, NULL, test_rand);
}

/* =========================================================================
 * Group 1: Lifecycle
 * ========================================================================= */

static void test_create_destroy(void **state)
{
    (void)state;
    keys_system_t *ctx = make_ctx();
    assert_non_null(ctx);
    assert_int_equal(keys_system_get_state(ctx), KEYS_STATE_NONE);
    keys_system_destroy(ctx);
}

static void test_begin_game(void **state)
{
    (void)state;
    keys_system_t *ctx = make_ctx();
    keys_system_begin(ctx, KEYS_MODE_GAME, 0);
    assert_int_equal(keys_system_get_state(ctx), KEYS_STATE_TITLE);
    assert_int_equal(keys_system_get_mode(ctx), KEYS_MODE_GAME);
    assert_int_equal(keys_system_is_finished(ctx), 0);
    keys_system_destroy(ctx);
}

static void test_begin_editor(void **state)
{
    (void)state;
    keys_system_t *ctx = make_ctx();
    keys_system_begin(ctx, KEYS_MODE_EDITOR, 0);
    assert_int_equal(keys_system_get_state(ctx), KEYS_STATE_TITLE);
    assert_int_equal(keys_system_get_mode(ctx), KEYS_MODE_EDITOR);
    keys_system_destroy(ctx);
}

/* =========================================================================
 * Group 2: Game state flow
 * ========================================================================= */

static void test_game_title_to_text(void **state)
{
    (void)state;
    keys_system_t *ctx = make_ctx();
    keys_system_begin(ctx, KEYS_MODE_GAME, 0);
    keys_system_update(ctx, 0);
    assert_int_equal(keys_system_get_state(ctx), KEYS_STATE_TEXT);
    keys_system_destroy(ctx);
}

static void test_game_text_to_sparkle(void **state)
{
    (void)state;
    keys_system_t *ctx = make_ctx();
    keys_system_begin(ctx, KEYS_MODE_GAME, 0);
    keys_system_update(ctx, 0); /* TITLE -> TEXT */
    keys_system_update(ctx, 1); /* TEXT -> SPARKLE */
    assert_int_equal(keys_system_get_state(ctx), KEYS_STATE_SPARKLE);
    keys_system_destroy(ctx);
}

static void test_game_finishes_at_end(void **state)
{
    (void)state;
    keys_system_t *ctx = make_ctx();
    keys_system_begin(ctx, KEYS_MODE_GAME, 0);

    /* Advance to sparkle. */
    keys_system_update(ctx, 0); /* TITLE -> TEXT */
    keys_system_update(ctx, 1); /* TEXT -> SPARKLE (end_frame = 1 + 4000) */

    /* Jump to end. */
    keys_system_update(ctx, 4001); /* SPARKLE -> FINISH */
    assert_int_equal(keys_system_get_state(ctx), KEYS_STATE_FINISH);

    keys_system_update(ctx, 4002);
    assert_int_equal(keys_system_is_finished(ctx), 1);
    assert_int_equal(g_finished_game, 1);

    keys_sound_t snd = keys_system_get_sound(ctx);
    assert_non_null(snd.name);
    assert_string_equal(snd.name, "boing");
    keys_system_destroy(ctx);
}

/* =========================================================================
 * Group 3: Editor state flow
 * ========================================================================= */

static void test_editor_text_to_sparkle(void **state)
{
    (void)state;
    keys_system_t *ctx = make_ctx();
    keys_system_begin(ctx, KEYS_MODE_EDITOR, 0);
    keys_system_update(ctx, 0); /* TITLE -> TEXT */
    keys_system_update(ctx, 1); /* TEXT -> SPARKLE */
    assert_int_equal(keys_system_get_state(ctx), KEYS_STATE_SPARKLE);
    keys_system_destroy(ctx);
}

static void test_editor_finishes_with_warp(void **state)
{
    (void)state;
    keys_system_t *ctx = make_ctx();
    keys_system_begin(ctx, KEYS_MODE_EDITOR, 0);
    keys_system_update(ctx, 0); /* TITLE -> TEXT */
    keys_system_update(ctx, 1); /* TEXT -> SPARKLE (end_frame = 1 + 4000) */

    keys_system_update(ctx, 4001); /* SPARKLE -> FINISH */
    keys_system_update(ctx, 4002);
    assert_int_equal(keys_system_is_finished(ctx), 1);
    assert_int_equal(g_finished_editor, 1);

    keys_sound_t snd = keys_system_get_sound(ctx);
    assert_non_null(snd.name);
    assert_string_equal(snd.name, "warp");
    keys_system_destroy(ctx);
}

/* =========================================================================
 * Group 4: Data tables
 * ========================================================================= */

static void test_game_bindings(void **state)
{
    (void)state;
    keys_system_t *ctx = make_ctx();
    const keys_binding_entry_t *bindings = NULL;
    int count = keys_system_get_game_bindings(ctx, &bindings);
    assert_int_equal(count, KEYS_GAME_BINDINGS_COUNT);
    assert_non_null(bindings);
    /* First entry is left column. */
    assert_string_equal(bindings[0].text, "<s> = Sfx On/Off");
    assert_int_equal(bindings[0].column, 0);
    /* First right column entry (index 10). */
    assert_string_equal(bindings[10].text, "<j> = Paddle left");
    assert_int_equal(bindings[10].column, 1);
    keys_system_destroy(ctx);
}

static void test_editor_info_text(void **state)
{
    (void)state;
    keys_system_t *ctx = make_ctx();
    const keys_info_line_t *info = NULL;
    int count = keys_system_get_editor_info(ctx, &info);
    assert_int_equal(count, KEYS_EDITOR_INFO_COUNT);
    assert_non_null(info);
    assert_true(strstr(info[0].text, "level editor") != NULL);
    keys_system_destroy(ctx);
}

static void test_editor_bindings(void **state)
{
    (void)state;
    keys_system_t *ctx = make_ctx();
    const keys_binding_entry_t *bindings = NULL;
    int count = keys_system_get_editor_bindings(ctx, &bindings);
    assert_int_equal(count, KEYS_EDITOR_BINDINGS_COUNT);
    assert_non_null(bindings);
    assert_string_equal(bindings[0].text, "<r> = Redraw level");
    assert_int_equal(bindings[0].column, 0);
    /* First right column entry (index 5). */
    assert_string_equal(bindings[5].text, "<s> = Save level");
    assert_int_equal(bindings[5].column, 1);
    keys_system_destroy(ctx);
}

/* =========================================================================
 * Group 5: Sparkle and specials
 * ========================================================================= */

static void test_sparkle_active_in_sparkle_state(void **state)
{
    (void)state;
    keys_system_t *ctx = make_ctx();
    keys_system_begin(ctx, KEYS_MODE_GAME, 0);
    keys_system_update(ctx, 0); /* TITLE -> TEXT */
    keys_system_update(ctx, 1); /* TEXT -> SPARKLE */

    keys_sparkle_info_t info;
    keys_system_get_sparkle_info(ctx, &info);
    assert_int_equal(info.active, 1);
    keys_system_destroy(ctx);
}

static void test_specials_at_flash_interval(void **state)
{
    (void)state;
    keys_system_t *ctx = make_ctx();
    keys_system_begin(ctx, KEYS_MODE_GAME, 0);
    keys_system_update(ctx, 0); /* TITLE -> TEXT */
    keys_system_update(ctx, 1); /* TEXT -> SPARKLE */

    /* Frame 30 should trigger specials (FLASH=30). */
    keys_system_update(ctx, 30);
    assert_int_equal(keys_system_should_draw_specials(ctx), 1);

    keys_system_update(ctx, 31);
    assert_int_equal(keys_system_should_draw_specials(ctx), 0);
    keys_system_destroy(ctx);
}

/* =========================================================================
 * Group 6: Blink
 * ========================================================================= */

static void test_blink_fires_in_game_mode(void **state)
{
    (void)state;
    keys_system_t *ctx = make_ctx();
    keys_system_begin(ctx, KEYS_MODE_GAME, 0);
    keys_system_update(ctx, 0); /* TITLE -> TEXT */
    keys_system_update(ctx, 1); /* TEXT -> SPARKLE */

    /* Blink should not fire before gap. */
    keys_system_update(ctx, 9);
    assert_int_equal(keys_system_should_blink(ctx), 0);

    /* Blink fires at next_blink (begin sets it to frame + 10 = 10).
     * But TEXT handler at frame 1 doesn't reset blink.
     * begin() sets next_blink = 0 + 10 = 10. */
    keys_system_update(ctx, 10);
    assert_int_equal(keys_system_should_blink(ctx), 1);

    /* After firing, next_blink advances by BLINK_GAP. */
    keys_system_update(ctx, 11);
    assert_int_equal(keys_system_should_blink(ctx), 0);
    keys_system_destroy(ctx);
}

static void test_blink_never_fires_in_editor_mode(void **state)
{
    (void)state;
    keys_system_t *ctx = make_ctx();
    keys_system_begin(ctx, KEYS_MODE_EDITOR, 0);
    keys_system_update(ctx, 0); /* TITLE -> TEXT */
    keys_system_update(ctx, 1); /* TEXT -> SPARKLE */

    /* Editor mode should never blink. */
    keys_system_update(ctx, 10);
    assert_int_equal(keys_system_should_blink(ctx), 0);
    keys_system_destroy(ctx);
}

/* =========================================================================
 * Group 7: Null safety
 * ========================================================================= */

static void test_null_safety(void **state)
{
    (void)state;
    keys_system_destroy(NULL);
    keys_system_begin(NULL, KEYS_MODE_GAME, 0);
    assert_int_equal(keys_system_update(NULL, 0), 0);
    assert_int_equal(keys_system_get_state(NULL), KEYS_STATE_NONE);
    assert_int_equal(keys_system_is_finished(NULL), 1);
    assert_int_equal(keys_system_should_blink(NULL), 0);
    assert_int_equal(keys_system_should_draw_specials(NULL), 0);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_begin_game),
        cmocka_unit_test(test_begin_editor),
        /* Group 2: Game state flow */
        cmocka_unit_test(test_game_title_to_text),
        cmocka_unit_test(test_game_text_to_sparkle),
        cmocka_unit_test(test_game_finishes_at_end),
        /* Group 3: Editor state flow */
        cmocka_unit_test(test_editor_text_to_sparkle),
        cmocka_unit_test(test_editor_finishes_with_warp),
        /* Group 4: Data tables */
        cmocka_unit_test(test_game_bindings),
        cmocka_unit_test(test_editor_info_text),
        cmocka_unit_test(test_editor_bindings),
        /* Group 5: Sparkle and specials */
        cmocka_unit_test(test_sparkle_active_in_sparkle_state),
        cmocka_unit_test(test_specials_at_flash_interval),
        /* Group 6: Blink */
        cmocka_unit_test(test_blink_fires_in_game_mode),
        cmocka_unit_test(test_blink_never_fires_in_editor_mode),
        /* Group 7: Null safety */
        cmocka_unit_test(test_null_safety),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
