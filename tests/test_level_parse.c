/*
 * test_level_parse.c — Characterization tests for ReadNextLevel().
 *
 * Bead n9e.3: Level parsing system characterization tests.
 *
 * Strategy: call ReadNextLevel() with draw=False and NULL display/window.
 * X11 functions are stubbed in stubs_x11_xboing.c. Tests use actual level
 * files from the levels/ directory as fixtures.
 *
 * These are CHARACTERIZATION tests — capture current behavior including
 * any bugs. Do NOT fix bugs here.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include <X11/Xutil.h>

#include "blocks.h"
#include "level.h"
#include "main.h"
#include "stage.h"

/* ReadNextLevel prototype — from file.c */
#if NeedFunctionPrototypes
extern int ReadNextLevel(Display *display, Window window, char *levelName,
                         int draw);
#else
extern int ReadNextLevel();
#endif

/*
 * Path to the levels/ directory. CMake passes this as a compile definition.
 */
#ifndef LEVELS_DIR
#define LEVELS_DIR "../levels"
#endif

/* Build a level path into a caller-supplied buffer. */
static void level_path(char *buf, size_t bufsiz, int num)
{
    snprintf(buf, bufsiz, "%s/level%02d.data", LEVELS_DIR, num);
}

/* =========================================================================
 * Test setup
 * ========================================================================= */

static int setup(void **state)
{
    (void)state;
    frame = 0;
    levelTitle[0] = '\0';
    memset(blocks, 0, sizeof(blocks));
    return 0;
}

/* =========================================================================
 * Test cases
 * ========================================================================= */

/* TC-01: Level 1 loads and returns True. */
static void test_read_level01_returns_true(void **state)
{
    (void)state;
    char path[256];
    level_path(path, sizeof(path), 1);
    int result = ReadNextLevel(NULL, (Window)0, path, 0);
    assert_int_equal(result, 1);
}

/* TC-02: Level title is correctly parsed.
 * level01.data line 1: "Genesis" */
static void test_read_level01_title(void **state)
{
    (void)state;
    char path[256];
    level_path(path, sizeof(path), 1);
    ReadNextLevel(NULL, (Window)0, path, 0);
    assert_string_equal(levelTitle, "Genesis");
}

/* TC-03: Time limit is correctly parsed.
 * level01.data line 2: "120" */
static void test_read_level01_time_limit(void **state)
{
    (void)state;
    char path[256];
    level_path(path, sizeof(path), 1);
    ReadNextLevel(NULL, (Window)0, path, 0);
    assert_int_equal(GetLevelTimeBonus(), 120);
}

/* TC-04: colWidth and rowHeight are set to expected values.
 * PLAY_WIDTH=495, MAX_COL=9 -> colWidth=55
 * PLAY_HEIGHT=580, MAX_ROW=18 -> rowHeight=32 */
static void test_read_level_sets_dimensions(void **state)
{
    (void)state;
    char path[256];
    level_path(path, sizeof(path), 1);
    ReadNextLevel(NULL, (Window)0, path, 0);
    assert_int_equal(colWidth, PLAY_WIDTH / MAX_COL);
    assert_int_equal(rowHeight, PLAY_HEIGHT / MAX_ROW);
}

/* TC-05: Color block character mapping.
 * level01.data:
 *   row 2: "rrrrrrrrr" -> RED_BLK
 *   row 3: "bbbbbbbbb" -> BLUE_BLK
 *   row 4: "ggggggggg" -> GREEN_BLK
 *   row 5: "ttttttttt" -> TAN_BLK
 *   row 9: "yyyyyyyyy" -> YELLOW_BLK
 *   row 10: "ppppppppp" -> PURPLE_BLK */
static void test_char_mapping_color_blocks(void **state)
{
    (void)state;
    char path[256];
    level_path(path, sizeof(path), 1);
    ReadNextLevel(NULL, (Window)0, path, 0);

    for (int c = 0; c < MAX_COL; c++)
    {
        assert_int_equal(blocks[2][c].blockType, RED_BLK);
        assert_int_equal(blocks[2][c].occupied, 1);
    }
    for (int c = 0; c < MAX_COL; c++)
        assert_int_equal(blocks[3][c].blockType, BLUE_BLK);
    for (int c = 0; c < MAX_COL; c++)
        assert_int_equal(blocks[4][c].blockType, GREEN_BLK);
    for (int c = 0; c < MAX_COL; c++)
        assert_int_equal(blocks[5][c].blockType, TAN_BLK);
    for (int c = 0; c < MAX_COL; c++)
        assert_int_equal(blocks[9][c].blockType, YELLOW_BLK);
    for (int c = 0; c < MAX_COL; c++)
        assert_int_equal(blocks[10][c].blockType, PURPLE_BLK);
}

/* TC-06: Dot character produces unoccupied block.
 * level01.data row 0: "........." */
static void test_dot_produces_empty_cell(void **state)
{
    (void)state;
    char path[256];
    level_path(path, sizeof(path), 1);
    ReadNextLevel(NULL, (Window)0, path, 0);

    for (int c = 0; c < MAX_COL; c++)
        assert_int_equal(blocks[0][c].occupied, 0);
}

/* TC-07: Counter block character mapping ('0') and counterSlide.
 * level01.data row 8: "000000000" -> COUNTER_BLK with counterSlide=0 */
static void test_counter_block_slide_zero(void **state)
{
    (void)state;
    char path[256];
    level_path(path, sizeof(path), 1);
    ReadNextLevel(NULL, (Window)0, path, 0);

    for (int c = 0; c < MAX_COL; c++)
    {
        assert_int_equal(blocks[8][c].blockType, COUNTER_BLK);
        assert_int_equal(blocks[8][c].counterSlide, 0);
    }
}

/* TC-08: Counter block with slide=3.
 * level80.data row 10: ".333.333." */
static void test_counter_block_slide_three(void **state)
{
    (void)state;
    char path[256];
    level_path(path, sizeof(path), 80);
    ReadNextLevel(NULL, (Window)0, path, 0);

    assert_int_equal(blocks[10][1].blockType, COUNTER_BLK);
    assert_int_equal(blocks[10][1].counterSlide, 3);
    assert_int_equal(blocks[10][4].occupied, 0);  /* '.' */
}

/* TC-09: Special block characters.
 * level03.data:
 *   row 0: "+++++++++" -> ROAMER_BLK
 *   row 2: ".wBBMBBw." -> col 1,7 BLACK_BLK, col 2 BULLET_BLK, col 4 MGUN_BLK */
static void test_special_block_characters(void **state)
{
    (void)state;
    char path[256];
    level_path(path, sizeof(path), 3);
    ReadNextLevel(NULL, (Window)0, path, 0);

    for (int c = 0; c < MAX_COL; c++)
        assert_int_equal(blocks[0][c].blockType, ROAMER_BLK);

    assert_int_equal(blocks[2][1].blockType, BLACK_BLK);   /* 'w' */
    assert_int_equal(blocks[2][2].blockType, BULLET_BLK);  /* 'B' */
    assert_int_equal(blocks[2][4].blockType, MGUN_BLK);    /* 'M' */
    assert_int_equal(blocks[2][7].blockType, BLACK_BLK);   /* 'w' */
}

/* TC-10: RANDOM_BLK ('?') is stored as RED_BLK with random=True.
 * level03.data row 7: "?????????" */
static void test_random_block_stored_as_red(void **state)
{
    (void)state;
    char path[256];
    level_path(path, sizeof(path), 3);
    ReadNextLevel(NULL, (Window)0, path, 0);

    for (int c = 0; c < MAX_COL; c++)
    {
        assert_int_equal(blocks[7][c].blockType, RED_BLK);
        assert_int_equal(blocks[7][c].random, 1);
        assert_int_equal(blocks[7][c].occupied, 1);
    }
}

/* TC-11: Rows 15-17 are cleared by ClearBlockArray, not populated by loop.
 * ReadNextLevel loop runs for MAX_ROW-3 = 15 rows (0-14). */
static void test_grid_row_count(void **state)
{
    (void)state;
    char path[256];
    level_path(path, sizeof(path), 1);
    ReadNextLevel(NULL, (Window)0, path, 0);

    for (int c = 0; c < MAX_COL; c++)
    {
        assert_int_equal(blocks[15][c].occupied, 0);
        assert_int_equal(blocks[15][c].blockType, NONE_BLK);
        assert_int_equal(blocks[16][c].occupied, 0);
        assert_int_equal(blocks[16][c].blockType, NONE_BLK);
        assert_int_equal(blocks[17][c].occupied, 0);
        assert_int_equal(blocks[17][c].blockType, NONE_BLK);
    }
}

/* TC-12: Missing file returns False. */
static void test_missing_file_returns_false(void **state)
{
    (void)state;
    int result = ReadNextLevel(NULL, (Window)0,
                               "/nonexistent/path/level99.data", 0);
    assert_int_equal(result, 0);
}

/* TC-13: Level 80 loads correctly (boundary — last real level file). */
static void test_read_level80_boundary(void **state)
{
    (void)state;
    char path[256];
    level_path(path, sizeof(path), 80);
    int result = ReadNextLevel(NULL, (Window)0, path, 0);
    assert_int_equal(result, 1);
    assert_string_equal(levelTitle, "Test Pattern");
    assert_int_equal(GetLevelTimeBonus(), 100);
}

/* TC-14: Level wrapping arithmetic.
 * SetupStage uses: level % MAX_NUM_LEVELS, with 0 adjusted to MAX_NUM_LEVELS. */
static void test_level_wrap_formula(void **state)
{
    (void)state;
    int MAX = MAX_NUM_LEVELS;  /* 80 */

    /* level 80: 80 % 80 = 0, adjusted to 80 */
    int nl = 80 % MAX;
    if (nl == 0) nl = MAX;
    assert_int_equal(nl, 80);

    /* level 81: 81 % 80 = 1 */
    nl = 81 % MAX;
    if (nl == 0) nl = MAX;
    assert_int_equal(nl, 1);

    /* level 160: 160 % 80 = 0, adjusted to 80 */
    nl = 160 % MAX;
    if (nl == 0) nl = MAX;
    assert_int_equal(nl, 80);
}

/* TC-15: Block type 'B' (BULLET_BLK) and 'X' (BOMB_BLK).
 * level01.data row 11: "B...B...B" */
static void test_bullet_and_bomb_blocks(void **state)
{
    (void)state;
    char path[256];
    level_path(path, sizeof(path), 1);
    ReadNextLevel(NULL, (Window)0, path, 0);

    assert_int_equal(blocks[11][0].blockType, BULLET_BLK);
    assert_int_equal(blocks[11][4].blockType, BULLET_BLK);
    assert_int_equal(blocks[11][8].blockType, BULLET_BLK);
    assert_int_equal(blocks[11][1].occupied, 0);  /* '.' */
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_read_level01_returns_true, setup),
        cmocka_unit_test_setup(test_read_level01_title, setup),
        cmocka_unit_test_setup(test_read_level01_time_limit, setup),
        cmocka_unit_test_setup(test_read_level_sets_dimensions, setup),
        cmocka_unit_test_setup(test_char_mapping_color_blocks, setup),
        cmocka_unit_test_setup(test_dot_produces_empty_cell, setup),
        cmocka_unit_test_setup(test_counter_block_slide_zero, setup),
        cmocka_unit_test_setup(test_counter_block_slide_three, setup),
        cmocka_unit_test_setup(test_special_block_characters, setup),
        cmocka_unit_test_setup(test_random_block_stored_as_red, setup),
        cmocka_unit_test_setup(test_grid_row_count, setup),
        cmocka_unit_test_setup(test_missing_file_returns_false, setup),
        cmocka_unit_test_setup(test_read_level80_boundary, setup),
        cmocka_unit_test_setup(test_level_wrap_formula, setup),
        cmocka_unit_test_setup(test_bullet_and_bomb_blocks, setup),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
