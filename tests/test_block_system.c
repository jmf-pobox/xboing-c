/*
 * test_block_system.c — CMocka tests for the pure C block grid system.
 *
 * Tests the block_system module: lifecycle, block management, collision
 * detection (diagonal cross-product replacing XPolygonRegion/XRectInRegion),
 * and grid queries.
 *
 * Bead: xboing-1ka.2 (Port block system with pure C collision regions)
 * ADR: ADR-016 in docs/DESIGN.md
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>

/* CMocka must come after std headers */
#include <cmocka.h>

#include "block_system.h"
#include "block_types.h"

/* =========================================================================
 * Test constants — match production values
 * ========================================================================= */

#define COL_WIDTH 55  /* PLAY_WIDTH / MAX_COL = 495 / 9 */
#define ROW_HEIGHT 32 /* PLAY_HEIGHT / MAX_ROW = 580 / 18 (truncated) */

/* =========================================================================
 * Helper: create a block system for tests
 * ========================================================================= */

static block_system_t *make_ctx(void)
{
    block_system_status_t st;
    block_system_t *ctx = block_system_create(COL_WIDTH, ROW_HEIGHT, &st);
    assert_non_null(ctx);
    assert_int_equal(st, BLOCK_SYS_OK);
    return ctx;
}

/* =========================================================================
 * Group 1: Lifecycle (3 tests)
 * ========================================================================= */

/* TC-01: Create and destroy succeed */
static void test_create_destroy(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();
    block_system_destroy(ctx);
}

/* TC-02: Grid starts empty */
static void test_initial_grid_empty(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    for (int r = 0; r < MAX_ROW; r++)
    {
        for (int c = 0; c < MAX_COL; c++)
        {
            assert_int_equal(block_system_is_occupied(ctx, r, c), 0);
            assert_int_equal(block_system_get_type(ctx, r, c), NONE_BLK);
        }
    }

    block_system_destroy(ctx);
}

/* TC-03: Info catalog populated with correct type metadata */
static void test_info_catalog(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    /* Spot-check a few block types */
    const block_system_info_t *red = block_system_get_info(ctx, RED_BLK);
    assert_non_null(red);
    assert_int_equal(red->block_type, RED_BLK);
    assert_int_equal(red->width, 40);
    assert_int_equal(red->height, 20);

    const block_system_info_t *bomb = block_system_get_info(ctx, BOMB_BLK);
    assert_non_null(bomb);
    assert_int_equal(bomb->width, 30);
    assert_int_equal(bomb->height, 30);

    const block_system_info_t *black = block_system_get_info(ctx, BLACK_BLK);
    assert_non_null(black);
    assert_int_equal(black->width, 50);
    assert_int_equal(black->height, 30);

    const block_system_info_t *timer = block_system_get_info(ctx, TIMER_BLK);
    assert_non_null(timer);
    assert_int_equal(timer->width, 21);
    assert_int_equal(timer->height, 21);

    /* Out-of-range returns NULL */
    assert_null(block_system_get_info(ctx, -1));
    assert_null(block_system_get_info(ctx, MAX_BLOCKS));

    block_system_destroy(ctx);
}

/* =========================================================================
 * Group 2: Block management (7 tests)
 * ========================================================================= */

/* TC-04: Add a standard color block */
static void test_add_block(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_status_t st = block_system_add(ctx, 5, 3, RED_BLK, 0, 100);
    assert_int_equal(st, BLOCK_SYS_OK);
    assert_int_equal(block_system_is_occupied(ctx, 5, 3), 1);
    assert_int_equal(block_system_get_type(ctx, 5, 3), RED_BLK);
    assert_int_equal(block_system_get_hit_points(ctx, 5, 3), 100);

    block_system_destroy(ctx);
}

/* TC-05: Block geometry is calculated correctly */
static void test_add_block_geometry(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    /* RED_BLK at (5, 3): width=40, height=20 */
    block_system_add(ctx, 5, 3, RED_BLK, 0, 100);

    block_system_render_info_t info;
    block_system_get_render_info(ctx, 5, 3, &info);

    /* Standard block: 40x20, centered in 55x32 cell */
    assert_int_equal(info.width, 40);
    assert_int_equal(info.height, 20);

    /* offset_x = (55 - 40) / 2 = 7, offset_y = (32 - 20) / 2 = 6 */
    /* x = col * col_width + offset_x = 3 * 55 + 7 = 172 */
    /* y = row * row_height + offset_y = 5 * 32 + 6 = 166 */
    assert_int_equal(info.x, 172);
    assert_int_equal(info.y, 166);

    block_system_destroy(ctx);
}

/* TC-06: Hit points match score_block_hit_points */
static void test_add_block_hit_points(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 0, 0, BLUE_BLK, 0, 0);
    assert_int_equal(block_system_get_hit_points(ctx, 0, 0), 110);

    block_system_add(ctx, 0, 1, ROAMER_BLK, 0, 0);
    assert_int_equal(block_system_get_hit_points(ctx, 0, 1), 400);

    block_system_add(ctx, 0, 2, COUNTER_BLK, 3, 0);
    assert_int_equal(block_system_get_hit_points(ctx, 0, 2), 200);

    /* DROP_BLK: (MAX_ROW - row) * 100 = (18 - 5) * 100 = 1300 */
    block_system_add(ctx, 5, 0, DROP_BLK, 0, 0);
    assert_int_equal(block_system_get_hit_points(ctx, 5, 0), 1300);

    block_system_destroy(ctx);
}

/* TC-07: Clear a block */
static void test_clear_block(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 3, 4, GREEN_BLK, 0, 0);
    assert_int_equal(block_system_is_occupied(ctx, 3, 4), 1);

    block_system_clear(ctx, 3, 4);
    assert_int_equal(block_system_is_occupied(ctx, 3, 4), 0);
    assert_int_equal(block_system_get_type(ctx, 3, 4), NONE_BLK);

    block_system_destroy(ctx);
}

/* TC-08: Clear all blocks */
static void test_clear_all(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    /* Add several blocks */
    block_system_add(ctx, 0, 0, RED_BLK, 0, 0);
    block_system_add(ctx, 5, 5, BLUE_BLK, 0, 0);
    block_system_add(ctx, 17, 8, BOMB_BLK, 0, 0);

    block_system_clear_all(ctx);

    for (int r = 0; r < MAX_ROW; r++)
    {
        for (int c = 0; c < MAX_COL; c++)
        {
            assert_int_equal(block_system_is_occupied(ctx, r, c), 0);
        }
    }

    block_system_destroy(ctx);
}

/* TC-09: RANDOM_BLK becomes RED_BLK with random flag */
static void test_add_random_block(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 2, 2, RANDOM_BLK, 0, 100);

    /* RANDOM_BLK is stored as RED_BLK with random flag */
    assert_int_equal(block_system_get_type(ctx, 2, 2), RED_BLK);

    block_system_render_info_t info;
    block_system_get_render_info(ctx, 2, 2, &info);
    assert_int_equal(info.random, 1);

    block_system_destroy(ctx);
}

/* TC-10: Out-of-bounds add returns error */
static void test_add_out_of_bounds(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    assert_int_equal(block_system_add(ctx, -1, 0, RED_BLK, 0, 0), BLOCK_SYS_ERR_OUT_OF_BOUNDS);
    assert_int_equal(block_system_add(ctx, MAX_ROW, 0, RED_BLK, 0, 0), BLOCK_SYS_ERR_OUT_OF_BOUNDS);
    assert_int_equal(block_system_add(ctx, 0, MAX_COL, RED_BLK, 0, 0), BLOCK_SYS_ERR_OUT_OF_BOUNDS);

    block_system_destroy(ctx);
}

/* =========================================================================
 * Group 3: Collision detection — check_region (8 tests)
 *
 * Test setup: a RED_BLK at row=5, col=3.
 *   Block geometry: width=40, height=20, centered in 55x32 cell.
 *   x=172, y=166 (from TC-05).
 *   Center: cx=172+20=192, cy=166+10=176.
 *
 * The block is divided by its diagonals into 4 triangles.
 * The diagonals cross at (192, 176).
 * ========================================================================= */

/* TC-11: Ball center above block center → BLOCK_REGION_TOP */
static void test_check_region_top(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 5, 3, RED_BLK, 0, 0);

    /* Ball at (192, 168) — above center (192, 176), on the vertical midline */
    int r = block_system_check_region(5, 3, 192, 168, 0, ctx);
    assert_int_equal(r, BLOCK_REGION_TOP);

    block_system_destroy(ctx);
}

/* TC-12: Ball center below block center → BLOCK_REGION_BOTTOM */
static void test_check_region_bottom(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 5, 3, RED_BLK, 0, 0);

    /* Ball at (192, 184) — below center (192, 176), on the vertical midline */
    int r = block_system_check_region(5, 3, 192, 184, 0, ctx);
    assert_int_equal(r, BLOCK_REGION_BOTTOM);

    block_system_destroy(ctx);
}

/* TC-13: Ball center to the left of block center → BLOCK_REGION_LEFT */
static void test_check_region_left(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 5, 3, RED_BLK, 0, 0);

    /* Ball at (175, 176) — left of center (192, 176), on the horizontal midline */
    int r = block_system_check_region(5, 3, 175, 176, 0, ctx);
    assert_int_equal(r, BLOCK_REGION_LEFT);

    block_system_destroy(ctx);
}

/* TC-14: Ball center to the right of block center → BLOCK_REGION_RIGHT */
static void test_check_region_right(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 5, 3, RED_BLK, 0, 0);

    /* Ball at (209, 176) — right of center (192, 176), on the horizontal midline */
    int r = block_system_check_region(5, 3, 209, 176, 0, ctx);
    assert_int_equal(r, BLOCK_REGION_RIGHT);

    block_system_destroy(ctx);
}

/* TC-15: Ball outside block → BLOCK_REGION_NONE */
static void test_check_region_miss(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 5, 3, RED_BLK, 0, 0);

    /* Ball far away — AABB test fails */
    int r = block_system_check_region(5, 3, 0, 0, 0, ctx);
    assert_int_equal(r, BLOCK_REGION_NONE);

    block_system_destroy(ctx);
}

/* TC-16: Empty cell → BLOCK_REGION_NONE */
static void test_check_region_empty(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    /* No block at (5, 3) */
    int r = block_system_check_region(5, 3, 192, 176, 0, ctx);
    assert_int_equal(r, BLOCK_REGION_NONE);

    block_system_destroy(ctx);
}

/* TC-17: Out-of-bounds coordinates → BLOCK_REGION_NONE */
static void test_check_region_out_of_bounds(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    assert_int_equal(block_system_check_region(-1, 0, 0, 0, 0, ctx), BLOCK_REGION_NONE);
    assert_int_equal(block_system_check_region(MAX_ROW, 0, 0, 0, 0, ctx), BLOCK_REGION_NONE);
    assert_int_equal(block_system_check_region(0, MAX_COL, 0, 0, 0, ctx), BLOCK_REGION_NONE);

    block_system_destroy(ctx);
}

/* TC-18: Adjacency filter suppresses region when neighbor is occupied */
static void test_check_region_adjacency_filter(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    /* Place two blocks side by side: (5, 3) and (5, 4) */
    block_system_add(ctx, 5, 3, RED_BLK, 0, 0);
    block_system_add(ctx, 5, 4, BLUE_BLK, 0, 0);

    /*
     * Ball hits the RIGHT face of block (5, 3) — but block (5, 4) is occupied.
     * The adjacency filter should suppress this, returning NONE.
     */
    int r = block_system_check_region(5, 3, 209, 176, 0, ctx);
    assert_int_equal(r, BLOCK_REGION_NONE);

    /*
     * Ball hits the LEFT face of block (5, 4) — but block (5, 3) is occupied.
     * Also suppressed.
     */
    /* Block (5,4): x = 4*55 + 7 = 227, center_x = 227+20 = 247, center_y = 176 */
    r = block_system_check_region(5, 4, 230, 176, 0, ctx);
    assert_int_equal(r, BLOCK_REGION_NONE);

    /* Ball hits the TOP face of block (5, 3) — no block above. Not suppressed. */
    r = block_system_check_region(5, 3, 192, 168, 0, ctx);
    assert_int_equal(r, BLOCK_REGION_TOP);

    block_system_destroy(ctx);
}

/* =========================================================================
 * Group 4: Collision with non-standard block sizes (2 tests)
 * ========================================================================= */

/* TC-19: Collision with BOMB_BLK (30x30, larger than standard) */
static void test_check_region_bomb_block(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    /* BOMB_BLK at (3, 2): width=30, height=30 */
    block_system_add(ctx, 3, 2, BOMB_BLK, 0, 0);

    block_system_render_info_t info;
    block_system_get_render_info(ctx, 3, 2, &info);
    assert_int_equal(info.width, 30);
    assert_int_equal(info.height, 30);

    /* offset_x = (55-30)/2 = 12, offset_y = (32-30)/2 = 1 */
    /* x = 2*55+12 = 122, y = 3*32+1 = 97 */
    /* center: (122+15, 97+15) = (137, 112) */
    assert_int_equal(info.x, 122);
    assert_int_equal(info.y, 97);

    /* Ball at center-top of bomb → BLOCK_REGION_TOP */
    int r = block_system_check_region(3, 2, 137, 100, 0, ctx);
    assert_int_equal(r, BLOCK_REGION_TOP);

    block_system_destroy(ctx);
}

/* TC-20: Collision with BLACK_BLK (50x30, oversized) */
static void test_check_region_black_block(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    /* BLACK_BLK at (0, 0): width=50, height=30 */
    block_system_add(ctx, 0, 0, BLACK_BLK, 0, 0);

    block_system_render_info_t info;
    block_system_get_render_info(ctx, 0, 0, &info);
    assert_int_equal(info.width, 50);
    assert_int_equal(info.height, 30);

    /* offset_x = (55-50)/2 = 2, offset_y = (32-30)/2 = 1 */
    /* x = 0*55+2 = 2, y = 0*32+1 = 1 */
    /* center: (2+25, 1+15) = (27, 16) */
    assert_int_equal(info.x, 2);
    assert_int_equal(info.y, 1);

    /* Ball to the left of center → BLOCK_REGION_LEFT */
    int r = block_system_check_region(0, 0, 5, 16, 0, ctx);
    assert_int_equal(r, BLOCK_REGION_LEFT);

    block_system_destroy(ctx);
}

/* =========================================================================
 * Group 5: Cell availability and queries (5 tests)
 * ========================================================================= */

/* TC-21: cell_available returns true for empty cell */
static void test_cell_available_empty(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    assert_int_not_equal(block_system_cell_available(5, 3, ctx), 0);

    block_system_destroy(ctx);
}

/* TC-22: cell_available returns false for occupied cell */
static void test_cell_available_occupied(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 5, 3, RED_BLK, 0, 0);
    assert_int_equal(block_system_cell_available(5, 3, ctx), 0);

    block_system_destroy(ctx);
}

/* TC-23: still_active with required blocks → nonzero */
static void test_still_active_with_required(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 0, 0, RED_BLK, 0, 0);
    assert_int_not_equal(block_system_still_active(ctx), 0);

    block_system_destroy(ctx);
}

/* TC-24: still_active with only optional blocks → zero */
static void test_still_active_only_optional(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 0, 0, BLACK_BLK, 0, 0);
    block_system_add(ctx, 1, 0, BOMB_BLK, 0, 0);
    block_system_add(ctx, 2, 0, DEATH_BLK, 0, 0);
    assert_int_equal(block_system_still_active(ctx), 0);

    block_system_destroy(ctx);
}

/* TC-25: still_active empty grid → zero */
static void test_still_active_empty(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    assert_int_equal(block_system_still_active(ctx), 0);

    block_system_destroy(ctx);
}

/* =========================================================================
 * Group 6: Render info and special block geometry (2 tests)
 * ========================================================================= */

/* TC-26: Render info for occupied block */
static void test_render_info_occupied(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 2, 4, COUNTER_BLK, 3, 0);

    block_system_render_info_t info;
    block_system_status_t st = block_system_get_render_info(ctx, 2, 4, &info);
    assert_int_equal(st, BLOCK_SYS_OK);
    assert_int_equal(info.occupied, 1);
    assert_int_equal(info.block_type, COUNTER_BLK);
    assert_int_equal(info.counter_slide, 3);
    assert_int_equal(info.hit_points, 200);

    block_system_destroy(ctx);
}

/* TC-27: Render info for empty cell */
static void test_render_info_empty(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_render_info_t info;
    block_system_status_t st = block_system_get_render_info(ctx, 0, 0, &info);
    assert_int_equal(st, BLOCK_SYS_OK);
    assert_int_equal(info.occupied, 0);

    block_system_destroy(ctx);
}

/* =========================================================================
 * Group 7: Adjacency-filter gap reproducer (xboing-c-895)
 *
 * Two adjacent rows at (5,4) and (6,4).  Ball in the inter-block gap
 * at x=247, y=192, dx=1, dy=-5 (moving upward).
 *
 * Block geometry:
 *   Row 5, col 4: x=227, y=166, w=40, h=20 (center: 247, 176)
 *   Row 6, col 4: x=227, y=198, w=40, h=20 (center: 247, 208)
 *   Gap between them: y=187..197.  Ball center at y=192 is in the gap.
 *
 * Before the fix: both block hits suppressed (ball tunnels).
 * After the fix: row 6 hit is NOT suppressed (by=192 < bp->y=198).
 *
 * The test asserts that block_system_check_region returns non-NONE for
 * at least one of the two blocks — meaning a hit registers.
 *
 * Citation: src/block_system.c:618-646 (adjacency filter).
 * ========================================================================= */

/* TC-28b: Gap-zone ball is not suppressed on both faces after fix */
static void test_adjacency_filter_gap_not_suppressed(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    /* Place RED_BLK at row 5 and row 6, column 4 */
    block_system_add(ctx, 5, 4, RED_BLK, 0, 0);
    block_system_add(ctx, 6, 4, RED_BLK, 0, 0);

    /*
     * Ball center at x=247, y=192, dx=1, dy=-5.
     * Row 5 block: x=227, y=166, w=40, h=20.
     * Row 6 block: x=227, y=198, w=40, h=20.
     * y=192 is in the gap (row 5 block bottom = 186, row 6 block top = 198).
     * Ball AABB: left=237, top=183, right=257, bottom=201.
     * Overlaps row 5 AABB [166,186]: ball_bottom=201 > 166, ball_top=183 < 186 YES.
     * Overlaps row 6 AABB [198,218]: ball_bottom=201 > 198, ball_top=183 < 218 YES.
     */
    int ball_x = 247;
    int ball_y = 192;
    int ball_dx = 1;

    int r5 = block_system_check_region(5, 4, ball_x, ball_y, ball_dx, ctx);
    int r6 = block_system_check_region(6, 4, ball_x, ball_y, ball_dx, ctx);

    /*
     * With the fix: row 6 hit is real (by=192 < bp->y=198 — ball above
     * block top edge, in the gap) so r6 != BLOCK_REGION_NONE.
     * At least one block must register a hit.
     */
    assert_true(r5 != BLOCK_REGION_NONE || r6 != BLOCK_REGION_NONE);

    block_system_destroy(ctx);
}

/* =========================================================================
 * Group 8: Status string utility (1 test)
 * ========================================================================= */

/* TC-28: Status strings are non-NULL */
static void test_status_strings(void **state)
{
    (void)state;

    assert_non_null(block_system_status_string(BLOCK_SYS_OK));
    assert_non_null(block_system_status_string(BLOCK_SYS_ERR_NULL_ARG));
    assert_non_null(block_system_status_string(BLOCK_SYS_ERR_ALLOC_FAILED));
    assert_non_null(block_system_status_string(BLOCK_SYS_ERR_OUT_OF_BOUNDS));
}

/* =========================================================================
 * Group 9: block_system_decrement_gun_hit (gun-feature gaps 3, 4, 5)
 *
 * original/gun.c:318-350 — multi-hit logic for bullet-block collisions.
 * ========================================================================= */

/* TC-29: Regular block returns 0 on first bullet hit (destroyed by this hit).
 * Basket 3: the function no longer clears — the caller arms explosion via
 * block_system_explode, so the cell remains occupied until the explosion
 * lifecycle finalizes. */
static void test_gun_hit_regular_block_cleared(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 2, 3, RED_BLK, 0, 0);
    assert_int_equal(block_system_is_occupied(ctx, 2, 3), 1);

    int absorbed = block_system_decrement_gun_hit(ctx, 2, 3);
    assert_int_equal(absorbed, 0);
    /* Block still occupied — caller must arm explosion. */
    assert_int_equal(block_system_is_occupied(ctx, 2, 3), 1);

    block_system_destroy(ctx);
}

/* TC-30: HYPERSPACE_BLK absorbs bullet — block still occupied (Gap 3)
 * original/gun.c:341-345 — just redraws, never killed. */
static void test_gun_hit_hyperspace_absorbed(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 1, 0, HYPERSPACE_BLK, 0, 0);
    assert_int_equal(block_system_is_occupied(ctx, 1, 0), 1);

    int absorbed = block_system_decrement_gun_hit(ctx, 1, 0);
    assert_int_equal(absorbed, 1);
    assert_int_equal(block_system_is_occupied(ctx, 1, 0), 1);

    block_system_destroy(ctx);
}

/* TC-31: BLACK_BLK absorbs bullet — block still occupied (Gap 4)
 * original/gun.c:346-350 — just redraws, never killed. */
static void test_gun_hit_black_absorbed(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 0, 5, BLACK_BLK, 0, 0);
    assert_int_equal(block_system_is_occupied(ctx, 0, 5), 1);

    int absorbed = block_system_decrement_gun_hit(ctx, 0, 5);
    assert_int_equal(absorbed, 1);
    assert_int_equal(block_system_is_occupied(ctx, 0, 5), 1);

    block_system_destroy(ctx);
}

/* TC-32: Multi-hit special block requires 3 hits — MGUN_BLK (Gap 5)
 * original/gun.c:325-340: counterSlide initialized to SHOTS_TO_KILL_SPECIAL=3.
 * Hits 1 and 2 absorb; hit 3 clears. */
static void test_gun_hit_mgun_requires_three_hits(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 3, 4, MGUN_BLK, BLOCK_SHOTS_TO_KILL_SPECIAL, 0);
    assert_int_equal(block_system_is_occupied(ctx, 3, 4), 1);

    /* Hit 1: absorbed, still occupied */
    int r1 = block_system_decrement_gun_hit(ctx, 3, 4);
    assert_int_equal(r1, 1);
    assert_int_equal(block_system_is_occupied(ctx, 3, 4), 1);

    /* Hit 2: absorbed, still occupied */
    int r2 = block_system_decrement_gun_hit(ctx, 3, 4);
    assert_int_equal(r2, 1);
    assert_int_equal(block_system_is_occupied(ctx, 3, 4), 1);

    /* Hit 3: returns 0 (destroyed by this hit; caller arms explosion).
     * Basket 3: cell still occupied — block_system_decrement_gun_hit no
     * longer clears, the caller is responsible for arming the explosion
     * lifecycle. */
    int r3 = block_system_decrement_gun_hit(ctx, 3, 4);
    assert_int_equal(r3, 0);
    assert_int_equal(block_system_is_occupied(ctx, 3, 4), 1);

    block_system_destroy(ctx);
}

/* TC-33: COUNTER_BLK also requires 3 hits via counterSlide */
static void test_gun_hit_counter_blk_requires_three_hits(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 2, 2, COUNTER_BLK, BLOCK_SHOTS_TO_KILL_SPECIAL, 0);
    assert_int_equal(block_system_is_occupied(ctx, 2, 2), 1);

    /* Hits 1 and 2 absorbed */
    assert_int_equal(block_system_decrement_gun_hit(ctx, 2, 2), 1);
    assert_int_equal(block_system_is_occupied(ctx, 2, 2), 1);
    assert_int_equal(block_system_decrement_gun_hit(ctx, 2, 2), 1);
    assert_int_equal(block_system_is_occupied(ctx, 2, 2), 1);

    /* Hit 3 returns 0 (destroyed by this hit; caller arms explosion).
     * Basket 3: cell still occupied — caller arms the explosion lifecycle. */
    assert_int_equal(block_system_decrement_gun_hit(ctx, 2, 2), 0);
    assert_int_equal(block_system_is_occupied(ctx, 2, 2), 1);

    block_system_destroy(ctx);
}

/* TC-34: NULL ctx returns 0 safely */
static void test_gun_hit_null_ctx(void **state)
{
    (void)state;
    assert_int_equal(block_system_decrement_gun_hit(NULL, 0, 0), 0);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_initial_grid_empty),
        cmocka_unit_test(test_info_catalog),

        /* Group 2: Block management */
        cmocka_unit_test(test_add_block),
        cmocka_unit_test(test_add_block_geometry),
        cmocka_unit_test(test_add_block_hit_points),
        cmocka_unit_test(test_clear_block),
        cmocka_unit_test(test_clear_all),
        cmocka_unit_test(test_add_random_block),
        cmocka_unit_test(test_add_out_of_bounds),

        /* Group 3: Collision detection */
        cmocka_unit_test(test_check_region_top),
        cmocka_unit_test(test_check_region_bottom),
        cmocka_unit_test(test_check_region_left),
        cmocka_unit_test(test_check_region_right),
        cmocka_unit_test(test_check_region_miss),
        cmocka_unit_test(test_check_region_empty),
        cmocka_unit_test(test_check_region_out_of_bounds),
        cmocka_unit_test(test_check_region_adjacency_filter),
        cmocka_unit_test(test_adjacency_filter_gap_not_suppressed),

        /* Group 4: Non-standard block sizes */
        cmocka_unit_test(test_check_region_bomb_block),
        cmocka_unit_test(test_check_region_black_block),

        /* Group 5: Cell availability and queries */
        cmocka_unit_test(test_cell_available_empty),
        cmocka_unit_test(test_cell_available_occupied),
        cmocka_unit_test(test_still_active_with_required),
        cmocka_unit_test(test_still_active_only_optional),
        cmocka_unit_test(test_still_active_empty),

        /* Group 6: Render info */
        cmocka_unit_test(test_render_info_occupied),
        cmocka_unit_test(test_render_info_empty),

        /* Group 7: Adjacency-filter gap reproducer */
        /* (registered above, inline with Group 3) */

        /* Group 8: Status strings */
        cmocka_unit_test(test_status_strings),

        /* Group 9: block_system_decrement_gun_hit */
        cmocka_unit_test(test_gun_hit_regular_block_cleared),
        cmocka_unit_test(test_gun_hit_hyperspace_absorbed),
        cmocka_unit_test(test_gun_hit_black_absorbed),
        cmocka_unit_test(test_gun_hit_mgun_requires_three_hits),
        cmocka_unit_test(test_gun_hit_counter_blk_requires_three_hits),
        cmocka_unit_test(test_gun_hit_null_ctx),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
