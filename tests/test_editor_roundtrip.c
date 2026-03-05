/*
 * test_editor_roundtrip.c — Round-trip tests for editor save → level load.
 *
 * Tests the full cycle: draw blocks in editor grid → write level file →
 * load level file via level_system → verify grid matches.
 *
 * Also tests exhaustive character mapping: every block_type_to_char character
 * round-trips through level_system_char_to_block() correctly.
 *
 * The editor save format is:
 *   Line 1: title string
 *   Line 2: time bonus (seconds)
 *   Lines 3-17: 15 rows of 9 characters (block grid)
 *
 * Since block_type_to_char() is static in game_callbacks.c, we duplicate
 * the mapping here as a test-local lookup table.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cmocka.h>

#include "block_types.h"
#include "level_system.h"

/* =========================================================================
 * Test-local block_type → char mapping (mirrors game_callbacks.c)
 * ========================================================================= */

static char test_block_type_to_char(int block_type)
{
    switch (block_type)
    {
        case RED_BLK:
            return 'r';
        case BLUE_BLK:
            return 'b';
        case GREEN_BLK:
            return 'g';
        case TAN_BLK:
            return 't';
        case YELLOW_BLK:
            return 'y';
        case PURPLE_BLK:
            return 'p';
        case BULLET_BLK:
            return 'B';
        case BLACK_BLK:
            return 'w';
        case COUNTER_BLK:
            return '0'; /* default counter char; slides handled separately */
        case BOMB_BLK:
            return 'X';
        case DEATH_BLK:
            return 'D';
        case HYPERSPACE_BLK:
            return 'H';
        case MAXAMMO_BLK:
            return 'c';
        case ROAMER_BLK:
            return '+';
        case EXTRABALL_BLK:
            return 'L';
        case MGUN_BLK:
            return 'M';
        case WALLOFF_BLK:
            return 'W';
        case RANDOM_BLK:
            return '?';
        case DROP_BLK:
            return 'd';
        case TIMER_BLK:
            return 'T';
        case MULTIBALL_BLK:
            return 'm';
        case STICKY_BLK:
            return 's';
        case REVERSE_BLK:
            return 'R';
        case PAD_SHRINK_BLK:
            return '<';
        case PAD_EXPAND_BLK:
            return '>';
        default:
            return '.';
    }
}

/* =========================================================================
 * Mapping table for exhaustive char round-trip tests
 * ========================================================================= */

typedef struct
{
    char ch;
    int block_type;
    int counter_slide;
} char_mapping_t;

/* Every valid level file character and its expected block type / counter_slide */
static const char_mapping_t CHAR_MAP[] = {
    {'r', RED_BLK, 0},
    {'g', GREEN_BLK, 0},
    {'b', BLUE_BLK, 0},
    {'t', TAN_BLK, 0},
    {'y', YELLOW_BLK, 0},
    {'p', PURPLE_BLK, 0},
    {'w', BLACK_BLK, 0},
    {'B', BULLET_BLK, 0},
    {'H', HYPERSPACE_BLK, 0},
    {'c', MAXAMMO_BLK, 0},
    {'+', ROAMER_BLK, 0},
    {'X', BOMB_BLK, 0},
    {'D', DEATH_BLK, LEVEL_SHOTS_TO_KILL},
    {'L', EXTRABALL_BLK, 0},
    {'M', MGUN_BLK, LEVEL_SHOTS_TO_KILL},
    {'W', WALLOFF_BLK, LEVEL_SHOTS_TO_KILL},
    {'?', RANDOM_BLK, 0},
    {'d', DROP_BLK, 0},
    {'T', TIMER_BLK, 0},
    {'m', MULTIBALL_BLK, LEVEL_SHOTS_TO_KILL},
    {'s', STICKY_BLK, LEVEL_SHOTS_TO_KILL},
    {'R', REVERSE_BLK, LEVEL_SHOTS_TO_KILL},
    {'<', PAD_SHRINK_BLK, LEVEL_SHOTS_TO_KILL},
    {'>', PAD_EXPAND_BLK, LEVEL_SHOTS_TO_KILL},
    /* Counter block variants */
    {'0', COUNTER_BLK, 0},
    {'1', COUNTER_BLK, 1},
    {'2', COUNTER_BLK, 2},
    {'3', COUNTER_BLK, 3},
    {'4', COUNTER_BLK, 4},
    {'5', COUNTER_BLK, 5},
    /* Empty cell */
    {'.', NONE_BLK, 0},
};

#define CHAR_MAP_COUNT (sizeof(CHAR_MAP) / sizeof(CHAR_MAP[0]))

/* =========================================================================
 * Test grid — records blocks added during level_system_load_file()
 * ========================================================================= */

typedef struct
{
    int occupied;
    int block_type;
    int counter_slide;
} test_cell_t;

typedef struct
{
    test_cell_t grid[LEVEL_GRID_ROWS][LEVEL_GRID_COLS];
    int add_count;
} load_state_t;

static void on_add_block(int row, int col, int block_type, int counter_slide, void *ud)
{
    load_state_t *s = (load_state_t *)ud;
    if (row >= 0 && row < LEVEL_GRID_ROWS && col >= 0 && col < LEVEL_GRID_COLS)
    {
        s->grid[row][col].occupied = 1;
        s->grid[row][col].block_type = block_type;
        s->grid[row][col].counter_slide = counter_slide;
    }
    s->add_count++;
}

static void reset_load_state(load_state_t *s)
{
    memset(s, 0, sizeof(*s));
    for (int r = 0; r < LEVEL_GRID_ROWS; r++)
        for (int c = 0; c < LEVEL_GRID_COLS; c++)
            s->grid[r][c].block_type = NONE_BLK;
}

/* =========================================================================
 * Helper: write a level file from a block type grid
 * ========================================================================= */

static int write_level_file(const char *path, const char *title, int time_bonus,
                            /* cppcheck-suppress constParameter */
                            int grid[LEVEL_GRID_ROWS][LEVEL_GRID_COLS])
{
    FILE *fp = fopen(path, "w");
    if (!fp)
        return 0;

    fprintf(fp, "%s\n", title);
    fprintf(fp, "%d\n", time_bonus);

    for (int row = 0; row < LEVEL_GRID_ROWS; row++)
    {
        for (int col = 0; col < LEVEL_GRID_COLS; col++)
        {
            fputc(test_block_type_to_char(grid[row][col]), fp);
        }
        fputc('\n', fp);
    }

    fclose(fp);
    return 1;
}

/* =========================================================================
 * Fixture: creates a temp file and level_system
 * ========================================================================= */

typedef struct
{
    char tmppath[256];
    level_system_t *level;
    load_state_t load;
} test_fixture_t;

static int setup(void **vstate)
{
    test_fixture_t *f = calloc(1, sizeof(*f));
    assert_non_null(f);

    snprintf(f->tmppath, sizeof(f->tmppath), "/tmp/test_editor_rt_XXXXXX");
    int fd = mkstemp(f->tmppath);
    assert_true(fd >= 0);
    close(fd);

    reset_load_state(&f->load);

    level_system_callbacks_t cb = {.on_add_block = on_add_block};
    level_system_status_t status;
    f->level = level_system_create(&cb, &f->load, &status);
    assert_non_null(f->level);
    assert_int_equal(status, LEVEL_SYS_OK);

    *vstate = f;
    return 0;
}

static int teardown(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    level_system_destroy(f->level);
    unlink(f->tmppath);
    free(f);
    return 0;
}

/* =========================================================================
 * Section 1: Character mapping exhaustive tests
 * ========================================================================= */

static void test_char_to_block_exhaustive(void **vstate)
{
    (void)vstate;

    for (size_t i = 0; i < CHAR_MAP_COUNT; i++)
    {
        int counter_slide = -1;
        int block_type = level_system_char_to_block(CHAR_MAP[i].ch, &counter_slide);
        assert_int_equal(block_type, CHAR_MAP[i].block_type);
        assert_int_equal(counter_slide, CHAR_MAP[i].counter_slide);
    }
}

static void test_unknown_chars_map_to_none(void **vstate)
{
    (void)vstate;

    /* Characters not in the mapping should yield NONE_BLK */
    const char unknowns[] = "AEFGIJKNOPQSUVYZaefikjlnoqv!@#$%^&*(){}[]|\\:;\"'~`/ ";
    for (size_t i = 0; i < sizeof(unknowns) - 1; i++)
    {
        int counter_slide = -1;
        int block_type = level_system_char_to_block(unknowns[i], &counter_slide);
        assert_int_equal(block_type, NONE_BLK);
    }
}

static void test_block_type_to_char_round_trip(void **vstate)
{
    (void)vstate;

    /*
     * For each non-counter block type, verify:
     *   block_type → char → level_system_char_to_block → same block_type
     */
    int block_types[] = {
        RED_BLK,        BLUE_BLK,       GREEN_BLK,    TAN_BLK,       YELLOW_BLK,
        PURPLE_BLK,     BULLET_BLK,     BLACK_BLK,    BOMB_BLK,      DEATH_BLK,
        HYPERSPACE_BLK, MAXAMMO_BLK,    ROAMER_BLK,   EXTRABALL_BLK, MGUN_BLK,
        WALLOFF_BLK,    RANDOM_BLK,     DROP_BLK,     TIMER_BLK,     MULTIBALL_BLK,
        STICKY_BLK,     REVERSE_BLK,    PAD_SHRINK_BLK, PAD_EXPAND_BLK,
    };
    int count = (int)(sizeof(block_types) / sizeof(block_types[0]));

    for (int i = 0; i < count; i++)
    {
        char ch = test_block_type_to_char(block_types[i]);
        assert_true(ch != '.');

        int counter_slide = -1;
        int result = level_system_char_to_block(ch, &counter_slide);
        assert_int_equal(result, block_types[i]);
    }
}

/* =========================================================================
 * Section 2: File write → load round-trip
 * ========================================================================= */

static void test_empty_grid_round_trip(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Write an empty grid */
    int grid[LEVEL_GRID_ROWS][LEVEL_GRID_COLS];
    memset(grid, 0, sizeof(grid));
    for (int r = 0; r < LEVEL_GRID_ROWS; r++)
        for (int c = 0; c < LEVEL_GRID_COLS; c++)
            grid[r][c] = NONE_BLK;

    assert_true(write_level_file(f->tmppath, "Empty Level", 120, grid));

    /* Load it */
    level_system_status_t status = level_system_load_file(f->level, f->tmppath);
    assert_int_equal(status, LEVEL_SYS_OK);

    /* No blocks should have been added */
    assert_int_equal(f->load.add_count, 0);

    /* Title and time bonus */
    assert_string_equal(level_system_get_title(f->level), "Empty Level");
    assert_int_equal(level_system_get_time_bonus(f->level), 120);
}

static void test_full_grid_round_trip(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Fill grid with RED blocks */
    int grid[LEVEL_GRID_ROWS][LEVEL_GRID_COLS];
    for (int r = 0; r < LEVEL_GRID_ROWS; r++)
        for (int c = 0; c < LEVEL_GRID_COLS; c++)
            grid[r][c] = RED_BLK;

    assert_true(write_level_file(f->tmppath, "Full Red", 180, grid));

    level_system_status_t status = level_system_load_file(f->level, f->tmppath);
    assert_int_equal(status, LEVEL_SYS_OK);

    /* Every cell should have RED_BLK */
    assert_int_equal(f->load.add_count, LEVEL_GRID_ROWS * LEVEL_GRID_COLS);
    for (int r = 0; r < LEVEL_GRID_ROWS; r++)
    {
        for (int c = 0; c < LEVEL_GRID_COLS; c++)
        {
            assert_true(f->load.grid[r][c].occupied);
            assert_int_equal(f->load.grid[r][c].block_type, RED_BLK);
            assert_int_equal(f->load.grid[r][c].counter_slide, 0);
        }
    }

    assert_string_equal(level_system_get_title(f->level), "Full Red");
    assert_int_equal(level_system_get_time_bonus(f->level), 180);
}

static void test_mixed_block_types_round_trip(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Place a different block type in each cell of the first two rows */
    int grid[LEVEL_GRID_ROWS][LEVEL_GRID_COLS];
    memset(grid, 0, sizeof(grid));
    for (int r = 0; r < LEVEL_GRID_ROWS; r++)
        for (int c = 0; c < LEVEL_GRID_COLS; c++)
            grid[r][c] = NONE_BLK;

    /* Row 0: basic colors */
    grid[0][0] = RED_BLK;
    grid[0][1] = BLUE_BLK;
    grid[0][2] = GREEN_BLK;
    grid[0][3] = TAN_BLK;
    grid[0][4] = YELLOW_BLK;
    grid[0][5] = PURPLE_BLK;
    grid[0][6] = BULLET_BLK;
    grid[0][7] = BLACK_BLK;
    grid[0][8] = BOMB_BLK;

    /* Row 1: special blocks */
    grid[1][0] = DEATH_BLK;
    grid[1][1] = HYPERSPACE_BLK;
    grid[1][2] = EXTRABALL_BLK;
    grid[1][3] = MGUN_BLK;
    grid[1][4] = WALLOFF_BLK;
    grid[1][5] = RANDOM_BLK;
    grid[1][6] = DROP_BLK;
    grid[1][7] = TIMER_BLK;
    grid[1][8] = MULTIBALL_BLK;

    /* Row 2: more specials */
    grid[2][0] = STICKY_BLK;
    grid[2][1] = REVERSE_BLK;
    grid[2][2] = PAD_SHRINK_BLK;
    grid[2][3] = PAD_EXPAND_BLK;
    grid[2][4] = MAXAMMO_BLK;
    grid[2][5] = ROAMER_BLK;

    assert_true(write_level_file(f->tmppath, "Mixed Types", 90, grid));

    level_system_status_t status = level_system_load_file(f->level, f->tmppath);
    assert_int_equal(status, LEVEL_SYS_OK);

    /* Verify row 0 */
    assert_int_equal(f->load.grid[0][0].block_type, RED_BLK);
    assert_int_equal(f->load.grid[0][1].block_type, BLUE_BLK);
    assert_int_equal(f->load.grid[0][2].block_type, GREEN_BLK);
    assert_int_equal(f->load.grid[0][3].block_type, TAN_BLK);
    assert_int_equal(f->load.grid[0][4].block_type, YELLOW_BLK);
    assert_int_equal(f->load.grid[0][5].block_type, PURPLE_BLK);
    assert_int_equal(f->load.grid[0][6].block_type, BULLET_BLK);
    assert_int_equal(f->load.grid[0][7].block_type, BLACK_BLK);
    assert_int_equal(f->load.grid[0][8].block_type, BOMB_BLK);

    /* Verify row 1 */
    assert_int_equal(f->load.grid[1][0].block_type, DEATH_BLK);
    assert_int_equal(f->load.grid[1][1].block_type, HYPERSPACE_BLK);
    assert_int_equal(f->load.grid[1][2].block_type, EXTRABALL_BLK);
    assert_int_equal(f->load.grid[1][3].block_type, MGUN_BLK);
    assert_int_equal(f->load.grid[1][4].block_type, WALLOFF_BLK);
    assert_int_equal(f->load.grid[1][5].block_type, RANDOM_BLK);
    assert_int_equal(f->load.grid[1][6].block_type, DROP_BLK);
    assert_int_equal(f->load.grid[1][7].block_type, TIMER_BLK);
    assert_int_equal(f->load.grid[1][8].block_type, MULTIBALL_BLK);

    /* Verify row 2 */
    assert_int_equal(f->load.grid[2][0].block_type, STICKY_BLK);
    assert_int_equal(f->load.grid[2][1].block_type, REVERSE_BLK);
    assert_int_equal(f->load.grid[2][2].block_type, PAD_SHRINK_BLK);
    assert_int_equal(f->load.grid[2][3].block_type, PAD_EXPAND_BLK);
    assert_int_equal(f->load.grid[2][4].block_type, MAXAMMO_BLK);
    assert_int_equal(f->load.grid[2][5].block_type, ROAMER_BLK);

    /* Remaining cells should be empty */
    assert_false(f->load.grid[2][6].occupied);
    assert_false(f->load.grid[3][0].occupied);
}

static void test_counter_block_slides_round_trip(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /*
     * Counter blocks use digits '0'-'5' in the file format.
     * Write them directly as characters since test_block_type_to_char
     * always returns '0' for COUNTER_BLK.
     */
    FILE *fp = fopen(f->tmppath, "w");
    assert_non_null(fp);
    fprintf(fp, "Counter Test\n");
    fprintf(fp, "60\n");
    /* Row 0: counter slides 0-5, then empty */
    fprintf(fp, "012345...\n");
    /* Remaining 14 rows: empty */
    for (int r = 1; r < LEVEL_GRID_ROWS; r++)
        fprintf(fp, ".........\n");
    fclose(fp);

    level_system_status_t status = level_system_load_file(f->level, f->tmppath);
    assert_int_equal(status, LEVEL_SYS_OK);

    /* Verify counter block slides */
    for (int c = 0; c < 6; c++)
    {
        assert_true(f->load.grid[0][c].occupied);
        assert_int_equal(f->load.grid[0][c].block_type, COUNTER_BLK);
        assert_int_equal(f->load.grid[0][c].counter_slide, c);
    }

    /* Remaining cells empty */
    assert_false(f->load.grid[0][6].occupied);

    assert_string_equal(level_system_get_title(f->level), "Counter Test");
    assert_int_equal(level_system_get_time_bonus(f->level), 60);
}

static void test_title_and_time_survive_round_trip(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    int grid[LEVEL_GRID_ROWS][LEVEL_GRID_COLS];
    for (int r = 0; r < LEVEL_GRID_ROWS; r++)
        for (int c = 0; c < LEVEL_GRID_COLS; c++)
            grid[r][c] = NONE_BLK;

    assert_true(write_level_file(f->tmppath, "My Custom Level", 300, grid));

    level_system_status_t status = level_system_load_file(f->level, f->tmppath);
    assert_int_equal(status, LEVEL_SYS_OK);

    assert_string_equal(level_system_get_title(f->level), "My Custom Level");
    assert_int_equal(level_system_get_time_bonus(f->level), 300);
}

static void test_single_block_position_preserved(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Place one block at row 7, col 4 (center of grid) */
    int grid[LEVEL_GRID_ROWS][LEVEL_GRID_COLS];
    for (int r = 0; r < LEVEL_GRID_ROWS; r++)
        for (int c = 0; c < LEVEL_GRID_COLS; c++)
            grid[r][c] = NONE_BLK;

    grid[7][4] = GREEN_BLK;

    assert_true(write_level_file(f->tmppath, "Single Block", 120, grid));

    level_system_status_t status = level_system_load_file(f->level, f->tmppath);
    assert_int_equal(status, LEVEL_SYS_OK);

    assert_int_equal(f->load.add_count, 1);
    assert_true(f->load.grid[7][4].occupied);
    assert_int_equal(f->load.grid[7][4].block_type, GREEN_BLK);

    /* Neighbors should be empty */
    assert_false(f->load.grid[7][3].occupied);
    assert_false(f->load.grid[7][5].occupied);
    assert_false(f->load.grid[6][4].occupied);
    assert_false(f->load.grid[8][4].occupied);
}

/* =========================================================================
 * Section 3: Edge cases
 * ========================================================================= */

static void test_load_nonexistent_file(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    level_system_status_t status =
        level_system_load_file(f->level, "/tmp/nonexistent_level_file_XXXXXX");
    assert_int_not_equal(status, LEVEL_SYS_OK);
    assert_int_equal(f->load.add_count, 0);
}

static void test_reload_clears_previous(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Load a grid with blocks */
    int grid[LEVEL_GRID_ROWS][LEVEL_GRID_COLS];
    for (int r = 0; r < LEVEL_GRID_ROWS; r++)
        for (int c = 0; c < LEVEL_GRID_COLS; c++)
            grid[r][c] = RED_BLK;

    assert_true(write_level_file(f->tmppath, "First", 100, grid));
    level_system_load_file(f->level, f->tmppath);
    assert_string_equal(level_system_get_title(f->level), "First");

    /* Reset load state and load a different file */
    reset_load_state(&f->load);

    for (int r = 0; r < LEVEL_GRID_ROWS; r++)
        for (int c = 0; c < LEVEL_GRID_COLS; c++)
            grid[r][c] = NONE_BLK;
    grid[0][0] = BLUE_BLK;

    assert_true(write_level_file(f->tmppath, "Second", 200, grid));
    level_system_load_file(f->level, f->tmppath);

    /* Only one block should be present */
    assert_int_equal(f->load.add_count, 1);
    assert_string_equal(level_system_get_title(f->level), "Second");
    assert_int_equal(level_system_get_time_bonus(f->level), 200);
}

/* =========================================================================
 * Section 4: Real level file round-trip
 * ========================================================================= */

static void test_load_real_level01(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Load the actual level01.data file */
    const char *path = LEVELS_DIR "/level01.data";
    level_system_status_t status = level_system_load_file(f->level, path);

    /* File might not exist in CI — skip if so */
    if (status == LEVEL_SYS_ERR_FILE_NOT_FOUND)
    {
        skip();
        return;
    }

    assert_int_equal(status, LEVEL_SYS_OK);

    /* Level 1 should have a non-empty title and blocks */
    assert_true(strlen(level_system_get_title(f->level)) > 0);
    assert_true(f->load.add_count > 0);
    assert_true(level_system_get_time_bonus(f->level) > 0);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Section 1: Character mapping */
        cmocka_unit_test(test_char_to_block_exhaustive),
        cmocka_unit_test(test_unknown_chars_map_to_none),
        cmocka_unit_test(test_block_type_to_char_round_trip),

        /* Section 2: File round-trip */
        cmocka_unit_test_setup_teardown(test_empty_grid_round_trip, setup, teardown),
        cmocka_unit_test_setup_teardown(test_full_grid_round_trip, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mixed_block_types_round_trip, setup, teardown),
        cmocka_unit_test_setup_teardown(test_counter_block_slides_round_trip, setup, teardown),
        cmocka_unit_test_setup_teardown(test_title_and_time_survive_round_trip, setup, teardown),
        cmocka_unit_test_setup_teardown(test_single_block_position_preserved, setup, teardown),

        /* Section 3: Edge cases */
        cmocka_unit_test_setup_teardown(test_load_nonexistent_file, setup, teardown),
        cmocka_unit_test_setup_teardown(test_reload_clears_previous, setup, teardown),

        /* Section 4: Real level file */
        cmocka_unit_test_setup_teardown(test_load_real_level01, setup, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
