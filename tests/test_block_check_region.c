/*
 * test_block_check_region.c — TDD comparison test for the collision
 * classifier rewrite (bead xboing-c-83u).
 *
 * Runs identical inputs through both
 *   block_system_check_region       (legacy: point-in-triangle on ball center)
 *   block_system_check_region_bbox  (new:    bbox-vs-triangle bitmask, original-faithful)
 *
 * Each test names a concrete game scenario and pins what each function
 * is expected to return.  This serves three purposes:
 *
 *   1. TDD for the new function: the assertions on _bbox define its
 *      contract before implementation.
 *   2. Characterization of the legacy function: pinning the (often wrong)
 *      legacy results so future cleanup can decide which to keep.
 *   3. Divergence ledger: side-by-side expectations make every
 *      seam-tunneling-relevant case auditable in one place.
 *
 * Block grid in these tests: env col_width=55, row_height=32,
 * BLOCK_WIDTH=40, BLOCK_HEIGHT=20.  Block at (row, col) sits at
 *   bp->x = col*55 + (55-40)/2 = col*55 + 7
 *   bp->y = row*32 + (32-20)/2 = row*32 + 6
 * so e.g. block (1, 4) spans pixel x=227..267, y=38..58.
 *
 * Ball is 20 wide x 19 tall, centered at (bx, by); BALL_WC = 10,
 * BALL_HC = 9 — so the bbox is (bx-10, by-9) to (bx+10, by+10).
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
 * Fixture: a 18x9 grid with two seam blocks at row 1 cols 3 and 4.
 * Row 0 col 4 is also occupied to exercise the "neighbour above" adjacency
 * suppression branch.
 * ========================================================================= */

typedef struct
{
    block_system_t *ctx;
} fixture_t;

static int setup_seam(void **state)
{
    fixture_t *f = calloc(1, sizeof(*f));
    f->ctx = block_system_create(55, 32, NULL);

    /* Row 1 has the seam blocks at col 3 and col 4 (both red). */
    block_system_add(f->ctx, 1, 3, 1 /* RED_BLK */, 0, 0);
    block_system_add(f->ctx, 1, 4, 1, 0, 0);

    /* Row 0 col 4 is occupied above the right seam block — adjacency
     * suppression target for the TOP face of (1, 4). */
    block_system_add(f->ctx, 0, 4, 1, 0, 0);

    *state = f;
    return 0;
}

static int teardown_seam(void **state)
{
    fixture_t *f = *state;
    block_system_destroy(f->ctx);
    free(f);
    return 0;
}

/* Helper: run both classifiers, return their results. */
typedef struct
{
    int old_result;
    int new_result;
} both_results_t;

static both_results_t run_both(block_system_t *ctx, int row, int col, int bx, int by)
{
    both_results_t r;
    r.old_result = block_system_check_region(row, col, bx, by, 0, ctx);
    r.new_result = block_system_check_region_bbox(row, col, bx, by, 0, ctx);
    return r;
}

/* =========================================================================
 * Group 1: Out-of-bounds, empty, and disjoint cases — both functions agree.
 * ========================================================================= */

static void test_out_of_bounds_returns_none(void **state)
{
    fixture_t *f = *state;
    both_results_t r = run_both(f->ctx, -1, 0, 100, 100);
    assert_int_equal(r.old_result, BLOCK_REGION_NONE);
    assert_int_equal(r.new_result, BLOCK_REGION_NONE);
}

static void test_empty_cell_returns_none(void **state)
{
    fixture_t *f = *state;
    /* Row 2 col 4 is empty in our fixture. */
    both_results_t r = run_both(f->ctx, 2, 4, 244, 80);
    assert_int_equal(r.old_result, BLOCK_REGION_NONE);
    assert_int_equal(r.new_result, BLOCK_REGION_NONE);
}

static void test_ball_far_from_block_returns_none(void **state)
{
    fixture_t *f = *state;
    /* Block (1, 4) at (227..267, 38..58); place ball far right and far below. */
    both_results_t r = run_both(f->ctx, 1, 4, 400, 300);
    assert_int_equal(r.old_result, BLOCK_REGION_NONE);
    assert_int_equal(r.new_result, BLOCK_REGION_NONE);
}

/* =========================================================================
 * Group 2: First-contact on each face — the cases the legacy classifier
 * mostly gets right when adjacency permits.  Both functions agree here.
 * ========================================================================= */

/* Ball coming from below, center 8 px below the block's bottom edge.
 * Block (1, 4) bottom edge at y=58; ball center at y=66 (no adjacency
 * issue because no row-2 block).  Expected: BOTTOM. */
static void test_first_contact_from_below_returns_bottom(void **state)
{
    fixture_t *f = *state;
    both_results_t r = run_both(f->ctx, 1, 4, 247, 66);
    assert_int_equal(r.old_result, BLOCK_REGION_BOTTOM);
    assert_int_equal(r.new_result, BLOCK_REGION_BOTTOM);
}

/* Ball coming from above, center 5 px above the block's top edge.
 * Block (1, 4) top at y=38; ball center at y=29 — BUT row 0 col 4 is
 * occupied in our fixture, so TOP gets suppressed by the adjacency
 * filter for ISOLATED top faces.  Both functions suppress.
 *
 * To test the unsuppressed TOP case we use block (1, 3) which has no
 * block above it (row 0 col 3 is empty in the fixture). */
static void test_first_contact_from_above_returns_top(void **state)
{
    fixture_t *f = *state;
    /* Block (1, 3) spans x=172..212, y=38..58.  Ball center at (192, 29):
     * bbox (182..202, 20..39) — overlap only on Y at y=38..39, ball is
     * above the block.  TOP face fires (no row-0 col-3 to suppress). */
    both_results_t r = run_both(f->ctx, 1, 3, 192, 29);
    assert_int_equal(r.old_result, BLOCK_REGION_TOP);
    assert_int_equal(r.new_result, BLOCK_REGION_TOP);
}

/* Ball coming from the LEFT against an isolated face.  Block (1, 3)
 * has col 2 empty in the fixture.  Ball center at (165, 48) — bbox
 * (155..175, 39..58) overlaps block's left edge (x=172..175).
 *
 * Divergence: the legacy point-classifier sees the ball center is
 * unambiguously left-of-block and returns LEFT only.  The bbox
 * classifier sees the bbox also dips into the TOP and BOTTOM triangles
 * (the bbox is 19 px tall, so its top half overlaps the TOP triangle
 * and its bottom half overlaps the BOTTOM triangle along the block's
 * left edge).  None are suppressed by adjacency — the result is
 * LEFT|TOP|BOTTOM, the same bitmask original/ball.c:CheckRegions
 * would produce.  Downstream this hits the bounce switch's default
 * case (the original handled 4 single + 4 corner combos, not three-
 * region overlaps) — which is bug-for-bug parity with the original. */
static void test_first_contact_from_left_three_region_bbox_bitmask(void **state)
{
    fixture_t *f = *state;
    both_results_t r = run_both(f->ctx, 1, 3, 165, 48);
    assert_int_equal(r.old_result, BLOCK_REGION_LEFT);
    assert_int_equal(r.new_result, BLOCK_REGION_LEFT | BLOCK_REGION_TOP | BLOCK_REGION_BOTTOM);
}

/* =========================================================================
 * Group 3: Adjacency suppression.  Both functions suppress.
 * ========================================================================= */

/* DIVERGENCE.  TOP face of block (1, 4) ought to be suppressed
 * because row 0 col 4 is occupied.  The legacy classifier's relaxed
 * "phantom vs gap" adjacency check only suppresses when the ball
 * center is INSIDE the block on the affected axis (by >= bp->y).  For
 * a ball just above the block (by=29 < bp->y=38), the legacy treats
 * this as the "gap case" and lets TOP fire.  The new classifier
 * suppresses unconditionally when the neighbour is occupied (faithful
 * to original/ball.c:1414-1416), which is the correct behaviour:
 * the ball cannot physically reach the top face of (1, 4) without
 * passing through (0, 4). */
static void test_top_face_diverges_when_neighbour_above_occupied(void **state)
{
    fixture_t *f = *state;
    both_results_t r = run_both(f->ctx, 1, 4, 247, 29);
    assert_int_equal(r.old_result, BLOCK_REGION_TOP);
    assert_int_equal(r.new_result, BLOCK_REGION_NONE);
}

/* DIVERGENCE.  Ball just left of the seam in block (1, 3) — the
 * legacy classifier returns NONE because its center is inside the
 * block (bx=208 < bp->x+w=212), so it classifies as RIGHT and the
 * adjacency check suppresses (gap case requires bx > bp->x+w).
 *
 * The bbox classifier sees the bbox extends into the TOP and BOTTOM
 * triangles (no adjacency above or below for col=3), returning
 * TOP|BOTTOM.  Downstream this also hits the default switch — no
 * bounce — same effective outcome but for different reasons.  Pinned
 * here to document the structural difference. */
static void test_right_face_internal_diverges(void **state)
{
    fixture_t *f = *state;
    both_results_t r = run_both(f->ctx, 1, 3, 208, 48);
    assert_int_equal(r.old_result, BLOCK_REGION_NONE);
    assert_int_equal(r.new_result, BLOCK_REGION_TOP | BLOCK_REGION_BOTTOM);
}

/* =========================================================================
 * Group 4: Divergence cases — OLD and NEW disagree.
 *
 * These are the scenarios the bug report and our root-cause analysis
 * pointed at.  Both expectations are pinned so we have a written-down
 * record of where the legacy classifier is wrong.
 * ========================================================================= */

/* SEAM CASE.  Ball approaching the seam between (1, 3) and (1, 4) from
 * below, moving up.  Ball center at (219, 66) — squarely in the seam
 * gap (x=212..227) and 8 px below the block bottoms.  Ball bbox
 * (209..229, 57..76) overlaps (1, 3)'s right edge (3 px wide) AND
 * (1, 4)'s left edge (2 px wide), AND both blocks' bottom edges.
 *
 * Testing block (1, 3):
 *   Legacy: ball CENTER (bx=219) is right of (1, 3)'s right edge (212),
 *   so the diagonal cross-product classifier reports RIGHT.  Adjacency
 *   suppression for RIGHT requires bx <= bp->x + bp->width (219 <= 212),
 *   which is false ("gap case"), so the RIGHT hit fires.  Ball bounces
 *   horizontally only — dy untouched — and tunnels through the seam.
 *
 *   New: ball BBOX overlaps (1, 3)'s BOTTOM triangle AND its RIGHT
 *   triangle.  RIGHT is suppressed unconditionally because (1, 4) is
 *   occupied (original/ball.c:1398-1400 semantics).  BOTTOM is NOT
 *   suppressed (row 2 col 3 is empty).  Result: BOTTOM only.  Ball
 *   bounces correctly.
 */
static void test_seam_block_left_from_below_first_contact_agrees(void **state)
{
    fixture_t *f = *state;
    /* At first-contact y the two classifiers actually agree: the
     * center is far enough below the block (by=66, bp->y+h=58, so
     * 8 px below) that the diagonal-cross-product test correctly
     * lands in the BOTTOM quadrant.  This case is the post-PR-147
     * ray-march catches with the ordering+drift fixes. */
    both_results_t r = run_both(f->ctx, 1, 3, 219, 66);
    assert_int_equal(r.old_result, BLOCK_REGION_BOTTOM);
    assert_int_equal(r.new_result, BLOCK_REGION_BOTTOM);
}

/* The seam-tunneling misclassification fires when the ball ends up
 * sampled at a y INSIDE the block's range (where the legacy classifier
 * mistakes a vertical seam approach for a horizontal RIGHT-face hit).
 * Before PR #147 the buggy ray-march placed the ball there directly;
 * post-PR-147 this state should not be reachable in normal play, but
 * we pin the classifier divergence so it's auditable.
 *
 * Ball (219, 46): legacy → RIGHT (seam-tunneling classification —
 * ball center is right-of-block-edge and in mid-y range, the
 * diagonal cross-product returns RIGHT, the adjacency filter does NOT
 * suppress because bx > bp->x+w is the "gap" case).  Ball bounces
 * horizontally only, dy untouched, tunnels through the seam.
 *
 * New: bbox-vs-triangle finds the TOP triangle overlaps (the bbox
 * dips into the block's upper-right corner from above-and-right),
 * RIGHT is suppressed unconditionally by (1, 4) being occupied.
 * BOTTOM and LEFT triangles don't overlap.  Returns TOP only — ball
 * correctly reverses dy and bounces back up. */
static void test_seam_misclassification_mid_block_diverges(void **state)
{
    fixture_t *f = *state;
    both_results_t r = run_both(f->ctx, 1, 3, 219, 46);
    assert_int_equal(r.old_result, BLOCK_REGION_RIGHT);
    assert_int_equal(r.new_result, BLOCK_REGION_TOP);
}

/* CORNER HIT.  Ball coming from below-left of an isolated block,
 * straddling the BOTTOM-LEFT corner.  Block (1, 3) is isolated below
 * and to the left (rows 2 and col 2 are empty in the fixture).  Ball
 * center at (170, 60): bbox (160..180, 51..70) covers the bottom-left
 * corner of the block (172..212, 38..58).
 *
 * Legacy: classifies the center against the diagonal-split triangles
 *   d1 = 40*(60-38) - 20*(170-172) = 880 + 40 = 920 (>= 0)
 *   d2 = 20*(212-170) - 40*(60-38) = 840 - 880 = -40 (<= 0)
 *   d1 >= 0, d2 <= 0 → BOTTOM single-region.
 *
 * New: ball bbox overlaps BOTTOM triangle (yes) AND LEFT triangle (yes,
 * because bbox extends left of the block's centre x).  Neither face is
 * suppressed (neighbouring cells empty).  Returns BOTTOM | LEFT.
 */
static void test_corner_hit_bottom_left_diverges(void **state)
{
    fixture_t *f = *state;
    both_results_t r = run_both(f->ctx, 1, 3, 170, 60);
    assert_int_equal(r.old_result, BLOCK_REGION_BOTTOM);
    assert_int_equal(r.new_result, BLOCK_REGION_BOTTOM | BLOCK_REGION_LEFT);
}

/* =========================================================================
 * Group 5: Isolated block — exhaustive corner combinations on the new
 * classifier.  Validate that an unblocked isolated block reports the
 * expected corner bitmask for each of the four diagonal approaches.
 *
 * We use a separate fixture (single isolated block at row 5 col 4) so
 * adjacency suppression cannot mask the geometric result.
 * ========================================================================= */

static int setup_isolated(void **state)
{
    fixture_t *f = calloc(1, sizeof(*f));
    f->ctx = block_system_create(55, 32, NULL);
    block_system_add(f->ctx, 5, 4, 1, 0, 0);
    *state = f;
    return 0;
}

static void test_isolated_corner_top_left(void **state)
{
    fixture_t *f = *state;
    /* Block (5, 4): bp->x=227, bp->y=166, ends (267, 186).  Approach
     * from above-left: ball center (218, 159), bbox (208..228, 150..169)
     * straddles the top-left corner. */
    int new_result = block_system_check_region_bbox(5, 4, 218, 159, 0, f->ctx);
    assert_int_equal(new_result, BLOCK_REGION_TOP | BLOCK_REGION_LEFT);
}

static void test_isolated_corner_top_right(void **state)
{
    fixture_t *f = *state;
    int new_result = block_system_check_region_bbox(5, 4, 276, 159, 0, f->ctx);
    assert_int_equal(new_result, BLOCK_REGION_TOP | BLOCK_REGION_RIGHT);
}

static void test_isolated_corner_bottom_left(void **state)
{
    fixture_t *f = *state;
    int new_result = block_system_check_region_bbox(5, 4, 218, 195, 0, f->ctx);
    assert_int_equal(new_result, BLOCK_REGION_BOTTOM | BLOCK_REGION_LEFT);
}

static void test_isolated_corner_bottom_right(void **state)
{
    fixture_t *f = *state;
    int new_result = block_system_check_region_bbox(5, 4, 276, 195, 0, f->ctx);
    assert_int_equal(new_result, BLOCK_REGION_BOTTOM | BLOCK_REGION_RIGHT);
}

/* Edge-of-grid cases — row=0 has no row above, so TOP cannot be
 * suppressed by adjacency.  Similarly for col=0 / col=MAX_COL-1 / row=MAX_ROW-1. */
static void test_edge_of_grid_no_neighbour_above_for_row_zero(void **state)
{
    fixture_t *f = *state;
    /* Add a block at row 0 col 4 (no row -1 exists). */
    block_system_add(f->ctx, 0, 4, 1, 0, 0);
    /* Ball just above row 0 col 4 (bp->y=6 for row 0).  Center (247, 0),
     * bbox (237..257, -9..10) — TOP face approached from above. */
    int new_result = block_system_check_region_bbox(0, 4, 247, 0, 0, f->ctx);
    assert_int_equal(new_result, BLOCK_REGION_TOP);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: agreement on trivial cases */
        cmocka_unit_test_setup_teardown(test_out_of_bounds_returns_none, setup_seam,
                                        teardown_seam),
        cmocka_unit_test_setup_teardown(test_empty_cell_returns_none, setup_seam, teardown_seam),
        cmocka_unit_test_setup_teardown(test_ball_far_from_block_returns_none, setup_seam,
                                        teardown_seam),
        /* Group 2: first-contact agreement */
        cmocka_unit_test_setup_teardown(test_first_contact_from_below_returns_bottom, setup_seam,
                                        teardown_seam),
        cmocka_unit_test_setup_teardown(test_first_contact_from_above_returns_top, setup_seam,
                                        teardown_seam),
        cmocka_unit_test_setup_teardown(test_first_contact_from_left_three_region_bbox_bitmask,
                                        setup_seam, teardown_seam),
        /* Group 3: adjacency suppression divergence */
        cmocka_unit_test_setup_teardown(test_top_face_diverges_when_neighbour_above_occupied,
                                        setup_seam, teardown_seam),
        cmocka_unit_test_setup_teardown(test_right_face_internal_diverges, setup_seam,
                                        teardown_seam),
        /* Group 4: seam classifier divergence */
        cmocka_unit_test_setup_teardown(test_seam_block_left_from_below_first_contact_agrees,
                                        setup_seam, teardown_seam),
        cmocka_unit_test_setup_teardown(test_seam_misclassification_mid_block_diverges, setup_seam,
                                        teardown_seam),
        cmocka_unit_test_setup_teardown(test_corner_hit_bottom_left_diverges, setup_seam,
                                        teardown_seam),
        /* Group 5: isolated-block corner combinations (new function only) */
        cmocka_unit_test_setup_teardown(test_isolated_corner_top_left, setup_isolated,
                                        teardown_seam),
        cmocka_unit_test_setup_teardown(test_isolated_corner_top_right, setup_isolated,
                                        teardown_seam),
        cmocka_unit_test_setup_teardown(test_isolated_corner_bottom_left, setup_isolated,
                                        teardown_seam),
        cmocka_unit_test_setup_teardown(test_isolated_corner_bottom_right, setup_isolated,
                                        teardown_seam),
        cmocka_unit_test_setup_teardown(test_edge_of_grid_no_neighbour_above_for_row_zero,
                                        setup_isolated, teardown_seam),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
