/* mkdtemp is a POSIX.1-2001 XSI extension; _DEFAULT_SOURCE enables it on
 * glibc without conflicting with -Wpedantic. */
#define _DEFAULT_SOURCE

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cmocka.h>

#include "paths.h"

/* =========================================================================
 * Hermetic temp-tree helpers
 * ========================================================================= */

/*
 * Create a temp tree rooted at a mkdtemp directory.
 * Returns the root path in out_root (must be at least 64 bytes).
 * subdir (e.g. "xboing/levels") is created under the root.
 * Returns 0 on success, -1 on failure.
 */
static int make_temp_tree(char *out_root, size_t root_size, const char *subdir)
{
    char tmpl[] = "/tmp/test_paths_XXXXXX";
    if (mkdtemp(tmpl) == NULL)
        return -1;
    if (strlen(tmpl) + 1 > root_size)
        return -1;
    memcpy(out_root, tmpl, strlen(tmpl) + 1);

    /* Create <root>/<subdir> recursively (two levels at most). */
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", out_root, subdir);
    /* Create first component. */
    char tmp2[256];
    snprintf(tmp2, sizeof(tmp2), "%s", path);
    char *slash = strchr(tmp2 + strlen(out_root) + 1, '/');
    if (slash != NULL)
    {
        *slash = '\0';
        mkdir(tmp2, 0755);
        *slash = '/';
    }
    mkdir(path, 0755);
    return 0;
}

/*
 * Remove a two-level temp tree: <root>/xboing/<subdir> and parents.
 * Only removes what make_temp_tree created — does not recurse arbitrarily.
 */
static void remove_temp_tree(const char *root, const char *subdir)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", root, subdir);
    rmdir(path);

    /* Remove <root>/xboing if present. */
    snprintf(path, sizeof(path), "%s/xboing", root);
    rmdir(path);

    rmdir(root);
}

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

/* TC-22: Global score — defaults to XDG on fresh install (no legacy file). */
static void test_score_global_xdg_default(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/nonexistent/home", NULL, NULL, NULL, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_score_file_global(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    /* No legacy file on disk → XDG path. */
    assert_string_equal(buf, "/nonexistent/home/.local/share/xboing/scores.dat");
}

/* TC-23: Personal score — defaults to XDG on fresh install (no legacy file). */
static void test_score_personal_xdg_default(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/nonexistent/home", NULL, NULL, NULL, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_score_file_personal(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/nonexistent/home/.local/share/xboing/personal-scores.dat");
}

/* TC-24: Personal score — custom XDG_DATA_HOME reflected in path. */
static void test_score_personal_custom_xdg(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/nonexistent/home", "/xdg/data", NULL, NULL, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_score_file_personal(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/xdg/data/xboing/personal-scores.dat");
}

/* =========================================================================
 * Group 5: Save file resolution
 * ========================================================================= */

/* TC-25: Save info — defaults to XDG on fresh install (no legacy file). */
static void test_save_info_xdg_default(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/nonexistent/home", NULL, NULL, NULL, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_save_info(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/nonexistent/home/.local/share/xboing/save-info.dat");
}

/* TC-26: Save level — defaults to XDG on fresh install (no legacy file). */
static void test_save_level_xdg_default(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/nonexistent/home", NULL, NULL, NULL, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_save_level(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/nonexistent/home/.local/share/xboing/save-level.dat");
}

/* =========================================================================
 * Group 6: Directory accessors
 * ========================================================================= */

/* TC-27: Levels readable dir — legacy override. */
static void test_levels_dir_readable_legacy(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, "/custom/levels", NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_levels_dir_readable(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/custom/levels");
}

/* TC-28: Levels readable dir — CWD fallback when XDG_DATA_DIRS has no match
 *         (hermetic: use a nonexistent path so the install lookup always
 *         misses, regardless of what is installed on the host). */
static void test_levels_dir_readable_default(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, "/nonexistent/share", NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_levels_dir_readable(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "levels");
}

/* TC-29: Sounds readable dir — legacy override. */
static void test_sounds_dir_readable_legacy(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, NULL, "/custom/sounds", NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_sounds_dir_readable(&cfg, buf, sizeof(buf));
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
 * Group 7: Install data dir + read/write directory accessors
 * ========================================================================= */

/* TC-32: paths_install_data_dir — success: XDG_DATA_DIRS contains a real
 *         readable xboing/levels dir. */
static void test_install_data_dir_success(void **state)
{
    (void)state;
    char root[64];
    assert_int_equal(make_temp_tree(root, sizeof(root), "xboing/levels"), 0);

    char xdg[256];
    snprintf(xdg, sizeof(xdg), "%s", root);

    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, xdg, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_install_data_dir(&cfg, "levels", buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);

    char expected[256];
    snprintf(expected, sizeof(expected), "%s/xboing/levels", root);
    assert_string_equal(buf, expected);

    remove_temp_tree(root, "xboing/levels");
}

/* TC-33: paths_install_data_dir — not found: XDG_DATA_DIRS has no xboing subdir. */
static void test_install_data_dir_not_found(void **state)
{
    (void)state;
    char root[64];
    /* Create root dir but NOT xboing/levels inside it. */
    char tmpl[] = "/tmp/test_paths_XXXXXX";
    assert_non_null(mkdtemp(tmpl));
    memcpy(root, tmpl, strlen(tmpl) + 1);

    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, root, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_install_data_dir(&cfg, "levels", buf, sizeof(buf));
    assert_int_equal(st, PATHS_NOT_FOUND);

    rmdir(root);
}

/* TC-34: paths_install_data_dir — truncated: buf size 1 with a valid XDG
 *         entry.  Must return PATHS_TRUNCATED before opendir is reached
 *         (build_path detects truncation before the opendir call). */
static void test_install_data_dir_truncated(void **state)
{
    (void)state;
    char root[64];
    assert_int_equal(make_temp_tree(root, sizeof(root), "xboing/levels"), 0);

    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, root, NULL, NULL, NULL);

    char buf[1];
    paths_status_t st = paths_install_data_dir(&cfg, "levels", buf, sizeof(buf));
    assert_int_equal(st, PATHS_TRUNCATED);

    remove_temp_tree(root, "xboing/levels");
}

/* TC-35: paths_install_data_dir — NULL args each independently return
 *         PATHS_NOT_FOUND. */
static void test_install_data_dir_null_args(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, NULL, NULL, NULL);
    char buf[PATHS_MAX_PATH];

    assert_int_equal(paths_install_data_dir(NULL, "levels", buf, sizeof(buf)), PATHS_NOT_FOUND);
    assert_int_equal(paths_install_data_dir(&cfg, NULL, buf, sizeof(buf)), PATHS_NOT_FOUND);
    assert_int_equal(paths_install_data_dir(&cfg, "levels", NULL, sizeof(buf)), PATHS_NOT_FOUND);
    assert_int_equal(paths_install_data_dir(&cfg, "levels", buf, 0), PATHS_NOT_FOUND);
}

/* TC-36: paths_levels_dir_readable — env override returned verbatim. */
static void test_levels_dir_readable_env_override(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, "/some/path", NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_levels_dir_readable(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/some/path");
}

/* TC-37: paths_levels_dir_readable — install fallback via mkdtemp tree. */
static void test_levels_dir_readable_install_fallback(void **state)
{
    (void)state;
    char root[64];
    assert_int_equal(make_temp_tree(root, sizeof(root), "xboing/levels"), 0);

    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, root, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_levels_dir_readable(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);

    char expected[256];
    snprintf(expected, sizeof(expected), "%s/xboing/levels", root);
    assert_string_equal(buf, expected);

    remove_temp_tree(root, "xboing/levels");
}

/* TC-38: paths_levels_dir_readable — CWD fallback when XDG_DATA_DIRS points
 *         nowhere (hermetic: nonexistent path so install lookup always misses). */
static void test_levels_dir_readable_cwd_fallback(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, "/nonexistent/share", NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_levels_dir_readable(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "levels");
}

/* TC-39: paths_levels_dir_readable — truncated. */
static void test_levels_dir_readable_truncated(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, "/some/long/path/here", NULL, NULL);

    char buf[1];
    paths_status_t st = paths_levels_dir_readable(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_TRUNCATED);
}

/* TC-40: paths_levels_dir_readable — NULL args return PATHS_NOT_FOUND. */
static void test_levels_dir_readable_null_args(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, NULL, NULL, NULL);
    char buf[PATHS_MAX_PATH];

    assert_int_equal(paths_levels_dir_readable(NULL, buf, sizeof(buf)), PATHS_NOT_FOUND);
    assert_int_equal(paths_levels_dir_readable(&cfg, NULL, sizeof(buf)), PATHS_NOT_FOUND);
    assert_int_equal(paths_levels_dir_readable(&cfg, buf, 0), PATHS_NOT_FOUND);
}

/* TC-41: paths_levels_dir_writable — env override returned verbatim. */
static void test_levels_dir_writable_env_override(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, "/some/path", NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_levels_dir_writable(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/some/path");
}

/* TC-42: paths_levels_dir_writable — env override pointing at a read-only
 *         path is still returned (user assumes write-perm responsibility). */
static void test_levels_dir_writable_readonly_override(void **state)
{
    (void)state;
    paths_config_t cfg;
    /* /proc/1 is a real path that we can't write to — override still returned. */
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, "/proc/1", NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_levels_dir_writable(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/proc/1");
}

/* TC-43: paths_levels_dir_writable — explicit XDG_DATA_HOME. */
static void test_levels_dir_writable_xdg_data_home(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/user", "/data/share", NULL, NULL, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_levels_dir_writable(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/data/share/xboing/levels");
}

/* TC-44: paths_levels_dir_writable — XDG_DATA_HOME unset, falls back to
 *         $HOME/.local/share/xboing/levels. */
static void test_levels_dir_writable_home_fallback(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/user", NULL, NULL, NULL, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_levels_dir_writable(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/home/user/.local/share/xboing/levels");
}

/* TC-45: paths_levels_dir_writable — truncated. */
static void test_levels_dir_writable_truncated(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/user", "/data/share", NULL, NULL, NULL, NULL, NULL);

    char buf[1];
    paths_status_t st = paths_levels_dir_writable(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_TRUNCATED);
}

/* TC-46: paths_levels_dir_writable — NULL args return PATHS_NOT_FOUND. */
static void test_levels_dir_writable_null_args(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/user", NULL, NULL, NULL, NULL, NULL, NULL);
    char buf[PATHS_MAX_PATH];

    assert_int_equal(paths_levels_dir_writable(NULL, buf, sizeof(buf)), PATHS_NOT_FOUND);
    assert_int_equal(paths_levels_dir_writable(&cfg, NULL, sizeof(buf)), PATHS_NOT_FOUND);
    assert_int_equal(paths_levels_dir_writable(&cfg, buf, 0), PATHS_NOT_FOUND);
}

/* TC-47: paths_sounds_dir_readable — env override returned verbatim. */
static void test_sounds_dir_readable_env_override(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, NULL, "/custom/sounds", NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_sounds_dir_readable(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "/custom/sounds");
}

/* TC-48: paths_sounds_dir_readable — install fallback via mkdtemp tree. */
static void test_sounds_dir_readable_install_fallback(void **state)
{
    (void)state;
    char root[64];
    assert_int_equal(make_temp_tree(root, sizeof(root), "xboing/sounds"), 0);

    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, root, NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_sounds_dir_readable(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);

    char expected[256];
    snprintf(expected, sizeof(expected), "%s/xboing/sounds", root);
    assert_string_equal(buf, expected);

    remove_temp_tree(root, "xboing/sounds");
}

/* TC-49: paths_sounds_dir_readable — CWD fallback when XDG_DATA_DIRS points
 *         nowhere (hermetic: nonexistent path so install lookup always misses). */
static void test_sounds_dir_readable_cwd_fallback(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, "/nonexistent/share", NULL, NULL, NULL);

    char buf[PATHS_MAX_PATH];
    paths_status_t st = paths_sounds_dir_readable(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_OK);
    assert_string_equal(buf, "sounds");
}

/* TC-50: paths_sounds_dir_readable — truncated. */
static void test_sounds_dir_readable_truncated(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, NULL, "/custom/sounds", NULL);

    char buf[1];
    paths_status_t st = paths_sounds_dir_readable(&cfg, buf, sizeof(buf));
    assert_int_equal(st, PATHS_TRUNCATED);
}

/* TC-51: paths_sounds_dir_readable — NULL args return PATHS_NOT_FOUND. */
static void test_sounds_dir_readable_null_args(void **state)
{
    (void)state;
    paths_config_t cfg;
    paths_init_explicit(&cfg, "/home/test", NULL, NULL, NULL, NULL, NULL, NULL);
    char buf[PATHS_MAX_PATH];

    assert_int_equal(paths_sounds_dir_readable(NULL, buf, sizeof(buf)), PATHS_NOT_FOUND);
    assert_int_equal(paths_sounds_dir_readable(&cfg, NULL, sizeof(buf)), PATHS_NOT_FOUND);
    assert_int_equal(paths_sounds_dir_readable(&cfg, buf, 0), PATHS_NOT_FOUND);
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
        cmocka_unit_test(test_score_global_xdg_default),
        cmocka_unit_test(test_score_personal_xdg_default),
        cmocka_unit_test(test_score_personal_custom_xdg),
        /* Group 5: Save files */
        cmocka_unit_test(test_save_info_xdg_default),
        cmocka_unit_test(test_save_level_xdg_default),
        /* Group 6: Directory accessors */
        cmocka_unit_test(test_levels_dir_readable_legacy),
        cmocka_unit_test(test_levels_dir_readable_default),
        cmocka_unit_test(test_sounds_dir_readable_legacy),
        cmocka_unit_test(test_user_data_dir_default),
        cmocka_unit_test(test_user_data_dir_custom),
        /* Group 7: Install data dir + read/write directory accessors */
        cmocka_unit_test(test_install_data_dir_success),
        cmocka_unit_test(test_install_data_dir_not_found),
        cmocka_unit_test(test_install_data_dir_truncated),
        cmocka_unit_test(test_install_data_dir_null_args),
        cmocka_unit_test(test_levels_dir_readable_env_override),
        cmocka_unit_test(test_levels_dir_readable_install_fallback),
        cmocka_unit_test(test_levels_dir_readable_cwd_fallback),
        cmocka_unit_test(test_levels_dir_readable_truncated),
        cmocka_unit_test(test_levels_dir_readable_null_args),
        cmocka_unit_test(test_levels_dir_writable_env_override),
        cmocka_unit_test(test_levels_dir_writable_readonly_override),
        cmocka_unit_test(test_levels_dir_writable_xdg_data_home),
        cmocka_unit_test(test_levels_dir_writable_home_fallback),
        cmocka_unit_test(test_levels_dir_writable_truncated),
        cmocka_unit_test(test_levels_dir_writable_null_args),
        cmocka_unit_test(test_sounds_dir_readable_env_override),
        cmocka_unit_test(test_sounds_dir_readable_install_fallback),
        cmocka_unit_test(test_sounds_dir_readable_cwd_fallback),
        cmocka_unit_test(test_sounds_dir_readable_truncated),
        cmocka_unit_test(test_sounds_dir_readable_null_args),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
