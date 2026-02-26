/*
 * test_eyedude_system.c — Tests for the pure C EyeDude animated character.
 *
 * 6 groups:
 *   1. Lifecycle (3 tests)
 *   2. Reset and path check (3 tests)
 *   3. Walk animation (4 tests)
 *   4. Turn at midpoint (2 tests)
 *   5. Collision and death (3 tests)
 *   6. Null safety (1 test)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

/* cmocka must come after setjmp.h / stdarg.h / stddef.h */
#include <cmocka.h>

#include "eyedude_system.h"

/* =========================================================================
 * Test helpers
 * ========================================================================= */

static int g_path_clear;
static unsigned long g_score_added;
static int g_sound_count;
static char g_last_sound[64];
static int g_message_count;

static void reset_tracking(void)
{
    g_path_clear = 1;
    g_score_added = 0;
    g_sound_count = 0;
    g_last_sound[0] = '\0';
    g_message_count = 0;
}

static int on_path_clear(void *ud)
{
    (void)ud;
    return g_path_clear;
}

static void on_score(unsigned long pts, void *ud)
{
    (void)ud;
    g_score_added += pts;
}

static void on_sound(const char *name, void *ud)
{
    (void)ud;
    g_sound_count++;
    if (name)
    {
        strncpy(g_last_sound, name, sizeof(g_last_sound) - 1);
        g_last_sound[sizeof(g_last_sound) - 1] = '\0';
    }
}

static void on_message(const char *msg, void *ud)
{
    (void)ud;
    (void)msg;
    g_message_count++;
}

static eyedude_system_callbacks_t make_callbacks(void)
{
    eyedude_system_callbacks_t cbs;
    cbs.is_path_clear = on_path_clear;
    cbs.on_score = on_score;
    cbs.on_sound = on_sound;
    cbs.on_message = on_message;
    return cbs;
}

/* Deterministic random sequence */
static int g_rand_seq[16];
static int g_rand_idx;

static int test_rand(void)
{
    return g_rand_seq[g_rand_idx++ % 16];
}

static void set_rand_values(int v0, int v1)
{
    g_rand_seq[0] = v0; /* turn chance */
    g_rand_seq[1] = v1; /* direction */
    g_rand_idx = 0;
}

/* =========================================================================
 * Group 1: Lifecycle
 * ========================================================================= */

static void test_create_destroy(void **state)
{
    (void)state;
    eyedude_system_t *ctx = eyedude_system_create(NULL, NULL, NULL);
    assert_non_null(ctx);
    eyedude_system_destroy(ctx);
}

static void test_create_with_callbacks(void **state)
{
    (void)state;
    eyedude_system_callbacks_t cbs = make_callbacks();
    eyedude_system_t *ctx = eyedude_system_create(&cbs, NULL, test_rand);
    assert_non_null(ctx);
    eyedude_system_destroy(ctx);
}

static void test_initial_state(void **state)
{
    (void)state;
    eyedude_system_t *ctx = eyedude_system_create(NULL, NULL, NULL);
    assert_int_equal(eyedude_system_get_state(ctx), EYEDUDE_STATE_NONE);
    eyedude_system_destroy(ctx);
}

/* =========================================================================
 * Group 2: Reset and path check
 * ========================================================================= */

static void test_reset_path_clear_walks(void **state)
{
    (void)state;
    reset_tracking();
    g_path_clear = 1;
    set_rand_values(50, 1); /* 50 > 30 → no turn, 1%2=1 → walk left */
    eyedude_system_callbacks_t cbs = make_callbacks();
    eyedude_system_t *ctx = eyedude_system_create(&cbs, NULL, test_rand);

    eyedude_system_set_state(ctx, EYEDUDE_STATE_RESET);
    eyedude_system_update(ctx, 0, 495);

    assert_int_equal(eyedude_system_get_state(ctx), EYEDUDE_STATE_WALK);
    assert_int_equal(g_sound_count, 1);
    assert_string_equal(g_last_sound, "hithere");

    eyedude_system_destroy(ctx);
}

static void test_reset_path_blocked(void **state)
{
    (void)state;
    reset_tracking();
    g_path_clear = 0;
    eyedude_system_callbacks_t cbs = make_callbacks();
    eyedude_system_t *ctx = eyedude_system_create(&cbs, NULL, test_rand);

    eyedude_system_set_state(ctx, EYEDUDE_STATE_RESET);
    eyedude_system_update(ctx, 0, 495);

    /* Should abort to NONE when path is blocked */
    assert_int_equal(eyedude_system_get_state(ctx), EYEDUDE_STATE_NONE);
    assert_int_equal(g_sound_count, 0);

    eyedude_system_destroy(ctx);
}

static void test_reset_direction_right(void **state)
{
    (void)state;
    reset_tracking();
    g_path_clear = 1;
    set_rand_values(50, 0); /* 0%2=0 → walk right */
    eyedude_system_callbacks_t cbs = make_callbacks();
    eyedude_system_t *ctx = eyedude_system_create(&cbs, NULL, test_rand);

    eyedude_system_set_state(ctx, EYEDUDE_STATE_RESET);
    eyedude_system_update(ctx, 0, 495);

    eyedude_render_info_t info = eyedude_system_get_render_info(ctx);
    assert_int_equal(info.dir, EYEDUDE_DIR_RIGHT);
    assert_int_equal(info.x, -EYEDUDE_WC); /* Starts off-screen left */

    eyedude_system_destroy(ctx);
}

/* =========================================================================
 * Group 3: Walk animation
 * ========================================================================= */

static void test_walk_left_moves(void **state)
{
    (void)state;
    reset_tracking();
    g_path_clear = 1;
    set_rand_values(50, 1); /* no turn, walk left */
    eyedude_system_callbacks_t cbs = make_callbacks();
    eyedude_system_t *ctx = eyedude_system_create(&cbs, NULL, test_rand);

    eyedude_system_set_state(ctx, EYEDUDE_STATE_RESET);
    eyedude_system_update(ctx, 0, 495);

    /* Start position: x = 495 + 16 = 511 */
    eyedude_render_info_t info = eyedude_system_get_render_info(ctx);
    int start_x = info.x;
    assert_int_equal(start_x, 495 + EYEDUDE_WC);

    /* Walk one step at frame 30 */
    eyedude_system_update(ctx, EYEDUDE_FRAME_RATE, 495);
    info = eyedude_system_get_render_info(ctx);
    assert_int_equal(info.x, start_x - EYEDUDE_WALK_SPEED);
    assert_int_equal(info.frame_index, 1); /* Animation advanced */

    eyedude_system_destroy(ctx);
}

static void test_walk_right_moves(void **state)
{
    (void)state;
    reset_tracking();
    g_path_clear = 1;
    set_rand_values(50, 0); /* no turn, walk right */
    eyedude_system_callbacks_t cbs = make_callbacks();
    eyedude_system_t *ctx = eyedude_system_create(&cbs, NULL, test_rand);

    eyedude_system_set_state(ctx, EYEDUDE_STATE_RESET);
    eyedude_system_update(ctx, 0, 495);

    /* Start position: x = -16 */
    eyedude_render_info_t info = eyedude_system_get_render_info(ctx);
    int start_x = info.x;
    assert_int_equal(start_x, -EYEDUDE_WC);

    /* Walk one step */
    eyedude_system_update(ctx, EYEDUDE_FRAME_RATE, 495);
    info = eyedude_system_get_render_info(ctx);
    assert_int_equal(info.x, start_x + EYEDUDE_WALK_SPEED);

    eyedude_system_destroy(ctx);
}

static void test_walk_frame_rate_throttle(void **state)
{
    (void)state;
    reset_tracking();
    g_path_clear = 1;
    set_rand_values(50, 1); /* walk left */
    eyedude_system_callbacks_t cbs = make_callbacks();
    eyedude_system_t *ctx = eyedude_system_create(&cbs, NULL, test_rand);

    eyedude_system_set_state(ctx, EYEDUDE_STATE_RESET);
    eyedude_system_update(ctx, 0, 495);

    int start_x = eyedude_system_get_render_info(ctx).x;

    /* Frame 1: not a FRAME_RATE interval → should NOT move */
    eyedude_system_update(ctx, 1, 495);
    assert_int_equal(eyedude_system_get_render_info(ctx).x, start_x);

    eyedude_system_destroy(ctx);
}

static void test_walk_exits_screen(void **state)
{
    (void)state;
    reset_tracking();
    g_path_clear = 1;
    set_rand_values(50, 1); /* no turn, walk left */
    eyedude_system_callbacks_t cbs = make_callbacks();
    eyedude_system_t *ctx = eyedude_system_create(&cbs, NULL, test_rand);

    eyedude_system_set_state(ctx, EYEDUDE_STATE_RESET);
    eyedude_system_update(ctx, 0, 495);

    /* Walk until the dude exits the screen */
    int frame = 0;
    int safety = 0;
    while (eyedude_system_get_state(ctx) == EYEDUDE_STATE_WALK && safety < 1000)
    {
        frame += EYEDUDE_FRAME_RATE;
        eyedude_system_update(ctx, frame, 495);
        safety++;
    }

    /* Should have gone to NONE after exiting */
    assert_int_equal(eyedude_system_get_state(ctx), EYEDUDE_STATE_NONE);

    eyedude_system_destroy(ctx);
}

/* =========================================================================
 * Group 4: Turn at midpoint
 * ========================================================================= */

static void test_turn_at_midpoint(void **state)
{
    (void)state;
    reset_tracking();
    g_path_clear = 1;
    set_rand_values(10, 1); /* 10 < 30 → WILL turn, walk left */
    eyedude_system_callbacks_t cbs = make_callbacks();
    eyedude_system_t *ctx = eyedude_system_create(&cbs, NULL, test_rand);

    eyedude_system_set_state(ctx, EYEDUDE_STATE_RESET);
    eyedude_system_update(ctx, 0, 495);

    /* Walk left until reaching midpoint (x <= 247) */
    int frame = 0;
    int turned = 0;
    for (int i = 0; i < 200; i++)
    {
        frame += EYEDUDE_FRAME_RATE;
        eyedude_system_update(ctx, frame, 495);
        eyedude_render_info_t info = eyedude_system_get_render_info(ctx);
        if (info.dir == EYEDUDE_DIR_RIGHT)
        {
            turned = 1;
            break;
        }
    }

    assert_int_equal(turned, 1);

    eyedude_system_destroy(ctx);
}

static void test_no_turn_when_chance_high(void **state)
{
    (void)state;
    reset_tracking();
    g_path_clear = 1;
    set_rand_values(50, 1); /* 50 >= 30 → NO turn, walk left */
    eyedude_system_callbacks_t cbs = make_callbacks();
    eyedude_system_t *ctx = eyedude_system_create(&cbs, NULL, test_rand);

    eyedude_system_set_state(ctx, EYEDUDE_STATE_RESET);
    eyedude_system_update(ctx, 0, 495);

    /* Walk left past midpoint — should NOT turn */
    int frame = 0;
    int turned = 0;
    for (int i = 0; i < 200; i++)
    {
        frame += EYEDUDE_FRAME_RATE;
        eyedude_system_update(ctx, frame, 495);
        eyedude_render_info_t info = eyedude_system_get_render_info(ctx);
        if (info.dir == EYEDUDE_DIR_RIGHT)
        {
            turned = 1;
            break;
        }
        if (eyedude_system_get_state(ctx) != EYEDUDE_STATE_WALK)
        {
            break;
        }
    }

    assert_int_equal(turned, 0);

    eyedude_system_destroy(ctx);
}

/* =========================================================================
 * Group 5: Collision and death
 * ========================================================================= */

static void test_collision_detection(void **state)
{
    (void)state;
    reset_tracking();
    g_path_clear = 1;
    set_rand_values(50, 1); /* walk left */
    eyedude_system_callbacks_t cbs = make_callbacks();
    eyedude_system_t *ctx = eyedude_system_create(&cbs, NULL, test_rand);

    eyedude_system_set_state(ctx, EYEDUDE_STATE_RESET);
    eyedude_system_update(ctx, 0, 495);

    /* EyeDude starts at (511, 16) */
    /* Ball at exact same position should collide */
    assert_int_equal(eyedude_system_check_collision(ctx, 511, 16, 8, 8), 1);

    /* Ball far away should not collide */
    assert_int_equal(eyedude_system_check_collision(ctx, 100, 100, 8, 8), 0);

    eyedude_system_destroy(ctx);
}

static void test_die_awards_bonus(void **state)
{
    (void)state;
    reset_tracking();
    g_path_clear = 1;
    set_rand_values(50, 1);
    eyedude_system_callbacks_t cbs = make_callbacks();
    eyedude_system_t *ctx = eyedude_system_create(&cbs, NULL, test_rand);

    eyedude_system_set_state(ctx, EYEDUDE_STATE_RESET);
    eyedude_system_update(ctx, 0, 495);

    /* Trigger death */
    eyedude_system_set_state(ctx, EYEDUDE_STATE_DIE);
    eyedude_system_update(ctx, 1, 495);

    assert_int_equal(eyedude_system_get_state(ctx), EYEDUDE_STATE_NONE);
    assert_int_equal(g_score_added, EYEDUDE_HIT_BONUS);
    assert_int_equal(g_message_count, 1);
    assert_string_equal(g_last_sound, "supbons");

    eyedude_system_destroy(ctx);
}

static void test_no_collision_when_inactive(void **state)
{
    (void)state;
    eyedude_system_t *ctx = eyedude_system_create(NULL, NULL, NULL);

    /* Not walking → no collision */
    assert_int_equal(eyedude_system_check_collision(ctx, 0, 0, 8, 8), 0);

    eyedude_system_destroy(ctx);
}

/* =========================================================================
 * Group 6: Null safety
 * ========================================================================= */

static void test_null_safety(void **state)
{
    (void)state;
    eyedude_system_destroy(NULL);
    eyedude_system_set_state(NULL, EYEDUDE_STATE_WALK);
    eyedude_system_update(NULL, 0, 495);

    assert_int_equal(eyedude_system_get_state(NULL), EYEDUDE_STATE_NONE);
    assert_int_equal(eyedude_system_check_collision(NULL, 0, 0, 8, 8), 0);

    int x = -1, y = -1;
    eyedude_system_get_position(NULL, &x, &y);
    assert_int_equal(x, 0);
    assert_int_equal(y, 0);

    eyedude_render_info_t info = eyedude_system_get_render_info(NULL);
    assert_int_equal(info.visible, 0);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_create_with_callbacks),
        cmocka_unit_test(test_initial_state),

        /* Group 2: Reset and path check */
        cmocka_unit_test(test_reset_path_clear_walks),
        cmocka_unit_test(test_reset_path_blocked),
        cmocka_unit_test(test_reset_direction_right),

        /* Group 3: Walk animation */
        cmocka_unit_test(test_walk_left_moves),
        cmocka_unit_test(test_walk_right_moves),
        cmocka_unit_test(test_walk_frame_rate_throttle),
        cmocka_unit_test(test_walk_exits_screen),

        /* Group 4: Turn at midpoint */
        cmocka_unit_test(test_turn_at_midpoint),
        cmocka_unit_test(test_no_turn_when_chance_high),

        /* Group 5: Collision and death */
        cmocka_unit_test(test_collision_detection),
        cmocka_unit_test(test_die_awards_bonus),
        cmocka_unit_test(test_no_collision_when_inactive),

        /* Group 6: Null safety */
        cmocka_unit_test(test_null_safety),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
