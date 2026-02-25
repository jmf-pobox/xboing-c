/*
 * test_sdl2_texture.c — Unit tests for SDL2 texture loading and caching.
 *
 * Bead xboing-oaa.2: SDL2 texture loading and caching.
 *
 * Uses SDL_VIDEODRIVER=dummy (set in CMakeLists.txt test properties)
 * and real PNGs from assets/images/ (WORKING_DIRECTORY = source root).
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cmocka.h>

#include "sdl2_renderer.h"
#include "sdl2_texture.h"

/* =========================================================================
 * Helpers
 * ========================================================================= */

static sdl2_renderer_t *create_renderer(void)
{
    sdl2_renderer_config_t cfg = sdl2_renderer_config_defaults();
    return sdl2_renderer_create(&cfg);
}

/*
 * Empty directory path for tests that need a cache with no pre-loaded
 * textures.  Created once at group setup, removed at group teardown.
 */
static char empty_dir[] = "/tmp/xboing_test_XXXXXX";
static bool empty_dir_created = false;

static int group_setup(void **state)
{
    (void)state;
    if (mkdtemp(empty_dir) == NULL)
    {
        return -1;
    }
    empty_dir_created = true;
    return 0;
}

static int group_teardown(void **state)
{
    (void)state;
    if (empty_dir_created)
    {
        rmdir(empty_dir);
    }
    return 0;
}

/* =========================================================================
 * Group 1: Configuration defaults
 * ========================================================================= */

/* TC-01: Default config has expected values. */
static void test_config_defaults(void **state)
{
    (void)state;
    sdl2_texture_config_t cfg = sdl2_texture_config_defaults();

    assert_null(cfg.renderer);
    assert_non_null(cfg.base_dir);
    assert_string_equal(cfg.base_dir, "assets/images");
}

/* =========================================================================
 * Group 2: Error handling
 * ========================================================================= */

/* TC-02: create(NULL) returns NULL with ERR_NULL_ARG. */
static void test_create_null_config(void **state)
{
    (void)state;
    sdl2_texture_status_t status = SDL2T_OK;
    sdl2_texture_t *ctx = sdl2_texture_create(NULL, &status);

    assert_null(ctx);
    assert_int_equal(status, SDL2T_ERR_NULL_ARG);
}

/* TC-03: create() with NULL renderer returns ERR_RENDERER. */
static void test_create_null_renderer(void **state)
{
    (void)state;
    sdl2_texture_config_t cfg = sdl2_texture_config_defaults();
    sdl2_texture_status_t status = SDL2T_OK;
    sdl2_texture_t *ctx = sdl2_texture_create(&cfg, &status);

    assert_null(ctx);
    assert_int_equal(status, SDL2T_ERR_RENDERER);
}

/* TC-04: create() with nonexistent directory returns ERR_SCAN_FAILED. */
static void test_create_bad_directory(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);

    sdl2_texture_config_t cfg = sdl2_texture_config_defaults();
    cfg.renderer = sdl2_renderer_get(rctx);
    cfg.base_dir = "/nonexistent/path/that/does/not/exist";

    sdl2_texture_status_t status = SDL2T_OK;
    sdl2_texture_t *ctx = sdl2_texture_create(&cfg, &status);

    assert_null(ctx);
    assert_int_equal(status, SDL2T_ERR_SCAN_FAILED);

    sdl2_renderer_destroy(rctx);
}

/* TC-05: destroy(NULL) is a safe no-op. */
static void test_destroy_null(void **state)
{
    (void)state;
    sdl2_texture_destroy(NULL); /* Must not crash. */
}

/* TC-06: get() with NULL args returns ERR_NULL_ARG. */
static void test_get_null_args(void **state)
{
    (void)state;
    sdl2_texture_info_t info;
    assert_int_equal(sdl2_texture_get(NULL, "key", &info), SDL2T_ERR_NULL_ARG);
}

/* TC-07: get() on a valid but empty cache returns ERR_NOT_FOUND. */
static void test_get_nonexistent_key(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);

    /* Create a cache pointing at an empty temp dir. */
    sdl2_texture_config_t cfg = sdl2_texture_config_defaults();
    cfg.renderer = sdl2_renderer_get(rctx);
    cfg.base_dir = empty_dir;

    sdl2_texture_status_t status = SDL2T_OK;
    sdl2_texture_t *ctx = sdl2_texture_create(&cfg, &status);
    assert_non_null(ctx);
    assert_int_equal(status, SDL2T_OK);

    sdl2_texture_info_t info;
    assert_int_equal(sdl2_texture_get(ctx, "no/such/key", &info), SDL2T_ERR_NOT_FOUND);

    sdl2_texture_destroy(ctx);
    sdl2_renderer_destroy(rctx);
}

/* TC-08: load_file() with NULL args returns ERR_NULL_ARG. */
static void test_load_file_null_args(void **state)
{
    (void)state;
    assert_int_equal(sdl2_texture_load_file(NULL, "key", "path"), SDL2T_ERR_NULL_ARG);
}

/* =========================================================================
 * Group 3: Single-file loading via load_file()
 * ========================================================================= */

/* TC-09: load_file() + get() returns correct texture info. */
static void test_load_file_and_get(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);

    sdl2_texture_config_t cfg = sdl2_texture_config_defaults();
    cfg.renderer = sdl2_renderer_get(rctx);
    cfg.base_dir = empty_dir;

    sdl2_texture_status_t status;
    sdl2_texture_t *ctx = sdl2_texture_create(&cfg, &status);
    assert_non_null(ctx);

    status = sdl2_texture_load_file(ctx, "balls/ball1", "assets/images/balls/ball1.png");
    assert_int_equal(status, SDL2T_OK);

    sdl2_texture_info_t info;
    assert_int_equal(sdl2_texture_get(ctx, "balls/ball1", &info), SDL2T_OK);
    assert_non_null(info.texture);
    assert_int_equal(info.width, 20);
    assert_int_equal(info.height, 19);

    sdl2_texture_destroy(ctx);
    sdl2_renderer_destroy(rctx);
}

/* TC-10: count() increments after load_file(). */
static void test_load_file_count(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);

    sdl2_texture_config_t cfg = sdl2_texture_config_defaults();
    cfg.renderer = sdl2_renderer_get(rctx);
    cfg.base_dir = empty_dir;

    sdl2_texture_status_t status;
    sdl2_texture_t *ctx = sdl2_texture_create(&cfg, &status);
    assert_non_null(ctx);
    assert_int_equal(sdl2_texture_count(ctx), 0);

    sdl2_texture_load_file(ctx, "test1", "assets/images/balls/ball1.png");
    assert_int_equal(sdl2_texture_count(ctx), 1);

    sdl2_texture_load_file(ctx, "test2", "assets/images/guns/bullet.png");
    assert_int_equal(sdl2_texture_count(ctx), 2);

    sdl2_texture_destroy(ctx);
    sdl2_renderer_destroy(rctx);
}

/* TC-11: load_file() with same key replaces existing entry. */
static void test_load_file_replace(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);

    sdl2_texture_config_t cfg = sdl2_texture_config_defaults();
    cfg.renderer = sdl2_renderer_get(rctx);
    cfg.base_dir = empty_dir;

    sdl2_texture_status_t status;
    sdl2_texture_t *ctx = sdl2_texture_create(&cfg, &status);
    assert_non_null(ctx);

    /* Load ball1 under key "sprite". */
    sdl2_texture_load_file(ctx, "sprite", "assets/images/balls/ball1.png");
    assert_int_equal(sdl2_texture_count(ctx), 1);

    sdl2_texture_info_t info;
    sdl2_texture_get(ctx, "sprite", &info);
    assert_int_equal(info.width, 20);

    /* Replace with floppy under same key. */
    sdl2_texture_load_file(ctx, "sprite", "assets/images/floppy.png");
    assert_int_equal(sdl2_texture_count(ctx), 1); /* count unchanged */

    sdl2_texture_get(ctx, "sprite", &info);
    assert_int_equal(info.width, 32); /* now floppy's dimensions */

    sdl2_texture_destroy(ctx);
    sdl2_renderer_destroy(rctx);
}

/* TC-12: load_file() with key too long returns ERR_KEY_TOO_LONG. */
static void test_load_file_key_too_long(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);

    sdl2_texture_config_t cfg = sdl2_texture_config_defaults();
    cfg.renderer = sdl2_renderer_get(rctx);
    cfg.base_dir = empty_dir;

    sdl2_texture_status_t status;
    sdl2_texture_t *ctx = sdl2_texture_create(&cfg, &status);
    assert_non_null(ctx);

    /* Build a key that is exactly SDL2T_MAX_KEY_LEN + 1 characters. */
    char long_key[SDL2T_MAX_KEY_LEN + 2];
    memset(long_key, 'x', SDL2T_MAX_KEY_LEN + 1);
    long_key[SDL2T_MAX_KEY_LEN + 1] = '\0';

    status = sdl2_texture_load_file(ctx, long_key, "assets/images/balls/ball1.png");
    assert_int_equal(status, SDL2T_ERR_KEY_TOO_LONG);

    sdl2_texture_destroy(ctx);
    sdl2_renderer_destroy(rctx);
}

/* =========================================================================
 * Group 4: Bulk loading via create()
 * ========================================================================= */

/* TC-13: Loading guns/ subdirectory yields 2 entries. */
static void test_bulk_load_subdir(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);

    sdl2_texture_config_t cfg = sdl2_texture_config_defaults();
    cfg.renderer = sdl2_renderer_get(rctx);
    cfg.base_dir = "assets/images/guns";

    sdl2_texture_status_t status;
    sdl2_texture_t *ctx = sdl2_texture_create(&cfg, &status);
    assert_non_null(ctx);
    assert_int_equal(status, SDL2T_OK);
    assert_int_equal(sdl2_texture_count(ctx), 2);

    /* Keys should be flat (no subdir prefix since guns/ IS the base). */
    sdl2_texture_info_t info;
    assert_int_equal(sdl2_texture_get(ctx, "bullet", &info), SDL2T_OK);
    assert_int_equal(info.width, 7);
    assert_int_equal(info.height, 16);

    assert_int_equal(sdl2_texture_get(ctx, "tink", &info), SDL2T_OK);

    sdl2_texture_destroy(ctx);
    sdl2_renderer_destroy(rctx);
}

/* TC-14: Keys for nested subdirectories include the relative path. */
static void test_bulk_load_nested_keys(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);

    sdl2_texture_config_t cfg = sdl2_texture_config_defaults();
    cfg.renderer = sdl2_renderer_get(rctx);
    cfg.base_dir = "assets/images";

    sdl2_texture_status_t status;
    sdl2_texture_t *ctx = sdl2_texture_create(&cfg, &status);
    assert_non_null(ctx);
    assert_int_equal(status, SDL2T_OK);

    /* Subdirectory keys should include the relative path. */
    sdl2_texture_info_t info;
    assert_int_equal(sdl2_texture_get(ctx, "balls/ball1", &info), SDL2T_OK);
    assert_int_equal(sdl2_texture_get(ctx, "guns/bullet", &info), SDL2T_OK);
    assert_int_equal(sdl2_texture_get(ctx, "paddle/padmed", &info), SDL2T_OK);

    sdl2_texture_destroy(ctx);
    sdl2_renderer_destroy(rctx);
}

/* TC-15: Root-level files get flat keys (no directory prefix). */
static void test_bulk_load_root_flat_keys(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);

    sdl2_texture_config_t cfg = sdl2_texture_config_defaults();
    cfg.renderer = sdl2_renderer_get(rctx);
    cfg.base_dir = "assets/images";

    sdl2_texture_status_t status;
    sdl2_texture_t *ctx = sdl2_texture_create(&cfg, &status);
    assert_non_null(ctx);

    /* Root-level PNGs should have flat keys. */
    sdl2_texture_info_t info;
    assert_int_equal(sdl2_texture_get(ctx, "floppy", &info), SDL2T_OK);
    assert_int_equal(info.width, 32);
    assert_int_equal(info.height, 32);

    sdl2_texture_destroy(ctx);
    sdl2_renderer_destroy(rctx);
}

/* =========================================================================
 * Group 5: Integration — full assets/images/ load
 * ========================================================================= */

/* TC-16: Full load of assets/images/ gets all 180 textures. */
static void test_full_load_count(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);

    sdl2_texture_config_t cfg = sdl2_texture_config_defaults();
    cfg.renderer = sdl2_renderer_get(rctx);

    sdl2_texture_status_t status;
    sdl2_texture_t *ctx = sdl2_texture_create(&cfg, &status);
    assert_non_null(ctx);
    assert_int_equal(status, SDL2T_OK);
    assert_int_equal(sdl2_texture_count(ctx), 180);

    sdl2_texture_destroy(ctx);
    sdl2_renderer_destroy(rctx);
}

/* TC-17: Spot-check known textures for correct dimensions. */
static void test_full_load_spot_check(void **state)
{
    (void)state;
    sdl2_renderer_t *rctx = create_renderer();
    assert_non_null(rctx);

    sdl2_texture_config_t cfg = sdl2_texture_config_defaults();
    cfg.renderer = sdl2_renderer_get(rctx);

    sdl2_texture_status_t status;
    sdl2_texture_t *ctx = sdl2_texture_create(&cfg, &status);
    assert_non_null(ctx);

    sdl2_texture_info_t info;

    /* balls/ball1: 20x19 */
    assert_int_equal(sdl2_texture_get(ctx, "balls/ball1", &info), SDL2T_OK);
    assert_non_null(info.texture);
    assert_int_equal(info.width, 20);
    assert_int_equal(info.height, 19);

    /* blocks/redblk: 40x20 */
    assert_int_equal(sdl2_texture_get(ctx, "blocks/redblk", &info), SDL2T_OK);
    assert_int_equal(info.width, 40);
    assert_int_equal(info.height, 20);

    /* paddle/padmed: 50x15 */
    assert_int_equal(sdl2_texture_get(ctx, "paddle/padmed", &info), SDL2T_OK);
    assert_int_equal(info.width, 50);
    assert_int_equal(info.height, 15);

    /* guns/bullet: 7x16 */
    assert_int_equal(sdl2_texture_get(ctx, "guns/bullet", &info), SDL2T_OK);
    assert_int_equal(info.width, 7);
    assert_int_equal(info.height, 16);

    sdl2_texture_destroy(ctx);
    sdl2_renderer_destroy(rctx);
}

/* =========================================================================
 * Group 6: Status strings
 * ========================================================================= */

/* TC-18: All status codes have non-NULL string representations. */
static void test_status_strings(void **state)
{
    (void)state;
    sdl2_texture_status_t codes[] = {SDL2T_OK,
                                     SDL2T_ERR_NULL_ARG,
                                     SDL2T_ERR_RENDERER,
                                     SDL2T_ERR_IMG_INIT,
                                     SDL2T_ERR_NOT_FOUND,
                                     SDL2T_ERR_LOAD_FAILED,
                                     SDL2T_ERR_CACHE_FULL,
                                     SDL2T_ERR_KEY_TOO_LONG,
                                     SDL2T_ERR_SCAN_FAILED};

    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
    {
        const char *str = sdl2_texture_status_string(codes[i]);
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
        cmocka_unit_test(test_get_null_args),
        cmocka_unit_test(test_get_nonexistent_key),
        cmocka_unit_test(test_load_file_null_args),
        /* Group 3: Single-file load */
        cmocka_unit_test(test_load_file_and_get),
        cmocka_unit_test(test_load_file_count),
        cmocka_unit_test(test_load_file_replace),
        cmocka_unit_test(test_load_file_key_too_long),
        /* Group 4: Bulk loading */
        cmocka_unit_test(test_bulk_load_subdir),
        cmocka_unit_test(test_bulk_load_nested_keys),
        cmocka_unit_test(test_bulk_load_root_flat_keys),
        /* Group 5: Integration */
        cmocka_unit_test(test_full_load_count),
        cmocka_unit_test(test_full_load_spot_check),
        /* Group 6: Status strings */
        cmocka_unit_test(test_status_strings),
    };

    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
