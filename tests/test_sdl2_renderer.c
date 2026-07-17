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
    assert_string_equal(cfg.title, "- XBoing II -");
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

/* TC-05: Default scale=2 is clamped to fit the display's usable
 * bounds.  On a display tall enough for 1440px: 1150×1440 (2x).
 * On a smaller display: the window is shrunk to fit while preserving
 * the 575:720 aspect ratio.  May not be an exact integer multiple. */
static void test_window_size_default_scale(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    sdl2_renderer_t *ctx = sdl2_renderer_create(&cfg);

    assert_non_null(ctx);

    int w = 0, h = 0;
    sdl2_renderer_get_window_size(ctx, &w, &h);

    /* At least 1x logical size. */
    assert_true(w >= SDL2R_LOGICAL_WIDTH);
    assert_true(h >= SDL2R_LOGICAL_HEIGHT);

    /* Aspect ratio preserved within ±1 pixel of rounding. */
    int expected_w = h * SDL2R_LOGICAL_WIDTH / SDL2R_LOGICAL_HEIGHT;
    assert_true(w >= expected_w - 1 && w <= expected_w + 1);

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
 * Group 5a: Minimize/iconify (mission m-2026-07-17-014, bead 2z1)
 *
 * sdl2_renderer_minimize is the SDL2 equivalent of the original's
 * XIconifyWindow (original/main.c:856), bound to the I key -- it must
 * not affect fullscreen state (see test_iconify_does_not_toggle_fullscreen
 * in tests/test_keybindings.c for the input-layer regression). This
 * group covers the renderer-level function in isolation: NULL safety
 * and a clean call under the dummy video driver.  Neither the
 * SDL_WINDOW_MINIMIZED flag nor SDL_GetError() are asserted here --
 * both are driver-dependent (SDL's dummy video driver doesn't implement
 * window minimize and records "That operation is not supported" even
 * though the call itself is handled gracefully) (per sjl). */

/* TC-22: minimize(NULL) is a safe no-op. */
static void test_minimize_null_safe(void **state)
{
    (void)state;
    sdl2_renderer_minimize(NULL); /* Must not crash. */
}

/* TC-23: minimize on a valid context returns cleanly (smoke test) and
 * does not flip fullscreen state -- the bug this function replaced
 * (sdl2_renderer_toggle_fullscreen) would have made this assertion fail. */
static void test_minimize_no_crash_no_fullscreen_change(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    sdl2_renderer_t *ctx = sdl2_renderer_create(&cfg);
    assert_non_null(ctx);
    assert_false(sdl2_renderer_is_fullscreen(ctx));

    sdl2_renderer_minimize(ctx);

    assert_false(sdl2_renderer_is_fullscreen(ctx));

    sdl2_renderer_destroy(ctx);
}

/* =========================================================================
 * Group 5b: Mouse grab (-grab)
 * ========================================================================= */

/* Not grabbed by default; set true confines, set false releases.
 * SDL's dummy video driver tracks the grab flag, so this runs headless. */
static void test_mouse_grab_set_and_release(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    sdl2_renderer_t *ctx = sdl2_renderer_create(&cfg);
    assert_non_null(ctx);

    assert_false(sdl2_renderer_is_mouse_grabbed(ctx));

    sdl2_renderer_set_mouse_grab(ctx, true);
    assert_true(sdl2_renderer_is_mouse_grabbed(ctx));

    sdl2_renderer_set_mouse_grab(ctx, false);
    assert_false(sdl2_renderer_is_mouse_grabbed(ctx));

    sdl2_renderer_destroy(ctx);
}

/* NULL-safe — no crash, returns false. */
static void test_mouse_grab_null_safe(void **state)
{
    (void)state;
    sdl2_renderer_set_mouse_grab(NULL, true); /* must not crash */
    assert_false(sdl2_renderer_is_mouse_grabbed(NULL));
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
 * Group 9: sdl2_renderer_set_logical_width (bead xboing-di8)
 * ========================================================================= */

/* TC-15: Widen logical width; height unchanged. */
static void test_set_logical_width_widen(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    sdl2_renderer_t *ctx = sdl2_renderer_create(&cfg);
    assert_non_null(ctx);

    int rc = sdl2_renderer_set_logical_width(ctx, SDL2R_LOGICAL_WIDTH + 120);
    assert_int_equal(rc, 0);

    int w = 0, h = 0;
    sdl2_renderer_get_logical_size(ctx, &w, &h);
    assert_int_equal(w, SDL2R_LOGICAL_WIDTH + 120);
    assert_int_equal(h, SDL2R_LOGICAL_HEIGHT);

    sdl2_renderer_destroy(ctx);
}

/* TC-16: Windowed widen grows the physical window width proportionally,
 * preserving the logical:physical scale (aspect-ratio-preserving formula
 * from docs/specs/2026-07-11-editor-window-width.md). */
static void test_set_logical_width_windowed_grows_window(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    sdl2_renderer_t *ctx = sdl2_renderer_create(&cfg);
    assert_non_null(ctx);

    int win_h_before = 0;
    int win_w_before = 0;
    sdl2_renderer_get_window_size(ctx, &win_w_before, &win_h_before);

    int new_logical_w = SDL2R_LOGICAL_WIDTH + 120;
    int rc = sdl2_renderer_set_logical_width(ctx, new_logical_w);
    assert_int_equal(rc, 0);

    int win_w_after = 0;
    int win_h_after = 0;
    sdl2_renderer_get_window_size(ctx, &win_w_after, &win_h_after);

    assert_int_equal(win_h_after, win_h_before);
    int expected_w = (new_logical_w * win_h_before) / SDL2R_LOGICAL_HEIGHT;
    assert_int_equal(win_w_after, expected_w);
    assert_true(win_w_after > win_w_before);

    sdl2_renderer_destroy(ctx);
}

/* TC-17: Widen then restore to the original width — window width returns
 * to the EXACT original value (self-inverting formula). */
static void test_set_logical_width_widen_then_restore_exact(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    sdl2_renderer_t *ctx = sdl2_renderer_create(&cfg);
    assert_non_null(ctx);

    int win_w_orig = 0;
    int win_h_orig = 0;
    sdl2_renderer_get_window_size(ctx, &win_w_orig, &win_h_orig);

    int rc = sdl2_renderer_set_logical_width(ctx, SDL2R_LOGICAL_WIDTH + 120);
    assert_int_equal(rc, 0);

    rc = sdl2_renderer_set_logical_width(ctx, SDL2R_LOGICAL_WIDTH);
    assert_int_equal(rc, 0);

    int win_w_restored = 0;
    int win_h_restored = 0;
    sdl2_renderer_get_window_size(ctx, &win_w_restored, &win_h_restored);

    assert_int_equal(win_w_restored, win_w_orig);
    assert_int_equal(win_h_restored, win_h_orig);

    int w = 0, h = 0;
    sdl2_renderer_get_logical_size(ctx, &w, &h);
    assert_int_equal(w, SDL2R_LOGICAL_WIDTH);
    assert_int_equal(h, SDL2R_LOGICAL_HEIGHT);

    sdl2_renderer_destroy(ctx);
}

/* TC-18: No-op call with the current width leaves window size byte-identical. */
static void test_set_logical_width_noop_same_width(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    sdl2_renderer_t *ctx = sdl2_renderer_create(&cfg);
    assert_non_null(ctx);

    int win_w_before = 0;
    int win_h_before = 0;
    sdl2_renderer_get_window_size(ctx, &win_w_before, &win_h_before);

    int rc = sdl2_renderer_set_logical_width(ctx, SDL2R_LOGICAL_WIDTH);
    assert_int_equal(rc, 0);

    int win_w_after = 0;
    int win_h_after = 0;
    sdl2_renderer_get_window_size(ctx, &win_w_after, &win_h_after);

    assert_int_equal(win_w_after, win_w_before);
    assert_int_equal(win_h_after, win_h_before);

    sdl2_renderer_destroy(ctx);
}

/* TC-19: NULL ctx returns -1. */
static void test_set_logical_width_null_ctx(void **state)
{
    (void)state;
    int rc = sdl2_renderer_set_logical_width(NULL, SDL2R_LOGICAL_WIDTH);
    assert_int_equal(rc, -1);
}

/* TC-20: Non-positive width (zero and negative) returns -1, ctx unchanged. */
static void test_set_logical_width_non_positive(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    sdl2_renderer_t *ctx = sdl2_renderer_create(&cfg);
    assert_non_null(ctx);

    int rc = sdl2_renderer_set_logical_width(ctx, 0);
    assert_int_equal(rc, -1);

    rc = sdl2_renderer_set_logical_width(ctx, -50);
    assert_int_equal(rc, -1);

    int w = 0, h = 0;
    sdl2_renderer_get_logical_size(ctx, &w, &h);
    assert_int_equal(w, SDL2R_LOGICAL_WIDTH);
    assert_int_equal(h, SDL2R_LOGICAL_HEIGHT);

    sdl2_renderer_destroy(ctx);
}

/* TC-21: Fullscreen widen changes only logical size, not physical window
 * size (spec: "Fullscreen mode: the physical window already spans the
 * display and cannot grow, so only the logical width changes"). */
static void test_set_logical_width_fullscreen_no_window_change(void **state)
{
    (void)state;
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    cfg.fullscreen = true;
    sdl2_renderer_t *ctx = sdl2_renderer_create(&cfg);
    assert_non_null(ctx);

    int win_w_before = 0;
    int win_h_before = 0;
    sdl2_renderer_get_window_size(ctx, &win_w_before, &win_h_before);

    int rc = sdl2_renderer_set_logical_width(ctx, SDL2R_LOGICAL_WIDTH + 120);
    assert_int_equal(rc, 0);

    int win_w_after = 0;
    int win_h_after = 0;
    sdl2_renderer_get_window_size(ctx, &win_w_after, &win_h_after);

    assert_int_equal(win_w_after, win_w_before);
    assert_int_equal(win_h_after, win_h_before);

    int w = 0, h = 0;
    sdl2_renderer_get_logical_size(ctx, &w, &h);
    assert_int_equal(w, SDL2R_LOGICAL_WIDTH + 120);
    assert_int_equal(h, SDL2R_LOGICAL_HEIGHT);

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
        /* Group 5a: Minimize/iconify */
        cmocka_unit_test(test_minimize_null_safe),
        cmocka_unit_test(test_minimize_no_crash_no_fullscreen_change),
        cmocka_unit_test(test_mouse_grab_set_and_release),
        cmocka_unit_test(test_mouse_grab_null_safe),
        /* Group 6: Null safety */
        cmocka_unit_test(test_destroy_null),
        /* Group 7: Invalid config */
        cmocka_unit_test(test_create_zero_scale),
        cmocka_unit_test(test_create_negative_dimensions),
        cmocka_unit_test(test_create_null_config),
        /* Group 8: Window title */
        cmocka_unit_test(test_custom_title),
        /* Group 9: sdl2_renderer_set_logical_width */
        cmocka_unit_test(test_set_logical_width_widen),
        cmocka_unit_test(test_set_logical_width_windowed_grows_window),
        cmocka_unit_test(test_set_logical_width_widen_then_restore_exact),
        cmocka_unit_test(test_set_logical_width_noop_same_width),
        cmocka_unit_test(test_set_logical_width_null_ctx),
        cmocka_unit_test(test_set_logical_width_non_positive),
        cmocka_unit_test(test_set_logical_width_fullscreen_no_window_change),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
