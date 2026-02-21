/*
 * test_blocks.c -- Characterization tests for block grid and type catalog.
 *
 * Bead n9e.5: Block grid, type catalog, hitPoints, dimensions, and
 * level-completion logic characterization tests.
 *
 * Strategy: link blocks.c with X11 stubs (from stubs_x11_xboing.c) and
 * test SetupBlockInfo(), AddNewBlock(draw=False), ClearBlockArray(),
 * ClearBlock(), and StillActiveBlocks() directly. No rendering occurs.
 *
 * These are CHARACTERIZATION tests -- capture current behavior including
 * any bugs. Do NOT fix bugs here.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <cmocka.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "blocks.h"
#include "level.h"
#include "main.h"
#include "stage.h"

/* =========================================================================
 * Stubs for symbols blocks.c references that aren't in stubs_x11_xboing.c.
 * (stubs_x11_xboing.c can't define these because test_level_parse links
 * error.c and level.c which provide them.)
 * ========================================================================= */

/* error.c */
void ErrorMessage(char *message) { (void)message; }
void NormalMessage(char *message) { (void)message; }
void HandleXPMError(Display *display, int ErrorStatus, char *tag)
{ (void)display; (void)ErrorStatus; (void)tag; }

/* level.c globals */
u_long level = 1;
int bonus = 0;
int livesLeft = 3;
int bonusBlock = 0;
char levelTitle[BUF_SIZE];

/* level.c functions */
void DisplayLevelInfo(Display *d, Window w, u_long l)
{ (void)d; (void)w; (void)l; }

void AddABullet(Display *d) { (void)d; }

void AddToLevelTimeBonus(Display *d, Window w, int seconds)
{ (void)d; (void)w; (void)seconds; }

/* =========================================================================
 * Test setup / teardown
 * ========================================================================= */

static int setup(void **state)
{
    (void)state;
    frame = 0;
    blocksExploding = 0;
    colWidth  = PLAY_WIDTH / MAX_COL;
    rowHeight = PLAY_HEIGHT / MAX_ROW;
    ClearBlockArray();
    SetupBlockInfo();
    return 0;
}

/* =========================================================================
 * Section 1: Grid Constants
 * ========================================================================= */

/* TC-01: Grid dimensions. */
static void test_grid_dimensions(void **state)
{
    (void)state;
    assert_int_equal(MAX_ROW, 18);
    assert_int_equal(MAX_COL, 9);
}

/* TC-02: Block type range. */
static void test_block_type_range(void **state)
{
    (void)state;
    assert_int_equal(NONE_BLK, -2);
    assert_int_equal(KILL_BLK, -1);
    assert_int_equal(RED_BLK, 0);
    assert_int_equal(BLACKHIT_BLK, 29);
    assert_int_equal(MAX_BLOCKS, 30);
    assert_int_equal(MAX_STATIC_BLOCKS, 25);
}

/* TC-03: Standard block dimensions. */
static void test_standard_block_dimensions(void **state)
{
    (void)state;
    assert_int_equal(BLOCK_WIDTH, 40);
    assert_int_equal(BLOCK_HEIGHT, 20);
}

/* =========================================================================
 * Section 2: SetupBlockInfo -- Type Catalog
 * ========================================================================= */

/* TC-04: BlockInfo array index matches blockType for all 30 types. */
static void test_blockinfo_index_matches_type(void **state)
{
    (void)state;
    for (int i = 0; i < MAX_BLOCKS; i++)
        assert_int_equal(BlockInfo[i].blockType, i);
}

/* TC-05: Color block dimensions (40x20). */
static void test_blockinfo_color_block_dimensions(void **state)
{
    (void)state;
    int color_types[] = {
        RED_BLK, BLUE_BLK, GREEN_BLK, TAN_BLK, YELLOW_BLK, PURPLE_BLK
    };
    for (int i = 0; i < 6; i++)
    {
        assert_int_equal(BlockInfo[color_types[i]].width, 40);
        assert_int_equal(BlockInfo[color_types[i]].height, 20);
    }
}

/* TC-06: Special block dimensions vary per type. */
static void test_blockinfo_special_dimensions(void **state)
{
    (void)state;

    /* BLACK_BLK (wall) is larger: 50x30 */
    assert_int_equal(BlockInfo[BLACK_BLK].width, 50);
    assert_int_equal(BlockInfo[BLACK_BLK].height, 30);

    /* BOMB_BLK: 30x30 */
    assert_int_equal(BlockInfo[BOMB_BLK].width, 30);
    assert_int_equal(BlockInfo[BOMB_BLK].height, 30);

    /* DEATH_BLK: 30x30 */
    assert_int_equal(BlockInfo[DEATH_BLK].width, 30);
    assert_int_equal(BlockInfo[DEATH_BLK].height, 30);

    /* ROAMER_BLK: 25x27 */
    assert_int_equal(BlockInfo[ROAMER_BLK].width, 25);
    assert_int_equal(BlockInfo[ROAMER_BLK].height, 27);

    /* TIMER_BLK: 21x21 */
    assert_int_equal(BlockInfo[TIMER_BLK].width, 21);
    assert_int_equal(BlockInfo[TIMER_BLK].height, 21);

    /* MGUN_BLK: 35x15 */
    assert_int_equal(BlockInfo[MGUN_BLK].width, 35);
    assert_int_equal(BlockInfo[MGUN_BLK].height, 15);

    /* HYPERSPACE_BLK: 31x31 */
    assert_int_equal(BlockInfo[HYPERSPACE_BLK].width, 31);
    assert_int_equal(BlockInfo[HYPERSPACE_BLK].height, 31);

    /* BLACKHIT_BLK matches BLACK_BLK: 50x30 */
    assert_int_equal(BlockInfo[BLACKHIT_BLK].width, 50);
    assert_int_equal(BlockInfo[BLACKHIT_BLK].height, 30);
}

/* TC-07: SetupBlockInfo slide field bug -- indices 1-8 write to [0].slide.
 * This is a copy-paste bug in the original code. Characterize it. */
static void test_blockinfo_slide_bug(void **state)
{
    (void)state;
    /* Only index 0 and indices >= 9 have their own .slide set.
     * Indices 1-8 mistakenly write to BlockInfo[0].slide instead
     * of BlockInfo[i].slide. The value written is always 0, so
     * the bug is harmless — but BlockInfo[1..8].slide is uninitialized
     * (or zero from static storage). Characterize that all slides are 0. */
    for (int i = 0; i < MAX_BLOCKS; i++)
        assert_int_equal(BlockInfo[i].slide, 0);
}

/* =========================================================================
 * Section 3: AddNewBlock -- hitPoints
 * ========================================================================= */

/* TC-08: Color block hitPoints. */
static void test_hitpoints_color_blocks(void **state)
{
    (void)state;
    AddNewBlock(NULL, (Window)0, 0, 0, RED_BLK, 0, 0);
    assert_int_equal(blocks[0][0].hitPoints, 100);

    AddNewBlock(NULL, (Window)0, 0, 1, GREEN_BLK, 0, 0);
    assert_int_equal(blocks[0][1].hitPoints, 120);

    AddNewBlock(NULL, (Window)0, 0, 2, BLUE_BLK, 0, 0);
    assert_int_equal(blocks[0][2].hitPoints, 110);

    AddNewBlock(NULL, (Window)0, 0, 3, TAN_BLK, 0, 0);
    assert_int_equal(blocks[0][3].hitPoints, 130);

    AddNewBlock(NULL, (Window)0, 0, 4, YELLOW_BLK, 0, 0);
    assert_int_equal(blocks[0][4].hitPoints, 140);

    AddNewBlock(NULL, (Window)0, 0, 5, PURPLE_BLK, 0, 0);
    assert_int_equal(blocks[0][5].hitPoints, 150);
}

/* TC-09: Ammo block hitPoints (BULLET, MAXAMMO = 50 each). */
static void test_hitpoints_ammo_blocks(void **state)
{
    (void)state;
    AddNewBlock(NULL, (Window)0, 0, 0, BULLET_BLK, 0, 0);
    assert_int_equal(blocks[0][0].hitPoints, 50);

    AddNewBlock(NULL, (Window)0, 0, 1, MAXAMMO_BLK, 0, 0);
    assert_int_equal(blocks[0][1].hitPoints, 50);
}

/* TC-10: Special block hitPoints (all 100). */
static void test_hitpoints_special_blocks(void **state)
{
    (void)state;
    int specials[] = {
        TIMER_BLK, HYPERSPACE_BLK, MGUN_BLK, WALLOFF_BLK,
        REVERSE_BLK, MULTIBALL_BLK, STICKY_BLK,
        PAD_SHRINK_BLK, PAD_EXPAND_BLK, EXTRABALL_BLK
    };
    for (int i = 0; i < 10; i++)
    {
        AddNewBlock(NULL, (Window)0, 0, (int)(i % MAX_COL), specials[i], 0, 0);
        assert_int_equal(blocks[0][i % MAX_COL].hitPoints, 100);
    }
}

/* TC-11: BOMB_BLK hitPoints = 50. */
static void test_hitpoints_bomb(void **state)
{
    (void)state;
    AddNewBlock(NULL, (Window)0, 0, 0, BOMB_BLK, 0, 0);
    assert_int_equal(blocks[0][0].hitPoints, 50);
}

/* TC-12: ROAMER_BLK hitPoints = 400. */
static void test_hitpoints_roamer(void **state)
{
    (void)state;
    AddNewBlock(NULL, (Window)0, 0, 0, ROAMER_BLK, 0, 0);
    assert_int_equal(blocks[0][0].hitPoints, 400);
}

/* TC-13: COUNTER_BLK hitPoints = 200. */
static void test_hitpoints_counter(void **state)
{
    (void)state;
    AddNewBlock(NULL, (Window)0, 0, 0, COUNTER_BLK, 3, 0);
    assert_int_equal(blocks[0][0].hitPoints, 200);
    assert_int_equal(blocks[0][0].counterSlide, 3);
}

/* TC-14: DROP_BLK hitPoints are row-dependent: (MAX_ROW - row) * 100. */
static void test_hitpoints_drop_row_dependent(void **state)
{
    (void)state;
    AddNewBlock(NULL, (Window)0, 0, 0, DROP_BLK, 0, 0);
    assert_int_equal(blocks[0][0].hitPoints, (MAX_ROW - 0) * 100);

    AddNewBlock(NULL, (Window)0, 5, 0, DROP_BLK, 0, 0);
    assert_int_equal(blocks[5][0].hitPoints, (MAX_ROW - 5) * 100);

    AddNewBlock(NULL, (Window)0, 14, 0, DROP_BLK, 0, 0);
    assert_int_equal(blocks[14][0].hitPoints, (MAX_ROW - 14) * 100);
}

/* TC-15: DEATH_BLK hitPoints = 0. */
static void test_hitpoints_death(void **state)
{
    (void)state;
    AddNewBlock(NULL, (Window)0, 0, 0, DEATH_BLK, 0, 0);
    assert_int_equal(blocks[0][0].hitPoints, 0);
}

/* =========================================================================
 * Section 4: AddNewBlock -- Special Flags
 * ========================================================================= */

/* TC-16: RANDOM_BLK is stored as RED_BLK with random=True. */
static void test_random_block_becomes_red(void **state)
{
    (void)state;
    AddNewBlock(NULL, (Window)0, 0, 0, RANDOM_BLK, 0, 0);
    assert_int_equal(blocks[0][0].blockType, RED_BLK);
    assert_int_equal(blocks[0][0].random, 1);
    assert_int_equal(blocks[0][0].occupied, 1);
}

/* TC-17: DROP_BLK sets the drop flag. */
static void test_drop_block_flag(void **state)
{
    (void)state;
    AddNewBlock(NULL, (Window)0, 0, 0, DROP_BLK, 0, 0);
    assert_int_equal(blocks[0][0].drop, 1);
    assert_int_equal(blocks[0][0].blockType, DROP_BLK);
}

/* TC-18: Normal blocks don't set random or drop flags. */
static void test_normal_block_no_special_flags(void **state)
{
    (void)state;
    AddNewBlock(NULL, (Window)0, 0, 0, RED_BLK, 0, 0);
    assert_int_equal(blocks[0][0].random, 0);
    assert_int_equal(blocks[0][0].drop, 0);
}

/* =========================================================================
 * Section 5: ClearBlock / ClearBlockArray
 * ========================================================================= */

/* TC-19: ClearBlock resets all fields to defaults. */
static void test_clear_block_resets_fields(void **state)
{
    (void)state;
    AddNewBlock(NULL, (Window)0, 5, 3, PURPLE_BLK, 0, 0);
    assert_int_equal(blocks[5][3].occupied, 1);

    ClearBlock(5, 3);
    assert_int_equal(blocks[5][3].occupied, 0);
    assert_int_equal(blocks[5][3].blockType, NONE_BLK);
    assert_int_equal(blocks[5][3].hitPoints, 0);
    assert_int_equal(blocks[5][3].random, 0);
    assert_int_equal(blocks[5][3].drop, 0);
}

/* TC-20: ClearBlockArray empties entire grid. */
static void test_clear_block_array(void **state)
{
    (void)state;
    /* Place some blocks */
    AddNewBlock(NULL, (Window)0, 0, 0, RED_BLK, 0, 0);
    AddNewBlock(NULL, (Window)0, 5, 4, BLUE_BLK, 0, 0);
    AddNewBlock(NULL, (Window)0, 14, 8, BOMB_BLK, 0, 0);

    ClearBlockArray();

    for (int r = 0; r < MAX_ROW; r++)
        for (int c = 0; c < MAX_COL; c++)
        {
            assert_int_equal(blocks[r][c].occupied, 0);
            assert_int_equal(blocks[r][c].blockType, NONE_BLK);
        }
}

/* TC-21: AddNewBlock boundary check -- out-of-range row/col are ignored. */
static void test_addnewblock_boundary(void **state)
{
    (void)state;
    /* These should not crash (silently ignored per bounds check) */
    AddNewBlock(NULL, (Window)0, -1, 0, RED_BLK, 0, 0);
    AddNewBlock(NULL, (Window)0, 0, -1, RED_BLK, 0, 0);
    AddNewBlock(NULL, (Window)0, MAX_ROW + 1, 0, RED_BLK, 0, 0);
    AddNewBlock(NULL, (Window)0, 0, MAX_COL + 1, RED_BLK, 0, 0);

    /* Grid should still be empty */
    for (int r = 0; r < MAX_ROW; r++)
        for (int c = 0; c < MAX_COL; c++)
            assert_int_equal(blocks[r][c].occupied, 0);
}

/* =========================================================================
 * Section 6: StillActiveBlocks -- Level Completion
 * ========================================================================= */

/* TC-22: Empty grid returns False (level complete). */
static void test_still_active_empty_grid(void **state)
{
    (void)state;
    assert_int_equal(StillActiveBlocks(), 0);
}

/* TC-23: Color blocks count as active (must be destroyed). */
static void test_still_active_color_blocks(void **state)
{
    (void)state;
    int color_types[] = {
        RED_BLK, BLUE_BLK, GREEN_BLK, TAN_BLK, YELLOW_BLK, PURPLE_BLK
    };
    for (int i = 0; i < 6; i++)
    {
        ClearBlockArray();
        AddNewBlock(NULL, (Window)0, 0, 0, color_types[i], 0, 0);
        assert_int_equal(StillActiveBlocks(), 1);
    }
}

/* TC-24: COUNTER_BLK and DROP_BLK count as active. */
static void test_still_active_counter_drop(void **state)
{
    (void)state;
    ClearBlockArray();
    AddNewBlock(NULL, (Window)0, 0, 0, COUNTER_BLK, 3, 0);
    assert_int_equal(StillActiveBlocks(), 1);

    ClearBlockArray();
    AddNewBlock(NULL, (Window)0, 0, 0, DROP_BLK, 0, 0);
    assert_int_equal(StillActiveBlocks(), 1);
}

/* TC-25: Non-required blocks do NOT count as active.
 * These blocks can remain and the level is still complete. */
static void test_still_active_non_required_blocks(void **state)
{
    (void)state;
    int non_required[] = {
        BLACK_BLK, BULLET_BLK, ROAMER_BLK, BOMB_BLK, TIMER_BLK,
        HYPERSPACE_BLK, STICKY_BLK, MULTIBALL_BLK, MAXAMMO_BLK,
        PAD_SHRINK_BLK, PAD_EXPAND_BLK, REVERSE_BLK, MGUN_BLK,
        WALLOFF_BLK, EXTRABALL_BLK, DEATH_BLK
    };
    for (int i = 0; i < 16; i++)
    {
        ClearBlockArray();
        blocksExploding = 0;
        AddNewBlock(NULL, (Window)0, 0, 0, non_required[i], 0, 0);
        assert_int_equal(StillActiveBlocks(), 0);
    }
}

/* TC-26: Exploding blocks keep level active.
 * StillActiveBlocks returns True if blocksExploding > 1. */
static void test_still_active_exploding(void **state)
{
    (void)state;
    blocksExploding = 2;
    assert_int_equal(StillActiveBlocks(), 1);

    blocksExploding = 1;
    assert_int_equal(StillActiveBlocks(), 0);

    blocksExploding = 0;
    assert_int_equal(StillActiveBlocks(), 0);
}

/* =========================================================================
 * Section 7: SHOTS_TO_KILL_SPECIAL
 * ========================================================================= */

/* TC-27: SHOTS_TO_KILL_SPECIAL constant is 3. */
static void test_shots_to_kill_special(void **state)
{
    (void)state;
    assert_int_equal(SHOTS_TO_KILL_SPECIAL, 3);
}

/* TC-28: counterSlide is preserved when set via AddNewBlock. */
static void test_counter_slide_values(void **state)
{
    (void)state;
    for (int slide = 0; slide <= 5; slide++)
    {
        AddNewBlock(NULL, (Window)0, 0, (int)(slide % MAX_COL),
                    COUNTER_BLK, slide, 0);
        assert_int_equal(blocks[0][slide % MAX_COL].counterSlide, slide);
    }
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Section 1: Grid Constants */
        cmocka_unit_test(test_grid_dimensions),
        cmocka_unit_test(test_block_type_range),
        cmocka_unit_test(test_standard_block_dimensions),

        /* Section 2: SetupBlockInfo -- Type Catalog */
        cmocka_unit_test_setup(test_blockinfo_index_matches_type, setup),
        cmocka_unit_test_setup(test_blockinfo_color_block_dimensions, setup),
        cmocka_unit_test_setup(test_blockinfo_special_dimensions, setup),
        cmocka_unit_test_setup(test_blockinfo_slide_bug, setup),

        /* Section 3: hitPoints */
        cmocka_unit_test_setup(test_hitpoints_color_blocks, setup),
        cmocka_unit_test_setup(test_hitpoints_ammo_blocks, setup),
        cmocka_unit_test_setup(test_hitpoints_special_blocks, setup),
        cmocka_unit_test_setup(test_hitpoints_bomb, setup),
        cmocka_unit_test_setup(test_hitpoints_roamer, setup),
        cmocka_unit_test_setup(test_hitpoints_counter, setup),
        cmocka_unit_test_setup(test_hitpoints_drop_row_dependent, setup),
        cmocka_unit_test_setup(test_hitpoints_death, setup),

        /* Section 4: Special Flags */
        cmocka_unit_test_setup(test_random_block_becomes_red, setup),
        cmocka_unit_test_setup(test_drop_block_flag, setup),
        cmocka_unit_test_setup(test_normal_block_no_special_flags, setup),

        /* Section 5: ClearBlock / ClearBlockArray */
        cmocka_unit_test_setup(test_clear_block_resets_fields, setup),
        cmocka_unit_test_setup(test_clear_block_array, setup),
        cmocka_unit_test_setup(test_addnewblock_boundary, setup),

        /* Section 6: StillActiveBlocks */
        cmocka_unit_test_setup(test_still_active_empty_grid, setup),
        cmocka_unit_test_setup(test_still_active_color_blocks, setup),
        cmocka_unit_test_setup(test_still_active_counter_drop, setup),
        cmocka_unit_test_setup(test_still_active_non_required_blocks, setup),
        cmocka_unit_test_setup(test_still_active_exploding, setup),

        /* Section 7: Constants and Counter */
        cmocka_unit_test(test_shots_to_kill_special),
        cmocka_unit_test_setup(test_counter_slide_values, setup),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
