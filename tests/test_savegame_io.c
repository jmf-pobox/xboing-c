/*
 * test_savegame_io.c — Tests for JSON-based save/load game state I/O.
 *
 * 6 groups:
 *   1. Initialization (2 tests)
 *   2. Write and read round-trip (3 tests)
 *   3. File operations (3 tests)
 *   4. Error handling (2 tests)
 *   5. Edge cases (2 tests)
 *   6. Null safety (1 test)
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

#include "savegame_io.h"

/* =========================================================================
 * Test helpers
 * ========================================================================= */

static char tmp_path[256];

static int setup_tmpfile(void **state)
{
    (void)state;
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/xboing_test_sg_XXXXXX");
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

static savegame_data_t make_test_data(void)
{
    savegame_data_t d;
    d.score = 12500;
    d.level = 5;
    d.level_time = 120;
    d.game_time = 3600;
    d.lives_left = 3;
    d.start_level = 1;
    d.paddle_size = 50;
    d.num_bullets = 4;
    return d;
}

/* =========================================================================
 * Group 1: Initialization
 * ========================================================================= */

static void test_init_defaults(void **state)
{
    (void)state;
    savegame_data_t d;
    savegame_io_init(&d);
    assert_int_equal((int)d.score, 0);
    assert_int_equal((int)d.level, 1);
    assert_int_equal(d.level_time, 0);
    assert_int_equal((int)d.game_time, 0);
    assert_int_equal(d.lives_left, 3);
    assert_int_equal(d.start_level, 1);
    assert_int_equal(d.paddle_size, 50);
    assert_int_equal(d.num_bullets, 0);
}

static void test_init_null(void **state)
{
    (void)state;
    savegame_io_init(NULL); /* should not crash */
}

/* =========================================================================
 * Group 2: Write and read round-trip
 * ========================================================================= */

static void test_roundtrip_basic(void **state)
{
    (void)state;
    savegame_data_t orig = make_test_data();
    savegame_io_result_t r = savegame_io_write(tmp_path, &orig);
    assert_int_equal(r, SAVEGAME_IO_OK);

    savegame_data_t loaded;
    r = savegame_io_read(tmp_path, &loaded);
    assert_int_equal(r, SAVEGAME_IO_OK);
    assert_int_equal((int)loaded.score, 12500);
    assert_int_equal((int)loaded.level, 5);
    assert_int_equal(loaded.level_time, 120);
    assert_int_equal((int)loaded.game_time, 3600);
    assert_int_equal(loaded.lives_left, 3);
    assert_int_equal(loaded.start_level, 1);
    assert_int_equal(loaded.paddle_size, 50);
    assert_int_equal(loaded.num_bullets, 4);
}

static void test_roundtrip_zeros(void **state)
{
    (void)state;
    savegame_data_t orig;
    savegame_io_init(&orig);
    savegame_io_result_t r = savegame_io_write(tmp_path, &orig);
    assert_int_equal(r, SAVEGAME_IO_OK);

    savegame_data_t loaded;
    r = savegame_io_read(tmp_path, &loaded);
    assert_int_equal(r, SAVEGAME_IO_OK);
    assert_int_equal((int)loaded.score, 0);
    assert_int_equal((int)loaded.level, 1);
}

static void test_roundtrip_large_values(void **state)
{
    (void)state;
    savegame_data_t orig = make_test_data();
    orig.score = 999999999UL;
    orig.level = 80;
    orig.game_time = 86400UL;
    savegame_io_result_t r = savegame_io_write(tmp_path, &orig);
    assert_int_equal(r, SAVEGAME_IO_OK);

    savegame_data_t loaded;
    r = savegame_io_read(tmp_path, &loaded);
    assert_int_equal(r, SAVEGAME_IO_OK);
    assert_int_equal((int)loaded.score, 999999999);
    assert_int_equal((int)loaded.level, 80);
    assert_int_equal((int)loaded.game_time, 86400);
}

/* =========================================================================
 * Group 3: File operations
 * ========================================================================= */

static void test_exists_after_write(void **state)
{
    (void)state;
    savegame_data_t d = make_test_data();
    savegame_io_write(tmp_path, &d);
    assert_int_equal(savegame_io_exists(tmp_path), 1);
}

static void test_not_exists(void **state)
{
    (void)state;
    assert_int_equal(savegame_io_exists("/tmp/xboing_no_such_savefile_XXXXXX"), 0);
}

static void test_delete(void **state)
{
    (void)state;
    savegame_data_t d = make_test_data();
    savegame_io_write(tmp_path, &d);
    assert_int_equal(savegame_io_exists(tmp_path), 1);

    savegame_io_result_t r = savegame_io_delete(tmp_path);
    assert_int_equal(r, SAVEGAME_IO_OK);
    assert_int_equal(savegame_io_exists(tmp_path), 0);
}

/* =========================================================================
 * Group 4: Error handling
 * ========================================================================= */

static void test_read_nonexistent(void **state)
{
    (void)state;
    savegame_data_t d;
    savegame_io_result_t r = savegame_io_read("/tmp/xboing_no_such_file_XXXXXX.json", &d);
    assert_int_equal(r, SAVEGAME_IO_ERR_OPEN);
    /* Data should be initialized to defaults. */
    assert_int_equal(d.lives_left, 3);
}

static void test_read_corrupt(void **state)
{
    (void)state;
    FILE *fp = fopen(tmp_path, "w");
    assert_non_null(fp);
    fprintf(fp, "not json at all");
    fclose(fp);

    savegame_data_t d;
    savegame_io_result_t r = savegame_io_read(tmp_path, &d);
    assert_int_equal(r, SAVEGAME_IO_ERR_READ);
}

/* =========================================================================
 * Group 5: Edge cases
 * ========================================================================= */

static void test_overwrite_existing(void **state)
{
    (void)state;
    savegame_data_t d1 = make_test_data();
    d1.score = 1000;
    savegame_io_write(tmp_path, &d1);

    savegame_data_t d2 = make_test_data();
    d2.score = 2000;
    savegame_io_write(tmp_path, &d2);

    savegame_data_t loaded;
    savegame_io_read(tmp_path, &loaded);
    assert_int_equal((int)loaded.score, 2000);
}

static void test_delete_nonexistent(void **state)
{
    (void)state;
    savegame_io_result_t r = savegame_io_delete("/tmp/xboing_no_such_file_XXXXXX");
    assert_int_equal(r, SAVEGAME_IO_OK);
}

/* =========================================================================
 * Group 6: Null safety
 * ========================================================================= */

static void test_null_safety(void **state)
{
    (void)state;
    assert_int_equal(savegame_io_read(NULL, NULL), SAVEGAME_IO_ERR_NULL);
    assert_int_equal(savegame_io_write(NULL, NULL), SAVEGAME_IO_ERR_NULL);
    assert_int_equal(savegame_io_exists(NULL), 0);
    assert_int_equal(savegame_io_delete(NULL), SAVEGAME_IO_ERR_NULL);
    savegame_io_init(NULL);
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
        cmocka_unit_test_setup_teardown(test_roundtrip_zeros, setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_roundtrip_large_values, setup_tmpfile,
                                        teardown_tmpfile),
        /* Group 3: File operations */
        cmocka_unit_test_setup_teardown(test_exists_after_write, setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test(test_not_exists),
        cmocka_unit_test_setup_teardown(test_delete, setup_tmpfile, teardown_tmpfile),
        /* Group 4: Error handling */
        cmocka_unit_test(test_read_nonexistent),
        cmocka_unit_test_setup_teardown(test_read_corrupt, setup_tmpfile, teardown_tmpfile),
        /* Group 5: Edge cases */
        cmocka_unit_test_setup_teardown(test_overwrite_existing, setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test(test_delete_nonexistent),
        /* Group 6: Null safety */
        cmocka_unit_test(test_null_safety),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
