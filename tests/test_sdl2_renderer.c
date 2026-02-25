/*
 * test_sdl2_renderer.c — Unit tests for SDL2 window/renderer lifecycle.
 *
 * Bead xboing-oaa.1: SDL2 window and renderer creation.
 *
 * Uses SDL_VIDEODRIVER=dummy (set in CMakeLists.txt test properties)
 * so tests run in CI without a real display.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include <cmocka.h>

#include "sdl2_renderer.h"

/* =========================================================================
 * Group 1: Configuration defaults
 * ========================================================================= */

/* TC-01: Default config has expected values. */
static void test_config_defaults(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();

    assert_int_equal(cfg.logical_width, SDL2R_LOGICAL_WIDTH);
    assert_int_equal(cfg.logical_height, SDL2R_LOGICAL_HEIGHT);
    assert_int_equal(cfg.scale, SDL2R_DEFAULT_SCALE);
    assert_false(cfg.fullscreen);
    assert_true(cfg.vsync);
    assert_non_null(cfg.title);
    assert_string_equal(cfg.title, "XBoing");
}

/* =========================================================================
 * Group 2: Lifecycle
 * ========================================================================= */

/* TC-02: Create and destroy without leaks (ASan verifies). */
static void test_create_destroy(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    sdl2_renderer_t *ctx = sdl2_renderer_create(&cfg);

    assert_non_null(ctx);
    sdl2_renderer_destroy(ctx);
}

/* TC-03: get() and get_window() return non-NULL after creation. */
static void test_create_returns_valid_handles(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    sdl2_renderer_t *ctx = sdl2_renderer_create(&cfg);

    assert_non_null(ctx);
    assert_non_null(sdl2_renderer_get(ctx));
    assert_non_null(sdl2_renderer_get_window(ctx));

    sdl2_renderer_destroy(ctx);
}

/* =========================================================================
 * Group 3: Logical and window size
 * ========================================================================= */

/* TC-04: Logical size returns 575x720. */
static void test_logical_size(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    sdl2_renderer_t *ctx = sdl2_renderer_create(&cfg);

    assert_non_null(ctx);

    int w = 0, h = 0;
    sdl2_renderer_get_logical_size(ctx, &w, &h);
    assert_int_equal(w, 575);
    assert_int_equal(h, 720);

    sdl2_renderer_destroy(ctx);
}

/* TC-05: Default 2x scale gives 1150x1440 physical window. */
static void test_window_size_default_scale(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    sdl2_renderer_t *ctx = sdl2_renderer_create(&cfg);

    assert_non_null(ctx);

    int w = 0, h = 0;
    sdl2_renderer_get_window_size(ctx, &w, &h);
    assert_int_equal(w, 1150);
    assert_int_equal(h, 1440);

    sdl2_renderer_destroy(ctx);
}

/* TC-06: 1x scale gives 575x720 physical window. */
static void test_custom_scale(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    cfg.scale = 1;
    sdl2_renderer_t *ctx = sdl2_renderer_create(&cfg);

    assert_non_null(ctx);

    int w = 0, h = 0;
    sdl2_renderer_get_window_size(ctx, &w, &h);
    assert_int_equal(w, 575);
    assert_int_equal(h, 720);

    sdl2_renderer_destroy(ctx);
}

/* =========================================================================
 * Group 4: Render cycle
 * ========================================================================= */

/* TC-07: Clear + present doesn't crash (smoke test). */
static void test_clear_present_cycle(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    sdl2_renderer_t *ctx = sdl2_renderer_create(&cfg);

    assert_non_null(ctx);

    sdl2_renderer_clear(ctx);
    sdl2_renderer_present(ctx);

    sdl2_renderer_destroy(ctx);
}

/* =========================================================================
 * Group 5: Fullscreen
 * ========================================================================= */

/* TC-08: Toggle fullscreen changes state and toggle back reverts. */
static void test_fullscreen_toggle(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    sdl2_renderer_t *ctx = sdl2_renderer_create(&cfg);

    assert_non_null(ctx);
    assert_false(sdl2_renderer_is_fullscreen(ctx));

    bool result = sdl2_renderer_toggle_fullscreen(ctx);
    assert_true(result);
    assert_true(sdl2_renderer_is_fullscreen(ctx));

    result = sdl2_renderer_toggle_fullscreen(ctx);
    assert_false(result);
    assert_false(sdl2_renderer_is_fullscreen(ctx));

    sdl2_renderer_destroy(ctx);
}

/* TC-09: Not fullscreen by default. */
static void test_is_fullscreen_initial(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    sdl2_renderer_t *ctx = sdl2_renderer_create(&cfg);

    assert_non_null(ctx);
    assert_false(sdl2_renderer_is_fullscreen(ctx));

    sdl2_renderer_destroy(ctx);
}

/* =========================================================================
 * Group 6: Null safety
 * ========================================================================= */

/* TC-10: destroy(NULL) is a safe no-op. */
static void test_destroy_null(void **state)
{
    (void)state;
    sdl2_renderer_destroy(NULL); /* Must not crash. */
}

/* =========================================================================
 * Group 7: Invalid config
 * ========================================================================= */

/* TC-11: Zero scale returns NULL. */
static void test_create_zero_scale(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    cfg.scale = 0;
    assert_null(sdl2_renderer_create(&cfg));
}

/* TC-12: Negative dimensions return NULL. */
static void test_create_negative_dimensions(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    cfg.logical_width = -1;
    assert_null(sdl2_renderer_create(&cfg));

    cfg = sdl2_renderer_config_defaults();
    cfg.logical_height = -1;
    assert_null(sdl2_renderer_create(&cfg));
}

/* TC-13: NULL config returns NULL. */
static void test_create_null_config(void **state)
{
    (void)state;
    assert_null(sdl2_renderer_create(NULL));
}

/* =========================================================================
 * Group 8: Window title
 * ========================================================================= */

/* TC-14: Custom title is reflected in SDL_GetWindowTitle. */
static void test_custom_title(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    cfg.title = "Test Window";
    sdl2_renderer_t *ctx = sdl2_renderer_create(&cfg);

    assert_non_null(ctx);

    const char *title = SDL_GetWindowTitle(sdl2_renderer_get_window(ctx));
    assert_string_equal(title, "Test Window");

    sdl2_renderer_destroy(ctx);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Config defaults */
        cmocka_unit_test(test_config_defaults),
        /* Group 2: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_create_returns_valid_handles),
        /* Group 3: Sizes */
        cmocka_unit_test(test_logical_size),
        cmocka_unit_test(test_window_size_default_scale),
        cmocka_unit_test(test_custom_scale),
        /* Group 4: Render cycle */
        cmocka_unit_test(test_clear_present_cycle),
        /* Group 5: Fullscreen */
        cmocka_unit_test(test_fullscreen_toggle),
        cmocka_unit_test(test_is_fullscreen_initial),
        /* Group 6: Null safety */
        cmocka_unit_test(test_destroy_null),
        /* Group 7: Invalid config */
        cmocka_unit_test(test_create_zero_scale),
        cmocka_unit_test(test_create_negative_dimensions),
        cmocka_unit_test(test_create_null_config),
        /* Group 8: Window title */
        cmocka_unit_test(test_custom_title),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
