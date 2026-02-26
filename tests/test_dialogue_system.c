/*
 * test_dialogue_system.c — Tests for the pure C modal input dialogue.
 *
 * 7 groups:
 *   1. Lifecycle (2 tests)
 *   2. State flow (3 tests)
 *   3. Text validation (3 tests)
 *   4. Numeric validation (2 tests)
 *   5. Yes/No validation (2 tests)
 *   6. Backspace and overflow (3 tests)
 *   7. Null safety (1 test)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

/* cmocka must come after setjmp.h / stdarg.h / stddef.h */
#include <cmocka.h>

#include "dialogue_system.h"

/* =========================================================================
 * Group 1: Lifecycle
 * ========================================================================= */

static void test_create_destroy(void **state)
{
    (void)state;
    dialogue_system_t *ctx = dialogue_system_create();
    assert_non_null(ctx);
    assert_int_equal(dialogue_system_get_state(ctx), DIALOGUE_STATE_NONE);
    dialogue_system_destroy(ctx);
}

static void test_open_sets_state(void **state)
{
    (void)state;
    dialogue_system_t *ctx = dialogue_system_create();
    dialogue_system_open(ctx, "Enter name:", DIALOGUE_ICON_TEXT, DIALOGUE_VALIDATION_TEXT);
    assert_int_equal(dialogue_system_get_state(ctx), DIALOGUE_STATE_MAP);
    assert_string_equal(dialogue_system_get_message(ctx), "Enter name:");
    assert_int_equal(dialogue_system_get_icon(ctx), DIALOGUE_ICON_TEXT);
    assert_int_equal(dialogue_system_get_validation(ctx), DIALOGUE_VALIDATION_TEXT);
    assert_int_equal(dialogue_system_get_input_length(ctx), 0);
    dialogue_system_destroy(ctx);
}

/* =========================================================================
 * Group 2: State flow
 * ========================================================================= */

static void test_map_to_text(void **state)
{
    (void)state;
    dialogue_system_t *ctx = dialogue_system_create();
    dialogue_system_open(ctx, "Test", DIALOGUE_ICON_DISK, DIALOGUE_VALIDATION_ALL);
    dialogue_system_update(ctx); /* MAP -> TEXT */
    assert_int_equal(dialogue_system_get_state(ctx), DIALOGUE_STATE_TEXT);
    dialogue_system_destroy(ctx);
}

static void test_return_submits(void **state)
{
    (void)state;
    dialogue_system_t *ctx = dialogue_system_create();
    dialogue_system_open(ctx, "Test", DIALOGUE_ICON_TEXT, DIALOGUE_VALIDATION_ALL);
    dialogue_system_update(ctx); /* MAP -> TEXT */

    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, 'h');
    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, 'i');
    dialogue_system_key_input(ctx, DIALOGUE_KEY_RETURN, 0);
    assert_int_equal(dialogue_system_get_state(ctx), DIALOGUE_STATE_UNMAP);
    assert_int_equal(dialogue_system_was_cancelled(ctx), 0);

    dialogue_system_update(ctx); /* UNMAP -> FINISHED */
    assert_int_equal(dialogue_system_is_finished(ctx), 1);
    assert_string_equal(dialogue_system_get_input(ctx), "hi");
    dialogue_system_destroy(ctx);
}

static void test_escape_cancels(void **state)
{
    (void)state;
    dialogue_system_t *ctx = dialogue_system_create();
    dialogue_system_open(ctx, "Test", DIALOGUE_ICON_TEXT, DIALOGUE_VALIDATION_ALL);
    dialogue_system_update(ctx); /* MAP -> TEXT */

    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, 'x');
    dialogue_system_key_input(ctx, DIALOGUE_KEY_ESCAPE, 0);
    assert_int_equal(dialogue_system_get_state(ctx), DIALOGUE_STATE_UNMAP);
    assert_int_equal(dialogue_system_was_cancelled(ctx), 1);

    dialogue_system_update(ctx); /* UNMAP -> FINISHED */
    assert_int_equal(dialogue_system_is_finished(ctx), 1);
    dialogue_system_destroy(ctx);
}

/* =========================================================================
 * Group 3: Text validation
 * ========================================================================= */

static void test_text_accepts_letters(void **state)
{
    (void)state;
    dialogue_system_t *ctx = dialogue_system_create();
    dialogue_system_open(ctx, "Name:", DIALOGUE_ICON_TEXT, DIALOGUE_VALIDATION_TEXT);
    dialogue_system_update(ctx);

    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, 'A');
    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, 'b');
    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, ' ');
    assert_int_equal(dialogue_system_get_input_length(ctx), 3);
    assert_string_equal(dialogue_system_get_input(ctx), "Ab ");
    dialogue_system_destroy(ctx);
}

static void test_text_rejects_tilde(void **state)
{
    (void)state;
    dialogue_system_t *ctx = dialogue_system_create();
    dialogue_system_open(ctx, "Name:", DIALOGUE_ICON_TEXT, DIALOGUE_VALIDATION_TEXT);
    dialogue_system_update(ctx);

    /* '~' is 0x7e, beyond 'z' (0x7a), so TEXT rejects it. */
    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, '~');
    assert_int_equal(dialogue_system_get_input_length(ctx), 0);
    dialogue_system_destroy(ctx);
}

static void test_all_accepts_tilde(void **state)
{
    (void)state;
    dialogue_system_t *ctx = dialogue_system_create();
    dialogue_system_open(ctx, "File:", DIALOGUE_ICON_DISK, DIALOGUE_VALIDATION_ALL);
    dialogue_system_update(ctx);

    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, '~');
    assert_int_equal(dialogue_system_get_input_length(ctx), 1);
    assert_string_equal(dialogue_system_get_input(ctx), "~");
    dialogue_system_destroy(ctx);
}

/* =========================================================================
 * Group 4: Numeric validation
 * ========================================================================= */

static void test_numeric_accepts_digits(void **state)
{
    (void)state;
    dialogue_system_t *ctx = dialogue_system_create();
    dialogue_system_open(ctx, "Level:", DIALOGUE_ICON_TEXT, DIALOGUE_VALIDATION_NUMERIC);
    dialogue_system_update(ctx);

    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, '4');
    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, '2');
    assert_string_equal(dialogue_system_get_input(ctx), "42");
    dialogue_system_destroy(ctx);
}

static void test_numeric_rejects_letters(void **state)
{
    (void)state;
    dialogue_system_t *ctx = dialogue_system_create();
    dialogue_system_open(ctx, "Level:", DIALOGUE_ICON_TEXT, DIALOGUE_VALIDATION_NUMERIC);
    dialogue_system_update(ctx);

    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, 'a');
    assert_int_equal(dialogue_system_get_input_length(ctx), 0);
    dialogue_system_destroy(ctx);
}

/* =========================================================================
 * Group 5: Yes/No validation
 * ========================================================================= */

static void test_yesno_accepts_valid(void **state)
{
    (void)state;
    dialogue_system_t *ctx = dialogue_system_create();
    dialogue_system_open(ctx, "Save?", DIALOGUE_ICON_DISK, DIALOGUE_VALIDATION_YES_NO);
    dialogue_system_update(ctx);

    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, 'Y');
    assert_string_equal(dialogue_system_get_input(ctx), "Y");
    dialogue_system_destroy(ctx);
}

static void test_yesno_single_char_only(void **state)
{
    (void)state;
    dialogue_system_t *ctx = dialogue_system_create();
    dialogue_system_open(ctx, "Save?", DIALOGUE_ICON_DISK, DIALOGUE_VALIDATION_YES_NO);
    dialogue_system_update(ctx);

    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, 'n');
    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, 'y'); /* Should be rejected */
    assert_int_equal(dialogue_system_get_input_length(ctx), 1);
    assert_string_equal(dialogue_system_get_input(ctx), "n");
    dialogue_system_destroy(ctx);
}

/* =========================================================================
 * Group 6: Backspace and overflow
 * ========================================================================= */

static void test_backspace_removes_char(void **state)
{
    (void)state;
    dialogue_system_t *ctx = dialogue_system_create();
    dialogue_system_open(ctx, "Test", DIALOGUE_ICON_TEXT, DIALOGUE_VALIDATION_ALL);
    dialogue_system_update(ctx);

    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, 'a');
    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, 'b');
    dialogue_system_key_input(ctx, DIALOGUE_KEY_BACKSPACE, 0);
    assert_string_equal(dialogue_system_get_input(ctx), "a");

    dialogue_sound_t snd = dialogue_system_get_sound(ctx);
    assert_string_equal(snd.name, "key");
    dialogue_system_destroy(ctx);
}

static void test_backspace_on_empty(void **state)
{
    (void)state;
    dialogue_system_t *ctx = dialogue_system_create();
    dialogue_system_open(ctx, "Test", DIALOGUE_ICON_TEXT, DIALOGUE_VALIDATION_ALL);
    dialogue_system_update(ctx);

    dialogue_system_key_input(ctx, DIALOGUE_KEY_BACKSPACE, 0);
    assert_int_equal(dialogue_system_get_input_length(ctx), 0);
    /* No sound on empty backspace. */
    dialogue_sound_t snd = dialogue_system_get_sound(ctx);
    assert_null(snd.name);
    dialogue_system_destroy(ctx);
}

static void test_overflow_plays_tone(void **state)
{
    (void)state;
    dialogue_system_t *ctx = dialogue_system_create();
    dialogue_system_set_max_chars(ctx, 3);
    dialogue_system_open(ctx, "Test", DIALOGUE_ICON_TEXT, DIALOGUE_VALIDATION_ALL);
    dialogue_system_update(ctx);

    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, 'a');
    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, 'b');
    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, 'c');
    /* Buffer full at 3 chars. */
    dialogue_system_key_input(ctx, DIALOGUE_KEY_CHAR, 'd');
    assert_int_equal(dialogue_system_get_input_length(ctx), 3);

    dialogue_sound_t snd = dialogue_system_get_sound(ctx);
    assert_string_equal(snd.name, "tone");
    dialogue_system_destroy(ctx);
}

/* =========================================================================
 * Group 7: Null safety
 * ========================================================================= */

static void test_null_safety(void **state)
{
    (void)state;
    dialogue_system_destroy(NULL);
    dialogue_system_open(NULL, "test", DIALOGUE_ICON_TEXT, DIALOGUE_VALIDATION_TEXT);
    assert_int_equal(dialogue_system_update(NULL), 0);
    assert_int_equal(dialogue_system_get_state(NULL), DIALOGUE_STATE_NONE);
    assert_int_equal(dialogue_system_is_finished(NULL), 1);
    assert_int_equal(dialogue_system_was_cancelled(NULL), 1);
    dialogue_system_key_input(NULL, DIALOGUE_KEY_CHAR, 'x');
    dialogue_system_set_max_chars(NULL, 10);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_open_sets_state),
        /* Group 2: State flow */
        cmocka_unit_test(test_map_to_text),
        cmocka_unit_test(test_return_submits),
        cmocka_unit_test(test_escape_cancels),
        /* Group 3: Text validation */
        cmocka_unit_test(test_text_accepts_letters),
        cmocka_unit_test(test_text_rejects_tilde),
        cmocka_unit_test(test_all_accepts_tilde),
        /* Group 4: Numeric validation */
        cmocka_unit_test(test_numeric_accepts_digits),
        cmocka_unit_test(test_numeric_rejects_letters),
        /* Group 5: Yes/No validation */
        cmocka_unit_test(test_yesno_accepts_valid),
        cmocka_unit_test(test_yesno_single_char_only),
        /* Group 6: Backspace and overflow */
        cmocka_unit_test(test_backspace_removes_char),
        cmocka_unit_test(test_backspace_on_empty),
        cmocka_unit_test(test_overflow_plays_tone),
        /* Group 7: Null safety */
        cmocka_unit_test(test_null_safety),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
