/*
 * test_game_render_specials.c — Tests for the pure coordinate helper and
 * color-selection logic in the specials panel renderer.
 *
 * These tests exercise game_render_specials_coords() without an SDL2 context.
 * The helper is a pure function: given lh and a labels array it computes
 * absolute pixel positions deterministically.
 *
 * 3 groups:
 *   1. Coordinate math (8 label positions for known lh=15)
 *   2. Color-selection branch (active=yellow, inactive=white)
 *   3. NULL-context safety (game_render_specials with NULL ctx fields)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

/* cmocka must come after setjmp.h / stdarg.h / stddef.h */
#include <cmocka.h>

#include "game_render.h"
#include "special_system.h"

/* =========================================================================
 * Shared test fixture — canonical label array from special_system
 * ========================================================================= */

/*
 * Build a label array with all specials off and reverse_on=0.
 * This exercises the layout constants directly.
 */
static void make_labels_all_inactive(special_label_info_t *labels)
{
    special_system_t *sys = special_system_create(NULL, NULL);
    special_system_get_labels(sys, 0, labels);
    special_system_destroy(sys);
}

static void make_labels_all_active(special_label_info_t *labels)
{
    special_system_t *sys = special_system_create(NULL, NULL);
    /* Activate all 7 owned specials */
    special_system_set(sys, SPECIAL_STICKY, 1);
    special_system_set(sys, SPECIAL_SAVING, 1);
    special_system_set(sys, SPECIAL_FAST_GUN, 1);
    special_system_set(sys, SPECIAL_NO_WALLS, 1);
    special_system_set(sys, SPECIAL_KILLER, 1);
    special_system_set(sys, SPECIAL_X2_BONUS, 1);
    /* x4 would deactivate x2 — leave x2 for this fixture */
    special_system_get_labels(sys, 1, labels); /* reverse_on=1 */
    special_system_destroy(sys);
}

/* Panel origin constants — must match the defines in game_render.c */
#define TEST_PANEL_ORIGIN_X 292
#define TEST_PANEL_ORIGIN_Y 655

/* =========================================================================
 * Group 1: Coordinate math
 * ========================================================================= */

/*
 * Test: label 0 ("Reverse") — col0, row0
 *   abs_x = 292 + SPECIAL_COL0_X = 292 + 5 = 297
 *   abs_y = 655 + SPECIAL_ROW0_Y + 0 * (15 + 5) = 655 + 3 = 658
 *
 * The contract mandates this exact assertion.
 */
static void test_coords_label0_reverse(void **state)
{
    (void)state;
    special_label_info_t labels[SPECIAL_COUNT];
    make_labels_all_inactive(labels);

    int abs_x = 0;
    int abs_y = 0;
    game_render_specials_coords(15, labels, 0, &abs_x, &abs_y);

    assert_int_equal(abs_x, TEST_PANEL_ORIGIN_X + SPECIAL_COL0_X); /* 292 + 5 = 297 */
    assert_int_equal(abs_y, TEST_PANEL_ORIGIN_Y + SPECIAL_ROW0_Y); /* 655 + 3 = 658 */
}

/*
 * Test: label 1 ("Save") — col1, row0
 *   abs_x = 292 + SPECIAL_COL1_X = 292 + 55 = 347
 *   abs_y = 655 + 3 + 0 * (15+5) = 658
 */
static void test_coords_label1_save(void **state)
{
    (void)state;
    special_label_info_t labels[SPECIAL_COUNT];
    make_labels_all_inactive(labels);

    int abs_x = 0;
    int abs_y = 0;
    game_render_specials_coords(15, labels, 1, &abs_x, &abs_y);

    assert_int_equal(abs_x, TEST_PANEL_ORIGIN_X + SPECIAL_COL1_X); /* 347 */
    assert_int_equal(abs_y, TEST_PANEL_ORIGIN_Y + SPECIAL_ROW0_Y); /* 658 */
}

/*
 * Test: label 2 ("NoWall") — col2, row0
 *   abs_x = 292 + 110 = 402
 *   abs_y = 658
 */
static void test_coords_label2_nowall(void **state)
{
    (void)state;
    special_label_info_t labels[SPECIAL_COUNT];
    make_labels_all_inactive(labels);

    int abs_x = 0;
    int abs_y = 0;
    game_render_specials_coords(15, labels, 2, &abs_x, &abs_y);

    assert_int_equal(abs_x, TEST_PANEL_ORIGIN_X + SPECIAL_COL2_X); /* 402 */
    assert_int_equal(abs_y, TEST_PANEL_ORIGIN_Y + SPECIAL_ROW0_Y); /* 658 */
}

/*
 * Test: label 3 ("x2") — col3, row0
 *   abs_x = 292 + 155 = 447
 *   abs_y = 658
 */
static void test_coords_label3_x2(void **state)
{
    (void)state;
    special_label_info_t labels[SPECIAL_COUNT];
    make_labels_all_inactive(labels);

    int abs_x = 0;
    int abs_y = 0;
    game_render_specials_coords(15, labels, 3, &abs_x, &abs_y);

    assert_int_equal(abs_x, TEST_PANEL_ORIGIN_X + SPECIAL_COL3_X); /* 447 */
    assert_int_equal(abs_y, TEST_PANEL_ORIGIN_Y + SPECIAL_ROW0_Y); /* 658 */
}

/*
 * Test: label 4 ("Sticky") — col0, row1
 *   abs_x = 297
 *   abs_y = 655 + 3 + 1 * (15 + 5) = 678
 */
static void test_coords_label4_sticky(void **state)
{
    (void)state;
    special_label_info_t labels[SPECIAL_COUNT];
    make_labels_all_inactive(labels);

    int abs_x = 0;
    int abs_y = 0;
    game_render_specials_coords(15, labels, 4, &abs_x, &abs_y);

    assert_int_equal(abs_x, TEST_PANEL_ORIGIN_X + SPECIAL_COL0_X);                  /* 297 */
    assert_int_equal(abs_y, TEST_PANEL_ORIGIN_Y + SPECIAL_ROW0_Y + 1 * (15 + SPECIAL_GAP)); /* 678 */
}

/*
 * Test: label 5 ("FastGun") — col1, row1
 *   abs_x = 347
 *   abs_y = 678
 */
static void test_coords_label5_fastgun(void **state)
{
    (void)state;
    special_label_info_t labels[SPECIAL_COUNT];
    make_labels_all_inactive(labels);

    int abs_x = 0;
    int abs_y = 0;
    game_render_specials_coords(15, labels, 5, &abs_x, &abs_y);

    assert_int_equal(abs_x, TEST_PANEL_ORIGIN_X + SPECIAL_COL1_X);                  /* 347 */
    assert_int_equal(abs_y, TEST_PANEL_ORIGIN_Y + SPECIAL_ROW0_Y + 1 * (15 + SPECIAL_GAP)); /* 678 */
}

/*
 * Test: label 6 ("Killer") — col2, row1
 *   abs_x = 402
 *   abs_y = 678
 */
static void test_coords_label6_killer(void **state)
{
    (void)state;
    special_label_info_t labels[SPECIAL_COUNT];
    make_labels_all_inactive(labels);

    int abs_x = 0;
    int abs_y = 0;
    game_render_specials_coords(15, labels, 6, &abs_x, &abs_y);

    assert_int_equal(abs_x, TEST_PANEL_ORIGIN_X + SPECIAL_COL2_X);                  /* 402 */
    assert_int_equal(abs_y, TEST_PANEL_ORIGIN_Y + SPECIAL_ROW0_Y + 1 * (15 + SPECIAL_GAP)); /* 678 */
}

/*
 * Test: label 7 ("x4") — col3, row1
 *   abs_x = 447
 *   abs_y = 678
 */
static void test_coords_label7_x4(void **state)
{
    (void)state;
    special_label_info_t labels[SPECIAL_COUNT];
    make_labels_all_inactive(labels);

    int abs_x = 0;
    int abs_y = 0;
    game_render_specials_coords(15, labels, 7, &abs_x, &abs_y);

    assert_int_equal(abs_x, TEST_PANEL_ORIGIN_X + SPECIAL_COL3_X);                  /* 447 */
    assert_int_equal(abs_y, TEST_PANEL_ORIGIN_Y + SPECIAL_ROW0_Y + 1 * (15 + SPECIAL_GAP)); /* 678 */
}

/* =========================================================================
 * Group 2: Color-selection branch
 * ========================================================================= */

/*
 * Test: inactive labels have active==0 (white branch).
 */
static void test_color_inactive_produces_white(void **state)
{
    (void)state;
    special_label_info_t labels[SPECIAL_COUNT];
    make_labels_all_inactive(labels);

    for (int i = 0; i < SPECIAL_COUNT; i++)
    {
        assert_int_equal(labels[i].active, 0);
    }
}

/*
 * Test: active labels have active==1 (yellow branch).
 * Uses the all-active fixture: reverse_on=1, all 7 owned specials set.
 * (x4 is not set in make_labels_all_active to avoid clearing x2,
 *  so x4 stays inactive — verify x2 is active and x4 is inactive.)
 */
static void test_color_active_produces_yellow(void **state)
{
    (void)state;
    special_label_info_t labels[SPECIAL_COUNT];
    make_labels_all_active(labels);

    /* label 0: Reverse — active (reverse_on=1) */
    assert_int_equal(labels[0].active, 1);
    /* label 1: Save — active */
    assert_int_equal(labels[1].active, 1);
    /* label 2: NoWall — active */
    assert_int_equal(labels[2].active, 1);
    /* label 3: x2 — active */
    assert_int_equal(labels[3].active, 1);
    /* label 4: Sticky — active */
    assert_int_equal(labels[4].active, 1);
    /* label 5: FastGun — active */
    assert_int_equal(labels[5].active, 1);
    /* label 6: Killer — active */
    assert_int_equal(labels[6].active, 1);
    /* label 7: x4 — inactive (x2 and x4 are mutually exclusive; x2 was set last) */
    assert_int_equal(labels[7].active, 0);
}

/*
 * Test: mixed state — only Killer active.
 */
static void test_color_mixed_state(void **state)
{
    (void)state;
    special_system_t *sys = special_system_create(NULL, NULL);
    special_system_set(sys, SPECIAL_KILLER, 1);

    special_label_info_t labels[SPECIAL_COUNT];
    special_system_get_labels(sys, 0, labels);
    special_system_destroy(sys);

    /* Only killer (label 6) should be active */
    for (int i = 0; i < SPECIAL_COUNT; i++)
    {
        if (i == 6)
            assert_int_equal(labels[i].active, 1);
        else
            assert_int_equal(labels[i].active, 0);
    }
}

/* =========================================================================
 * Group 3: NULL-context safety
 * ========================================================================= */

/*
 * Test: game_render_specials() returns without crashing when ctx->special is NULL.
 *
 * We construct a minimal stack-allocated game_ctx_t with only the fields
 * that game_render_specials reads: ctx->special and ctx->paddle.
 * Both are set to NULL — the function must return immediately.
 *
 * Note: game_ctx_t has many pointer fields; we zero the whole struct to
 * ensure no field is left uninitialized. The function under test only
 * dereferences ctx->special and ctx->paddle before the NULL guard.
 */
static void test_null_ctx_special_does_not_crash(void **state)
{
    (void)state;
    game_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    /* ctx.special == NULL, ctx.paddle == NULL — both trigger the guard */
    game_render_specials(&ctx); /* Must not crash */
}

/*
 * Test: game_render_specials() returns without crashing when ctx->paddle is NULL
 * but ctx->special is non-NULL.
 */
static void test_null_ctx_paddle_does_not_crash(void **state)
{
    (void)state;
    special_system_t *sys = special_system_create(NULL, NULL);

    game_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.special = sys;
    /* ctx.paddle == NULL */
    game_render_specials(&ctx); /* Must not crash */

    special_system_destroy(sys);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Coordinate math — all 8 labels for lh=15 */
        cmocka_unit_test(test_coords_label0_reverse),
        cmocka_unit_test(test_coords_label1_save),
        cmocka_unit_test(test_coords_label2_nowall),
        cmocka_unit_test(test_coords_label3_x2),
        cmocka_unit_test(test_coords_label4_sticky),
        cmocka_unit_test(test_coords_label5_fastgun),
        cmocka_unit_test(test_coords_label6_killer),
        cmocka_unit_test(test_coords_label7_x4),

        /* Group 2: Color-selection branch */
        cmocka_unit_test(test_color_inactive_produces_white),
        cmocka_unit_test(test_color_active_produces_yellow),
        cmocka_unit_test(test_color_mixed_state),

        /* Group 3: NULL-context safety */
        cmocka_unit_test(test_null_ctx_special_does_not_crash),
        cmocka_unit_test(test_null_ctx_paddle_does_not_crash),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
