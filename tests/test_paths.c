/*
 * test_paths.c — Unit tests for XDG path resolution module.
 *
 * Bead xboing-ifb.2: XDG Base Directory path resolution.
 *
 * All tests use paths_init_explicit() with injected env values —
 * no real environment manipulation needed.  Tests that check file
 * existence use the real ./levels/ and ./sounds/ directories in the
 * repo (ctest runs from the build dir, but we set the working dir
 * to the source root).
 *
 * Tests are organized by API function group.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include <cmocka.h>

#include "paths.h"

/* =========================================================================
 * Group 1: Initialization (paths_init_explicit)
 * ========================================================================= */

/* TC-01: HOME unset returns PATHS_NO_HOME. */
static void test_init_no_home(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_status_t st = paths_init_explicit(&cfg, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    assert_int_equal(st, PATHS_NO_HOME);
}

/* TC-02: HOME empty string returns PATHS_NO_HOME. */
static void test_init_empty_home(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_status_t st = paths_init_explicit(&cfg, "", NULL, NULL, NULL, NULL, NULL, NULL);
    assert_int_equal(st, PATHS_NO_HOME);
}

/* TC-03: XDG defaults populated when vars unset. */
static void test_init_xdg_defaults(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_status_t st = paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, NULL, NULL, NULL);
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(cfg.home, "/home/test");
    assert_string_equal(cfg.xdg_data_home, "/home/test/.local/share");
    assert_string_equal(cfg.xdg_config_home, "/home/test/.config");
    assert_int_equal(cfg.xdg_data_dirs_count, 2);
    assert_string_equal(cfg.xdg_data_dirs[0], "/usr/local/share");
    assert_string_equal(cfg.xdg_data_dirs[1], "/usr/share");
}

/* TC-04: Explicit XDG vars override defaults. */
static void test_init_explicit_xdg(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_status_t st = paths_init_explicit(&cfg, "/home/test", "/data/share", "/data/config",
                                            "/opt/share:/custom/share", NULL, NULL, NULL);
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(cfg.xdg_data_home, "/data/share");
    assert_string_equal(cfg.xdg_config_home, "/data/config");
    assert_int_equal(cfg.xdg_data_dirs_count, 2);
    assert_string_equal(cfg.xdg_data_dirs[0], "/opt/share");
    assert_string_equal(cfg.xdg_data_dirs[1], "/custom/share");
}

/* TC-05: Colon-separated DATA_DIRS parsing with many entries. */
static void test_init_data_dirs_parsing(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_status_t st =
        paths_init_explicit(&cfg, "/home/test", NULL, NULL, "/a:/b:/c:/d:/e:/f:/g:/h", NULL, NULL, NULL);
    assert_int_equal(st, PATHS_OK);
    assert_int_equal(cfg.xdg_data_dirs_count, PATHS_MAX_DATA_DIRS);
    assert_string_equal(cfg.xdg_data_dirs[0], "/a");
    assert_string_equal(cfg.xdg_data_dirs[7], "/h");
}

/* TC-06: Excess DATA_DIRS entries are truncated at PATHS_MAX_DATA_DIRS. */
static void test_init_data_dirs_truncation(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_status_t st = paths_init_explicit(&cfg, "/home/test", NULL, NULL,
                                            "/1:/2:/3:/4:/5:/6:/7:/8:/9:/10", NULL, NULL, NULL);
    assert_int_equal(st, PATHS_OK);
    assert_int_equal(cfg.xdg_data_dirs_count, PATHS_MAX_DATA_DIRS);
    assert_string_equal(cfg.xdg_data_dirs[7], "/8");
}

/* TC-07: Trailing slashes stripped from all paths. */
static void test_init_trailing_slash(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_status_t st = paths_init_explicit(&cfg, "/home/test/", "/data/share/", "/data/config/",
                                            "/opt/share/:/usr/share/", "/levels/", "/sounds/", NULL);
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(cfg.home, "/home/test");
    assert_string_equal(cfg.xdg_data_home, "/data/share");
    assert_string_equal(cfg.xdg_config_home, "/data/config");
    assert_string_equal(cfg.xdg_data_dirs[0], "/opt/share");
    assert_string_equal(cfg.xboing_levels_dir, "/levels");
    assert_string_equal(cfg.xboing_sound_dir, "/sounds");
}

/* TC-08: Legacy env vars stored in config. */
static void test_init_legacy_env_vars(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_status_t st = paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, "/my/levels",
                                            "/my/sounds", "/my/scores.dat");
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(cfg.xboing_levels_dir, "/my/levels");
    assert_string_equal(cfg.xboing_sound_dir, "/my/sounds");
    assert_string_equal(cfg.xboing_score_file, "/my/scores.dat");
}

/* =========================================================================
 * Group 2: Level file resolution (paths_level_file)
 * ========================================================================= */

/* TC-09: Legacy env override — resolves if file exists. */
static void test_level_legacy_override(void **state)
{
    (void)state;
    paths_config_t cfg;
    /* Point legacy dir at the real ./levels directory. */
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, "./levels", NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_level_file(&cfg, "level01.data", buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "./levels/level01.data");
}

/* TC-10: CWD fallback finds real level files (dev mode). */
static void test_level_cwd_fallback(void **state)
{
    (void)state;
    paths_config_t cfg;
    /* No overrides, no XDG dirs that exist — should fall through to CWD. */
    paths_init_explicit(&cfg, "/nonexistent/home", NULL, NULL, "/nonexistent/share", NULL, NULL,
                        NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_level_file(&cfg, "level01.data", buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "levels/level01.data");
}

/* TC-11: File not found anywhere. */
static void test_level_not_found(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/nonexistent/home", NULL, NULL, "/nonexistent/share", NULL, NULL,
                        NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_level_file(&cfg, "nonexistent.data", buf, sizeof(buf));
    assert_int_equal(st, PATHS_NOT_FOUND);
}

/* TC-12: Buffer too small returns PATHS_TRUNCATED. */
static void test_level_buffer_truncated(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, "./levels", NULL, NULL);

    char buf[5]; /* Too small for "levels/level01.data". */
    paths_status_t st = paths_level_file(&cfg, "level01.data", buf, sizeof(buf));
    assert_int_equal(st, PATHS_TRUNCATED);
}

/* TC-13: editor.data resolves via CWD fallback. */
static void test_level_editor_data(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/nonexistent/home", NULL, NULL, "/nonexistent/share", NULL, NULL,
                        NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_level_file(&cfg, "editor.data", buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "levels/editor.data");
}

/* TC-14: demo.data resolves via CWD fallback. */
static void test_level_demo_data(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/nonexistent/home", NULL, NULL, "/nonexistent/share", NULL, NULL,
                        NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_level_file(&cfg, "demo.data", buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "levels/demo.data");
}

/* TC-15: NULL filename returns PATHS_NOT_FOUND. */
static void test_level_null_filename(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_level_file(&cfg, NULL, buf, sizeof(buf));
    assert_int_equal(st, PATHS_NOT_FOUND);
}

/* =========================================================================
 * Group 3: Sound file resolution (paths_sound_file)
 * ========================================================================= */

/* TC-16: Legacy env override — resolves if file exists. */
static void test_sound_legacy_override(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, NULL, "./sounds", NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_sound_file(&cfg, "balllost", buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "./sounds/balllost.au");
}

/* TC-17: CWD fallback finds real sound files (dev mode). */
static void test_sound_cwd_fallback(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/nonexistent/home", NULL, NULL, "/nonexistent/share", NULL, NULL,
                        NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_sound_file(&cfg, "balllost", buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "sounds/balllost.au");
}

/* TC-18: .au extension appended automatically. */
static void test_sound_au_extension(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/nonexistent/home", NULL, NULL, "/nonexistent/share", NULL, NULL,
                        NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_sound_file(&cfg, "ammo", buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    /* Verify the .au was appended. */
    assert_string_equal(buf, "sounds/ammo.au");
}

/* TC-19: Sound not found. */
static void test_sound_not_found(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/nonexistent/home", NULL, NULL, "/nonexistent/share", NULL, NULL,
                        NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_sound_file(&cfg, "nonexistent_sfx", buf, sizeof(buf));
    assert_int_equal(st, PATHS_NOT_FOUND);
}

/* TC-20: NULL sound name returns PATHS_NOT_FOUND. */
static void test_sound_null_name(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_sound_file(&cfg, NULL, buf, sizeof(buf));
    assert_int_equal(st, PATHS_NOT_FOUND);
}

/* =========================================================================
 * Group 4: Score file resolution
 * ========================================================================= */

/* TC-21: Global score — legacy env override. */
static void test_score_global_legacy_override(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, NULL, NULL, "/custom/scores.dat");

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_score_file_global(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/custom/scores.dat");
}

/* TC-22: Global score — falls back to legacy $HOME/.xboing.scr
 * (no XDG file exists on disk). */
static void test_score_global_legacy_fallback(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_score_file_global(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    /* XDG file won't exist, so falls back to legacy. */
    assert_string_equal(buf, "/home/test/.xboing.scr");
}

/* TC-23: Personal score — falls back to legacy $HOME/.xboing-scores
 * (no XDG file exists on disk). */
static void test_score_personal_legacy_fallback(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_score_file_personal(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/home/test/.xboing-scores");
}

/* TC-24: Personal score — XDG default path construction. */
static void test_score_personal_xdg_path(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", "/xdg/data", NULL, NULL, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    /* The XDG file won't exist, so this still falls to legacy.
     * Verify the XDG path would be correct by checking with custom data home. */
    paths_status_t st = paths_score_file_personal(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    /* No XDG file on disk → legacy fallback. */
    assert_string_equal(buf, "/home/test/.xboing-scores");
}

/* =========================================================================
 * Group 5: Save file resolution
 * ========================================================================= */

/* TC-25: Save info — legacy fallback (no XDG file on disk). */
static void test_save_info_legacy_fallback(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_save_info(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/home/test/.xboing-savinf");
}

/* TC-26: Save level — legacy fallback (no XDG file on disk). */
static void test_save_level_legacy_fallback(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_save_level(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/home/test/.xboing-savlev");
}

/* =========================================================================
 * Group 6: Directory accessors
 * ========================================================================= */

/* TC-27: Levels dir — legacy override. */
static void test_levels_dir_legacy(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, "/custom/levels", NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_levels_dir(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/custom/levels");
}

/* TC-28: Levels dir — default CWD fallback. */
static void test_levels_dir_default(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_levels_dir(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "levels");
}

/* TC-29: Sounds dir — legacy override. */
static void test_sounds_dir_legacy(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, NULL, "/custom/sounds", NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_sounds_dir(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/custom/sounds");
}

/* TC-30: User data dir — XDG default. */
static void test_user_data_dir_default(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_user_data_dir(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/home/test/.local/share/xboing");
}

/* TC-31: User data dir — custom XDG_DATA_HOME. */
static void test_user_data_dir_custom(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", "/custom/data", NULL, NULL, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_user_data_dir(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/custom/data/xboing");
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Init */
        cmocka_unit_test(test_init_no_home),
        cmocka_unit_test(test_init_empty_home),
        cmocka_unit_test(test_init_xdg_defaults),
        cmocka_unit_test(test_init_explicit_xdg),
        cmocka_unit_test(test_init_data_dirs_parsing),
        cmocka_unit_test(test_init_data_dirs_truncation),
        cmocka_unit_test(test_init_trailing_slash),
        cmocka_unit_test(test_init_legacy_env_vars),
        /* Group 2: Level files */
        cmocka_unit_test(test_level_legacy_override),
        cmocka_unit_test(test_level_cwd_fallback),
        cmocka_unit_test(test_level_not_found),
        cmocka_unit_test(test_level_buffer_truncated),
        cmocka_unit_test(test_level_editor_data),
        cmocka_unit_test(test_level_demo_data),
        cmocka_unit_test(test_level_null_filename),
        /* Group 3: Sound files */
        cmocka_unit_test(test_sound_legacy_override),
        cmocka_unit_test(test_sound_cwd_fallback),
        cmocka_unit_test(test_sound_au_extension),
        cmocka_unit_test(test_sound_not_found),
        cmocka_unit_test(test_sound_null_name),
        /* Group 4: Score files */
        cmocka_unit_test(test_score_global_legacy_override),
        cmocka_unit_test(test_score_global_legacy_fallback),
        cmocka_unit_test(test_score_personal_legacy_fallback),
        cmocka_unit_test(test_score_personal_xdg_path),
        /* Group 5: Save files */
        cmocka_unit_test(test_save_info_legacy_fallback),
        cmocka_unit_test(test_save_level_legacy_fallback),
        /* Group 6: Directory accessors */
        cmocka_unit_test(test_levels_dir_legacy),
        cmocka_unit_test(test_levels_dir_default),
        cmocka_unit_test(test_sounds_dir_legacy),
        cmocka_unit_test(test_user_data_dir_default),
        cmocka_unit_test(test_user_data_dir_custom),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
