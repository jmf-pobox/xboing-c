/*
 * test_presents_system.c — Tests for the pure C presents sequencer.
 *
 * 7 groups:
 *   1. Lifecycle (3 tests)
 *   2. Flag state (2 tests)
 *   3. Letter stamping (3 tests)
 *   4. Sparkle animation (2 tests)
 *   5. Typewriter text (3 tests)
 *   6. Curtain wipe (2 tests)
 *   7. Skip and null safety (3 tests)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

/* cmocka must come after setjmp.h / stdarg.h / stddef.h */
#include <cmocka.h>

#include "presents_system.h"

/* =========================================================================
 * Test helpers
 * ========================================================================= */

static int g_finished;

static void on_finished(void *user_data)
{
    (void)user_data;
    g_finished = 1;
}

static const char *get_nickname(void *user_data)
{
    (void)user_data;
    return "TestPlayer";
}

static presents_system_t *make_ctx(void)
{
    presents_system_callbacks_t cb = {0};
    cb.on_finished = on_finished;
    cb.get_nickname = get_nickname;
    return presents_system_create(&cb, NULL);
}

/* Advance frames until state changes or limit is hit. */
static int advance_to_state(presents_system_t *ctx, presents_state_t target, int start_frame,
                            int max_frames)
{
    int f = start_frame;
    for (int i = 0; i < max_frames; i++)
    {
        presents_system_update(ctx, f);
        if (presents_system_get_state(ctx) == target)
        {
            return f;
        }
        f++;
    }
    return f;
}

/* =========================================================================
 * Group 1: Lifecycle
 * ========================================================================= */

static void test_create_destroy(void **state)
{
    (void)state;
    presents_system_t *ctx = make_ctx();
    assert_non_null(ctx);
    assert_int_equal(presents_system_get_state(ctx), PRESENTS_STATE_NONE);
    presents_system_destroy(ctx);
}

static void test_begin_sets_flag_state(void **state)
{
    (void)state;
    presents_system_t *ctx = make_ctx();
    presents_system_begin(ctx, 0);
    assert_int_equal(presents_system_get_state(ctx), PRESENTS_STATE_FLAG);
    assert_int_equal(presents_system_is_finished(ctx), 0);
    presents_system_destroy(ctx);
}

static void test_update_returns_active(void **state)
{
    (void)state;
    presents_system_t *ctx = make_ctx();
    presents_system_begin(ctx, 0);
    int active = presents_system_update(ctx, 0);
    assert_int_equal(active, 1);
    presents_system_destroy(ctx);
}

/* =========================================================================
 * Group 2: Flag state
 * ========================================================================= */

static void test_flag_produces_sound(void **state)
{
    (void)state;
    presents_system_t *ctx = make_ctx();
    presents_system_begin(ctx, 0);
    presents_system_update(ctx, 0);

    presents_sound_t snd = presents_system_get_sound(ctx);
    assert_non_null(snd.name);
    assert_string_equal(snd.name, "intro");
    assert_int_equal(snd.volume, 40);
    presents_system_destroy(ctx);
}

static void test_flag_info_positions(void **state)
{
    (void)state;
    presents_system_t *ctx = make_ctx();
    presents_system_begin(ctx, 0);
    presents_system_update(ctx, 0);

    presents_flag_info_t info;
    presents_system_get_flag_info(ctx, &info);
    assert_int_equal(info.flag_y, 15);
    assert_int_equal(info.earth_y, 93);
    assert_int_equal(info.copyright_y, PRESENTS_TOTAL_HEIGHT - 20);
    presents_system_destroy(ctx);
}

/* =========================================================================
 * Group 3: Letter stamping
 * ========================================================================= */

static void test_flag_transitions_through_text_to_letters(void **state)
{
    (void)state;
    presents_system_t *ctx = make_ctx();
    presents_system_begin(ctx, 0);

    /* Advance past FLAG -> WAIT -> TEXT1 -> WAIT -> TEXT2 -> WAIT ->
     * TEXT3 -> WAIT -> TEXT_CLEAR -> WAIT -> LETTERS */
    int f = advance_to_state(ctx, PRESENTS_STATE_LETTERS, 0, 5000);
    assert_int_equal(presents_system_get_state(ctx), PRESENTS_STATE_LETTERS);
    assert_true(f < 5000);
    presents_system_destroy(ctx);
}

static void test_first_letter_is_x(void **state)
{
    (void)state;
    presents_system_t *ctx = make_ctx();
    presents_system_begin(ctx, 0);

    int f = advance_to_state(ctx, PRESENTS_STATE_LETTERS, 0, 5000);
    /* Now in LETTERS state. Update once to stamp first letter. */
    presents_system_update(ctx, f + 1);

    presents_letter_info_t info;
    int ok = presents_system_get_letter_info(ctx, &info);
    assert_int_equal(ok, 1);
    assert_int_equal(info.letter_index, 0); /* X */
    assert_int_equal(info.x, 40);
    assert_int_equal(info.width, PRESENTS_LETTER_X_W);
    presents_system_destroy(ctx);
}

static void test_all_six_letters_stamped(void **state)
{
    (void)state;
    presents_system_t *ctx = make_ctx();
    presents_system_begin(ctx, 0);

    /* Advance to LETTERS, then stamp all 6 + "II". */
    int f = advance_to_state(ctx, PRESENTS_STATE_LETTERS, 0, 5000);

    int letters_seen = 0;
    for (int i = 0; i < 10000 && !presents_system_is_finished(ctx); i++)
    {
        presents_state_t before = presents_system_get_state(ctx);
        presents_system_update(ctx, f);
        presents_state_t after = presents_system_get_state(ctx);
        (void)before;

        presents_letter_info_t info;
        if (presents_system_get_letter_info(ctx, &info))
        {
            if (info.letter_index >= letters_seen)
            {
                letters_seen = info.letter_index + 1;
            }
        }

        /* Check if "II" was drawn. */
        if (after == PRESENTS_STATE_WAIT || after == PRESENTS_STATE_SHINE)
        {
            presents_ii_info_t ii;
            if (presents_system_get_ii_info(ctx, &ii))
            {
                break;
            }
        }
        f++;
    }

    assert_int_equal(letters_seen, PRESENTS_TITLE_LETTERS);

    presents_ii_info_t ii;
    int has_ii = presents_system_get_ii_info(ctx, &ii);
    assert_int_equal(has_ii, 1);
    assert_int_equal(ii.width, PRESENTS_LETTER_I_W);
    presents_system_destroy(ctx);
}

/* =========================================================================
 * Group 4: Sparkle animation
 * ========================================================================= */

static void test_sparkle_starts_after_ii(void **state)
{
    (void)state;
    presents_system_t *ctx = make_ctx();
    presents_system_begin(ctx, 0);

    int f = advance_to_state(ctx, PRESENTS_STATE_SHINE, 0, 10000);
    assert_int_equal(presents_system_get_state(ctx), PRESENTS_STATE_SHINE);
    assert_true(f < 10000);
    presents_system_destroy(ctx);
}

static void test_sparkle_produces_ping_sound(void **state)
{
    (void)state;
    presents_system_t *ctx = make_ctx();
    presents_system_begin(ctx, 0);

    int f = advance_to_state(ctx, PRESENTS_STATE_SHINE, 0, 10000);
    /* First update in SHINE should produce "ping" sound. */
    presents_system_update(ctx, f + 1);

    presents_sound_t snd = presents_system_get_sound(ctx);
    assert_non_null(snd.name);
    assert_string_equal(snd.name, "ping");
    presents_system_destroy(ctx);
}

/* =========================================================================
 * Group 5: Typewriter text
 * ========================================================================= */

static void test_typewriter_reaches_special_text(void **state)
{
    (void)state;
    presents_system_t *ctx = make_ctx();
    presents_system_begin(ctx, 0);

    int f = advance_to_state(ctx, PRESENTS_STATE_SPECIAL_TEXT1, 0, 15000);
    assert_int_equal(presents_system_get_state(ctx), PRESENTS_STATE_SPECIAL_TEXT1);
    assert_true(f < 15000);
    presents_system_destroy(ctx);
}

static void test_typewriter_reveals_chars_incrementally(void **state)
{
    (void)state;
    presents_system_t *ctx = make_ctx();
    presents_system_begin(ctx, 0);

    int f = advance_to_state(ctx, PRESENTS_STATE_SPECIAL_TEXT1, 0, 15000);

    /* Advance a few frames to reveal some characters. */
    for (int i = 0; i < 200; i++)
    {
        presents_system_update(ctx, f + i);
    }

    presents_typewriter_info_t info;
    int ok = presents_system_get_typewriter_info(ctx, 0, &info);
    assert_int_equal(ok, 1);
    assert_true(info.chars_visible > 0);
    assert_non_null(info.text);
    /* Should contain the nickname. */
    assert_non_null(strstr(info.text, "TestPlayer"));
    presents_system_destroy(ctx);
}

static void test_typewriter_key_sound(void **state)
{
    (void)state;
    presents_system_t *ctx = make_ctx();
    presents_system_begin(ctx, 0);

    int f = advance_to_state(ctx, PRESENTS_STATE_SPECIAL_TEXT1, 0, 15000);

    /* First update initializes, subsequent ones produce "key" sounds. */
    int found_key = 0;
    for (int i = 0; i < 200; i++)
    {
        presents_system_update(ctx, f + i);
        presents_sound_t snd = presents_system_get_sound(ctx);
        if (snd.name && strcmp(snd.name, "key") == 0)
        {
            found_key = 1;
            break;
        }
    }
    assert_int_equal(found_key, 1);
    presents_system_destroy(ctx);
}

/* =========================================================================
 * Group 6: Curtain wipe
 * ========================================================================= */

static void test_wipe_reaches_clear_state(void **state)
{
    (void)state;
    presents_system_t *ctx = make_ctx();
    presents_system_begin(ctx, 0);

    int f = advance_to_state(ctx, PRESENTS_STATE_CLEAR, 0, 50000);
    assert_int_equal(presents_system_get_state(ctx), PRESENTS_STATE_CLEAR);
    assert_true(f < 50000);
    presents_system_destroy(ctx);
}

static void test_wipe_converges_to_center(void **state)
{
    (void)state;
    presents_system_t *ctx = make_ctx();
    presents_system_begin(ctx, 0);

    int f = advance_to_state(ctx, PRESENTS_STATE_CLEAR, 0, 50000);

    /* Run wipe until complete. */
    presents_wipe_info_t wipe;
    for (int i = 0; i < 5000; i++)
    {
        presents_system_update(ctx, f + i);
        presents_system_get_wipe_info(ctx, &wipe);
        if (wipe.complete)
        {
            break;
        }
    }
    assert_int_equal(wipe.complete, 1);
    assert_true(wipe.top_y > PRESENTS_TOTAL_HEIGHT / 2);
    presents_system_destroy(ctx);
}

/* =========================================================================
 * Group 7: Skip and null safety
 * ========================================================================= */

static void test_skip_jumps_to_finish(void **state)
{
    (void)state;
    g_finished = 0;
    presents_system_t *ctx = make_ctx();
    presents_system_begin(ctx, 0);
    presents_system_update(ctx, 0);

    /* Skip. */
    presents_system_skip(ctx, 1);
    /* Need to advance past the WAIT to reach FINISH. */
    for (int i = 1; i < 10; i++)
    {
        presents_system_update(ctx, i);
        if (presents_system_is_finished(ctx))
        {
            break;
        }
    }
    assert_int_equal(presents_system_is_finished(ctx), 1);
    assert_int_equal(g_finished, 1);
    presents_system_destroy(ctx);
}

static void test_full_sequence_completes(void **state)
{
    (void)state;
    g_finished = 0;
    presents_system_t *ctx = make_ctx();
    presents_system_begin(ctx, 0);

    int f = 0;
    for (f = 0; f < 100000; f++)
    {
        int active = presents_system_update(ctx, f);
        if (!active)
        {
            break;
        }
    }
    assert_int_equal(presents_system_is_finished(ctx), 1);
    assert_int_equal(g_finished, 1);
    assert_true(f < 100000);
    presents_system_destroy(ctx);
}

static void test_null_safety(void **state)
{
    (void)state;
    /* All functions should handle NULL gracefully. */
    presents_system_destroy(NULL);
    presents_system_begin(NULL, 0);
    assert_int_equal(presents_system_update(NULL, 0), 0);
    presents_system_skip(NULL, 0);
    assert_int_equal(presents_system_get_state(NULL), PRESENTS_STATE_NONE);
    assert_int_equal(presents_system_is_finished(NULL), 1);
    assert_int_equal(presents_system_get_active_typewriter_line(NULL), -1);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_begin_sets_flag_state),
        cmocka_unit_test(test_update_returns_active),
        /* Group 2: Flag state */
        cmocka_unit_test(test_flag_produces_sound),
        cmocka_unit_test(test_flag_info_positions),
        /* Group 3: Letter stamping */
        cmocka_unit_test(test_flag_transitions_through_text_to_letters),
        cmocka_unit_test(test_first_letter_is_x),
        cmocka_unit_test(test_all_six_letters_stamped),
        /* Group 4: Sparkle animation */
        cmocka_unit_test(test_sparkle_starts_after_ii),
        cmocka_unit_test(test_sparkle_produces_ping_sound),
        /* Group 5: Typewriter text */
        cmocka_unit_test(test_typewriter_reaches_special_text),
        cmocka_unit_test(test_typewriter_reveals_chars_incrementally),
        cmocka_unit_test(test_typewriter_key_sound),
        /* Group 6: Curtain wipe */
        cmocka_unit_test(test_wipe_reaches_clear_state),
        cmocka_unit_test(test_wipe_converges_to_center),
        /* Group 7: Skip and null safety */
        cmocka_unit_test(test_skip_jumps_to_finish),
        cmocka_unit_test(test_full_sequence_completes),
        cmocka_unit_test(test_null_safety),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
