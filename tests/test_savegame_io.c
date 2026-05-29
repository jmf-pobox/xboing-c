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
    savegame_io_init(&d);
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
 * Group 7: v2 round-trip (specials, eyedude, balls, paddle fields)
 * ========================================================================= */

static savegame_data_t make_v2_test_data(void)
{
    savegame_data_t d;
    savegame_io_init(&d);
    d.score = 125000;
    d.level = 7;
    d.level_time = 180;
    d.time_remaining = 142;
    d.game_time = 14400;
    d.lives_left = 3;
    d.start_level = 1;
    d.user_tilts = 2;
    d.bonus_count = 5;
    d.paddle_pos = 247;
    d.paddle_size_type = 5; /* PADDLE_SIZE_MEDIUM */
    d.paddle_size = 50;
    d.paddle_reverse = 1;
    d.paddle_sticky = 0;
    d.num_bullets = 6;
    d.gun_unlimited = 1;
    d.specials = (savegame_specials_t){.sticky = 0,
                                       .saving = 1,
                                       .fast_gun = 1,
                                       .no_walls = 0,
                                       .killer = 1,
                                       .x2 = 1,
                                       .x4 = 0};
    d.eyedude =
        (savegame_eyedude_t){.state = 3, .dir = 1, .x = 120, .y = 16, .slide = 2, .inc = 5, .turn = 0};
    d.balls[0] = (savegame_ball_t){
        .active = 1, .state = 1, .x = 247, .y = 520, .dx = 3, .dy = -4, .wait_mode = 7};
    d.balls[1] = (savegame_ball_t){
        .active = 1, .state = 5, .x = 100, .y = 200, .dx = -2, .dy = -3, .wait_mode = 1};
    /* remaining balls inactive (zero-initialized via savegame_io_init) */
    return d;
}

static void test_v2_roundtrip_all_fields(void **state)
{
    (void)state;
    savegame_data_t orig = make_v2_test_data();

    savegame_io_result_t r = savegame_io_write(tmp_path, &orig);
    assert_int_equal(r, SAVEGAME_IO_OK);

    savegame_data_t loaded;
    r = savegame_io_read(tmp_path, &loaded);
    assert_int_equal(r, SAVEGAME_IO_OK);

    /* Scalar fields */
    assert_int_equal((int)loaded.score, 125000);
    assert_int_equal((int)loaded.level, 7);
    assert_int_equal(loaded.level_time, 180);
    assert_int_equal(loaded.time_remaining, 142);
    assert_int_equal((int)loaded.game_time, 14400);
    assert_int_equal(loaded.user_tilts, 2);
    assert_int_equal(loaded.bonus_count, 5);
    assert_int_equal(loaded.paddle_pos, 247);
    assert_int_equal(loaded.paddle_size_type, 5);
    assert_int_equal(loaded.paddle_reverse, 1);
    assert_int_equal(loaded.paddle_sticky, 0);
    assert_int_equal(loaded.gun_unlimited, 1);

    /* Nested: specials */
    assert_int_equal(loaded.specials.saving, 1);
    assert_int_equal(loaded.specials.fast_gun, 1);
    assert_int_equal(loaded.specials.killer, 1);
    assert_int_equal(loaded.specials.x2, 1);
    assert_int_equal(loaded.specials.x4, 0);
    assert_int_equal(loaded.specials.no_walls, 0);
    assert_int_equal(loaded.specials.sticky, 0);

    /* Nested: eyedude */
    assert_int_equal(loaded.eyedude.state, 3);
    assert_int_equal(loaded.eyedude.dir, 1);
    assert_int_equal(loaded.eyedude.x, 120);
    assert_int_equal(loaded.eyedude.y, 16);
    assert_int_equal(loaded.eyedude.slide, 2);
    assert_int_equal(loaded.eyedude.inc, 5);
    assert_int_equal(loaded.eyedude.turn, 0);

    /* Array: balls */
    assert_int_equal(loaded.balls[0].active, 1);
    assert_int_equal(loaded.balls[0].state, 1);
    assert_int_equal(loaded.balls[0].x, 247);
    assert_int_equal(loaded.balls[0].y, 520);
    assert_int_equal(loaded.balls[0].dx, 3);
    assert_int_equal(loaded.balls[0].dy, -4);
    assert_int_equal(loaded.balls[0].wait_mode, 7);
    assert_int_equal(loaded.balls[1].active, 1);
    assert_int_equal(loaded.balls[1].state, 5);
    assert_int_equal(loaded.balls[1].x, 100);
    assert_int_equal(loaded.balls[1].y, 200);
    assert_int_equal(loaded.balls[1].dx, -2);
    assert_int_equal(loaded.balls[1].dy, -3);
    assert_int_equal(loaded.balls[1].wait_mode, 1);
    assert_int_equal(loaded.balls[2].active, 0);
}

static void test_v1_file_rejected(void **state)
{
    (void)state;
    /* Write a v1-formatted file by hand. */
    FILE *fp = fopen(tmp_path, "w");
    assert_non_null(fp);
    fprintf(fp,
            "{\"version\": 1, \"score\": 1000, \"level\": 3, \"lives_left\": 2,\n"
            " \"start_level\": 1, \"paddle_size\": 50, \"num_bullets\": 4,\n"
            " \"level_time\": 0, \"game_time\": 0}\n");
    fclose(fp);

    savegame_data_t d;
    savegame_io_result_t r = savegame_io_read(tmp_path, &d);
    assert_int_equal(r, SAVEGAME_IO_ERR_VERSION);
}

/* =========================================================================
 * Group 8: save-level.dat round-trip
 * ========================================================================= */

static savegame_level_t make_test_level(void)
{
    savegame_level_t lvl;
    savegame_level_init(&lvl);
    strncpy(lvl.title, "Trying times", LEVEL_TITLE_MAX - 1);
    lvl.time_bonus = 180;
    /* Three occupied cells */
    lvl.cells[0][1] =
        (savegame_cell_t){.occupied = 1, .block_type = 2, .counter_slide = 0, .random = 0,
                          .hit_points = 0, .next_frame_offset = 0};
    lvl.cells[3][2] = (savegame_cell_t){.occupied = 1,
                                        .block_type = 8,
                                        .counter_slide = 3,
                                        .random = 1,
                                        .hit_points = 0,
                                        .next_frame_offset = 0};
    lvl.cells[4][0] = (savegame_cell_t){.occupied = 1,
                                        .block_type = 29,
                                        .counter_slide = 0,
                                        .random = 0,
                                        .hit_points = 2,
                                        .next_frame_offset = 1540};
    return lvl;
}

static void test_level_roundtrip(void **state)
{
    (void)state;
    savegame_level_t orig = make_test_level();

    savegame_io_result_t r = savegame_level_write(tmp_path, &orig);
    assert_int_equal(r, SAVEGAME_IO_OK);

    savegame_level_t loaded;
    r = savegame_level_read(tmp_path, &loaded);
    assert_int_equal(r, SAVEGAME_IO_OK);

    assert_string_equal(loaded.title, "Trying times");
    assert_int_equal(loaded.time_bonus, 180);

    assert_int_equal(loaded.cells[0][1].occupied, 1);
    assert_int_equal(loaded.cells[0][1].block_type, 2);

    assert_int_equal(loaded.cells[3][2].occupied, 1);
    assert_int_equal(loaded.cells[3][2].block_type, 8);
    assert_int_equal(loaded.cells[3][2].counter_slide, 3);
    assert_int_equal(loaded.cells[3][2].random, 1);

    assert_int_equal(loaded.cells[4][0].occupied, 1);
    assert_int_equal(loaded.cells[4][0].block_type, 29);
    assert_int_equal(loaded.cells[4][0].hit_points, 2);
    assert_int_equal(loaded.cells[4][0].next_frame_offset, 1540);

    /* Unoccupied cells stay unoccupied. */
    assert_int_equal(loaded.cells[0][0].occupied, 0);
    assert_int_equal(loaded.cells[17][8].occupied, 0);
}

static void test_level_sparse_omits_empty(void **state)
{
    (void)state;
    savegame_level_t orig;
    savegame_level_init(&orig);
    strncpy(orig.title, "empty", LEVEL_TITLE_MAX - 1);
    orig.time_bonus = 60;

    savegame_io_result_t r = savegame_level_write(tmp_path, &orig);
    assert_int_equal(r, SAVEGAME_IO_OK);

    /* Verify the file contains an empty cells array. */
    FILE *fp = fopen(tmp_path, "r");
    assert_non_null(fp);
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    assert_non_null(buf);
    size_t nread = fread(buf, 1, (size_t)sz, fp);
    assert_int_equal((long)nread, sz);
    buf[sz] = '\0';
    fclose(fp);

    assert_non_null(strstr(buf, "\"cells\": [\n"));
    free(buf);
}

static void test_level_version_rejected(void **state)
{
    (void)state;
    FILE *fp = fopen(tmp_path, "w");
    assert_non_null(fp);
    fprintf(fp,
            "{\"version\": 99, \"title\": \"x\", \"time_bonus\": 0, \"cells\": []}\n");
    fclose(fp);

    savegame_level_t lvl;
    savegame_io_result_t r = savegame_level_read(tmp_path, &lvl);
    assert_int_equal(r, SAVEGAME_IO_ERR_VERSION);
}

static void test_level_null_safety(void **state)
{
    (void)state;
    assert_int_equal(savegame_level_read(NULL, NULL), SAVEGAME_IO_ERR_NULL);
    assert_int_equal(savegame_level_write(NULL, NULL), SAVEGAME_IO_ERR_NULL);
    savegame_level_init(NULL);
}

static void test_level_title_escape_roundtrip(void **state)
{
    (void)state;
    savegame_level_t orig;
    savegame_level_init(&orig);
    /* Title contains both special characters that need escaping. */
    strncpy(orig.title, "Level: \"Escape\\Test\"", LEVEL_TITLE_MAX - 1);
    orig.time_bonus = 100;

    savegame_io_result_t r = savegame_level_write(tmp_path, &orig);
    assert_int_equal(r, SAVEGAME_IO_OK);

    savegame_level_t loaded;
    r = savegame_level_read(tmp_path, &loaded);
    assert_int_equal(r, SAVEGAME_IO_OK);
    assert_string_equal(loaded.title, "Level: \"Escape\\Test\"");
}

static void test_v2_read_non_numeric_value_recovers(void **state)
{
    (void)state;
    /* Hand-author a file with a string where an int is expected.
     * Parser should consume the bogus value and keep parsing. */
    FILE *fp = fopen(tmp_path, "w");
    assert_non_null(fp);
    fprintf(fp,
            "{\n"
            "  \"version\": 2,\n"
            "  \"score\": \"not a number\",\n"
            "  \"level\": 5,\n"
            "  \"lives_left\": 2\n"
            "}\n");
    fclose(fp);

    savegame_data_t d;
    savegame_io_result_t r = savegame_io_read(tmp_path, &d);
    assert_int_equal(r, SAVEGAME_IO_OK);
    /* Bad field defaults to 0; valid fields still load. */
    assert_int_equal((int)d.score, 0);
    assert_int_equal((int)d.level, 5);
    assert_int_equal(d.lives_left, 2);
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
        /* Group 7: v2 round-trip */
        cmocka_unit_test_setup_teardown(test_v2_roundtrip_all_fields, setup_tmpfile,
                                        teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_v1_file_rejected, setup_tmpfile, teardown_tmpfile),
        /* Group 8: save-level.dat */
        cmocka_unit_test_setup_teardown(test_level_roundtrip, setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_level_sparse_omits_empty, setup_tmpfile,
                                        teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_level_version_rejected, setup_tmpfile,
                                        teardown_tmpfile),
        cmocka_unit_test(test_level_null_safety),
        cmocka_unit_test_setup_teardown(test_level_title_escape_roundtrip, setup_tmpfile,
                                        teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_v2_read_non_numeric_value_recovers, setup_tmpfile,
                                        teardown_tmpfile),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
