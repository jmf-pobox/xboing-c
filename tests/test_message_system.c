/*
 * test_message_system.c — Tests for the pure C message display system.
 *
 * 5 groups:
 *   1. Lifecycle (2 tests)
 *   2. Set and query (3 tests)
 *   3. Auto-clear behavior (3 tests)
 *   4. Default message (3 tests)
 *   5. Null safety (1 test)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

/* cmocka must come after setjmp.h / stdarg.h / stddef.h */
#include <cmocka.h>

#include "message_system.h"

/* =========================================================================
 * Group 1: Lifecycle
 * ========================================================================= */

static void test_create_destroy(void **state)
{
    (void)state;
    message_system_t *ctx = message_system_create();
    assert_non_null(ctx);
    assert_string_equal(message_system_get_text(ctx), "");
    assert_string_equal(message_system_get_default(ctx), "");
    assert_int_equal(message_system_text_changed(ctx), 0);
    message_system_destroy(ctx);
}

static void test_initial_update_no_change(void **state)
{
    (void)state;
    message_system_t *ctx = message_system_create();
    int changed = message_system_update(ctx, 0);
    assert_int_equal(changed, 0);
    assert_int_equal(message_system_text_changed(ctx), 0);
    message_system_destroy(ctx);
}

/* =========================================================================
 * Group 2: Set and query
 * ========================================================================= */

static void test_set_persistent(void **state)
{
    (void)state;
    message_system_t *ctx = message_system_create();
    message_system_set(ctx, "Hello, World!", 0, 100);
    assert_string_equal(message_system_get_text(ctx), "Hello, World!");
    assert_int_equal(message_system_text_changed(ctx), 1);

    /* No auto-clear: message persists after many frames. */
    message_system_update(ctx, 5000);
    assert_string_equal(message_system_get_text(ctx), "Hello, World!");
    message_system_destroy(ctx);
}

static void test_set_overwrites(void **state)
{
    (void)state;
    message_system_t *ctx = message_system_create();
    message_system_set(ctx, "First", 0, 0);
    assert_string_equal(message_system_get_text(ctx), "First");

    message_system_set(ctx, "Second", 0, 10);
    assert_string_equal(message_system_get_text(ctx), "Second");
    message_system_destroy(ctx);
}

static void test_set_null_text(void **state)
{
    (void)state;
    message_system_t *ctx = message_system_create();
    message_system_set(ctx, "Something", 0, 0);
    message_system_set(ctx, NULL, 0, 10);
    assert_string_equal(message_system_get_text(ctx), "");
    message_system_destroy(ctx);
}

/* =========================================================================
 * Group 3: Auto-clear behavior
 * ========================================================================= */

static void test_auto_clear_fires(void **state)
{
    (void)state;
    message_system_t *ctx = message_system_create();
    message_system_set(ctx, "Bonus!", 1, 100);

    /* Before clear frame: message stays. */
    message_system_update(ctx, 100 + MESSAGE_CLEAR_DELAY - 1);
    assert_string_equal(message_system_get_text(ctx), "Bonus!");

    /* At clear frame: reverts to default (empty). */
    message_system_update(ctx, 100 + MESSAGE_CLEAR_DELAY);
    assert_string_equal(message_system_get_text(ctx), "");
    assert_int_equal(message_system_text_changed(ctx), 1);
    message_system_destroy(ctx);
}

static void test_auto_clear_reverts_to_default(void **state)
{
    (void)state;
    message_system_t *ctx = message_system_create();
    message_system_set_default(ctx, "- Level 5 -");
    message_system_set(ctx, "Extra Life!", 1, 200);

    /* After clear delay, should show the default. */
    message_system_update(ctx, 200 + MESSAGE_CLEAR_DELAY);
    assert_string_equal(message_system_get_text(ctx), "- Level 5 -");
    assert_int_equal(message_system_text_changed(ctx), 1);
    message_system_destroy(ctx);
}

static void test_auto_clear_only_fires_once(void **state)
{
    (void)state;
    message_system_t *ctx = message_system_create();
    message_system_set_default(ctx, "- Default -");
    message_system_set(ctx, "Alert!", 1, 0);

    /* Fire the clear. */
    message_system_update(ctx, MESSAGE_CLEAR_DELAY);
    assert_int_equal(message_system_text_changed(ctx), 1);

    /* Next frame should not report changed. */
    message_system_update(ctx, MESSAGE_CLEAR_DELAY + 1);
    assert_int_equal(message_system_text_changed(ctx), 0);
    message_system_destroy(ctx);
}

/* =========================================================================
 * Group 4: Default message
 * ========================================================================= */

static void test_set_default(void **state)
{
    (void)state;
    message_system_t *ctx = message_system_create();
    message_system_set_default(ctx, "- Pirate -");
    assert_string_equal(message_system_get_default(ctx), "- Pirate -");
    message_system_destroy(ctx);
}

static void test_set_default_null(void **state)
{
    (void)state;
    message_system_t *ctx = message_system_create();
    message_system_set_default(ctx, "- Something -");
    message_system_set_default(ctx, NULL);
    assert_string_equal(message_system_get_default(ctx), "");
    message_system_destroy(ctx);
}

static void test_default_does_not_affect_current(void **state)
{
    (void)state;
    message_system_t *ctx = message_system_create();
    message_system_set(ctx, "Current", 0, 0);
    message_system_set_default(ctx, "- Level 1 -");

    /* Current should still be "Current", not the default. */
    assert_string_equal(message_system_get_text(ctx), "Current");
    message_system_destroy(ctx);
}

/* =========================================================================
 * Group 5: Null safety
 * ========================================================================= */

static void test_null_safety(void **state)
{
    (void)state;
    message_system_destroy(NULL);
    message_system_set(NULL, "test", 0, 0);
    message_system_set_default(NULL, "test");
    assert_int_equal(message_system_update(NULL, 0), 0);
    assert_string_equal(message_system_get_text(NULL), "");
    assert_string_equal(message_system_get_default(NULL), "");
    assert_int_equal(message_system_text_changed(NULL), 0);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_initial_update_no_change),
        /* Group 2: Set and query */
        cmocka_unit_test(test_set_persistent),
        cmocka_unit_test(test_set_overwrites),
        cmocka_unit_test(test_set_null_text),
        /* Group 3: Auto-clear behavior */
        cmocka_unit_test(test_auto_clear_fires),
        cmocka_unit_test(test_auto_clear_reverts_to_default),
        cmocka_unit_test(test_auto_clear_only_fires_once),
        /* Group 4: Default message */
        cmocka_unit_test(test_set_default),
        cmocka_unit_test(test_set_default_null),
        cmocka_unit_test(test_default_does_not_affect_current),
        /* Group 5: Null safety */
        cmocka_unit_test(test_null_safety),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
