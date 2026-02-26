/*
 * test_sdl2_font.c — Unit tests for SDL2 TTF font rendering.
 *
 * Bead xboing-oaa.3: SDL2 TTF font rendering.
 *
 * Uses SDL_VIDEODRIVER=dummy (set in CMakeLists.txt test properties)
 * and real TTFs from assets/fonts/ (WORKING_DIRECTORY = source root).
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include <cmocka.h>

#include "sdl2_font.h"
#include "sdl2_renderer.h"

/* =========================================================================
 * Helpers
 * ========================================================================= */

static sdl2_renderer_t *create_renderer(void)
{
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    return sdl2_renderer_create(&cfg);
}

/*
 * Create a font context using default config and the test renderer.
 * Returns NULL if either renderer or font creation fails.
 */
static sdl2_font_t *create_font_ctx(const sdl2_renderer_t *rctx)
{
    sdl2_font_config_t cfg = sdl2_font_config_defaults();
    cfg.renderer = sdl2_renderer_get(rctx);
    sdl2_font_status_t status;
    return sdl2_font_create(&cfg, &status);
}

/* =========================================================================
 * Group 1: Configuration defaults
 * ========================================================================= */

/* TC-01: Default config has expected values. */
static void test_config_defaults(void **state)
{
    (void)state;
    sdl2_font_config_t cfg = sdl2_font_config_defaults();

    assert_null(cfg.renderer);
    assert_non_null(cfg.font_dir);
    assert_string_equal(cfg.font_dir, SDL2F_DEFAULT_FONT_DIR);
}

/* =========================================================================
 * Group 2: Error handling
 * ========================================================================= */

/* TC-02: create(NULL) returns NULL with ERR_NULL_ARG. */
static void test_create_null_config(void **state)
{
    (void)state;
    sdl2_font_status_t status = SDL2F_OK;
    sdl2_font_t *ctx = sdl2_font_create(NULL, &status);

    assert_null(ctx);
    assert_int_equal(status, SDL2F_ERR_NULL_ARG);
}

/* TC-03: create() with NULL renderer returns ERR_RENDERER. */
static void test_create_null_renderer(void **state)
{
    (void)state;
    sdl2_font_config_t cfg = sdl2_font_config_defaults();
    sdl2_font_status_t status = SDL2F_OK;
    sdl2_font_t *ctx = sdl2_font_create(&cfg, &status);

    assert_null(ctx);
    assert_int_equal(status, SDL2F_ERR_RENDERER);
}

/* TC-04: create() with nonexistent font directory returns ERR_FONT_LOAD. */
static void test_create_bad_directory(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);

    sdl2_font_config_t cfg = sdl2_font_config_defaults();
    cfg.renderer = sdl2_renderer_get(rctx);
    cfg.font_dir = "/nonexistent/path/that/does/not/exist";

    sdl2_font_status_t status = SDL2F_OK;
    sdl2_font_t *ctx = sdl2_font_create(&cfg, &status);

    assert_null(ctx);
    assert_int_equal(status, SDL2F_ERR_FONT_LOAD);

    sdl2_renderer_destroy(rctx);
}

/* TC-05: destroy(NULL) is a safe no-op. */
static void test_destroy_null(void **state)
{
    (void)state;
    sdl2_font_destroy(NULL); /* Must not crash. */
}

/* TC-06: draw(NULL ctx) returns ERR_NULL_ARG. */
static void test_draw_null_ctx(void **state)
{
    (void)state;
    SDL_Color white = {255, 255, 255, 255};
    assert_int_equal(sdl2_font_draw(NULL, SDL2F_FONT_TEXT, "hello", 0, 0, white),
                     SDL2F_ERR_NULL_ARG);
}

/* TC-07: draw() with invalid font_id returns ERR_INVALID_FONT_ID. */
static void test_draw_invalid_font_id(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);
    sdl2_font_t *fctx = create_font_ctx(rctx);
    assert_non_null(fctx);

    SDL_Color white = {255, 255, 255, 255};
    assert_int_equal(sdl2_font_draw(fctx, SDL2F_FONT_COUNT, "hello", 0, 0, white),
                     SDL2F_ERR_INVALID_FONT_ID);
    assert_int_equal(sdl2_font_draw(fctx, (sdl2_font_id_t)-1, "hello", 0, 0, white),
                     SDL2F_ERR_INVALID_FONT_ID);

    sdl2_font_destroy(fctx);
    sdl2_renderer_destroy(rctx);
}

/* TC-08: measure(NULL) returns ERR_NULL_ARG. */
static void test_measure_null_args(void **state)
{
    (void)state;
    sdl2_font_metrics_t metrics;
    assert_int_equal(sdl2_font_measure(NULL, SDL2F_FONT_TEXT, "hello", &metrics),
                     SDL2F_ERR_NULL_ARG);
}

/* =========================================================================
 * Group 3: Lifecycle
 * ========================================================================= */

/* TC-09: Create and destroy succeeds (ASan verifies no leaks). */
static void test_create_destroy(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);

    sdl2_font_config_t cfg = sdl2_font_config_defaults();
    cfg.renderer = sdl2_renderer_get(rctx);

    sdl2_font_status_t status;
    sdl2_font_t *fctx = sdl2_font_create(&cfg, &status);
    assert_non_null(fctx);
    assert_int_equal(status, SDL2F_OK);

    sdl2_font_destroy(fctx);
    sdl2_renderer_destroy(rctx);
}

/* TC-10: All four slots have non-zero line heights after creation. */
static void test_all_slots_loaded(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);
    sdl2_font_t *fctx = create_font_ctx(rctx);
    assert_non_null(fctx);

    for (int i = 0; i < SDL2F_FONT_COUNT; i++)
    {
        assert_true(sdl2_font_line_height(fctx, (sdl2_font_id_t)i) > 0);
    }

    sdl2_font_destroy(fctx);
    sdl2_renderer_destroy(rctx);
}

/* =========================================================================
 * Group 4: Text measurement
 * ========================================================================= */

/* TC-11: Measuring non-empty text returns positive width and height. */
static void test_measure_positive(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);
    sdl2_font_t *fctx = create_font_ctx(rctx);
    assert_non_null(fctx);

    sdl2_font_metrics_t metrics;
    sdl2_font_status_t st = sdl2_font_measure(fctx, SDL2F_FONT_TEXT, "Hello World", &metrics);
    assert_int_equal(st, SDL2F_OK);
    assert_true(metrics.width > 0);
    assert_true(metrics.height > 0);

    sdl2_font_destroy(fctx);
    sdl2_renderer_destroy(rctx);
}

/* TC-12: Measuring empty string returns width=0. */
static void test_measure_empty_string(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);
    sdl2_font_t *fctx = create_font_ctx(rctx);
    assert_non_null(fctx);

    sdl2_font_metrics_t metrics;
    sdl2_font_status_t st = sdl2_font_measure(fctx, SDL2F_FONT_TEXT, "", &metrics);
    assert_int_equal(st, SDL2F_OK);
    assert_int_equal(metrics.width, 0);
    assert_true(metrics.height > 0);

    sdl2_font_destroy(fctx);
    sdl2_renderer_destroy(rctx);
}

/* TC-13: Title font (24pt bold) is taller than copy font (12pt regular). */
static void test_title_taller_than_copy(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);
    sdl2_font_t *fctx = create_font_ctx(rctx);
    assert_non_null(fctx);

    int title_h = sdl2_font_line_height(fctx, SDL2F_FONT_TITLE);
    int copy_h = sdl2_font_line_height(fctx, SDL2F_FONT_COPY);
    assert_true(title_h > copy_h);

    sdl2_font_destroy(fctx);
    sdl2_renderer_destroy(rctx);
}

/* TC-14: line_height returns positive values for all fonts. */
static void test_line_height_positive(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);
    sdl2_font_t *fctx = create_font_ctx(rctx);
    assert_non_null(fctx);

    assert_true(sdl2_font_line_height(fctx, SDL2F_FONT_TITLE) > 0);
    assert_true(sdl2_font_line_height(fctx, SDL2F_FONT_TEXT) > 0);
    assert_true(sdl2_font_line_height(fctx, SDL2F_FONT_DATA) > 0);
    assert_true(sdl2_font_line_height(fctx, SDL2F_FONT_COPY) > 0);

    sdl2_font_destroy(fctx);
    sdl2_renderer_destroy(rctx);
}

/* TC-15: Font sizes are ordered: title > text > data > copy. */
static void test_size_ordering(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);
    sdl2_font_t *fctx = create_font_ctx(rctx);
    assert_non_null(fctx);

    int h_title = sdl2_font_line_height(fctx, SDL2F_FONT_TITLE);
    int h_text = sdl2_font_line_height(fctx, SDL2F_FONT_TEXT);
    int h_data = sdl2_font_line_height(fctx, SDL2F_FONT_DATA);
    int h_copy = sdl2_font_line_height(fctx, SDL2F_FONT_COPY);

    assert_true(h_title > h_text);
    assert_true(h_text > h_data);
    assert_true(h_data > h_copy);

    sdl2_font_destroy(fctx);
    sdl2_renderer_destroy(rctx);
}

/* =========================================================================
 * Group 5: Drawing smoke tests
 * ========================================================================= */

/* TC-16: draw() returns OK for normal text. */
static void test_draw_ok(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);
    sdl2_font_t *fctx = create_font_ctx(rctx);
    assert_non_null(fctx);

    SDL_Color white = {255, 255, 255, 255};
    assert_int_equal(sdl2_font_draw(fctx, SDL2F_FONT_TEXT, "Hello", 10, 20, white), SDL2F_OK);

    sdl2_font_destroy(fctx);
    sdl2_renderer_destroy(rctx);
}

/* TC-17: draw_shadow() returns OK. */
static void test_draw_shadow_ok(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);
    sdl2_font_t *fctx = create_font_ctx(rctx);
    assert_non_null(fctx);

    SDL_Color red = {255, 0, 0, 255};
    assert_int_equal(sdl2_font_draw_shadow(fctx, SDL2F_FONT_TITLE, "Score", 10, 20, red),
                     SDL2F_OK);

    sdl2_font_destroy(fctx);
    sdl2_renderer_destroy(rctx);
}

/* TC-18: draw_shadow_centred() returns OK. */
static void test_draw_shadow_centred_ok(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);
    sdl2_font_t *fctx = create_font_ctx(rctx);
    assert_non_null(fctx);

    SDL_Color green = {0, 255, 0, 255};
    assert_int_equal(
        sdl2_font_draw_shadow_centred(fctx, SDL2F_FONT_TEXT, "Centred", 100, green, 575),
        SDL2F_OK);

    sdl2_font_destroy(fctx);
    sdl2_renderer_destroy(rctx);
}

/* TC-19: Drawing empty string returns OK (no-op). */
static void test_draw_empty_string(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);
    sdl2_font_t *fctx = create_font_ctx(rctx);
    assert_non_null(fctx);

    SDL_Color white = {255, 255, 255, 255};
    assert_int_equal(sdl2_font_draw(fctx, SDL2F_FONT_TEXT, "", 0, 0, white), SDL2F_OK);
    assert_int_equal(sdl2_font_draw_shadow(fctx, SDL2F_FONT_TEXT, "", 0, 0, white), SDL2F_OK);
    assert_int_equal(sdl2_font_draw_shadow_centred(fctx, SDL2F_FONT_TEXT, "", 0, white, 575),
                     SDL2F_OK);

    sdl2_font_destroy(fctx);
    sdl2_renderer_destroy(rctx);
}

/* =========================================================================
 * Group 6: Status strings
 * ========================================================================= */

/* TC-20: All status codes have non-NULL string representations. */
static void test_status_strings(void **state)
{
    (void)state;
    sdl2_font_status_t codes[] = {SDL2F_OK,           SDL2F_ERR_NULL_ARG,     SDL2F_ERR_RENDERER,
                                   SDL2F_ERR_TTF_INIT, SDL2F_ERR_FONT_LOAD,    SDL2F_ERR_RENDER_FAILED,
                                   SDL2F_ERR_INVALID_FONT_ID};

    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
    {
        const char *str = sdl2_font_status_string(codes[i]);
        assert_non_null(str);
        assert_true(strlen(str) > 0);
    }
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Config defaults */
        cmocka_unit_test(test_config_defaults),
        /* Group 2: Error handling */
        cmocka_unit_test(test_create_null_config),
        cmocka_unit_test(test_create_null_renderer),
        cmocka_unit_test(test_create_bad_directory),
        cmocka_unit_test(test_destroy_null),
        cmocka_unit_test(test_draw_null_ctx),
        cmocka_unit_test(test_draw_invalid_font_id),
        cmocka_unit_test(test_measure_null_args),
        /* Group 3: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_all_slots_loaded),
        /* Group 4: Text measurement */
        cmocka_unit_test(test_measure_positive),
        cmocka_unit_test(test_measure_empty_string),
        cmocka_unit_test(test_title_taller_than_copy),
        cmocka_unit_test(test_line_height_positive),
        cmocka_unit_test(test_size_ordering),
        /* Group 5: Drawing smoke */
        cmocka_unit_test(test_draw_ok),
        cmocka_unit_test(test_draw_shadow_ok),
        cmocka_unit_test(test_draw_shadow_centred_ok),
        cmocka_unit_test(test_draw_empty_string),
        /* Group 6: Status strings */
        cmocka_unit_test(test_status_strings),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
