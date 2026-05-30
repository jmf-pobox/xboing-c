/*
 * test_block_system_explosion.c — Tests for the block explosion state
 * machine (block_system_explode + block_system_update_explosions).
 *
 * Pure-logic CMocka tests, no SDL2 link required.  Covers basket 3 of
 * the visual-fidelity audit (xboing-c-tmr).  Reference:
 * docs/research/2026-05-02-block-explosion.md and
 * docs/reviews/2026-05-02-basket3-block-explosion-spec-review.md.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>

/* cmocka must come after std headers */
#include <cmocka.h>

#include "block_system.h"
#include "block_types.h"

#define COL_WIDTH 55
#define ROW_HEIGHT 32

static block_system_t *make_ctx(void)
{
    block_system_status_t st;
    block_system_t *ctx = block_system_create(COL_WIDTH, ROW_HEIGHT, &st);
    assert_non_null(ctx);
    assert_int_equal(st, BLOCK_SYS_OK);
    return ctx;
}

/* =========================================================================
 * 1. Trigger semantics
 * ========================================================================= */

static void test_explode_arms_block(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();
    assert_int_equal(block_system_add(ctx, 5, 3, RED_BLK, 0, 0), BLOCK_SYS_OK);
    assert_int_equal(block_system_get_exploding_count(ctx), 0);

    assert_int_equal(block_system_explode(ctx, 5, 3, 100), BLOCK_SYS_OK);

    block_system_render_info_t info;
    assert_int_equal(block_system_get_render_info(ctx, 5, 3, &info), BLOCK_SYS_OK);
    assert_int_equal(info.occupied, 1);
    assert_int_equal(info.exploding, 1);
    assert_int_equal(info.explode_slide, 1);
    assert_int_equal(block_system_get_exploding_count(ctx), 1);

    block_system_destroy(ctx);
}

static void test_explode_returns_err_when_unoccupied(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();
    assert_int_equal(block_system_explode(ctx, 0, 0, 0), BLOCK_SYS_ERR_INVALID_STATE);
    assert_int_equal(block_system_get_exploding_count(ctx), 0);
    block_system_destroy(ctx);
}

static void test_explode_returns_err_when_already_exploding(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();
    assert_int_equal(block_system_add(ctx, 1, 1, BLUE_BLK, 0, 0), BLOCK_SYS_OK);
    assert_int_equal(block_system_explode(ctx, 1, 1, 0), BLOCK_SYS_OK);
    assert_int_equal(block_system_get_exploding_count(ctx), 1);

    /* Second arm attempt rejected; counter unchanged. */
    assert_int_equal(block_system_explode(ctx, 1, 1, 0), BLOCK_SYS_ERR_INVALID_STATE);
    assert_int_equal(block_system_get_exploding_count(ctx), 1);
    block_system_destroy(ctx);
}

static void test_explode_returns_err_for_hyperspace(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();
    assert_int_equal(block_system_add(ctx, 4, 4, HYPERSPACE_BLK, 0, 0), BLOCK_SYS_OK);
    assert_int_equal(block_system_explode(ctx, 4, 4, 0), BLOCK_SYS_ERR_INVALID_STATE);
    assert_int_equal(block_system_get_exploding_count(ctx), 0);
    block_system_destroy(ctx);
}

static void test_explode_returns_err_for_out_of_bounds(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();
    assert_int_equal(block_system_explode(ctx, -1, 0, 0), BLOCK_SYS_ERR_OUT_OF_BOUNDS);
    assert_int_equal(block_system_explode(ctx, 0, -1, 0), BLOCK_SYS_ERR_OUT_OF_BOUNDS);
    assert_int_equal(block_system_explode(ctx, MAX_ROW, 0, 0), BLOCK_SYS_ERR_OUT_OF_BOUNDS);
    assert_int_equal(block_system_explode(ctx, 0, MAX_COL, 0), BLOCK_SYS_ERR_OUT_OF_BOUNDS);
    block_system_destroy(ctx);
}

/* =========================================================================
 * 2. State machine cadence
 * ========================================================================= */

static void test_update_advances_one_stage_per_tick_at_delay(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();
    assert_int_equal(block_system_add(ctx, 2, 2, GREEN_BLK, 0, 0), BLOCK_SYS_OK);
    assert_int_equal(block_system_explode(ctx, 2, 2, 0), BLOCK_SYS_OK);

    block_system_render_info_t info;

    /* Same-tick stage 1: arming sets next_frame=0 and slide=1.  Calling
     * update at frame=0 fires stage 1 and increments slide to 2,
     * next_frame to 10. */
    block_system_update_explosions(ctx, 0, NULL, NULL);
    assert_int_equal(block_system_get_render_info(ctx, 2, 2, &info), BLOCK_SYS_OK);
    assert_int_equal(info.exploding, 1);
    assert_int_equal(info.explode_slide, 2);

    block_system_update_explosions(ctx, 10, NULL, NULL);
    assert_int_equal(block_system_get_render_info(ctx, 2, 2, &info), BLOCK_SYS_OK);
    assert_int_equal(info.explode_slide, 3);

    block_system_update_explosions(ctx, 20, NULL, NULL);
    assert_int_equal(block_system_get_render_info(ctx, 2, 2, &info), BLOCK_SYS_OK);
    assert_int_equal(info.explode_slide, 4);

    /* Stage 4 → 5 triggers finalize; cell is cleared. */
    block_system_update_explosions(ctx, 30, NULL, NULL);
    assert_int_equal(block_system_get_render_info(ctx, 2, 2, &info), BLOCK_SYS_OK);
    assert_int_equal(info.occupied, 0);
    assert_int_equal(info.exploding, 0);

    block_system_destroy(ctx);
}

static void test_update_clears_cell_at_finalize(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();
    assert_int_equal(block_system_add(ctx, 7, 7, BLUE_BLK, 0, 0), BLOCK_SYS_OK);
    assert_int_equal(block_system_explode(ctx, 7, 7, 0), BLOCK_SYS_OK);

    /* Drive the animation through stages 1..4 to finalize. */
    for (int f = 0; f <= 30; f += BLOCK_EXPLODE_DELAY)
    {
        block_system_update_explosions(ctx, f, NULL, NULL);
    }

    assert_int_equal(block_system_is_occupied(ctx, 7, 7), 0);
    assert_int_equal(block_system_get_exploding_count(ctx), 0);
    block_system_destroy(ctx);
}

/* =========================================================================
 * 3. Finalize callback
 * ========================================================================= */

typedef struct
{
    int call_count;
    int row;
    int col;
    int block_type;
    int hit_points;
    int saw_unoccupied;
    block_system_t *ctx;
} finalize_recorder_t;

static void recorder_cb(int row, int col, int block_type, int hit_points, void *ud)
{
    finalize_recorder_t *r = ud;
    r->call_count++;
    r->row = row;
    r->col = col;
    r->block_type = block_type;
    r->hit_points = hit_points;
    /* Probe: is the source cell unoccupied at callback time? */
    if (r->ctx)
        r->saw_unoccupied = !block_system_is_occupied(r->ctx, row, col);
}

static void test_update_invokes_finalize_callback_with_saved_state(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();
    assert_int_equal(block_system_add(ctx, 3, 5, BULLET_BLK, 0, 0), BLOCK_SYS_OK);
    int saved_hit_points = block_system_get_hit_points(ctx, 3, 5);
    assert_int_equal(block_system_explode(ctx, 3, 5, 0), BLOCK_SYS_OK);

    finalize_recorder_t r = {0};
    for (int f = 0; f <= 30; f += BLOCK_EXPLODE_DELAY)
    {
        block_system_update_explosions(ctx, f, recorder_cb, &r);
    }

    assert_int_equal(r.call_count, 1);
    assert_int_equal(r.row, 3);
    assert_int_equal(r.col, 5);
    assert_int_equal(r.block_type, BULLET_BLK);
    assert_int_equal(r.hit_points, saved_hit_points);
    block_system_destroy(ctx);
}

static void test_update_no_callback_invocation_before_finalize(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();
    assert_int_equal(block_system_add(ctx, 0, 0, RED_BLK, 0, 0), BLOCK_SYS_OK);
    assert_int_equal(block_system_explode(ctx, 0, 0, 0), BLOCK_SYS_OK);

    finalize_recorder_t r = {0};
    /* Stages 1..4: callback should NOT fire. */
    for (int f = 0; f <= 20; f += BLOCK_EXPLODE_DELAY)
    {
        block_system_update_explosions(ctx, f, recorder_cb, &r);
    }
    assert_int_equal(r.call_count, 0);

    /* Stage 5 fires callback. */
    block_system_update_explosions(ctx, 30, recorder_cb, &r);
    assert_int_equal(r.call_count, 1);
    block_system_destroy(ctx);
}

/* =========================================================================
 * 4. Boundary / scaling
 * ========================================================================= */

static void test_update_does_nothing_when_zero_exploding(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();
    assert_int_equal(block_system_add(ctx, 0, 0, RED_BLK, 0, 0), BLOCK_SYS_OK);

    finalize_recorder_t r = {0};
    /* No arming — counter is 0, fast-return path. */
    for (int f = 0; f < 100; f++)
    {
        block_system_update_explosions(ctx, f, recorder_cb, &r);
    }
    assert_int_equal(r.call_count, 0);
    assert_int_equal(block_system_is_occupied(ctx, 0, 0), 1);
    block_system_destroy(ctx);
}

static void test_update_handles_multiple_concurrent_explosions(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();
    assert_int_equal(block_system_add(ctx, 0, 0, RED_BLK, 0, 0), BLOCK_SYS_OK);
    assert_int_equal(block_system_add(ctx, 1, 1, BLUE_BLK, 0, 0), BLOCK_SYS_OK);
    assert_int_equal(block_system_add(ctx, 2, 2, GREEN_BLK, 0, 0), BLOCK_SYS_OK);

    /* Arm at staggered start frames. */
    assert_int_equal(block_system_explode(ctx, 0, 0, 0), BLOCK_SYS_OK);
    assert_int_equal(block_system_explode(ctx, 1, 1, 5), BLOCK_SYS_OK);
    assert_int_equal(block_system_explode(ctx, 2, 2, 10), BLOCK_SYS_OK);
    assert_int_equal(block_system_get_exploding_count(ctx), 3);

    finalize_recorder_t r = {.ctx = ctx};

    /* Drive forward enough ticks for all three to finalize. */
    for (int f = 0; f <= 50; f++)
    {
        block_system_update_explosions(ctx, f, recorder_cb, &r);
    }

    assert_int_equal(r.call_count, 3);
    assert_int_equal(block_system_is_occupied(ctx, 0, 0), 0);
    assert_int_equal(block_system_is_occupied(ctx, 1, 1), 0);
    assert_int_equal(block_system_is_occupied(ctx, 2, 2), 0);
    assert_int_equal(block_system_get_exploding_count(ctx), 0);
    block_system_destroy(ctx);
}

static void test_explode_keeps_cell_occupied_through_animation(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();
    assert_int_equal(block_system_add(ctx, 6, 6, TAN_BLK, 0, 0), BLOCK_SYS_OK);
    assert_int_equal(block_system_explode(ctx, 6, 6, 0), BLOCK_SYS_OK);

    /* Stages 1..4: cell remains occupied AND unavailable for placement. */
    for (int f = 0; f <= 20; f += BLOCK_EXPLODE_DELAY)
    {
        block_system_update_explosions(ctx, f, NULL, NULL);
        assert_int_equal(block_system_is_occupied(ctx, 6, 6), 1);
        /* cell_available should report false during animation. */
        assert_int_equal(block_system_cell_available(6, 6, ctx), 0);
    }

    /* Stage 5: finalize clears the cell. */
    block_system_update_explosions(ctx, 30, NULL, NULL);
    assert_int_equal(block_system_is_occupied(ctx, 6, 6), 0);
    assert_int_equal(block_system_cell_available(6, 6, ctx), 1);
    block_system_destroy(ctx);
}

/* =========================================================================
 * 5. Ordering invariants — gjm B-2, B-3
 * ========================================================================= */

/* B-2: block_system_is_occupied must return 0 inside the finalize callback. */
static void test_finalize_callback_sees_cell_unoccupied(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();
    assert_int_equal(block_system_add(ctx, 4, 0, PURPLE_BLK, 0, 0), BLOCK_SYS_OK);
    assert_int_equal(block_system_explode(ctx, 4, 0, 0), BLOCK_SYS_OK);

    finalize_recorder_t r = {.ctx = ctx};
    for (int f = 0; f <= 30; f += BLOCK_EXPLODE_DELAY)
    {
        block_system_update_explosions(ctx, f, recorder_cb, &r);
    }

    assert_int_equal(r.call_count, 1);
    /* The probe inside the callback recorded 1 (cell was unoccupied). */
    assert_int_equal(r.saw_unoccupied, 1);
    block_system_destroy(ctx);
}

/* B-3: BOMB chain must skip HYPERSPACE neighbors.  Drives the chain via
 * a stub callback that mirrors game_callbacks_on_block_finalize for
 * BOMB_BLK — keeps the test pure-logic with no SDL2 dependency. */
typedef struct
{
    block_system_t *ctx;
    int frame_at_finalize;
} bomb_chain_ud_t;

static void bomb_chain_cb(int row, int col, int block_type, int hit_points, void *ud)
{
    (void)hit_points;
    bomb_chain_ud_t *bcu = ud;
    if (block_type != BOMB_BLK)
        return;
    /* Mirror the production BOMB chain logic. */
    for (int dr = -1; dr <= 1; dr++)
    {
        for (int dc = -1; dc <= 1; dc++)
        {
            if (dr == 0 && dc == 0)
                continue;
            (void)block_system_explode(bcu->ctx, row + dr, col + dc,
                                       bcu->frame_at_finalize + BLOCK_EXPLODE_DELAY);
        }
    }
}

static void test_bomb_chain_skips_hyperspace_neighbor(void **state)
{
    (void)state;
    block_system_t *ctx = make_ctx();
    assert_int_equal(block_system_add(ctx, 1, 1, BOMB_BLK, 0, 0), BLOCK_SYS_OK);
    assert_int_equal(block_system_add(ctx, 1, 2, HYPERSPACE_BLK, 0, 0), BLOCK_SYS_OK);

    assert_int_equal(block_system_explode(ctx, 1, 1, 0), BLOCK_SYS_OK);

    /* Drive the source explosion through finalize.  When stage 5 fires
     * at frame=30, the callback (bomb_chain_cb) attempts to explode 8
     * neighbors at frame=30+BLOCK_EXPLODE_DELAY.  HYPERSPACE neighbor
     * must reject. */
    bomb_chain_ud_t bcu = {.ctx = ctx, .frame_at_finalize = 30};
    for (int f = 0; f <= 30; f += BLOCK_EXPLODE_DELAY)
    {
        block_system_update_explosions(ctx, f, bomb_chain_cb, &bcu);
    }

    assert_int_equal(block_system_is_occupied(ctx, 1, 2), 1);
    assert_int_equal(block_system_get_type(ctx, 1, 2), HYPERSPACE_BLK);

    block_system_render_info_t info;
    assert_int_equal(block_system_get_render_info(ctx, 1, 2, &info), BLOCK_SYS_OK);
    assert_int_equal(info.exploding, 0);
    block_system_destroy(ctx);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_explode_arms_block),
        cmocka_unit_test(test_explode_returns_err_when_unoccupied),
        cmocka_unit_test(test_explode_returns_err_when_already_exploding),
        cmocka_unit_test(test_explode_returns_err_for_hyperspace),
        cmocka_unit_test(test_explode_returns_err_for_out_of_bounds),
        cmocka_unit_test(test_update_advances_one_stage_per_tick_at_delay),
        cmocka_unit_test(test_update_clears_cell_at_finalize),
        cmocka_unit_test(test_update_invokes_finalize_callback_with_saved_state),
        cmocka_unit_test(test_update_no_callback_invocation_before_finalize),
        cmocka_unit_test(test_update_does_nothing_when_zero_exploding),
        cmocka_unit_test(test_update_handles_multiple_concurrent_explosions),
        cmocka_unit_test(test_explode_keeps_cell_occupied_through_animation),
        cmocka_unit_test(test_finalize_callback_sees_cell_unoccupied),
        cmocka_unit_test(test_bomb_chain_skips_hyperspace_neighbor),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
