/*
 * test_highscore_io.c — Tests for JSON-based high score file I/O.
 *
 * 7 groups:
 *   1. Table initialization (2 tests)
 *   2. Sorting (3 tests)
 *   3. Insert and ranking (4 tests)
 *   4. Write and read round-trip (3 tests)
 *   5. Edge cases (3 tests)
 *   6. Error handling (2 tests)
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

#include "highscore_io.h"

/* =========================================================================
 * Test helpers
 * ========================================================================= */

static char tmp_path[256];

static int setup_tmpfile(void **state)
{
    (void)state;
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/xboing_test_hs_XXXXXX");
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
    /* Also remove temp file used by atomic write. */
    char tmp2[260];
    snprintf(tmp2, sizeof(tmp2), "%s.tmp", tmp_path);
    (void)remove(tmp2);
    return 0;
}

static highscore_table_t make_populated_table(void)
{
    highscore_table_t t;
    highscore_io_init_table(&t);
    strncpy(t.master_name, "Champion", HIGHSCORE_NAME_LEN - 1);
    strncpy(t.master_text, "Victory is mine!", HIGHSCORE_NAME_LEN - 1);
    for (int i = 0; i < 5; i++)
    {
        t.entries[i].score = (unsigned long)(50000 - i * 5000);
        t.entries[i].level = (unsigned long)(10 - i);
        t.entries[i].game_time = (unsigned long)(3600 + i * 600);
        t.entries[i].timestamp = 1700000000UL;
        snprintf(t.entries[i].name, HIGHSCORE_NAME_LEN, "Player %d", i + 1);
    }
    return t;
}

/* =========================================================================
 * Group 1: Table initialization
 * ========================================================================= */

static void test_init_table(void **state)
{
    (void)state;
    highscore_table_t t;
    highscore_io_init_table(&t);
    assert_string_equal(t.master_text, "Anyone play this game?");
    assert_string_equal(t.master_name, "");
    for (int i = 0; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        assert_int_equal((int)t.entries[i].score, 0);
    }
}

static void test_init_table_null(void **state)
{
    (void)state;
    /* Should not crash. */
    highscore_io_init_table(NULL);
}

/* =========================================================================
 * Group 2: Sorting
 * ========================================================================= */

static void test_sort_already_sorted(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    highscore_io_sort(&t);
    assert_int_equal((int)t.entries[0].score, 50000);
    assert_int_equal((int)t.entries[1].score, 45000);
    assert_int_equal((int)t.entries[4].score, 30000);
}

static void test_sort_reversed(void **state)
{
    (void)state;
    highscore_table_t t;
    highscore_io_init_table(&t);
    for (int i = 0; i < 5; i++)
    {
        t.entries[i].score = (unsigned long)((i + 1) * 1000);
    }
    highscore_io_sort(&t);
    assert_int_equal((int)t.entries[0].score, 5000);
    assert_int_equal((int)t.entries[1].score, 4000);
    assert_int_equal((int)t.entries[4].score, 1000);
}

static void test_sort_zeros_at_end(void **state)
{
    (void)state;
    highscore_table_t t;
    highscore_io_init_table(&t);
    t.entries[0].score = 0;
    t.entries[1].score = 5000;
    t.entries[2].score = 0;
    t.entries[3].score = 3000;
    highscore_io_sort(&t);
    assert_int_equal((int)t.entries[0].score, 5000);
    assert_int_equal((int)t.entries[1].score, 3000);
    assert_int_equal((int)t.entries[2].score, 0);
}

/* =========================================================================
 * Group 3: Insert and ranking
 * ========================================================================= */

static void test_ranking_top(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    int rank = highscore_io_get_ranking(&t, 60000);
    assert_int_equal(rank, 1);
}

static void test_ranking_middle(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    int rank = highscore_io_get_ranking(&t, 42000);
    assert_int_equal(rank, 3);
}

static void test_ranking_not_placed(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    /* Fill all 10 entries. */
    for (int i = 5; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        t.entries[i].score = (unsigned long)(25000 - (i - 5) * 1000);
    }
    int rank = highscore_io_get_ranking(&t, 100);
    assert_int_equal(rank, -1);
}

static void test_insert_shifts_down(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    highscore_io_result_t r = highscore_io_insert(&t, 47000, 9, 3000, 1700000001UL, "New Player");
    assert_int_equal(r, HIGHSCORE_IO_OK);
    assert_int_equal((int)t.entries[0].score, 50000);
    assert_int_equal((int)t.entries[1].score, 47000);
    assert_string_equal(t.entries[1].name, "New Player");
    assert_int_equal((int)t.entries[2].score, 45000);
}

/* =========================================================================
 * Group 4: Write and read round-trip
 * ========================================================================= */

static void test_write_read_roundtrip(void **state)
{
    (void)state;
    highscore_table_t orig = make_populated_table();
    highscore_io_result_t r = highscore_io_write(tmp_path, &orig);
    assert_int_equal(r, HIGHSCORE_IO_OK);

    highscore_table_t loaded;
    r = highscore_io_read(tmp_path, &loaded);
    assert_int_equal(r, HIGHSCORE_IO_OK);

    assert_string_equal(loaded.master_name, "Champion");
    assert_string_equal(loaded.master_text, "Victory is mine!");
    assert_int_equal((int)loaded.entries[0].score, 50000);
    assert_string_equal(loaded.entries[0].name, "Player 1");
    assert_int_equal((int)loaded.entries[4].score, 30000);
    assert_string_equal(loaded.entries[4].name, "Player 5");
}

static void test_roundtrip_empty_table(void **state)
{
    (void)state;
    highscore_table_t orig;
    highscore_io_init_table(&orig);
    highscore_io_result_t r = highscore_io_write(tmp_path, &orig);
    assert_int_equal(r, HIGHSCORE_IO_OK);

    highscore_table_t loaded;
    r = highscore_io_read(tmp_path, &loaded);
    assert_int_equal(r, HIGHSCORE_IO_OK);
    assert_string_equal(loaded.master_text, "Anyone play this game?");
    for (int i = 0; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        assert_int_equal((int)loaded.entries[i].score, 0);
    }
}

static void test_roundtrip_special_chars(void **state)
{
    (void)state;
    highscore_table_t orig;
    highscore_io_init_table(&orig);
    strncpy(orig.master_name, "Test \"User\"", HIGHSCORE_NAME_LEN - 1);
    strncpy(orig.master_text, "Back\\slash", HIGHSCORE_NAME_LEN - 1);
    orig.entries[0].score = 1000;
    strncpy(orig.entries[0].name, "Name with \"quotes\"", HIGHSCORE_NAME_LEN - 1);

    highscore_io_result_t r = highscore_io_write(tmp_path, &orig);
    assert_int_equal(r, HIGHSCORE_IO_OK);

    highscore_table_t loaded;
    r = highscore_io_read(tmp_path, &loaded);
    assert_int_equal(r, HIGHSCORE_IO_OK);
    assert_string_equal(loaded.master_name, "Test \"User\"");
    assert_string_equal(loaded.master_text, "Back\\slash");
    assert_string_equal(loaded.entries[0].name, "Name with \"quotes\"");
}

/* =========================================================================
 * Group 5: Edge cases
 * ========================================================================= */

static void test_insert_into_full_table(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    for (int i = 5; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        t.entries[i].score = (unsigned long)(25000 - (i - 5) * 1000);
        snprintf(t.entries[i].name, HIGHSCORE_NAME_LEN, "Player %d", i + 1);
    }

    /* Insert at #1 — last entry should be dropped. */
    highscore_io_result_t r = highscore_io_insert(&t, 99999, 15, 7200, 1700000002UL, "Legend");
    assert_int_equal(r, HIGHSCORE_IO_OK);
    assert_int_equal((int)t.entries[0].score, 99999);
    assert_string_equal(t.entries[0].name, "Legend");
    /* Old #1 should now be #2. */
    assert_int_equal((int)t.entries[1].score, 50000);
    /* Table still has exactly 10 entries. */
    assert_int_equal((int)t.entries[HIGHSCORE_NUM_ENTRIES - 1].score, 22000);
}

static void test_insert_too_low(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    for (int i = 5; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        t.entries[i].score = (unsigned long)(25000 - (i - 5) * 1000);
    }
    highscore_io_result_t r = highscore_io_insert(&t, 100, 1, 60, 1700000003UL, "Loser");
    assert_int_equal(r, HIGHSCORE_IO_ERR_NOT_RANKED);
}

static void test_insert_updates_master(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    highscore_io_insert(&t, 99999, 15, 7200, 1700000002UL, "New Boss");
    assert_string_equal(t.master_name, "New Boss");
}

/* =========================================================================
 * Group 6: Error handling
 * ========================================================================= */

static void test_read_nonexistent(void **state)
{
    (void)state;
    highscore_table_t t;
    highscore_io_result_t r = highscore_io_read("/tmp/xboing_no_such_file_XXXXXX.json", &t);
    assert_int_equal(r, HIGHSCORE_IO_ERR_OPEN);
    /* Table should still be initialized to defaults. */
    assert_string_equal(t.master_text, "Anyone play this game?");
}

static void test_read_corrupt_file(void **state)
{
    (void)state;
    /* Write garbage to the file. */
    FILE *fp = fopen(tmp_path, "w");
    assert_non_null(fp);
    fprintf(fp, "this is not json");
    fclose(fp);

    highscore_table_t t;
    highscore_io_result_t r = highscore_io_read(tmp_path, &t);
    assert_int_equal(r, HIGHSCORE_IO_ERR_READ);
}

/* =========================================================================
 * Group 7: Null safety
 * ========================================================================= */

static void test_null_safety(void **state)
{
    (void)state;
    assert_int_equal(highscore_io_read(NULL, NULL), HIGHSCORE_IO_ERR_NULL);
    assert_int_equal(highscore_io_write(NULL, NULL), HIGHSCORE_IO_ERR_NULL);
    highscore_io_sort(NULL);
    assert_int_equal(highscore_io_insert(NULL, 0, 0, 0, 0, NULL), HIGHSCORE_IO_ERR_NULL);
    assert_int_equal(highscore_io_get_ranking(NULL, 0), -1);
    highscore_io_init_table(NULL);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Table initialization */
        cmocka_unit_test(test_init_table),
        cmocka_unit_test(test_init_table_null),
        /* Group 2: Sorting */
        cmocka_unit_test(test_sort_already_sorted),
        cmocka_unit_test(test_sort_reversed),
        cmocka_unit_test(test_sort_zeros_at_end),
        /* Group 3: Insert and ranking */
        cmocka_unit_test(test_ranking_top),
        cmocka_unit_test(test_ranking_middle),
        cmocka_unit_test(test_ranking_not_placed),
        cmocka_unit_test(test_insert_shifts_down),
        /* Group 4: Write and read round-trip */
        cmocka_unit_test_setup_teardown(test_write_read_roundtrip, setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_roundtrip_empty_table, setup_tmpfile,
                                        teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_roundtrip_special_chars, setup_tmpfile,
                                        teardown_tmpfile),
        /* Group 5: Edge cases */
        cmocka_unit_test(test_insert_into_full_table),
        cmocka_unit_test(test_insert_too_low),
        cmocka_unit_test(test_insert_updates_master),
        /* Group 6: Error handling */
        cmocka_unit_test(test_read_nonexistent),
        cmocka_unit_test_setup_teardown(test_read_corrupt_file, setup_tmpfile, teardown_tmpfile),
        /* Group 7: Null safety */
        cmocka_unit_test(test_null_safety),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
