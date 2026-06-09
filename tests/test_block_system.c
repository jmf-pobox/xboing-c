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

/* Note: TC-11..TC-22 (legacy point-in-triangle collision tests) were
 * removed when block_system_check_region was deleted.  The bbox-vs-
 * triangle classifier (block_system_check_region_bbox) is exercised
 * exhaustively in tests/test_block_system_check_region.c (bead
 * xboing-c-83u).  Numbering jumps from TC-10 (out-of-bounds add) to
 * TC-21 (cell_available) below. */


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
 * Group 10: Savegame v2 accessors
 * ========================================================================= */

static void test_get_black_next_frame_roundtrip(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 4, 2, BLACK_BLK, 0, 0);
    /* Initial value should be 0 (no cooldown active). */
    assert_int_equal(block_system_get_black_next_frame(ctx, 4, 2), 0);

    block_system_set_black_next_frame(ctx, 4, 2, 1540);
    assert_int_equal(block_system_get_black_next_frame(ctx, 4, 2), 1540);

    block_system_destroy(ctx);
}

static void test_get_black_next_frame_wrong_type(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 0, 0, RED_BLK, 0, 0);
    /* Wrong block type → returns 0. */
    assert_int_equal(block_system_get_black_next_frame(ctx, 0, 0), 0);

    /* Set is a no-op on wrong type. */
    block_system_set_black_next_frame(ctx, 0, 0, 999);
    assert_int_equal(block_system_get_black_next_frame(ctx, 0, 0), 0);

    block_system_destroy(ctx);
}

static void test_get_random_roundtrip(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    block_system_add(ctx, 3, 3, RED_BLK, 0, 0);
    assert_int_equal(block_system_get_random(ctx, 3, 3), 0);

    block_system_set_random(ctx, 3, 3, 1);
    assert_int_equal(block_system_get_random(ctx, 3, 3), 1);

    /* Set normalizes truthy values to 1. */
    block_system_set_random(ctx, 3, 3, 42);
    assert_int_equal(block_system_get_random(ctx, 3, 3), 1);

    block_system_set_random(ctx, 3, 3, 0);
    assert_int_equal(block_system_get_random(ctx, 3, 3), 0);

    block_system_destroy(ctx);
}

static void test_savegame_accessors_unoccupied(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();

    /* Unoccupied cell — getters return 0, setters are no-ops. */
    assert_int_equal(block_system_get_black_next_frame(ctx, 5, 5), 0);
    assert_int_equal(block_system_get_random(ctx, 5, 5), 0);

    block_system_set_black_next_frame(ctx, 5, 5, 100);
    block_system_set_random(ctx, 5, 5, 1);

    /* Still zero — set didn't write. */
    assert_int_equal(block_system_get_black_next_frame(ctx, 5, 5), 0);
    assert_int_equal(block_system_get_random(ctx, 5, 5), 0);

    block_system_destroy(ctx);
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

        /* Group 3 and 4 removed with block_system_check_region —
         * see tests/test_block_system_check_region.c. */

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

        /* Group 10: Savegame v2 accessors */
        cmocka_unit_test(test_get_black_next_frame_roundtrip),
        cmocka_unit_test(test_get_black_next_frame_wrong_type),
        cmocka_unit_test(test_get_random_roundtrip),
        cmocka_unit_test(test_savegame_accessors_unoccupied),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
