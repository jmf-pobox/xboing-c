/*
 * test_level_system.c — CMocka tests for level_system module.
 *
 * Characterization tests for the pure C level system port.
 * Tests use real level files from levels/ directory.
 *
 * Test groups:
 *   1. Lifecycle (3 tests)
 *   2. Level wrapping (5 tests)
 *   3. Character mapping (4 tests)
 *   4. File loading (5 tests)
 *   5. Background cycling (3 tests)
 *   6. Error handling (3 tests)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* CMocka must come after setjmp.h */
#include <cmocka.h>

#include "level_system.h"

/* =========================================================================
 * Test level file path — set by CMake define LEVELS_DIR
 * ========================================================================= */

#ifndef LEVELS_DIR
#define LEVELS_DIR "./levels"
#endif

static void make_level_path(char *buf, int bufsize, int level_num)
{
    snprintf(buf, (size_t)bufsize, "%s/level%02d.data", LEVELS_DIR, level_num);
}

/* =========================================================================
 * Stub callback state
 * ========================================================================= */

typedef struct
{
    int add_block_count;
    int last_row;
    int last_col;
    int last_block_type;
    int last_counter_slide;

    /* Track all blocks added for grid verification */
    int grid_types[LEVEL_GRID_ROWS][LEVEL_GRID_COLS];
    int grid_slides[LEVEL_GRID_ROWS][LEVEL_GRID_COLS];
    int grid_occupied[LEVEL_GRID_ROWS][LEVEL_GRID_COLS];
} stub_state_t;

static void reset_stub(stub_state_t *s)
{
    memset(s, 0, sizeof(*s));
    for (int r = 0; r < LEVEL_GRID_ROWS; r++)
    {
        for (int c = 0; c < LEVEL_GRID_COLS; c++)
        {
            s->grid_types[r][c] = NONE_BLK;
        }
    }
}

static void stub_on_add_block(int row, int col, int block_type, int counter_slide, void *ud)
{
    stub_state_t *s = ud;
    s->add_block_count++;
    s->last_row = row;
    s->last_col = col;
    s->last_block_type = block_type;
    s->last_counter_slide = counter_slide;

    if (row >= 0 && row < LEVEL_GRID_ROWS && col >= 0 && col < LEVEL_GRID_COLS)
    {
        s->grid_types[row][col] = block_type;
        s->grid_slides[row][col] = counter_slide;
        s->grid_occupied[row][col] = 1;
    }
}

/* =========================================================================
 * Helper
 * ========================================================================= */

static level_system_t *create_test_ctx(stub_state_t *s)
{
    reset_stub(s);

    level_system_callbacks_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.on_add_block = stub_on_add_block;

    level_system_status_t status;
    level_system_t *ctx = level_system_create(&cb, s, &status);
    assert_non_null(ctx);
    assert_int_equal(status, LEVEL_SYS_OK);
    return ctx;
}

/* =========================================================================
 * Group 1: Lifecycle
 * ========================================================================= */

static void test_create_destroy(void **state)
{
    (void)state;
    level_system_status_t st;
    level_system_t *ctx = level_system_create(NULL, NULL, &st);
    assert_non_null(ctx);
    assert_int_equal(st, LEVEL_SYS_OK);
    level_system_destroy(ctx);
}

static void test_create_initial_state(void **state)
{
    (void)state;
    stub_state_t s;
    level_system_t *ctx = create_test_ctx(&s);

    assert_string_equal(level_system_get_title(ctx), "");
    assert_int_equal(level_system_get_time_bonus(ctx), 0);
    assert_int_equal(level_system_get_background(ctx), 1);

    level_system_destroy(ctx);
}

static void test_destroy_null_safe(void **state)
{
    (void)state;
    level_system_destroy(NULL); /* Should not crash */
}

/* =========================================================================
 * Group 2: Level wrapping
 * ========================================================================= */

static void test_wrap_level_1(void **state)
{
    (void)state;
    assert_int_equal(level_system_wrap_number(1), 1);
}

static void test_wrap_level_80(void **state)
{
    (void)state;
    assert_int_equal(level_system_wrap_number(80), 80);
}

static void test_wrap_level_81(void **state)
{
    (void)state;
    assert_int_equal(level_system_wrap_number(81), 1);
}

static void test_wrap_level_160(void **state)
{
    (void)state;
    assert_int_equal(level_system_wrap_number(160), 80);
}

static void test_wrap_level_161(void **state)
{
    (void)state;
    assert_int_equal(level_system_wrap_number(161), 1);
}

/* =========================================================================
 * Group 3: Character mapping
 * ========================================================================= */

static void test_char_color_blocks(void **state)
{
    (void)state;
    int slide;

    assert_int_equal(level_system_char_to_block('r', &slide), RED_BLK);
    assert_int_equal(slide, 0);

    assert_int_equal(level_system_char_to_block('b', &slide), BLUE_BLK);
    assert_int_equal(level_system_char_to_block('g', &slide), GREEN_BLK);
    assert_int_equal(level_system_char_to_block('t', &slide), TAN_BLK);
    assert_int_equal(level_system_char_to_block('y', &slide), YELLOW_BLK);
    assert_int_equal(level_system_char_to_block('p', &slide), PURPLE_BLK);
}

static void test_char_counter_blocks(void **state)
{
    (void)state;
    int slide;

    assert_int_equal(level_system_char_to_block('0', &slide), COUNTER_BLK);
    assert_int_equal(slide, 0);

    assert_int_equal(level_system_char_to_block('3', &slide), COUNTER_BLK);
    assert_int_equal(slide, 3);

    assert_int_equal(level_system_char_to_block('5', &slide), COUNTER_BLK);
    assert_int_equal(slide, 5);
}

static void test_char_special_blocks(void **state)
{
    (void)state;
    int slide;

    /* Special blocks requiring SHOTS_TO_KILL hits */
    assert_int_equal(level_system_char_to_block('D', &slide), DEATH_BLK);
    assert_int_equal(slide, LEVEL_SHOTS_TO_KILL);

    assert_int_equal(level_system_char_to_block('M', &slide), MGUN_BLK);
    assert_int_equal(slide, LEVEL_SHOTS_TO_KILL);

    assert_int_equal(level_system_char_to_block('W', &slide), WALLOFF_BLK);
    assert_int_equal(slide, LEVEL_SHOTS_TO_KILL);

    assert_int_equal(level_system_char_to_block('m', &slide), MULTIBALL_BLK);
    assert_int_equal(slide, LEVEL_SHOTS_TO_KILL);

    assert_int_equal(level_system_char_to_block('s', &slide), STICKY_BLK);
    assert_int_equal(slide, LEVEL_SHOTS_TO_KILL);

    assert_int_equal(level_system_char_to_block('R', &slide), REVERSE_BLK);
    assert_int_equal(slide, LEVEL_SHOTS_TO_KILL);

    assert_int_equal(level_system_char_to_block('<', &slide), PAD_SHRINK_BLK);
    assert_int_equal(slide, LEVEL_SHOTS_TO_KILL);

    assert_int_equal(level_system_char_to_block('>', &slide), PAD_EXPAND_BLK);
    assert_int_equal(slide, LEVEL_SHOTS_TO_KILL);

    /* Non-special blocks with counter_slide=0 */
    assert_int_equal(level_system_char_to_block('H', &slide), HYPERSPACE_BLK);
    assert_int_equal(slide, 0);

    assert_int_equal(level_system_char_to_block('B', &slide), BULLET_BLK);
    assert_int_equal(slide, 0);

    assert_int_equal(level_system_char_to_block('+', &slide), ROAMER_BLK);
    assert_int_equal(slide, 0);

    assert_int_equal(level_system_char_to_block('X', &slide), BOMB_BLK);
    assert_int_equal(slide, 0);

    assert_int_equal(level_system_char_to_block('?', &slide), RANDOM_BLK);
    assert_int_equal(slide, 0);

    assert_int_equal(level_system_char_to_block('d', &slide), DROP_BLK);
    assert_int_equal(slide, 0);

    assert_int_equal(level_system_char_to_block('T', &slide), TIMER_BLK);
    assert_int_equal(slide, 0);

    assert_int_equal(level_system_char_to_block('L', &slide), EXTRABALL_BLK);
    assert_int_equal(slide, 0);

    assert_int_equal(level_system_char_to_block('c', &slide), MAXAMMO_BLK);
    assert_int_equal(slide, 0);
}

static void test_char_empty_cell(void **state)
{
    (void)state;
    int slide;

    assert_int_equal(level_system_char_to_block('.', &slide), NONE_BLK);
    assert_int_equal(slide, 0);

    /* Unknown characters also map to NONE_BLK */
    assert_int_equal(level_system_char_to_block('Z', &slide), NONE_BLK);
    assert_int_equal(level_system_char_to_block(' ', &slide), NONE_BLK);
}

/* =========================================================================
 * Group 4: File loading
 * ========================================================================= */

static void test_load_level01(void **state)
{
    (void)state;
    stub_state_t s;
    level_system_t *ctx = create_test_ctx(&s);

    char path[512];
    make_level_path(path, (int)sizeof(path), 1);

    level_system_status_t st = level_system_load_file(ctx, path);
    assert_int_equal(st, LEVEL_SYS_OK);

    assert_string_equal(level_system_get_title(ctx), "Genesis");
    assert_int_equal(level_system_get_time_bonus(ctx), 120);

    level_system_destroy(ctx);
}

static void test_load_level01_color_blocks(void **state)
{
    (void)state;
    stub_state_t s;
    level_system_t *ctx = create_test_ctx(&s);

    char path[512];
    make_level_path(path, (int)sizeof(path), 1);

    level_system_load_file(ctx, path);

    /* Row 2: all RED_BLK */
    for (int c = 0; c < 9; c++)
    {
        assert_int_equal(s.grid_types[2][c], RED_BLK);
    }

    /* Row 3: all BLUE_BLK */
    for (int c = 0; c < 9; c++)
    {
        assert_int_equal(s.grid_types[3][c], BLUE_BLK);
    }

    /* Row 4: all GREEN_BLK */
    for (int c = 0; c < 9; c++)
    {
        assert_int_equal(s.grid_types[4][c], GREEN_BLK);
    }

    /* Row 5: all TAN_BLK */
    for (int c = 0; c < 9; c++)
    {
        assert_int_equal(s.grid_types[5][c], TAN_BLK);
    }

    /* Row 8: all COUNTER_BLK with slide=0 */
    for (int c = 0; c < 9; c++)
    {
        assert_int_equal(s.grid_types[8][c], COUNTER_BLK);
        assert_int_equal(s.grid_slides[8][c], 0);
    }

    /* Row 9: all YELLOW_BLK */
    for (int c = 0; c < 9; c++)
    {
        assert_int_equal(s.grid_types[9][c], YELLOW_BLK);
    }

    /* Row 10: all PURPLE_BLK */
    for (int c = 0; c < 9; c++)
    {
        assert_int_equal(s.grid_types[10][c], PURPLE_BLK);
    }

    /* Row 11: BULLET at cols 0, 4, 8; empty at 1-3, 5-7 */
    assert_int_equal(s.grid_types[11][0], BULLET_BLK);
    assert_int_equal(s.grid_types[11][4], BULLET_BLK);
    assert_int_equal(s.grid_types[11][8], BULLET_BLK);
    assert_int_equal(s.grid_occupied[11][1], 0);
    assert_int_equal(s.grid_occupied[11][2], 0);

    /* Rows 0-1, 6-7, 12-14: all empty */
    for (int c = 0; c < 9; c++)
    {
        assert_int_equal(s.grid_occupied[0][c], 0);
        assert_int_equal(s.grid_occupied[1][c], 0);
        assert_int_equal(s.grid_occupied[6][c], 0);
        assert_int_equal(s.grid_occupied[7][c], 0);
        assert_int_equal(s.grid_occupied[12][c], 0);
        assert_int_equal(s.grid_occupied[13][c], 0);
        assert_int_equal(s.grid_occupied[14][c], 0);
    }

    level_system_destroy(ctx);
}

static void test_load_level03_mixed(void **state)
{
    (void)state;
    stub_state_t s;
    level_system_t *ctx = create_test_ctx(&s);

    char path[512];
    make_level_path(path, (int)sizeof(path), 3);

    level_system_status_t st = level_system_load_file(ctx, path);
    assert_int_equal(st, LEVEL_SYS_OK);

    assert_string_equal(level_system_get_title(ctx), "Make my day!");
    assert_int_equal(level_system_get_time_bonus(ctx), 210);

    /* Row 0: all ROAMER */
    for (int c = 0; c < 9; c++)
    {
        assert_int_equal(s.grid_types[0][c], ROAMER_BLK);
    }

    /* Row 5: COUNTER with slide=2 */
    for (int c = 0; c < 9; c++)
    {
        assert_int_equal(s.grid_types[5][c], COUNTER_BLK);
        assert_int_equal(s.grid_slides[5][c], 2);
    }

    /* Row 6: all DROP */
    for (int c = 0; c < 9; c++)
    {
        assert_int_equal(s.grid_types[6][c], DROP_BLK);
    }

    /* Rows 7-8: all RANDOM */
    for (int c = 0; c < 9; c++)
    {
        assert_int_equal(s.grid_types[7][c], RANDOM_BLK);
        assert_int_equal(s.grid_types[8][c], RANDOM_BLK);
    }

    /* Row 2: .wBBMBBw. */
    assert_int_equal(s.grid_occupied[2][0], 0); /* '.' */
    assert_int_equal(s.grid_types[2][1], BLACK_BLK);
    assert_int_equal(s.grid_types[2][2], BULLET_BLK);
    assert_int_equal(s.grid_types[2][3], BULLET_BLK);
    assert_int_equal(s.grid_types[2][4], MGUN_BLK);
    assert_int_equal(s.grid_slides[2][4], LEVEL_SHOTS_TO_KILL);
    assert_int_equal(s.grid_types[2][5], BULLET_BLK);
    assert_int_equal(s.grid_types[2][7], BLACK_BLK);
    assert_int_equal(s.grid_occupied[2][8], 0); /* '.' */

    /* Row 11: X.pp.pp.X — BOMB at cols 0, 8; PURPLE at 2-3, 5-6 */
    assert_int_equal(s.grid_types[11][0], BOMB_BLK);
    assert_int_equal(s.grid_types[11][8], BOMB_BLK);
    assert_int_equal(s.grid_types[11][2], PURPLE_BLK);
    assert_int_equal(s.grid_types[11][5], PURPLE_BLK);
    assert_int_equal(s.grid_occupied[11][1], 0);
    assert_int_equal(s.grid_occupied[11][4], 0);

    level_system_destroy(ctx);
}

static void test_load_level80_boundary(void **state)
{
    (void)state;
    stub_state_t s;
    level_system_t *ctx = create_test_ctx(&s);

    char path[512];
    make_level_path(path, (int)sizeof(path), 80);

    level_system_status_t st = level_system_load_file(ctx, path);
    assert_int_equal(st, LEVEL_SYS_OK);

    assert_string_equal(level_system_get_title(ctx), "Test Pattern");
    assert_int_equal(level_system_get_time_bonus(ctx), 100);

    /* Verify some blocks were loaded */
    assert_true(s.add_block_count > 0);

    level_system_destroy(ctx);
}

static void test_load_no_callback_safe(void **state)
{
    (void)state;
    level_system_t *ctx = level_system_create(NULL, NULL, NULL);
    assert_non_null(ctx);

    char path[512];
    make_level_path(path, (int)sizeof(path), 1);

    /* Should parse without crashing even with no callback */
    level_system_status_t st = level_system_load_file(ctx, path);
    assert_int_equal(st, LEVEL_SYS_OK);

    assert_string_equal(level_system_get_title(ctx), "Genesis");
    assert_int_equal(level_system_get_time_bonus(ctx), 120);

    level_system_destroy(ctx);
}

/* =========================================================================
 * Group 5: Background cycling
 * ========================================================================= */

static void test_background_initial(void **state)
{
    (void)state;
    stub_state_t s;
    level_system_t *ctx = create_test_ctx(&s);

    /* Initial background is 1 (before any level loads) */
    assert_int_equal(level_system_get_background(ctx), 1);

    level_system_destroy(ctx);
}

static void test_background_advance_cycle(void **state)
{
    (void)state;
    stub_state_t s;
    level_system_t *ctx = create_test_ctx(&s);

    /* First advance: 1 → 2 */
    assert_int_equal(level_system_advance_background(ctx), 2);
    assert_int_equal(level_system_get_background(ctx), 2);

    /* Cycle through: 2 → 3 → 4 → 5 → 2 */
    assert_int_equal(level_system_advance_background(ctx), 3);
    assert_int_equal(level_system_advance_background(ctx), 4);
    assert_int_equal(level_system_advance_background(ctx), 5);
    assert_int_equal(level_system_advance_background(ctx), 2); /* Wraps */

    level_system_destroy(ctx);
}

static void test_background_many_cycles(void **state)
{
    (void)state;
    stub_state_t s;
    level_system_t *ctx = create_test_ctx(&s);

    /* Run through multiple full cycles to verify stability */
    for (int i = 0; i < 20; i++)
    {
        int bg = level_system_advance_background(ctx);
        assert_true(bg >= LEVEL_BG_FIRST && bg <= LEVEL_BG_LAST);
    }

    level_system_destroy(ctx);
}

/* =========================================================================
 * Group 6: Error handling
 * ========================================================================= */

static void test_load_missing_file(void **state)
{
    (void)state;
    stub_state_t s;
    level_system_t *ctx = create_test_ctx(&s);

    level_system_status_t st = level_system_load_file(ctx, "/nonexistent/path/level99.data");
    assert_int_equal(st, LEVEL_SYS_ERR_FILE_NOT_FOUND);

    /* State should be unchanged */
    assert_string_equal(level_system_get_title(ctx), "");
    assert_int_equal(level_system_get_time_bonus(ctx), 0);

    level_system_destroy(ctx);
}

static void test_load_null_args(void **state)
{
    (void)state;
    stub_state_t s;
    level_system_t *ctx = create_test_ctx(&s);

    assert_int_equal(level_system_load_file(NULL, "/some/path"), LEVEL_SYS_ERR_NULL_ARG);
    assert_int_equal(level_system_load_file(ctx, NULL), LEVEL_SYS_ERR_NULL_ARG);

    level_system_destroy(ctx);
}

static void test_status_strings(void **state)
{
    (void)state;
    assert_string_equal(level_system_status_string(LEVEL_SYS_OK), "LEVEL_SYS_OK");
    assert_string_equal(level_system_status_string(LEVEL_SYS_ERR_FILE_NOT_FOUND),
                        "LEVEL_SYS_ERR_FILE_NOT_FOUND");
}

/* =========================================================================
 * Main — register all test groups
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_create_initial_state),
        cmocka_unit_test(test_destroy_null_safe),

        /* Group 2: Level wrapping */
        cmocka_unit_test(test_wrap_level_1),
        cmocka_unit_test(test_wrap_level_80),
        cmocka_unit_test(test_wrap_level_81),
        cmocka_unit_test(test_wrap_level_160),
        cmocka_unit_test(test_wrap_level_161),

        /* Group 3: Character mapping */
        cmocka_unit_test(test_char_color_blocks),
        cmocka_unit_test(test_char_counter_blocks),
        cmocka_unit_test(test_char_special_blocks),
        cmocka_unit_test(test_char_empty_cell),

        /* Group 4: File loading */
        cmocka_unit_test(test_load_level01),
        cmocka_unit_test(test_load_level01_color_blocks),
        cmocka_unit_test(test_load_level03_mixed),
        cmocka_unit_test(test_load_level80_boundary),
        cmocka_unit_test(test_load_no_callback_safe),

        /* Group 5: Background cycling */
        cmocka_unit_test(test_background_initial),
        cmocka_unit_test(test_background_advance_cycle),
        cmocka_unit_test(test_background_many_cycles),

        /* Group 6: Error handling */
        cmocka_unit_test(test_load_missing_file),
        cmocka_unit_test(test_load_null_args),
        cmocka_unit_test(test_status_strings),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
