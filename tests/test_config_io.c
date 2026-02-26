/*
 * test_config_io.c — Tests for TOML-based user preferences I/O.
 *
 * 7 groups:
 *   1. Initialization (2 tests)
 *   2. Write and read round-trip (3 tests)
 *   3. TOML parsing (4 tests)
 *   4. File operations (2 tests)
 *   5. Error handling (2 tests)
 *   6. Edge cases (3 tests)
 *   7. Null safety (1 test)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* cmocka must come after setjmp.h / stdarg.h / stddef.h */
#include <cmocka.h>

#include "config_io.h"

/* =========================================================================
 * Test helpers
 * ========================================================================= */

static char tmp_path[256];

static int setup_tmpfile(void **state)
{
    (void)state;
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/xboing_test_cfg_XXXXXX");
    int fd = mkstemp(tmp_path);
    if (fd < 0)
    {
        return -1;
    }
    close(fd);
    return 0;
}

static int teardown_tmpfile(void **state)
{
    (void)state;
    (void)remove(tmp_path);
    char tmp2[260];
    snprintf(tmp2, sizeof(tmp2), "%s.tmp", tmp_path);
    (void)remove(tmp2);
    return 0;
}

static config_data_t make_test_data(void)
{
    config_data_t d;
    config_io_init(&d);
    d.speed = 7;
    d.start_level = 10;
    d.use_keys = true;
    d.sfx = false;
    d.sound = true;
    d.max_volume = 80;
    strncpy(d.nickname, "TestPlayer", CONFIG_IO_MAX_NICKNAME_LEN);
    d.nickname[CONFIG_IO_MAX_NICKNAME_LEN] = '\0';
    return d;
}

/* Write raw text to the temp file. */
static void write_raw(const char *text)
{
    FILE *fp = fopen(tmp_path, "w");
    assert_non_null(fp);
    fputs(text, fp);
    fclose(fp);
}

/* =========================================================================
 * Group 1: Initialization
 * ========================================================================= */

static void test_init_defaults(void **state)
{
    (void)state;
    config_data_t d;
    config_io_init(&d);
    assert_int_equal(d.speed, 5);
    assert_int_equal(d.start_level, 1);
    assert_false(d.use_keys);
    assert_true(d.sfx);
    assert_false(d.sound);
    assert_int_equal(d.max_volume, 0);
    assert_string_equal(d.nickname, "");
}

static void test_init_null(void **state)
{
    (void)state;
    config_io_init(NULL); /* should not crash */
}

/* =========================================================================
 * Group 2: Write and read round-trip
 * ========================================================================= */

static void test_roundtrip_basic(void **state)
{
    (void)state;
    config_data_t orig = make_test_data();
    config_io_result_t r = config_io_write(tmp_path, &orig);
    assert_int_equal(r, CONFIG_IO_OK);

    config_data_t loaded;
    r = config_io_read(tmp_path, &loaded);
    assert_int_equal(r, CONFIG_IO_OK);
    assert_int_equal(loaded.speed, 7);
    assert_int_equal(loaded.start_level, 10);
    assert_true(loaded.use_keys);
    assert_false(loaded.sfx);
    assert_true(loaded.sound);
    assert_int_equal(loaded.max_volume, 80);
    assert_string_equal(loaded.nickname, "TestPlayer");
}

static void test_roundtrip_defaults(void **state)
{
    (void)state;
    config_data_t orig;
    config_io_init(&orig);
    config_io_result_t r = config_io_write(tmp_path, &orig);
    assert_int_equal(r, CONFIG_IO_OK);

    config_data_t loaded;
    r = config_io_read(tmp_path, &loaded);
    assert_int_equal(r, CONFIG_IO_OK);
    assert_int_equal(loaded.speed, 5);
    assert_int_equal(loaded.start_level, 1);
    assert_false(loaded.use_keys);
    assert_true(loaded.sfx);
    assert_false(loaded.sound);
    assert_int_equal(loaded.max_volume, 0);
    assert_string_equal(loaded.nickname, "");
}

static void test_roundtrip_special_chars(void **state)
{
    (void)state;
    config_data_t orig;
    config_io_init(&orig);
    strncpy(orig.nickname, "A\"B\\C", CONFIG_IO_MAX_NICKNAME_LEN);
    orig.nickname[CONFIG_IO_MAX_NICKNAME_LEN] = '\0';

    config_io_result_t r = config_io_write(tmp_path, &orig);
    assert_int_equal(r, CONFIG_IO_OK);

    config_data_t loaded;
    r = config_io_read(tmp_path, &loaded);
    assert_int_equal(r, CONFIG_IO_OK);
    assert_string_equal(loaded.nickname, "A\"B\\C");
}

/* =========================================================================
 * Group 3: TOML parsing
 * ========================================================================= */

static void test_parse_comments(void **state)
{
    (void)state;
    write_raw("# This is a comment\n"
              "speed = 3\n"
              "# Another comment\n"
              "start_level = 5\n");

    config_data_t d;
    config_io_result_t r = config_io_read(tmp_path, &d);
    assert_int_equal(r, CONFIG_IO_OK);
    assert_int_equal(d.speed, 3);
    assert_int_equal(d.start_level, 5);
}

static void test_parse_blank_lines(void **state)
{
    (void)state;
    write_raw("\n\n"
              "speed = 8\n"
              "\n"
              "sfx = false\n"
              "\n");

    config_data_t d;
    config_io_result_t r = config_io_read(tmp_path, &d);
    assert_int_equal(r, CONFIG_IO_OK);
    assert_int_equal(d.speed, 8);
    assert_false(d.sfx);
}

static void test_parse_extra_whitespace(void **state)
{
    (void)state;
    write_raw("  speed  =  3  \n"
              "\tstart_level\t=\t20\t\n");

    config_data_t d;
    config_io_result_t r = config_io_read(tmp_path, &d);
    assert_int_equal(r, CONFIG_IO_OK);
    assert_int_equal(d.speed, 3);
    assert_int_equal(d.start_level, 20);
}

static void test_parse_unknown_keys_ignored(void **state)
{
    (void)state;
    write_raw("speed = 4\n"
              "future_feature = true\n"
              "another_thing = \"hello\"\n"
              "max_volume = 50\n");

    config_data_t d;
    config_io_result_t r = config_io_read(tmp_path, &d);
    assert_int_equal(r, CONFIG_IO_OK);
    assert_int_equal(d.speed, 4);
    assert_int_equal(d.max_volume, 50);
}

/* =========================================================================
 * Group 4: File operations
 * ========================================================================= */

static void test_exists_after_write(void **state)
{
    (void)state;
    config_data_t d = make_test_data();
    config_io_write(tmp_path, &d);
    assert_int_equal(config_io_exists(tmp_path), 1);
}

static void test_not_exists(void **state)
{
    (void)state;
    assert_int_equal(config_io_exists("/tmp/xboing_no_such_config_XXXXXX"), 0);
}

/* =========================================================================
 * Group 5: Error handling
 * ========================================================================= */

static void test_read_nonexistent(void **state)
{
    (void)state;
    config_data_t d;
    config_io_result_t r = config_io_read("/tmp/xboing_no_such_config_XXXXXX.toml", &d);
    assert_int_equal(r, CONFIG_IO_ERR_OPEN);
    /* Data should be initialized to defaults. */
    assert_int_equal(d.speed, 5);
    assert_true(d.sfx);
}

static void test_read_empty_file(void **state)
{
    (void)state;
    write_raw("");

    config_data_t d;
    config_io_result_t r = config_io_read(tmp_path, &d);
    assert_int_equal(r, CONFIG_IO_OK);
    /* All defaults. */
    assert_int_equal(d.speed, 5);
    assert_int_equal(d.start_level, 1);
}

/* =========================================================================
 * Group 6: Edge cases
 * ========================================================================= */

static void test_overwrite_existing(void **state)
{
    (void)state;
    config_data_t d1 = make_test_data();
    d1.speed = 3;
    config_io_write(tmp_path, &d1);

    config_data_t d2 = make_test_data();
    d2.speed = 9;
    config_io_write(tmp_path, &d2);

    config_data_t loaded;
    config_io_read(tmp_path, &loaded);
    assert_int_equal(loaded.speed, 9);
}

static void test_out_of_range_values_keep_defaults(void **state)
{
    (void)state;
    write_raw("speed = 99\n"
              "start_level = 0\n"
              "max_volume = 200\n");

    config_data_t d;
    config_io_result_t r = config_io_read(tmp_path, &d);
    assert_int_equal(r, CONFIG_IO_OK);
    /* Out-of-range values should leave defaults in place. */
    assert_int_equal(d.speed, 5);
    assert_int_equal(d.start_level, 1);
    assert_int_equal(d.max_volume, 0);
}

static void test_partial_config(void **state)
{
    (void)state;
    /* Only some keys present — rest stay at defaults. */
    write_raw("speed = 2\n"
              "sound = true\n");

    config_data_t d;
    config_io_result_t r = config_io_read(tmp_path, &d);
    assert_int_equal(r, CONFIG_IO_OK);
    assert_int_equal(d.speed, 2);
    assert_true(d.sound);
    /* Unspecified keys keep defaults. */
    assert_int_equal(d.start_level, 1);
    assert_false(d.use_keys);
    assert_true(d.sfx);
    assert_int_equal(d.max_volume, 0);
    assert_string_equal(d.nickname, "");
}

/* =========================================================================
 * Group 7: Null safety
 * ========================================================================= */

static void test_null_safety(void **state)
{
    (void)state;
    assert_int_equal(config_io_read(NULL, NULL), CONFIG_IO_ERR_NULL);
    assert_int_equal(config_io_write(NULL, NULL), CONFIG_IO_ERR_NULL);
    assert_int_equal(config_io_exists(NULL), 0);
    config_io_init(NULL);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Initialization */
        cmocka_unit_test(test_init_defaults),
        cmocka_unit_test(test_init_null),
        /* Group 2: Write and read round-trip */
        cmocka_unit_test_setup_teardown(test_roundtrip_basic, setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_roundtrip_defaults, setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_roundtrip_special_chars, setup_tmpfile,
                                        teardown_tmpfile),
        /* Group 3: TOML parsing */
        cmocka_unit_test_setup_teardown(test_parse_comments, setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_parse_blank_lines, setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_parse_extra_whitespace, setup_tmpfile,
                                        teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_parse_unknown_keys_ignored, setup_tmpfile,
                                        teardown_tmpfile),
        /* Group 4: File operations */
        cmocka_unit_test_setup_teardown(test_exists_after_write, setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test(test_not_exists),
        /* Group 5: Error handling */
        cmocka_unit_test(test_read_nonexistent),
        cmocka_unit_test_setup_teardown(test_read_empty_file, setup_tmpfile, teardown_tmpfile),
        /* Group 6: Edge cases */
        cmocka_unit_test_setup_teardown(test_overwrite_existing, setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_out_of_range_values_keep_defaults, setup_tmpfile,
                                        teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_partial_config, setup_tmpfile, teardown_tmpfile),
        /* Group 7: Null safety */
        cmocka_unit_test(test_null_safety),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
