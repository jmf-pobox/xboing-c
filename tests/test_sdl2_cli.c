/*
 * test_sdl2_cli.c — Unit tests for the SDL2 CLI option parser.
 *
 * Pure C tests — no SDL2 dependency.  Exercises sdl2_cli_parse()
 * with synthetic argv arrays covering all options, validation
 * ranges, and error paths.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

/* cmocka.h must follow the setjmp/stdarg/stddef includes. */
#include <cmocka.h>

#include "sdl2_cli.h"

/* =========================================================================
 * Helpers
 * ========================================================================= */

/*
 * Build an argv-style array from a string literal list.
 * First element is always "xboing" (program name).
 */
#define ARGV(...)                                                                                  \
    (char *const[])                                                                                \
    {                                                                                              \
        __VA_ARGS__                                                                                \
    }
#define ARGC(...) (int)(sizeof(ARGV(__VA_ARGS__)) / sizeof(char *const))

/* =========================================================================
 * Group 1: Config defaults
 * ========================================================================= */

static void test_defaults_speed(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    assert_int_equal(cfg.speed, SDL2C_DEFAULT_SPEED);
}

static void test_defaults_start_level(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    assert_int_equal(cfg.start_level, SDL2C_DEFAULT_LEVEL);
}

static void test_defaults_use_keys(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    assert_false(cfg.use_keys);
}

static void test_defaults_sfx(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    assert_true(cfg.sfx);
}

static void test_defaults_sound(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    assert_false(cfg.sound);
}

static void test_defaults_max_volume(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    assert_int_equal(cfg.max_volume, SDL2C_MIN_VOLUME);
}

static void test_defaults_nickname_empty(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    assert_string_equal(cfg.nickname, "");
}

static void test_defaults_debug(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    assert_false(cfg.debug);
}

static void test_defaults_grab(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    assert_false(cfg.grab);
}

/* =========================================================================
 * Group 2: NULL argument handling
 * ========================================================================= */

static void test_parse_null_config(void **state)
{
    (void)state;
    char *const argv[] = {"xboing"};
    sdl2_cli_status_t st = sdl2_cli_parse(1, argv, NULL, NULL);
    assert_int_equal(st, SDL2C_ERR_NULL_ARG);
}

/* =========================================================================
 * Group 3: Informational exit flags
 * ========================================================================= */

static void test_exit_help(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing", "-help"};
    assert_int_equal(sdl2_cli_parse(2, argv, &cfg, NULL), SDL2C_EXIT_HELP);
}

static void test_exit_usage(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing", "-usage"};
    assert_int_equal(sdl2_cli_parse(2, argv, &cfg, NULL), SDL2C_EXIT_HELP);
}

static void test_exit_version(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing", "-version"};
    assert_int_equal(sdl2_cli_parse(2, argv, &cfg, NULL), SDL2C_EXIT_VERSION);
}

static void test_exit_setup(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing", "-setup"};
    assert_int_equal(sdl2_cli_parse(2, argv, &cfg, NULL), SDL2C_EXIT_SETUP);
}

static void test_exit_scores(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing", "-scores"};
    assert_int_equal(sdl2_cli_parse(2, argv, &cfg, NULL), SDL2C_EXIT_SCORES);
}

/* =========================================================================
 * Group 4: Boolean flags
 * ========================================================================= */

static void test_flag_debug(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing", "-debug"};
    assert_int_equal(sdl2_cli_parse(2, argv, &cfg, NULL), SDL2C_OK);
    assert_true(cfg.debug);
}

static void test_flag_keys(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing", "-keys"};
    assert_int_equal(sdl2_cli_parse(2, argv, &cfg, NULL), SDL2C_OK);
    assert_true(cfg.use_keys);
}

static void test_flag_sound(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing", "-sound"};
    assert_int_equal(sdl2_cli_parse(2, argv, &cfg, NULL), SDL2C_OK);
    assert_true(cfg.sound);
}

static void test_flag_nosfx(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing", "-nosfx"};
    assert_int_equal(sdl2_cli_parse(2, argv, &cfg, NULL), SDL2C_OK);
    assert_false(cfg.sfx);
}

static void test_flag_grab(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing", "-grab"};
    assert_int_equal(sdl2_cli_parse(2, argv, &cfg, NULL), SDL2C_OK);
    assert_true(cfg.grab);
}

/* =========================================================================
 * Group 5: Speed option
 * ========================================================================= */

static void test_speed_valid_min(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing", "-speed", "1"};
    assert_int_equal(sdl2_cli_parse(3, argv, &cfg, NULL), SDL2C_OK);
    assert_int_equal(cfg.speed, 1);
}

static void test_speed_valid_max(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing", "-speed", "9"};
    assert_int_equal(sdl2_cli_parse(3, argv, &cfg, NULL), SDL2C_OK);
    assert_int_equal(cfg.speed, 9);
}

static void test_speed_too_low(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    const char *bad = NULL;
    char *const argv[] = {"xboing", "-speed", "0"};
    assert_int_equal(sdl2_cli_parse(3, argv, &cfg, &bad), SDL2C_ERR_INVALID_VALUE);
    assert_string_equal(bad, "-speed");
}

static void test_speed_too_high(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    const char *bad = NULL;
    char *const argv[] = {"xboing", "-speed", "10"};
    assert_int_equal(sdl2_cli_parse(3, argv, &cfg, &bad), SDL2C_ERR_INVALID_VALUE);
    assert_string_equal(bad, "-speed");
}

static void test_speed_missing_value(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    const char *bad = NULL;
    char *const argv[] = {"xboing", "-speed"};
    assert_int_equal(sdl2_cli_parse(2, argv, &cfg, &bad), SDL2C_ERR_MISSING_VALUE);
    assert_string_equal(bad, "-speed");
}

static void test_speed_non_numeric(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    const char *bad = NULL;
    char *const argv[] = {"xboing", "-speed", "abc"};
    assert_int_equal(sdl2_cli_parse(3, argv, &cfg, &bad), SDL2C_ERR_INVALID_VALUE);
    assert_string_equal(bad, "-speed");
}

/* =========================================================================
 * Group 6: Start level option
 * ========================================================================= */

static void test_startlevel_valid_min(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing", "-startlevel", "1"};
    assert_int_equal(sdl2_cli_parse(3, argv, &cfg, NULL), SDL2C_OK);
    assert_int_equal(cfg.start_level, 1);
}

static void test_startlevel_valid_max(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing", "-startlevel", "80"};
    assert_int_equal(sdl2_cli_parse(3, argv, &cfg, NULL), SDL2C_OK);
    assert_int_equal(cfg.start_level, 80);
}

static void test_startlevel_too_low(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    const char *bad = NULL;
    char *const argv[] = {"xboing", "-startlevel", "0"};
    assert_int_equal(sdl2_cli_parse(3, argv, &cfg, &bad), SDL2C_ERR_INVALID_VALUE);
}

static void test_startlevel_too_high(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    const char *bad = NULL;
    char *const argv[] = {"xboing", "-startlevel", "81"};
    assert_int_equal(sdl2_cli_parse(3, argv, &cfg, &bad), SDL2C_ERR_INVALID_VALUE);
}

static void test_startlevel_missing_value(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    const char *bad = NULL;
    char *const argv[] = {"xboing", "-startlevel"};
    assert_int_equal(sdl2_cli_parse(2, argv, &cfg, &bad), SDL2C_ERR_MISSING_VALUE);
}

/* =========================================================================
 * Group 7: Max volume option
 * ========================================================================= */

static void test_maxvol_valid_min(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing", "-maxvol", "0"};
    assert_int_equal(sdl2_cli_parse(3, argv, &cfg, NULL), SDL2C_OK);
    assert_int_equal(cfg.max_volume, 0);
}

static void test_maxvol_valid_max(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing", "-maxvol", "100"};
    assert_int_equal(sdl2_cli_parse(3, argv, &cfg, NULL), SDL2C_OK);
    assert_int_equal(cfg.max_volume, 100);
}

static void test_maxvol_too_low(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    const char *bad = NULL;
    char *const argv[] = {"xboing", "-maxvol", "-1"};
    assert_int_equal(sdl2_cli_parse(3, argv, &cfg, &bad), SDL2C_ERR_INVALID_VALUE);
}

static void test_maxvol_too_high(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    const char *bad = NULL;
    char *const argv[] = {"xboing", "-maxvol", "101"};
    assert_int_equal(sdl2_cli_parse(3, argv, &cfg, &bad), SDL2C_ERR_INVALID_VALUE);
}

static void test_maxvol_missing_value(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    const char *bad = NULL;
    char *const argv[] = {"xboing", "-maxvol"};
    assert_int_equal(sdl2_cli_parse(2, argv, &cfg, &bad), SDL2C_ERR_MISSING_VALUE);
}

/* =========================================================================
 * Group 8: Nickname option
 * ========================================================================= */

static void test_nickname_short(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing", "-nickname", "Ace"};
    assert_int_equal(sdl2_cli_parse(3, argv, &cfg, NULL), SDL2C_OK);
    assert_string_equal(cfg.nickname, "Ace");
}

static void test_nickname_max_length(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    /* Exactly 20 characters. */
    char *const argv[] = {"xboing", "-nickname", "12345678901234567890"};
    assert_int_equal(sdl2_cli_parse(3, argv, &cfg, NULL), SDL2C_OK);
    assert_string_equal(cfg.nickname, "12345678901234567890");
    assert_int_equal((int)strlen(cfg.nickname), SDL2C_MAX_NICKNAME_LEN);
}

static void test_nickname_truncated(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    /* 25 characters — should be truncated to 20. */
    char *const argv[] = {"xboing", "-nickname", "1234567890123456789012345"};
    assert_int_equal(sdl2_cli_parse(3, argv, &cfg, NULL), SDL2C_OK);
    assert_int_equal((int)strlen(cfg.nickname), SDL2C_MAX_NICKNAME_LEN);
    assert_string_equal(cfg.nickname, "12345678901234567890");
}

static void test_nickname_missing_value(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    const char *bad = NULL;
    char *const argv[] = {"xboing", "-nickname"};
    assert_int_equal(sdl2_cli_parse(2, argv, &cfg, &bad), SDL2C_ERR_MISSING_VALUE);
}

/* =========================================================================
 * Group 9: Error handling
 * ========================================================================= */

static void test_unknown_option(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    const char *bad = NULL;
    char *const argv[] = {"xboing", "-foobar"};
    assert_int_equal(sdl2_cli_parse(2, argv, &cfg, &bad), SDL2C_ERR_UNKNOWN_OPTION);
    assert_string_equal(bad, "-foobar");
}

static void test_no_dash_prefix(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    const char *bad = NULL;
    char *const argv[] = {"xboing", "garbage"};
    assert_int_equal(sdl2_cli_parse(2, argv, &cfg, &bad), SDL2C_ERR_UNKNOWN_OPTION);
    assert_string_equal(bad, "garbage");
}

/* =========================================================================
 * Group 10: Multiple options combined
 * ========================================================================= */

static void test_multiple_flags(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing", "-debug", "-keys", "-sound", "-grab"};
    assert_int_equal(sdl2_cli_parse(5, argv, &cfg, NULL), SDL2C_OK);
    assert_true(cfg.debug);
    assert_true(cfg.use_keys);
    assert_true(cfg.sound);
    assert_true(cfg.grab);
}

static void test_mixed_flags_and_values(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing",  "-speed", "7",      "-debug",      "-nickname", "Player1",
                          "-maxvol", "75",     "-nosfx", "-startlevel", "10",        "-keys"};
    assert_int_equal(sdl2_cli_parse(12, argv, &cfg, NULL), SDL2C_OK);
    assert_int_equal(cfg.speed, 7);
    assert_true(cfg.debug);
    assert_string_equal(cfg.nickname, "Player1");
    assert_int_equal(cfg.max_volume, 75);
    assert_false(cfg.sfx);
    assert_int_equal(cfg.start_level, 10);
    assert_true(cfg.use_keys);
}

static void test_no_args(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    char *const argv[] = {"xboing"};
    assert_int_equal(sdl2_cli_parse(1, argv, &cfg, NULL), SDL2C_OK);
    /* All defaults preserved. */
    assert_int_equal(cfg.speed, SDL2C_DEFAULT_SPEED);
    assert_int_equal(cfg.start_level, SDL2C_DEFAULT_LEVEL);
    assert_false(cfg.debug);
}

static void test_info_flag_stops_parsing(void **state)
{
    (void)state;
    sdl2_cli_config_t cfg = sdl2_cli_config_defaults();
    /* -help appears after -debug — debug should be set, then -help returns. */
    char *const argv[] = {"xboing", "-debug", "-help"};
    assert_int_equal(sdl2_cli_parse(3, argv, &cfg, NULL), SDL2C_EXIT_HELP);
    assert_true(cfg.debug);
}

/* =========================================================================
 * Group 11: Status strings
 * ========================================================================= */

static void test_status_string_ok(void **state)
{
    (void)state;
    assert_non_null(sdl2_cli_status_string(SDL2C_OK));
}

static void test_status_string_all_non_null(void **state)
{
    (void)state;
    assert_non_null(sdl2_cli_status_string(SDL2C_EXIT_HELP));
    assert_non_null(sdl2_cli_status_string(SDL2C_EXIT_VERSION));
    assert_non_null(sdl2_cli_status_string(SDL2C_EXIT_SETUP));
    assert_non_null(sdl2_cli_status_string(SDL2C_EXIT_SCORES));
    assert_non_null(sdl2_cli_status_string(SDL2C_ERR_NULL_ARG));
    assert_non_null(sdl2_cli_status_string(SDL2C_ERR_MISSING_VALUE));
    assert_non_null(sdl2_cli_status_string(SDL2C_ERR_INVALID_VALUE));
    assert_non_null(sdl2_cli_status_string(SDL2C_ERR_UNKNOWN_OPTION));
}

static void test_status_string_unknown(void **state)
{
    (void)state;
    /* Out-of-range value should still return a non-NULL string. */
    assert_non_null(sdl2_cli_status_string((sdl2_cli_status_t)999));
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest defaults_tests[] = {
        cmocka_unit_test(test_defaults_speed),          cmocka_unit_test(test_defaults_start_level),
        cmocka_unit_test(test_defaults_use_keys),       cmocka_unit_test(test_defaults_sfx),
        cmocka_unit_test(test_defaults_sound),          cmocka_unit_test(test_defaults_max_volume),
        cmocka_unit_test(test_defaults_nickname_empty), cmocka_unit_test(test_defaults_debug),
        cmocka_unit_test(test_defaults_grab),
    };

    const struct CMUnitTest null_tests[] = {
        cmocka_unit_test(test_parse_null_config),
    };

    const struct CMUnitTest exit_tests[] = {
        cmocka_unit_test(test_exit_help),    cmocka_unit_test(test_exit_usage),
        cmocka_unit_test(test_exit_version), cmocka_unit_test(test_exit_setup),
        cmocka_unit_test(test_exit_scores),
    };

    const struct CMUnitTest flag_tests[] = {
        cmocka_unit_test(test_flag_debug), cmocka_unit_test(test_flag_keys),
        cmocka_unit_test(test_flag_sound), cmocka_unit_test(test_flag_nosfx),
        cmocka_unit_test(test_flag_grab),
    };

    const struct CMUnitTest speed_tests[] = {
        cmocka_unit_test(test_speed_valid_min),     cmocka_unit_test(test_speed_valid_max),
        cmocka_unit_test(test_speed_too_low),       cmocka_unit_test(test_speed_too_high),
        cmocka_unit_test(test_speed_missing_value), cmocka_unit_test(test_speed_non_numeric),
    };

    const struct CMUnitTest level_tests[] = {
        cmocka_unit_test(test_startlevel_valid_min),
        cmocka_unit_test(test_startlevel_valid_max),
        cmocka_unit_test(test_startlevel_too_low),
        cmocka_unit_test(test_startlevel_too_high),
        cmocka_unit_test(test_startlevel_missing_value),
    };

    const struct CMUnitTest volume_tests[] = {
        cmocka_unit_test(test_maxvol_valid_min),     cmocka_unit_test(test_maxvol_valid_max),
        cmocka_unit_test(test_maxvol_too_low),       cmocka_unit_test(test_maxvol_too_high),
        cmocka_unit_test(test_maxvol_missing_value),
    };

    const struct CMUnitTest nickname_tests[] = {
        cmocka_unit_test(test_nickname_short),
        cmocka_unit_test(test_nickname_max_length),
        cmocka_unit_test(test_nickname_truncated),
        cmocka_unit_test(test_nickname_missing_value),
    };

    const struct CMUnitTest error_tests[] = {
        cmocka_unit_test(test_unknown_option),
        cmocka_unit_test(test_no_dash_prefix),
    };

    const struct CMUnitTest combo_tests[] = {
        cmocka_unit_test(test_multiple_flags),
        cmocka_unit_test(test_mixed_flags_and_values),
        cmocka_unit_test(test_no_args),
        cmocka_unit_test(test_info_flag_stops_parsing),
    };

    const struct CMUnitTest status_tests[] = {
        cmocka_unit_test(test_status_string_ok),
        cmocka_unit_test(test_status_string_all_non_null),
        cmocka_unit_test(test_status_string_unknown),
    };

    int failed = 0;
    failed += cmocka_run_group_tests_name("defaults", defaults_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("null_args", null_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("exit_flags", exit_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("boolean_flags", flag_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("speed", speed_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("start_level", level_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("max_volume", volume_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("nickname", nickname_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("errors", error_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("combinations", combo_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("status_strings", status_tests, NULL, NULL);

    return failed;
}
