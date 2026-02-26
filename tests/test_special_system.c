/*
 * test_special_system.c — Tests for the pure C special/power-up system.
 *
 * 6 groups:
 *   1. Lifecycle (3 tests)
 *   2. Individual toggles (7 tests)
 *   3. Mutual exclusion (3 tests)
 *   4. Turn off all (3 tests)
 *   5. State queries and labels (4 tests)
 *   6. Attract-mode randomization (3 tests)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* cmocka must come after setjmp.h / stdarg.h / stddef.h */
#include <cmocka.h>

#include "special_system.h"

/* =========================================================================
 * Test helpers
 * ========================================================================= */

/* Track wall state callback invocations */
static int g_wall_callback_count;
static int g_wall_last_no_walls;

static void wall_state_callback(int no_walls, void *ud)
{
    (void)ud;
    g_wall_callback_count++;
    g_wall_last_no_walls = no_walls;
}

static void reset_wall_tracking(void)
{
    g_wall_callback_count = 0;
    g_wall_last_no_walls = -1;
}

/* Deterministic random for attract-mode tests */
static int g_rand_sequence[16];
static int g_rand_index;

static int deterministic_rand(void)
{
    return g_rand_sequence[g_rand_index++];
}

/* =========================================================================
 * Group 1: Lifecycle
 * ========================================================================= */

/* Test: create and destroy with no callbacks */
static void test_create_destroy_no_callbacks(void **state)
{
    (void)state;
    special_system_t *ctx = special_system_create(NULL, NULL);
    assert_non_null(ctx);
    special_system_destroy(ctx);
}

/* Test: create and destroy with callbacks */
static void test_create_destroy_with_callbacks(void **state)
{
    (void)state;
    special_system_callbacks_t cbs = {.on_wall_state_changed = wall_state_callback};
    special_system_t *ctx = special_system_create(&cbs, NULL);
    assert_non_null(ctx);
    special_system_destroy(ctx);
}

/* Test: all specials start off */
static void test_initial_state_all_off(void **state)
{
    (void)state;
    special_system_t *ctx = special_system_create(NULL, NULL);
    assert_non_null(ctx);

    assert_int_equal(special_system_is_active(ctx, SPECIAL_STICKY), 0);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_SAVING), 0);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_FAST_GUN), 0);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_NO_WALLS), 0);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_KILLER), 0);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_X2_BONUS), 0);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_X4_BONUS), 0);

    /* Reverse always returns 0 (owned by paddle system) */
    assert_int_equal(special_system_is_active(ctx, SPECIAL_REVERSE), 0);

    special_system_destroy(ctx);
}

/* =========================================================================
 * Group 2: Individual toggles
 * ========================================================================= */

/* Test: toggle sticky on and off */
static void test_toggle_sticky(void **state)
{
    (void)state;
    special_system_t *ctx = special_system_create(NULL, NULL);

    special_system_set(ctx, SPECIAL_STICKY, 1);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_STICKY), 1);

    special_system_set(ctx, SPECIAL_STICKY, 0);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_STICKY), 0);

    special_system_destroy(ctx);
}

/* Test: toggle saving on and off */
static void test_toggle_saving(void **state)
{
    (void)state;
    special_system_t *ctx = special_system_create(NULL, NULL);

    special_system_set(ctx, SPECIAL_SAVING, 1);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_SAVING), 1);

    special_system_set(ctx, SPECIAL_SAVING, 0);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_SAVING), 0);

    special_system_destroy(ctx);
}

/* Test: toggle fast gun on and off */
static void test_toggle_fast_gun(void **state)
{
    (void)state;
    special_system_t *ctx = special_system_create(NULL, NULL);

    special_system_set(ctx, SPECIAL_FAST_GUN, 1);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_FAST_GUN), 1);

    special_system_set(ctx, SPECIAL_FAST_GUN, 0);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_FAST_GUN), 0);

    special_system_destroy(ctx);
}

/* Test: toggle killer on and off */
static void test_toggle_killer(void **state)
{
    (void)state;
    special_system_t *ctx = special_system_create(NULL, NULL);

    special_system_set(ctx, SPECIAL_KILLER, 1);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_KILLER), 1);

    special_system_set(ctx, SPECIAL_KILLER, 0);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_KILLER), 0);

    special_system_destroy(ctx);
}

/* Test: toggle no_walls fires callback */
static void test_toggle_no_walls_with_callback(void **state)
{
    (void)state;
    reset_wall_tracking();
    special_system_callbacks_t cbs = {.on_wall_state_changed = wall_state_callback};
    special_system_t *ctx = special_system_create(&cbs, NULL);

    special_system_set(ctx, SPECIAL_NO_WALLS, 1);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_NO_WALLS), 1);
    assert_int_equal(g_wall_callback_count, 1);
    assert_int_equal(g_wall_last_no_walls, 1);

    special_system_set(ctx, SPECIAL_NO_WALLS, 0);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_NO_WALLS), 0);
    assert_int_equal(g_wall_callback_count, 2);
    assert_int_equal(g_wall_last_no_walls, 0);

    special_system_destroy(ctx);
}

/* Test: toggle x2 bonus on and off */
static void test_toggle_x2_bonus(void **state)
{
    (void)state;
    special_system_t *ctx = special_system_create(NULL, NULL);

    special_system_set(ctx, SPECIAL_X2_BONUS, 1);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_X2_BONUS), 1);

    special_system_set(ctx, SPECIAL_X2_BONUS, 0);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_X2_BONUS), 0);

    special_system_destroy(ctx);
}

/* Test: toggle x4 bonus on and off */
static void test_toggle_x4_bonus(void **state)
{
    (void)state;
    special_system_t *ctx = special_system_create(NULL, NULL);

    special_system_set(ctx, SPECIAL_X4_BONUS, 1);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_X4_BONUS), 1);

    special_system_set(ctx, SPECIAL_X4_BONUS, 0);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_X4_BONUS), 0);

    special_system_destroy(ctx);
}

/* =========================================================================
 * Group 3: Mutual exclusion (x2/x4)
 * ========================================================================= */

/* Test: activating x2 deactivates x4 */
static void test_x2_deactivates_x4(void **state)
{
    (void)state;
    special_system_t *ctx = special_system_create(NULL, NULL);

    special_system_set(ctx, SPECIAL_X4_BONUS, 1);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_X4_BONUS), 1);

    special_system_set(ctx, SPECIAL_X2_BONUS, 1);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_X2_BONUS), 1);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_X4_BONUS), 0);

    special_system_destroy(ctx);
}

/* Test: activating x4 deactivates x2 */
static void test_x4_deactivates_x2(void **state)
{
    (void)state;
    special_system_t *ctx = special_system_create(NULL, NULL);

    special_system_set(ctx, SPECIAL_X2_BONUS, 1);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_X2_BONUS), 1);

    special_system_set(ctx, SPECIAL_X4_BONUS, 1);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_X4_BONUS), 1);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_X2_BONUS), 0);

    special_system_destroy(ctx);
}

/* Test: setting SPECIAL_REVERSE is rejected (owned by paddle) */
static void test_reverse_rejected(void **state)
{
    (void)state;
    special_system_t *ctx = special_system_create(NULL, NULL);

    special_system_set(ctx, SPECIAL_REVERSE, 1);
    /* is_active always returns 0 for REVERSE */
    assert_int_equal(special_system_is_active(ctx, SPECIAL_REVERSE), 0);

    /* No internal state changed — verify via state snapshot */
    special_system_state_t snap;
    special_system_get_state(ctx, 0, &snap);
    assert_int_equal(snap.reverse_on, 0);

    special_system_destroy(ctx);
}

/* =========================================================================
 * Group 4: Turn off all
 * ========================================================================= */

/* Test: turn_off clears all except saving */
static void test_turn_off_preserves_saving(void **state)
{
    (void)state;
    special_system_t *ctx = special_system_create(NULL, NULL);

    /* Activate everything */
    special_system_set(ctx, SPECIAL_STICKY, 1);
    special_system_set(ctx, SPECIAL_SAVING, 1);
    special_system_set(ctx, SPECIAL_FAST_GUN, 1);
    special_system_set(ctx, SPECIAL_NO_WALLS, 1);
    special_system_set(ctx, SPECIAL_KILLER, 1);
    special_system_set(ctx, SPECIAL_X2_BONUS, 1);

    special_system_turn_off(ctx);

    assert_int_equal(special_system_is_active(ctx, SPECIAL_STICKY), 0);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_SAVING), 1); /* Preserved! */
    assert_int_equal(special_system_is_active(ctx, SPECIAL_FAST_GUN), 0);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_NO_WALLS), 0);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_KILLER), 0);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_X2_BONUS), 0);
    assert_int_equal(special_system_is_active(ctx, SPECIAL_X4_BONUS), 0);

    special_system_destroy(ctx);
}

/* Test: turn_off fires wall callback when walls were off */
static void test_turn_off_fires_wall_callback(void **state)
{
    (void)state;
    reset_wall_tracking();
    special_system_callbacks_t cbs = {.on_wall_state_changed = wall_state_callback};
    special_system_t *ctx = special_system_create(&cbs, NULL);

    special_system_set(ctx, SPECIAL_NO_WALLS, 1);
    reset_wall_tracking(); /* Clear the activation callback */

    special_system_turn_off(ctx);
    assert_int_equal(g_wall_callback_count, 1);
    assert_int_equal(g_wall_last_no_walls, 0);

    special_system_destroy(ctx);
}

/* Test: turn_off does NOT fire wall callback when walls were already on */
static void test_turn_off_no_wall_callback_when_walls_on(void **state)
{
    (void)state;
    reset_wall_tracking();
    special_system_callbacks_t cbs = {.on_wall_state_changed = wall_state_callback};
    special_system_t *ctx = special_system_create(&cbs, NULL);

    /* Walls are on (no_walls=0) — turn_off should not fire callback */
    special_system_set(ctx, SPECIAL_STICKY, 1);
    special_system_turn_off(ctx);
    assert_int_equal(g_wall_callback_count, 0);

    special_system_destroy(ctx);
}

/* =========================================================================
 * Group 5: State queries and labels
 * ========================================================================= */

/* Test: get_state populates all fields including injected reverse_on */
static void test_get_state_snapshot(void **state)
{
    (void)state;
    special_system_t *ctx = special_system_create(NULL, NULL);

    special_system_set(ctx, SPECIAL_STICKY, 1);
    special_system_set(ctx, SPECIAL_KILLER, 1);

    special_system_state_t snap;
    special_system_get_state(ctx, 1, &snap); /* reverse_on = 1 */

    assert_int_equal(snap.reverse_on, 1);
    assert_int_equal(snap.sticky_bat, 1);
    assert_int_equal(snap.saving, 0);
    assert_int_equal(snap.fast_gun, 0);
    assert_int_equal(snap.no_walls, 0);
    assert_int_equal(snap.killer, 1);
    assert_int_equal(snap.x2_bonus, 0);
    assert_int_equal(snap.x4_bonus, 0);

    special_system_destroy(ctx);
}

/* Test: get_state with NULL context returns zeroed state */
static void test_get_state_null_ctx(void **state)
{
    (void)state;
    special_system_state_t snap;
    memset(&snap, 0xFF, sizeof(snap)); /* Dirty fill */

    special_system_get_state(NULL, 1, &snap);

    assert_int_equal(snap.reverse_on, 0);
    assert_int_equal(snap.sticky_bat, 0);
    assert_int_equal(snap.saving, 0);
    assert_int_equal(snap.fast_gun, 0);
    assert_int_equal(snap.no_walls, 0);
    assert_int_equal(snap.killer, 0);
    assert_int_equal(snap.x2_bonus, 0);
    assert_int_equal(snap.x4_bonus, 0);
}

/* Test: get_labels returns correct layout */
static void test_get_labels_layout(void **state)
{
    (void)state;
    special_system_t *ctx = special_system_create(NULL, NULL);
    special_system_set(ctx, SPECIAL_KILLER, 1);

    special_label_info_t labels[SPECIAL_COUNT];
    special_system_get_labels(ctx, 0, labels);

    /* Row 0: Reverse, Save, NoWall, x2 */
    assert_string_equal(labels[0].label, "Reverse");
    assert_int_equal(labels[0].col_x, SPECIAL_COL0_X);
    assert_int_equal(labels[0].row, 0);
    assert_int_equal(labels[0].active, 0); /* reverse off */

    assert_string_equal(labels[1].label, "Save");
    assert_int_equal(labels[1].col_x, SPECIAL_COL1_X);
    assert_int_equal(labels[1].row, 0);
    assert_int_equal(labels[1].active, 0);

    assert_string_equal(labels[2].label, "NoWall");
    assert_int_equal(labels[2].col_x, SPECIAL_COL2_X);
    assert_int_equal(labels[2].row, 0);
    assert_int_equal(labels[2].active, 0);

    assert_string_equal(labels[3].label, "x2");
    assert_int_equal(labels[3].col_x, SPECIAL_COL3_X);
    assert_int_equal(labels[3].row, 0);
    assert_int_equal(labels[3].active, 0);

    /* Row 1: Sticky, FastGun, Killer, x4 */
    assert_string_equal(labels[4].label, "Sticky");
    assert_int_equal(labels[4].col_x, SPECIAL_COL0_X);
    assert_int_equal(labels[4].row, 1);
    assert_int_equal(labels[4].active, 0);

    assert_string_equal(labels[5].label, "FastGun");
    assert_int_equal(labels[5].col_x, SPECIAL_COL1_X);
    assert_int_equal(labels[5].row, 1);
    assert_int_equal(labels[5].active, 0);

    assert_string_equal(labels[6].label, "Killer");
    assert_int_equal(labels[6].col_x, SPECIAL_COL2_X);
    assert_int_equal(labels[6].row, 1);
    assert_int_equal(labels[6].active, 1); /* killer is on */

    assert_string_equal(labels[7].label, "x4");
    assert_int_equal(labels[7].col_x, SPECIAL_COL3_X);
    assert_int_equal(labels[7].row, 1);
    assert_int_equal(labels[7].active, 0);

    special_system_destroy(ctx);
}

/* Test: get_labels reflects reverse_on injection */
static void test_get_labels_reverse_injection(void **state)
{
    (void)state;
    special_system_t *ctx = special_system_create(NULL, NULL);

    special_label_info_t labels[SPECIAL_COUNT];

    /* reverse_on = 0 */
    special_system_get_labels(ctx, 0, labels);
    assert_int_equal(labels[0].active, 0);

    /* reverse_on = 1 */
    special_system_get_labels(ctx, 1, labels);
    assert_int_equal(labels[0].active, 1);

    special_system_destroy(ctx);
}

/* =========================================================================
 * Group 6: Attract-mode randomization
 * ========================================================================= */

/* Test: randomize sets all specials based on rand output */
static void test_randomize_all_on(void **state)
{
    (void)state;
    special_system_t *ctx = special_system_create(NULL, NULL);

    /* All rand() % 100 values > 50 → all active */
    g_rand_index = 0;
    for (int i = 0; i < 8; i++)
    {
        g_rand_sequence[i] = 75; /* 75 % 100 = 75, > 50 → active */
    }

    special_system_state_t snap = special_system_randomize(ctx, deterministic_rand);

    assert_int_equal(snap.sticky_bat, 1);
    assert_int_equal(snap.saving, 1);
    assert_int_equal(snap.fast_gun, 1);
    assert_int_equal(snap.no_walls, 1);
    assert_int_equal(snap.killer, 1);
    assert_int_equal(snap.x2_bonus, 1);
    assert_int_equal(snap.x4_bonus, 1);
    assert_int_equal(snap.reverse_on, 1);

    special_system_destroy(ctx);
}

/* Test: randomize sets all specials off when rand values <= 50 */
static void test_randomize_all_off(void **state)
{
    (void)state;
    special_system_t *ctx = special_system_create(NULL, NULL);

    /* All rand() % 100 values <= 50 → all inactive */
    g_rand_index = 0;
    for (int i = 0; i < 8; i++)
    {
        g_rand_sequence[i] = 30; /* 30 % 100 = 30, <= 50 → inactive */
    }

    special_system_state_t snap = special_system_randomize(ctx, deterministic_rand);

    assert_int_equal(snap.sticky_bat, 0);
    assert_int_equal(snap.saving, 0);
    assert_int_equal(snap.fast_gun, 0);
    assert_int_equal(snap.no_walls, 0);
    assert_int_equal(snap.killer, 0);
    assert_int_equal(snap.x2_bonus, 0);
    assert_int_equal(snap.x4_bonus, 0);
    assert_int_equal(snap.reverse_on, 0);

    special_system_destroy(ctx);
}

/* Test: randomize with NULL context returns zeroed state */
static void test_randomize_null_ctx(void **state)
{
    (void)state;
    special_system_state_t snap = special_system_randomize(NULL, deterministic_rand);

    assert_int_equal(snap.sticky_bat, 0);
    assert_int_equal(snap.saving, 0);
    assert_int_equal(snap.fast_gun, 0);
    assert_int_equal(snap.no_walls, 0);
    assert_int_equal(snap.killer, 0);
    assert_int_equal(snap.x2_bonus, 0);
    assert_int_equal(snap.x4_bonus, 0);
    assert_int_equal(snap.reverse_on, 0);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_destroy_no_callbacks),
        cmocka_unit_test(test_create_destroy_with_callbacks),
        cmocka_unit_test(test_initial_state_all_off),

        /* Group 2: Individual toggles */
        cmocka_unit_test(test_toggle_sticky),
        cmocka_unit_test(test_toggle_saving),
        cmocka_unit_test(test_toggle_fast_gun),
        cmocka_unit_test(test_toggle_killer),
        cmocka_unit_test(test_toggle_no_walls_with_callback),
        cmocka_unit_test(test_toggle_x2_bonus),
        cmocka_unit_test(test_toggle_x4_bonus),

        /* Group 3: Mutual exclusion */
        cmocka_unit_test(test_x2_deactivates_x4),
        cmocka_unit_test(test_x4_deactivates_x2),
        cmocka_unit_test(test_reverse_rejected),

        /* Group 4: Turn off all */
        cmocka_unit_test(test_turn_off_preserves_saving),
        cmocka_unit_test(test_turn_off_fires_wall_callback),
        cmocka_unit_test(test_turn_off_no_wall_callback_when_walls_on),

        /* Group 5: State queries and labels */
        cmocka_unit_test(test_get_state_snapshot),
        cmocka_unit_test(test_get_state_null_ctx),
        cmocka_unit_test(test_get_labels_layout),
        cmocka_unit_test(test_get_labels_reverse_injection),

        /* Group 6: Attract-mode randomization */
        cmocka_unit_test(test_randomize_all_on),
        cmocka_unit_test(test_randomize_all_off),
        cmocka_unit_test(test_randomize_null_ctx),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
