/*
 * test_sdl2_cursor.c — CMocka tests for SDL2 cursor management.
 *
 * Uses SDL_VIDEODRIVER=dummy for headless CI testing.
 * Tests cursor creation, setting, name/status strings, and error handling.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include <cmocka.h>

#include "sdl2_cursor.h"

/* =========================================================================
 * Fixtures — SDL2 video subsystem and cursor context
 * ========================================================================= */

static int group_setup_sdl(void **state)
{
    (void)state;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
    {
        fprintf(stderr, "SDL_InitSubSystem(VIDEO) failed: %s\n", SDL_GetError());
        return 1;
    }
    return 0;
}

static int group_teardown_sdl(void **state)
{
    (void)state;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return 0;
}

static int setup_cursor_ctx(void **state)
{
    sdl2_cursor_status_t st;
    sdl2_cursor_t *ctx = sdl2_cursor_create(&st);
    assert_non_null(ctx);
    assert_int_equal(st, SDL2CUR_OK);
    *state = ctx;
    return 0;
}

static int teardown_cursor_ctx(void **state)
{
    sdl2_cursor_destroy((sdl2_cursor_t *)*state);
    *state = NULL;
    return 0;
}

/* =========================================================================
 * Group 1: Error handling
 * ========================================================================= */

/* TC-01: destroy(NULL) is safe. */
static void test_destroy_null(void **state)
{
    (void)state;
    sdl2_cursor_destroy(NULL); /* must not crash */
}

/* TC-02: set() with NULL ctx returns ERR_NULL_ARG. */
static void test_set_null_ctx(void **state)
{
    (void)state;
    assert_int_equal(sdl2_cursor_set(NULL, SDL2CUR_WAIT), SDL2CUR_ERR_NULL_ARG);
}

/* TC-03: set() with invalid ID returns ERR_INVALID_ID. */
static void test_set_invalid_id(void **state)
{
    sdl2_cursor_t *ctx = (sdl2_cursor_t *)*state;
    assert_int_equal(sdl2_cursor_set(ctx, SDL2CUR_COUNT), SDL2CUR_ERR_INVALID_ID);
    assert_int_equal(sdl2_cursor_set(ctx, (sdl2_cursor_id_t)-1), SDL2CUR_ERR_INVALID_ID);
}

/* TC-04: current() with NULL ctx returns SDL2CUR_COUNT. */
static void test_current_null_ctx(void **state)
{
    (void)state;
    assert_int_equal(sdl2_cursor_current(NULL), SDL2CUR_COUNT);
}

/* =========================================================================
 * Group 2: Lifecycle
 * ========================================================================= */

/* TC-05: Create and destroy (ASan verifies no leaks). */
static void test_create_destroy(void **state)
{
    (void)state;
    sdl2_cursor_status_t st;
    sdl2_cursor_t *ctx = sdl2_cursor_create(&st);
    assert_non_null(ctx);
    assert_int_equal(st, SDL2CUR_OK);
    sdl2_cursor_destroy(ctx);
}

/* TC-06: Create with NULL status pointer succeeds. */
static void test_create_null_status(void **state)
{
    (void)state;
    sdl2_cursor_t *ctx = sdl2_cursor_create(NULL);
    assert_non_null(ctx);
    sdl2_cursor_destroy(ctx);
}

/* TC-07: Initial current() returns SDL2CUR_COUNT (no cursor set yet). */
static void test_initial_current(void **state)
{
    sdl2_cursor_t *ctx = (sdl2_cursor_t *)*state;
    assert_int_equal(sdl2_cursor_current(ctx), SDL2CUR_COUNT);
}

/* =========================================================================
 * Group 3: Cursor setting
 * ========================================================================= */

/* TC-08: Set each cursor type successfully. */
static void test_set_all_types(void **state)
{
    sdl2_cursor_t *ctx = (sdl2_cursor_t *)*state;

    for (int i = 0; i < SDL2CUR_COUNT; i++)
    {
        sdl2_cursor_id_t id = (sdl2_cursor_id_t)i;
        assert_int_equal(sdl2_cursor_set(ctx, id), SDL2CUR_OK);
        assert_int_equal(sdl2_cursor_current(ctx), id);
    }
}

/* TC-09: Set NONE then visible cursor restores visibility. */
static void test_set_none_then_visible(void **state)
{
    sdl2_cursor_t *ctx = (sdl2_cursor_t *)*state;

    assert_int_equal(sdl2_cursor_set(ctx, SDL2CUR_NONE), SDL2CUR_OK);
    assert_int_equal(sdl2_cursor_current(ctx), SDL2CUR_NONE);

    assert_int_equal(sdl2_cursor_set(ctx, SDL2CUR_POINT), SDL2CUR_OK);
    assert_int_equal(sdl2_cursor_current(ctx), SDL2CUR_POINT);
}

/* TC-10: Set same cursor twice is a no-op (returns OK). */
static void test_set_same_twice(void **state)
{
    sdl2_cursor_t *ctx = (sdl2_cursor_t *)*state;

    assert_int_equal(sdl2_cursor_set(ctx, SDL2CUR_WAIT), SDL2CUR_OK);
    assert_int_equal(sdl2_cursor_set(ctx, SDL2CUR_WAIT), SDL2CUR_OK);
    assert_int_equal(sdl2_cursor_current(ctx), SDL2CUR_WAIT);
}

/* TC-11: Set each cursor and verify current tracks it. */
static void test_current_tracks_set(void **state)
{
    sdl2_cursor_t *ctx = (sdl2_cursor_t *)*state;

    sdl2_cursor_set(ctx, SDL2CUR_PLUS);
    assert_int_equal(sdl2_cursor_current(ctx), SDL2CUR_PLUS);

    sdl2_cursor_set(ctx, SDL2CUR_SKULL);
    assert_int_equal(sdl2_cursor_current(ctx), SDL2CUR_SKULL);

    sdl2_cursor_set(ctx, SDL2CUR_NONE);
    assert_int_equal(sdl2_cursor_current(ctx), SDL2CUR_NONE);
}

/* =========================================================================
 * Group 4: Name strings
 * ========================================================================= */

/* TC-12: All cursor IDs have non-NULL names. */
static void test_all_names_non_null(void **state)
{
    (void)state;
    for (int i = 0; i < SDL2CUR_COUNT; i++)
    {
        const char *name = sdl2_cursor_name((sdl2_cursor_id_t)i);
        assert_non_null(name);
        assert_true(name[0] != '\0');
    }
}

/* TC-13: Specific name values. */
static void test_name_values(void **state)
{
    (void)state;
    assert_string_equal(sdl2_cursor_name(SDL2CUR_WAIT), "wait");
    assert_string_equal(sdl2_cursor_name(SDL2CUR_PLUS), "plus");
    assert_string_equal(sdl2_cursor_name(SDL2CUR_NONE), "none");
    assert_string_equal(sdl2_cursor_name(SDL2CUR_POINT), "point");
    assert_string_equal(sdl2_cursor_name(SDL2CUR_SKULL), "skull");
}

/* TC-14: Out-of-range ID returns "unknown". */
static void test_name_out_of_range(void **state)
{
    (void)state;
    assert_string_equal(sdl2_cursor_name(SDL2CUR_COUNT), "unknown");
    assert_string_equal(sdl2_cursor_name((sdl2_cursor_id_t)99), "unknown");
}

/* =========================================================================
 * Group 5: Status strings
 * ========================================================================= */

/* TC-15: All status codes have non-NULL strings. */
static void test_all_status_strings(void **state)
{
    (void)state;
    const sdl2_cursor_status_t codes[] = {
        SDL2CUR_OK,
        SDL2CUR_ERR_NULL_ARG,
        SDL2CUR_ERR_CREATE_FAILED,
        SDL2CUR_ERR_INVALID_ID,
    };
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
    {
        const char *s = sdl2_cursor_status_string(codes[i]);
        assert_non_null(s);
        assert_true(s[0] != '\0');
    }
}

/* TC-16: Unknown status returns non-NULL. */
static void test_status_string_unknown(void **state)
{
    (void)state;
    const char *s = sdl2_cursor_status_string((sdl2_cursor_status_t)99);
    assert_non_null(s);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest error_tests[] = {
        cmocka_unit_test(test_destroy_null),
        cmocka_unit_test(test_set_null_ctx),
        cmocka_unit_test_setup_teardown(test_set_invalid_id, setup_cursor_ctx, teardown_cursor_ctx),
        cmocka_unit_test(test_current_null_ctx),
    };

    const struct CMUnitTest lifecycle_tests[] = {
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_create_null_status),
        cmocka_unit_test_setup_teardown(test_initial_current, setup_cursor_ctx,
                                        teardown_cursor_ctx),
    };

    const struct CMUnitTest set_tests[] = {
        cmocka_unit_test_setup_teardown(test_set_all_types, setup_cursor_ctx, teardown_cursor_ctx),
        cmocka_unit_test_setup_teardown(test_set_none_then_visible, setup_cursor_ctx,
                                        teardown_cursor_ctx),
        cmocka_unit_test_setup_teardown(test_set_same_twice, setup_cursor_ctx, teardown_cursor_ctx),
        cmocka_unit_test_setup_teardown(test_current_tracks_set, setup_cursor_ctx,
                                        teardown_cursor_ctx),
    };

    const struct CMUnitTest name_tests[] = {
        cmocka_unit_test(test_all_names_non_null),
        cmocka_unit_test(test_name_values),
        cmocka_unit_test(test_name_out_of_range),
    };

    const struct CMUnitTest status_tests[] = {
        cmocka_unit_test(test_all_status_strings),
        cmocka_unit_test(test_status_string_unknown),
    };

    int fail = 0;
    fail += cmocka_run_group_tests_name("error handling", error_tests, group_setup_sdl,
                                        group_teardown_sdl);
    fail += cmocka_run_group_tests_name("lifecycle", lifecycle_tests, group_setup_sdl,
                                        group_teardown_sdl);
    fail += cmocka_run_group_tests_name("cursor setting", set_tests, group_setup_sdl,
                                        group_teardown_sdl);
    fail += cmocka_run_group_tests_name("name strings", name_tests, group_setup_sdl,
                                        group_teardown_sdl);
    fail += cmocka_run_group_tests_name("status strings", status_tests, group_setup_sdl,
                                        group_teardown_sdl);
    return fail;
}
