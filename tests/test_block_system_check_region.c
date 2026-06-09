/*
 * test_block_system_check_region.c — Tests for the original-faithful
 * bbox-vs-triangle collision classifier (block_system_check_region_bbox).
 *
 * Each test names a concrete game scenario and pins the expected return
 * value of the classifier.  Together they document:
 *
 *   - Trivial cases (out of bounds, empty cell, ball far from block)
 *   - First-contact behaviour on each isolated face (TOP, BOTTOM, LEFT)
 *   - Adjacency suppression (face hits silenced when a neighbour is
 *     occupied in that direction)
 *   - Seam-between-adjacent-blocks behaviour (the bug class that this
 *     classifier exists to fix — see bead xboing-c-83u)
 *   - Exhaustive corner combinations for an isolated block
 *
 * Block grid in these tests: env col_width=55, row_height=32,
 * BLOCK_WIDTH=40, BLOCK_HEIGHT=20.  Block at (row, col) sits at
 *   bp->x = col*55 + (55-40)/2 = col*55 + 7
 *   bp->y = row*32 + (32-20)/2 = row*32 + 6
 * so e.g. block (1, 4) spans pixel x=227..267, y=38..58.
 *
 * Ball is BALL_WIDTH=20 wide and BALL_HEIGHT=19 tall, centred at
 * (bx, by) with BALL_WC=10 and BALL_HC=9.  The bounding rectangle is
 * the half-open box [bx-10, bx-10+20) x [by-9, by-9+19) — i.e. the
 * pixels (bx-10, by-9) inclusive through (bx+9, by+9) inclusive.
 * Block rectangles use the same half-open convention: a block at
 * (bp->x, bp->y) covers pixels [bp->x, bp->x+40) x [bp->y, bp->y+20).
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "block_system.h"
#include "block_types.h"

/* =========================================================================
 * Fixture A — seam grid
 * Row 1 has the seam blocks at col 3 and col 4 (both red).  Row 0 col 4
 * is occupied above the right seam block — adjacency suppression target
 * for the TOP face of (1, 4).
 * ========================================================================= */

typedef struct
{
    block_system_t *ctx;
} fixture_t;

static int setup_seam(void **state)
{
    fixture_t *f = calloc(1, sizeof(*f));
    assert_non_null(f);

    block_system_status_t st;
    f->ctx = block_system_create(55, 32, &st);
    assert_non_null(f->ctx);
    assert_int_equal(st, BLOCK_SYS_OK);

    assert_int_equal(block_system_add(f->ctx, 1, 3, RED_BLK, 0, 0), BLOCK_SYS_OK);
    assert_int_equal(block_system_add(f->ctx, 1, 4, RED_BLK, 0, 0), BLOCK_SYS_OK);
    assert_int_equal(block_system_add(f->ctx, 0, 4, RED_BLK, 0, 0), BLOCK_SYS_OK);

    *state = f;
    return 0;
}

static int teardown_fixture(void **state)
{
    fixture_t *f = *state;
    block_system_destroy(f->ctx);
    free(f);
    return 0;
}

/* =========================================================================
 * Group 1 — trivial cases: classifier returns NONE.
 * ========================================================================= */

static void test_out_of_bounds_returns_none(void **state)
{
    fixture_t *f = *state;
    assert_int_equal(block_system_check_region_bbox(-1, 0, 100, 100, 0, f->ctx), BLOCK_REGION_NONE);
}

static void test_empty_cell_returns_none(void **state)
{
    fixture_t *f = *state;
    /* Row 2 col 4 is empty in the fixture. */
    assert_int_equal(block_system_check_region_bbox(2, 4, 244, 80, 0, f->ctx), BLOCK_REGION_NONE);
}

static void test_ball_far_from_block_returns_none(void **state)
{
    fixture_t *f = *state;
    /* Block (1, 4) at (227..267, 38..58); place ball far right and below. */
    assert_int_equal(block_system_check_region_bbox(1, 4, 400, 300, 0, f->ctx), BLOCK_REGION_NONE);
}

/* =========================================================================
 * Group 2 — first contact on each isolated face.
 * Adjacency does NOT suppress these because the relevant neighbour is empty
 * in the fixture.
 * ========================================================================= */

/* Ball coming from below: centre 8 px below block (1, 4)'s bottom edge.
 * Block bottom y=58, ball centre y=66.  Row 2 col 4 is empty, so BOTTOM
 * is not suppressed. */
static void test_first_contact_from_below_returns_bottom(void **state)
{
    fixture_t *f = *state;
    assert_int_equal(block_system_check_region_bbox(1, 4, 247, 66, 0, f->ctx), BLOCK_REGION_BOTTOM);
}

/* Ball coming from above block (1, 3).  Row 0 col 3 is empty in the
 * fixture, so TOP is not suppressed. */
static void test_first_contact_from_above_returns_top(void **state)
{
    fixture_t *f = *state;
    /* Block (1, 3) at (172..212, 38..58); ball centre (192, 29) →
     * bbox (182..202, 20..39) dips into the block's top edge at y=38. */
    assert_int_equal(block_system_check_region_bbox(1, 3, 192, 29, 0, f->ctx), BLOCK_REGION_TOP);
}

/* Ball coming from the left of block (1, 3).  Col 2 is empty, so LEFT
 * is not suppressed.  The bbox-vs-triangle test reports LEFT|TOP|BOTTOM
 * here because the ball at the block's vertical centre also dips into
 * the TOP and BOTTOM triangles' corner slivers — this is the 3-region
 * case the bounce switch's default handles (no bounce).  See the
 * implementation plan for why this scenario is unreachable under per-pixel
 * ray-march in normal play. */
static void test_first_contact_from_left_returns_three_region_bitmask(void **state)
{
    fixture_t *f = *state;
    assert_int_equal(block_system_check_region_bbox(1, 3, 165, 48, 0, f->ctx),
                     BLOCK_REGION_LEFT | BLOCK_REGION_TOP | BLOCK_REGION_BOTTOM);
}

/* =========================================================================
 * Group 3 — adjacency suppression.
 * ========================================================================= */

/* TOP face of block (1, 4) is suppressed because row 0 col 4 is occupied.
 * The bbox classifier suppresses unconditionally when the neighbour is
 * occupied, matching original/ball.c:1414-1416. */
static void test_top_face_suppressed_by_neighbour_above(void **state)
{
    fixture_t *f = *state;
    assert_int_equal(block_system_check_region_bbox(1, 4, 247, 29, 0, f->ctx), BLOCK_REGION_NONE);
}

/* The RIGHT face of (1, 3) is suppressed by (1, 4) being occupied.
 * Ball centre inside block (1, 3) just left of its right edge dips into
 * TOP and BOTTOM corner slivers; RIGHT is suppressed.  Returns TOP|BOTTOM. */
static void test_right_face_suppressed_internal_position(void **state)
{
    fixture_t *f = *state;
    assert_int_equal(block_system_check_region_bbox(1, 3, 208, 48, 0, f->ctx),
                     BLOCK_REGION_TOP | BLOCK_REGION_BOTTOM);
}

/* =========================================================================
 * Group 4 — the seam scenario the bbox classifier was created for.
 * ========================================================================= */

/* Ball approaching the seam between (1, 3) and (1, 4) from below at the
 * first-contact y.  Both seam blocks are tested; the BOTTOM triangle
 * overlaps the bbox in both cases.  RIGHT (for col=3) and LEFT (for col=4)
 * are suppressed by their respective seam neighbour.  Result: BOTTOM. */
static void test_seam_block_left_from_below_first_contact(void **state)
{
    fixture_t *f = *state;
    assert_int_equal(block_system_check_region_bbox(1, 3, 219, 66, 0, f->ctx), BLOCK_REGION_BOTTOM);
}

/* =========================================================================
 * Group 5 — corner-hit combinations on an isolated block.
 * ========================================================================= */

static int setup_isolated(void **state)
{
    fixture_t *f = calloc(1, sizeof(*f));
    assert_non_null(f);

    block_system_status_t st;
    f->ctx = block_system_create(55, 32, &st);
    assert_non_null(f->ctx);
    assert_int_equal(st, BLOCK_SYS_OK);

    assert_int_equal(block_system_add(f->ctx, 5, 4, RED_BLK, 0, 0), BLOCK_SYS_OK);

    *state = f;
    return 0;
}

static void test_isolated_corner_top_left(void **state)
{
    fixture_t *f = *state;
    /* Block (5, 4): bp->x=227, bp->y=166, ends (267, 186).  Ball centre
     * (218, 159), bbox (208..228, 150..169) straddles the top-left corner. */
    assert_int_equal(block_system_check_region_bbox(5, 4, 218, 159, 0, f->ctx),
                     BLOCK_REGION_TOP | BLOCK_REGION_LEFT);
}

static void test_isolated_corner_top_right(void **state)
{
    fixture_t *f = *state;
    assert_int_equal(block_system_check_region_bbox(5, 4, 276, 159, 0, f->ctx),
                     BLOCK_REGION_TOP | BLOCK_REGION_RIGHT);
}

static void test_isolated_corner_bottom_left(void **state)
{
    fixture_t *f = *state;
    assert_int_equal(block_system_check_region_bbox(5, 4, 218, 195, 0, f->ctx),
                     BLOCK_REGION_BOTTOM | BLOCK_REGION_LEFT);
}

static void test_isolated_corner_bottom_right(void **state)
{
    fixture_t *f = *state;
    assert_int_equal(block_system_check_region_bbox(5, 4, 276, 195, 0, f->ctx),
                     BLOCK_REGION_BOTTOM | BLOCK_REGION_RIGHT);
}

/* Edge of grid — row 0 has no row above, so TOP cannot be suppressed by
 * adjacency. */
static void test_edge_of_grid_no_neighbour_above_for_row_zero(void **state)
{
    fixture_t *f = *state;
    /* Use the isolated fixture and add a block at row 0 col 4. */
    assert_int_equal(block_system_add(f->ctx, 0, 4, RED_BLK, 0, 0), BLOCK_SYS_OK);
    /* Ball above row 0 col 4 (bp->y=6).  Centre (247, 0), bbox
     * (237..257, -9..10) — TOP face approached from above. */
    assert_int_equal(block_system_check_region_bbox(0, 4, 247, 0, 0, f->ctx), BLOCK_REGION_TOP);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: trivial */
        cmocka_unit_test_setup_teardown(test_out_of_bounds_returns_none, setup_seam,
                                        teardown_fixture),
        cmocka_unit_test_setup_teardown(test_empty_cell_returns_none, setup_seam,
                                        teardown_fixture),
        cmocka_unit_test_setup_teardown(test_ball_far_from_block_returns_none, setup_seam,
                                        teardown_fixture),
        /* Group 2: first-contact on isolated faces */
        cmocka_unit_test_setup_teardown(test_first_contact_from_below_returns_bottom, setup_seam,
                                        teardown_fixture),
        cmocka_unit_test_setup_teardown(test_first_contact_from_above_returns_top, setup_seam,
                                        teardown_fixture),
        cmocka_unit_test_setup_teardown(test_first_contact_from_left_returns_three_region_bitmask,
                                        setup_seam, teardown_fixture),
        /* Group 3: adjacency suppression */
        cmocka_unit_test_setup_teardown(test_top_face_suppressed_by_neighbour_above, setup_seam,
                                        teardown_fixture),
        cmocka_unit_test_setup_teardown(test_right_face_suppressed_internal_position, setup_seam,
                                        teardown_fixture),
        /* Group 4: seam scenario */
        cmocka_unit_test_setup_teardown(test_seam_block_left_from_below_first_contact, setup_seam,
                                        teardown_fixture),
        /* Group 5: isolated-block corner combinations */
        cmocka_unit_test_setup_teardown(test_isolated_corner_top_left, setup_isolated,
                                        teardown_fixture),
        cmocka_unit_test_setup_teardown(test_isolated_corner_top_right, setup_isolated,
                                        teardown_fixture),
        cmocka_unit_test_setup_teardown(test_isolated_corner_bottom_left, setup_isolated,
                                        teardown_fixture),
        cmocka_unit_test_setup_teardown(test_isolated_corner_bottom_right, setup_isolated,
                                        teardown_fixture),
        cmocka_unit_test_setup_teardown(test_edge_of_grid_no_neighbour_above_for_row_zero,
                                        setup_isolated, teardown_fixture),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
