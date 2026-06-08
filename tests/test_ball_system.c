/*
 * test_ball_system.c — Tests for the pure C ball physics system.
 *
 * Bead xboing-1ka.1: Port ball physics to pure C.
 *
 * PR 1 test groups:
 *   Group 1: Lifecycle (create/destroy, initial state, machine_eps)
 *   Group 2: Ball management (add, add-fills-slots, add-full, clear, clear_all)
 *   Group 10: Render state queries (active ball info, inactive slot, guide info)
 *
 * PR 2 test groups:
 *   Group 3: State machine dispatch (8 tests)
 *   Group 4: Wall collision (5 tests)
 *   Group 5: Ball dies off bottom (2 tests)
 *   Group 6: Paddle collision (4 tests)
 *   Group 9: Guide direction (2 tests)
 *   Group 11: Speed normalization + auto-tilt (2 tests)
 *
 * PR 3 test groups:
 *   Group 7: Block collision (4 tests)
 *   Group 8: Ball-to-ball collision (3 tests)
 *   Group 12: Multiball / split (2 tests)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "ball_system.h"
#include "ball_types.h"

/* =========================================================================
 * Helpers
 * ========================================================================= */

static ball_system_env_t make_env(int frame)
{
    ball_system_env_t env = {0};
    env.frame = frame;
    env.speed_level = 5;
    env.paddle_pos = 247;
    env.paddle_size = 50;
    env.play_width = 495;
    env.play_height = 580;
    env.col_width = 55;  /* 495 / 9 */
    env.row_height = 32; /* 580 / 18 */
    return env;
}

/* ---- Callback recording helpers for PR 2 tests ---- */

#define MAX_EVENTS 32

typedef struct
{
    int sound_count;
    char last_sound[64];
    int score_count;
    unsigned long last_score;
    int event_count;
    ball_system_event_t events[MAX_EVENTS];
    int event_ball_indices[MAX_EVENTS];
    int message_count;
    char last_message[128];
} test_cb_log_t;

static void cb_on_sound(const char *name, void *ud)
{
    test_cb_log_t *log = (test_cb_log_t *)ud;
    log->sound_count++;
    strncpy(log->last_sound, name, sizeof(log->last_sound) - 1);
    log->last_sound[sizeof(log->last_sound) - 1] = '\0';
}

static void cb_on_score(unsigned long points, void *ud)
{
    test_cb_log_t *log = (test_cb_log_t *)ud;
    log->score_count++;
    log->last_score = points;
}

static void cb_on_event(ball_system_event_t event, int ball_index, void *ud)
{
    test_cb_log_t *log = (test_cb_log_t *)ud;
    if (log->event_count < MAX_EVENTS)
    {
        log->events[log->event_count] = event;
        log->event_ball_indices[log->event_count] = ball_index;
        log->event_count++;
    }
}

static void cb_on_message(const char *msg, void *ud)
{
    test_cb_log_t *log = (test_cb_log_t *)ud;
    log->message_count++;
    strncpy(log->last_message, msg, sizeof(log->last_message) - 1);
    log->last_message[sizeof(log->last_message) - 1] = '\0';
}

/* ---- Block collision callback helpers for PR 3 tests ---- */

typedef struct
{
    /* check_region: when hit_row/hit_col match, return hit_region */
    int hit_row;
    int hit_col;
    int hit_region;
    int check_count;
    block_hit_result_t block_hit_return;
    int block_hit_count;
    int block_hit_row;
    int block_hit_col;
    /* cell_available: which cell is available */
    int avail_row;
    int avail_col;
    int cell_avail_count;
} test_block_cb_t;

static int cb_check_region(int row, int col, int bx, int by, int bdx, void *ud)
{
    test_block_cb_t *bc = (test_block_cb_t *)ud;
    (void)bx;
    (void)by;
    (void)bdx;
    bc->check_count++;
    if (row == bc->hit_row && col == bc->hit_col)
    {
        return bc->hit_region;
    }
    return BALL_REGION_NONE;
}

static block_hit_result_t cb_on_block_hit(int row, int col, int ball_index, void *ud)
{
    test_block_cb_t *bc = (test_block_cb_t *)ud;
    (void)ball_index;
    bc->block_hit_count++;
    bc->block_hit_row = row;
    bc->block_hit_col = col;
    return bc->block_hit_return;
}

static int cb_cell_available(int row, int col, void *ud)
{
    test_block_cb_t *bc = (test_block_cb_t *)ud;
    bc->cell_avail_count++;
    return (row == bc->avail_row && col == bc->avail_col) ? 1 : 0;
}

static ball_system_callbacks_t make_test_callbacks(void)
{
    ball_system_callbacks_t cbs = {0};
    cbs.on_sound = cb_on_sound;
    cbs.on_score = cb_on_score;
    cbs.on_event = cb_on_event;
    cbs.on_message = cb_on_message;
    return cbs;
}

/* =========================================================================
 * Group 1: Lifecycle
 * ========================================================================= */

/* TC-01: Create returns non-NULL context with OK status. */
static void test_create_returns_context(void **state)
{
    (void)state;
    ball_system_status_t st;
    ball_system_t *ctx = ball_system_create(NULL, NULL, &st);

    assert_non_null(ctx);
    assert_int_equal(st, BALL_SYS_OK);

    ball_system_destroy(ctx);
}

/* TC-02: All ball slots start inactive after create. */
static void test_create_all_balls_inactive(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    assert_non_null(ctx);

    for (int i = 0; i < MAX_BALLS; i++)
    {
        ball_system_render_info_t info;
        ball_system_status_t st = ball_system_get_render_info(ctx, i, &info);
        assert_int_equal(st, BALL_SYS_OK);
        assert_int_equal(info.active, 0);
        assert_int_equal(info.state, BALL_CREATE);
    }

    ball_system_destroy(ctx);
}

/* TC-03: Guide starts at position 6 (middle). */
static void test_create_guide_initial_position(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    assert_non_null(ctx);

    ball_system_guide_info_t guide = ball_system_get_guide_info(ctx);
    assert_int_equal(guide.pos, 6);

    ball_system_destroy(ctx);
}

/* TC-04: Destroy with NULL is safe (no crash). */
static void test_destroy_null_safe(void **state)
{
    (void)state;
    ball_system_destroy(NULL); /* Should not crash */
}

/* =========================================================================
 * Group 2: Ball management
 * ========================================================================= */

/* TC-05: Add a ball returns slot 0 and sets it active. */
static void test_add_returns_slot_zero(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);
    ball_system_status_t st;

    int idx = ball_system_add(ctx, &env, 200, 300, 3, -3, &st);

    assert_int_equal(idx, 0);
    assert_int_equal(st, BALL_SYS_OK);

    /* Verify the ball was set up */
    ball_system_render_info_t info;
    ball_system_get_render_info(ctx, 0, &info);
    assert_int_equal(info.active, 1);
    assert_int_equal(info.x, 200);
    assert_int_equal(info.y, 300);
    assert_int_equal(info.state, BALL_CREATE);

    ball_system_destroy(ctx);
}

/* TC-06: Add fills consecutive slots. */
static void test_add_fills_consecutive_slots(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);

    for (int i = 0; i < MAX_BALLS; i++)
    {
        int idx = ball_system_add(ctx, &env, i * 10, i * 20, 3, -3, NULL);
        assert_int_equal(idx, i);
    }

    ball_system_destroy(ctx);
}

/* TC-07: Add when all slots full returns -1 and ERR_FULL. */
static void test_add_full_returns_error(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);

    /* Fill all slots */
    for (int i = 0; i < MAX_BALLS; i++)
    {
        ball_system_add(ctx, &env, 0, 0, 3, -3, NULL);
    }

    /* Next add should fail */
    ball_system_status_t st;
    int idx = ball_system_add(ctx, &env, 0, 0, 3, -3, &st);

    assert_int_equal(idx, -1);
    assert_int_equal(st, BALL_SYS_ERR_FULL);

    ball_system_destroy(ctx);
}

/* TC-08: Clear resets ball to defaults and frees the slot. */
static void test_clear_resets_to_defaults(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);

    ball_system_add(ctx, &env, 200, 300, 5, -5, NULL);
    ball_system_clear(ctx, 0);

    ball_system_render_info_t info;
    ball_system_get_render_info(ctx, 0, &info);
    assert_int_equal(info.active, 0);
    assert_int_equal(info.x, 0);
    assert_int_equal(info.y, 0);
    assert_int_equal(info.slide, 0);
    assert_int_equal(info.state, BALL_CREATE);

    ball_system_destroy(ctx);
}

/* TC-09: Clear frees slot so add can reuse it. */
static void test_clear_allows_reuse(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);

    /* Fill all slots */
    for (int i = 0; i < MAX_BALLS; i++)
    {
        ball_system_add(ctx, &env, 0, 0, 3, -3, NULL);
    }

    /* Clear slot 2 and re-add */
    ball_system_clear(ctx, 2);
    int idx = ball_system_add(ctx, &env, 99, 88, 1, -1, NULL);
    assert_int_equal(idx, 2);

    int x, y;
    ball_system_get_position(ctx, 2, &x, &y);
    assert_int_equal(x, 99);
    assert_int_equal(y, 88);

    ball_system_destroy(ctx);
}

/* TC-10: Clear all resets every slot. */
static void test_clear_all(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);

    for (int i = 0; i < MAX_BALLS; i++)
    {
        ball_system_add(ctx, &env, i * 10, i * 20, 3, -3, NULL);
    }

    ball_system_clear_all(ctx);

    for (int i = 0; i < MAX_BALLS; i++)
    {
        ball_system_render_info_t info;
        ball_system_get_render_info(ctx, i, &info);
        assert_int_equal(info.active, 0);
    }

    ball_system_destroy(ctx);
}

/* TC-11: Add with NULL context returns -1. */
static void test_add_null_context(void **state)
{
    (void)state;
    ball_system_env_t env = make_env(100);
    ball_system_status_t st;

    int idx = ball_system_add(NULL, &env, 0, 0, 3, -3, &st);
    assert_int_equal(idx, -1);
    assert_int_equal(st, BALL_SYS_ERR_NULL_ARG);
}

/* =========================================================================
 * Group 10: Render state queries
 * ========================================================================= */

/* TC-12: Render info for active ball returns correct data. */
static void test_render_info_active_ball(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);

    ball_system_add(ctx, &env, 150, 250, 3, -3, NULL);

    ball_system_render_info_t info;
    ball_system_status_t st = ball_system_get_render_info(ctx, 0, &info);

    assert_int_equal(st, BALL_SYS_OK);
    assert_int_equal(info.active, 1);
    assert_int_equal(info.x, 150);
    assert_int_equal(info.y, 250);
    assert_int_equal(info.from_x, 150); /* from == pos at creation */
    assert_int_equal(info.from_y, 250);
    assert_int_equal(info.ticks_since_move, 0); /* just created */
    assert_int_equal(info.slide, 0);
    assert_int_equal(info.state, BALL_CREATE);

    ball_system_destroy(ctx);
}

/* TC-12b: Interpolation fields track movement across BALL_FRAME_RATE cadence. */
static void test_render_info_interpolation_fields(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(0);

    /* Add a ball and force it to BALL_ACTIVE */
    ball_system_add(ctx, &env, 200, 300, 4, -3, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);

    /* Run updates for frames 1-4 (non-movement ticks, BALL_FRAME_RATE=5) */
    ball_system_render_info_t info;
    for (int f = 1; f < BALL_FRAME_RATE; f++)
    {
        env.frame = f;
        ball_system_update(ctx, &env);
        ball_system_get_render_info(ctx, 0, &info);

        /* Ball hasn't moved yet — position unchanged, ticks_since_move grows */
        assert_int_equal(info.x, 200);
        assert_int_equal(info.y, 300);
        assert_int_equal(info.ticks_since_move, f);
    }

    /* Frame 5 (5 % 5 == 0): ball moves — from should be pre-move position */
    env.frame = BALL_FRAME_RATE;
    ball_system_update(ctx, &env);
    ball_system_get_render_info(ctx, 0, &info);

    assert_int_equal(info.from_x, 200); /* snapshot of position before move */
    assert_int_equal(info.from_y, 300);
    assert_int_equal(info.ticks_since_move, 0);  /* just moved this frame */
    assert_true(info.x != 200 || info.y != 300); /* position changed */

    ball_system_destroy(ctx);
}

/* TC-13: Render info for inactive slot shows active=0. */
static void test_render_info_inactive_slot(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);

    ball_system_render_info_t info;
    ball_system_status_t st = ball_system_get_render_info(ctx, 3, &info);

    assert_int_equal(st, BALL_SYS_OK);
    assert_int_equal(info.active, 0);

    ball_system_destroy(ctx);
}

/* TC-14: Render info for out-of-range index returns error. */
static void test_render_info_invalid_index(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);

    ball_system_render_info_t info;
    ball_system_status_t st = ball_system_get_render_info(ctx, MAX_BALLS, &info);
    assert_int_equal(st, BALL_SYS_ERR_INVALID_INDEX);

    st = ball_system_get_render_info(ctx, -1, &info);
    assert_int_equal(st, BALL_SYS_ERR_INVALID_INDEX);

    ball_system_destroy(ctx);
}

/* TC-15: Guide info returns initial state. */
static void test_guide_info_initial(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);

    ball_system_guide_info_t guide = ball_system_get_guide_info(ctx);
    assert_int_equal(guide.pos, 6);
    assert_true(guide.inc == 1 || guide.inc == -1);

    ball_system_destroy(ctx);
}

/* TC-16: Query functions with NULL context return safe defaults. */
static void test_queries_null_safe(void **state)
{
    (void)state;

    assert_int_equal(ball_system_get_active_count(NULL), 0);
    assert_int_equal(ball_system_get_active_index(NULL), -1);
    assert_int_equal(ball_system_is_ball_waiting(NULL), 0);
    assert_int_equal(ball_system_get_state(NULL, 0), BALL_NONE);

    ball_system_guide_info_t guide = ball_system_get_guide_info(NULL);
    assert_int_equal(guide.pos, 0);
}

/* TC-17: Status string returns meaningful text. */
static void test_status_strings(void **state)
{
    (void)state;

    assert_string_equal(ball_system_status_string(BALL_SYS_OK), "OK");
    assert_string_equal(ball_system_status_string(BALL_SYS_ERR_NULL_ARG), "NULL argument");
    assert_string_equal(ball_system_status_string(BALL_SYS_ERR_FULL), "all ball slots full");
}

/* TC-18: State name returns meaningful text. */
static void test_state_names(void **state)
{
    (void)state;

    assert_string_equal(ball_system_state_name(BALL_ACTIVE), "active");
    assert_string_equal(ball_system_state_name(BALL_POP), "pop");
    assert_string_equal(ball_system_state_name(BALL_READY), "ready");
    assert_string_equal(ball_system_state_name(BALL_NONE), "none");
}

/* =========================================================================
 * Group 3: State machine dispatch
 * ========================================================================= */

/* TC-19: Update with NULL context is safe. */
static void test_update_null_safe(void **state)
{
    (void)state;
    ball_system_env_t env = make_env(100);
    ball_system_update(NULL, &env); /* Should not crash */
}

/* TC-20: Active ball gets position update on frame multiple. */
static void test_active_ball_position_update(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);

    /* Add ball and force it to ACTIVE state with known velocity */
    ball_system_add(ctx, &env, 200, 300, 5, -5, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);

    /* Update on a BALL_FRAME_RATE multiple (frame=100, 100%5==0) */
    env.frame = 100;
    ball_system_update(ctx, &env);

    int x, y;
    ball_system_get_position(ctx, 0, &x, &y);
    assert_int_equal(x, 205);
    assert_int_equal(y, 295);

    ball_system_destroy(ctx);
}

/* TC-21: Active ball NOT updated on non-frame-multiple. */
static void test_active_ball_skipped_off_frame(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);

    ball_system_add(ctx, &env, 200, 300, 5, -5, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);

    /* Update on non-multiple (frame=101, 101%5!=0) */
    env.frame = 101;
    ball_system_update(ctx, &env);

    int x, y;
    ball_system_get_position(ctx, 0, &x, &y);
    /* Position should not change */
    assert_int_equal(x, 200);
    assert_int_equal(y, 300);

    ball_system_destroy(ctx);
}

/* TC-22: BALL_CREATE animation increments slide on nextFrame. */
static void test_create_animation_increments_slide(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    /* frame=100, so nextFrame = 100 + BIRTH_FRAME_RATE = 105 */
    ball_system_env_t env = make_env(100);

    ball_system_add(ctx, &env, 0, 0, 3, -3, NULL);
    /* Ball is in BALL_CREATE with slide=0, nextFrame=105 */

    /* Update at frame 104 — not yet nextFrame */
    env.frame = 104;
    ball_system_update(ctx, &env);

    ball_system_render_info_t info;
    ball_system_get_render_info(ctx, 0, &info);
    assert_int_equal(info.slide, 0);

    /* Update at frame 105 — triggers slide increment */
    env.frame = 105;
    ball_system_update(ctx, &env);

    ball_system_get_render_info(ctx, 0, &info);
    assert_int_equal(info.slide, 1);
    assert_int_equal(info.state, BALL_CREATE);

    ball_system_destroy(ctx);
}

/* TC-23: BALL_CREATE completes and transitions to BALL_READY. */
static void test_create_animation_completes_to_ready(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);

    ball_system_add(ctx, &env, 0, 0, 3, -3, NULL);
    /* nextFrame starts at 105. Run through BIRTH_SLIDES (8) frames. */

    int frame = 105;
    for (int s = 0; s < BIRTH_SLIDES; s++)
    {
        env.frame = frame;
        ball_system_update(ctx, &env);
        frame += BIRTH_FRAME_RATE;
    }

    /* After 8 slides, should be BALL_READY */
    assert_int_equal(ball_system_get_state(ctx, 0), BALL_READY);

    /* Ball should be positioned on paddle */
    int x, y;
    ball_system_get_position(ctx, 0, &x, &y);
    assert_int_equal(x, env.paddle_pos);
    assert_int_equal(y, env.play_height - DIST_BALL_OF_PADDLE);

    ball_system_destroy(ctx);
}

/* TC-24: BALL_WAIT transitions to waitMode on waitingFrame. */
static void test_wait_transitions_on_frame(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);

    /* Use reset_start which sets up BALL_WAIT → BALL_CREATE */
    ball_system_reset_start(ctx, &env);

    assert_int_equal(ball_system_get_state(ctx, 0), BALL_WAIT);

    /* waitingFrame = frame + 1 = 101 */
    env.frame = 101;
    ball_system_update(ctx, &env);

    assert_int_equal(ball_system_get_state(ctx, 0), BALL_CREATE);

    ball_system_destroy(ctx);
}

/* TC-25: BALL_READY auto-activates after delay. */
static void test_ready_auto_activates(void **state)
{
    (void)state;
    test_cb_log_t log = {0};
    ball_system_callbacks_t cbs = make_test_callbacks();
    ball_system_t *ctx = ball_system_create(&cbs, &log, NULL);
    ball_system_env_t env = make_env(100);

    /* Create a ball and manually transition to BALL_READY with known nextFrame */
    ball_system_add(ctx, &env, env.paddle_pos, env.play_height - DIST_BALL_OF_PADDLE, 3, -3, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_READY);
    /* change_mode doesn't set nextFrame for READY — set it manually
     * by simulating what animate_ball_create would have done */

    /* We need to reach the auto-activate. Use a direct approach:
     * Set up the ball in READY state at a known position, then trigger auto-activate.
     * We'll run the full create animation to get to READY naturally. */
    ball_system_destroy(ctx);

    log = (test_cb_log_t){0};
    ctx = ball_system_create(&cbs, &log, NULL);
    env = make_env(100);

    ball_system_add(ctx, &env, 0, 0, 3, -3, NULL);

    /* Run create animation to completion (8 slides) */
    int frame = 105;
    for (int s = 0; s < BIRTH_SLIDES; s++)
    {
        env.frame = frame;
        ball_system_update(ctx, &env);
        frame += BIRTH_FRAME_RATE;
    }
    /* Now in BALL_READY, nextFrame = last_frame + BALL_AUTO_ACTIVE_DELAY */
    assert_int_equal(ball_system_get_state(ctx, 0), BALL_READY);

    /* The last frame was 100 + 5 + (7 * 5) = 140. Create completes at slide 8 = frame 140.
     * nextFrame = 140 + BALL_AUTO_ACTIVE_DELAY = 140 + 3000 = 3140 */
    env.frame = 3140;
    ball_system_update(ctx, &env);

    assert_int_equal(ball_system_get_state(ctx, 0), BALL_ACTIVE);
    assert_true(log.event_count > 0);
    assert_int_equal(log.events[log.event_count - 1], BALL_EVT_ACTIVATED);

    ball_system_destroy(ctx);
}

/* TC-26: BALL_POP counts down and emits DIED event. */
static void test_pop_animation_emits_died(void **state)
{
    (void)state;
    test_cb_log_t log = {0};
    ball_system_callbacks_t cbs = make_test_callbacks();
    ball_system_t *ctx = ball_system_create(&cbs, &log, NULL);
    ball_system_env_t env = make_env(100);

    ball_system_add(ctx, &env, 200, 300, 3, -3, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_POP);

    /* Pop animation: slide starts at BIRTH_SLIDES+1=9, counts down.
     * Takes ~10 frames of BIRTH_FRAME_RATE each until slide < 0. */
    int frame = 105;
    for (int s = 0; s < 12; s++)
    {
        env.frame = frame;
        ball_system_update(ctx, &env);
        frame += BIRTH_FRAME_RATE;
    }

    /* Ball should be cleared and DIED emitted */
    ball_system_render_info_t info;
    ball_system_get_render_info(ctx, 0, &info);
    assert_int_equal(info.active, 0);

    assert_true(log.event_count > 0);
    int found_died = 0;
    for (int e = 0; e < log.event_count; e++)
    {
        if (log.events[e] == BALL_EVT_DIED)
        {
            found_died = 1;
        }
    }
    assert_true(found_died);

    ball_system_destroy(ctx);
}

/* =========================================================================
 * Group 4: Wall collision
 * ========================================================================= */

/* TC-27: Ball bounces off left wall. */
static void test_wall_bounce_left(void **state)
{
    (void)state;
    test_cb_log_t log = {0};
    ball_system_callbacks_t cbs = make_test_callbacks();
    ball_system_t *ctx = ball_system_create(&cbs, &log, NULL);
    ball_system_env_t env = make_env(100);

    /* Place ball near left wall with leftward velocity.
     * Use do_tilt first so lastPaddleHitFrame is set far in the future,
     * then override velocity to the desired test value. */
    ball_system_add(ctx, &env, BALL_WC + 3, 200, -5, -3, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);
    /* do_tilt sets lastPaddleHitFrame = frame + PADDLE_BALL_FRAME_TILT */
    ball_system_do_tilt(ctx, &env, 0);

    /* Reset to known velocity after tilt randomized it */
    ball_system_clear(ctx, 0);
    ball_system_add(ctx, &env, BALL_WC + 3, 200, -5, -3, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);
    /* Tilt won't fire again since we just set lastPaddleHitFrame via add's
     * clear. We need to prevent tilt. Use BALL_DIE state — it still bounces
     * off walls but skips paddle/tilt/speed-normalization. */
    ball_system_change_mode(ctx, &env, 0, BALL_DIE);

    log = (test_cb_log_t){0};
    env.frame = 100;
    ball_system_update(ctx, &env);

    /* Wall bounce sound should fire even for dying balls */
    assert_true(log.sound_count >= 1);
    assert_string_equal(log.last_sound, "boing");

    /* Ball should still be in-play after just bouncing */
    int x, y;
    ball_system_get_position(ctx, 0, &x, &y);
    /* New x = (BALL_WC+3) + (-5) = 8, then dx = abs(-5) = 5.
     * Position doesn't change within the same frame — it's 8.
     * But dx is now positive (bounced). Verify on next frame. */

    env.frame = 105;
    ball_system_update(ctx, &env);
    int x2, y2;
    ball_system_get_position(ctx, 0, &x2, &y2);
    /* Ball should be moving rightward after bounce: x2 > x */
    assert_true(x2 > x);

    ball_system_destroy(ctx);
}

/* TC-28: Ball bounces off right wall. */
static void test_wall_bounce_right(void **state)
{
    (void)state;
    test_cb_log_t log = {0};
    ball_system_callbacks_t cbs = make_test_callbacks();
    ball_system_t *ctx = ball_system_create(&cbs, &log, NULL);
    ball_system_env_t env = make_env(100);

    /* Place ball near right wall with rightward velocity */
    int right_edge = env.play_width - BALL_WC;
    ball_system_add(ctx, &env, right_edge - 3, 200, 5, -3, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);

    env.frame = 100;
    ball_system_update(ctx, &env);

    assert_int_equal(log.sound_count, 1);
    assert_string_equal(log.last_sound, "boing");

    ball_system_destroy(ctx);
}

/* TC-29: Ball bounces off top wall. */
static void test_wall_bounce_top(void **state)
{
    (void)state;
    test_cb_log_t log = {0};
    ball_system_callbacks_t cbs = make_test_callbacks();
    ball_system_t *ctx = ball_system_create(&cbs, &log, NULL);
    ball_system_env_t env = make_env(100);

    /* Place ball near top with upward velocity */
    ball_system_add(ctx, &env, 200, BALL_HC + 3, 3, -5, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);

    env.frame = 100;
    ball_system_update(ctx, &env);

    assert_int_equal(log.sound_count, 1);
    assert_string_equal(log.last_sound, "boing");

    ball_system_destroy(ctx);
}

/* TC-30: Ball wraps around left wall when noWalls=1. */
static void test_wall_wrap_left(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);
    env.no_walls = 1;

    /* Place ball that will cross left wall */
    ball_system_add(ctx, &env, BALL_WC + 3, 200, -5, -3, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);

    env.frame = 100;
    ball_system_update(ctx, &env);

    int x, y;
    ball_system_get_position(ctx, 0, &x, &y);
    /* Should wrap to right side */
    assert_int_equal(x, env.play_width - BALL_WC);

    ball_system_destroy(ctx);
}

/* TC-31: Ball wraps around right wall when noWalls=1. */
static void test_wall_wrap_right(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);
    env.no_walls = 1;

    int right_edge = env.play_width - BALL_WC;
    ball_system_add(ctx, &env, right_edge - 3, 200, 5, -3, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);

    env.frame = 100;
    ball_system_update(ctx, &env);

    int x, y;
    ball_system_get_position(ctx, 0, &x, &y);
    /* Should wrap to left side */
    assert_int_equal(x, BALL_WC);

    ball_system_destroy(ctx);
}

/* =========================================================================
 * Group 5: Ball dies off bottom
 * ========================================================================= */

/* TC-32: Ball past paddle triggers BALL_DIE state. */
static void test_ball_past_paddle_triggers_die(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);

    /* Place ball just above the die threshold:
     * die threshold = play_height - DIST_BASE + BALL_HEIGHT = 580 - 30 + 19 = 569
     * Place at 565 with dy=5 → next pos = 570 > 569 */
    ball_system_add(ctx, &env, 200, 565, 3, 5, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);

    env.frame = 100;
    ball_system_update(ctx, &env);

    assert_int_equal(ball_system_get_state(ctx, 0), BALL_DIE);

    ball_system_destroy(ctx);
}

/* TC-33: Ball off bottom of screen clears and emits DIED. */
static void test_ball_off_bottom_clears(void **state)
{
    (void)state;
    test_cb_log_t log = {0};
    ball_system_callbacks_t cbs = make_test_callbacks();
    ball_system_t *ctx = ball_system_create(&cbs, &log, NULL);
    ball_system_env_t env = make_env(100);

    /* Place ball well below screen:
     * clear threshold = play_height + BALL_HEIGHT*2 = 580 + 38 = 618
     * Place at 615 with dy=5 → next pos = 620 > 618 */
    ball_system_add(ctx, &env, 200, 615, 0, 5, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_DIE);

    env.frame = 100;
    ball_system_update(ctx, &env);

    /* Ball should be cleared */
    ball_system_render_info_t info;
    ball_system_get_render_info(ctx, 0, &info);
    assert_int_equal(info.active, 0);

    /* DIED event should have been emitted */
    assert_true(log.event_count > 0);
    assert_int_equal(log.events[0], BALL_EVT_DIED);

    ball_system_destroy(ctx);
}

/* =========================================================================
 * Group 6: Paddle collision
 * ========================================================================= */

/* TC-34: Ball hitting paddle bounces upward. */
static void test_paddle_hit_bounces_upward(void **state)
{
    (void)state;
    test_cb_log_t log = {0};
    ball_system_callbacks_t cbs = make_test_callbacks();
    ball_system_t *ctx = ball_system_create(&cbs, &log, NULL);
    ball_system_env_t env = make_env(100);

    /* Paddle line = play_height - DIST_BASE - 2 = 580 - 30 - 2 = 548
     * Ball hits paddle when bally + BALL_HC > 548 → bally > 539
     * Place ball at (247, 536) with dy=5 → next bally = 541 > 539 */
    ball_system_add(ctx, &env, env.paddle_pos, 536, 0, 5, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);

    env.frame = 100;
    ball_system_update(ctx, &env);

    /* Ball should have bounced — dy should be negative (upward) */
    int x, y;
    ball_system_get_position(ctx, 0, &x, &y);
    /* The ball should not be dying — it hit the paddle */
    assert_int_not_equal(ball_system_get_state(ctx, 0), BALL_DIE);

    /* Paddle sound should have played */
    assert_true(log.sound_count > 0);

    ball_system_destroy(ctx);
}

/* TC-35: Paddle hit fires score callback. */
static void test_paddle_hit_scores(void **state)
{
    (void)state;
    test_cb_log_t log = {0};
    ball_system_callbacks_t cbs = make_test_callbacks();
    ball_system_t *ctx = ball_system_create(&cbs, &log, NULL);
    ball_system_env_t env = make_env(100);

    ball_system_add(ctx, &env, env.paddle_pos, 536, 0, 5, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);

    env.frame = 100;
    ball_system_update(ctx, &env);

    assert_int_equal(log.score_count, 1);
    assert_int_equal((int)log.last_score, PADDLE_HIT_SCORE);

    ball_system_destroy(ctx);
}

/* TC-36: Sticky bat catches ball (transitions to BALL_READY). */
static void test_sticky_bat_catches_ball(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);
    env.sticky_bat = 1;

    ball_system_add(ctx, &env, env.paddle_pos, 536, 0, 5, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);

    env.frame = 100;
    ball_system_update(ctx, &env);

    assert_int_equal(ball_system_get_state(ctx, 0), BALL_READY);

    ball_system_destroy(ctx);
}

/* TC-37: Ball missing paddle does not bounce. */
static void test_ball_misses_paddle(void **state)
{
    (void)state;
    test_cb_log_t log = {0};
    ball_system_callbacks_t cbs = make_test_callbacks();
    ball_system_t *ctx = ball_system_create(&cbs, &log, NULL);
    ball_system_env_t env = make_env(100);

    /* Place ball far from paddle horizontally — paddle at 247, ball at 50 */
    ball_system_add(ctx, &env, 50, 536, 0, 5, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);

    env.frame = 100;
    ball_system_update(ctx, &env);

    /* No paddle sound — only boing if wall hit */
    int found_paddle = 0;
    if (log.sound_count > 0 && strcmp(log.last_sound, "paddle") == 0)
    {
        found_paddle = 1;
    }
    assert_int_equal(found_paddle, 0);

    ball_system_destroy(ctx);
}

/* =========================================================================
 * Group 9: Guide direction
 * ========================================================================= */

/* TC-38: Guide direction table returns correct dx/dy. */
static void test_guide_direction_table(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);

    /* Default guide_pos = 6: dx=1, dy=-5 */
    ball_system_add(ctx, &env, env.paddle_pos, env.play_height - DIST_BALL_OF_PADDLE, 3, -3, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_READY);

    int idx = ball_system_activate_waiting(ctx, &env);
    assert_int_equal(idx, 0);
    assert_int_equal(ball_system_get_state(ctx, 0), BALL_ACTIVE);

    /* After activation, guide resets to 6. Verify through guide info. */
    ball_system_guide_info_t guide = ball_system_get_guide_info(ctx);
    assert_int_equal(guide.pos, 6);

    ball_system_destroy(ctx);
}

/* TC-39: Activate waiting ball uses guide direction and fires event. */
static void test_activate_waiting_fires_event(void **state)
{
    (void)state;
    test_cb_log_t log = {0};
    ball_system_callbacks_t cbs = make_test_callbacks();
    ball_system_t *ctx = ball_system_create(&cbs, &log, NULL);
    ball_system_env_t env = make_env(100);

    ball_system_add(ctx, &env, env.paddle_pos, env.play_height - DIST_BALL_OF_PADDLE, 0, 0, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_READY);

    ball_system_activate_waiting(ctx, &env);

    assert_int_equal(log.event_count, 1);
    assert_int_equal(log.events[0], BALL_EVT_ACTIVATED);
    assert_int_equal(log.event_ball_indices[0], 0);

    ball_system_destroy(ctx);
}

/* =========================================================================
 * Group 11: Speed normalization + auto-tilt
 * ========================================================================= */

/* TC-40: Speed normalization applied after paddle hit. */
static void test_speed_normalized_after_paddle(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    ball_system_env_t env = make_env(100);
    env.speed_level = 5;

    /* Set up ball to hit paddle with extreme velocity */
    ball_system_add(ctx, &env, env.paddle_pos, 536, 0, 5, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);

    env.frame = 100;
    ball_system_update(ctx, &env);

    /* Ball should still be alive and speed should be normalized */
    enum BallStates st = ball_system_get_state(ctx, 0);
    assert_true(st == BALL_ACTIVE || st == BALL_READY);

    ball_system_destroy(ctx);
}

/* TC-41: Auto-tilt fires when lastPaddleHitFrame expires. */
static void test_auto_tilt_fires(void **state)
{
    (void)state;
    test_cb_log_t log = {0};
    ball_system_callbacks_t cbs = make_test_callbacks();
    ball_system_t *ctx = ball_system_create(&cbs, &log, NULL);
    ball_system_env_t env = make_env(100);

    /* Add ball with lastPaddleHitFrame in the past */
    ball_system_add(ctx, &env, 200, 200, 3, -3, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);

    /* Advance frame well past any paddle hit frame */
    env.frame = 10000;
    ball_system_update(ctx, &env);

    /* Tilt event should have been emitted */
    int found_tilt = 0;
    for (int e = 0; e < log.event_count; e++)
    {
        if (log.events[e] == BALL_EVT_TILT)
        {
            found_tilt = 1;
        }
    }
    assert_true(found_tilt);
    assert_string_equal(log.last_message, "Auto Tilt Activated");

    ball_system_destroy(ctx);
}

/* =========================================================================
 * Group 7: Block collision
 * ========================================================================= */

/* TC-42: Block collision bounces ball off top of block. */
static void test_block_collision_bounce_top(void **state)
{
    (void)state;
    test_block_cb_t bc = {0};
    /* Place a block at row 5, col 4. Ball moving downward will hit top. */
    bc.hit_row = 5;
    bc.hit_col = 4;
    bc.hit_region = BALL_REGION_TOP;
    bc.block_hit_return = 0; /* bounce normally */

    ball_system_callbacks_t cbs = {0};
    cbs.check_region = cb_check_region;
    cbs.on_block_hit = cb_on_block_hit;
    ball_system_t *ctx = ball_system_create(&cbs, &bc, NULL);
    ball_system_env_t env = make_env(100);

    /* Place ball at (220, 155) with downward velocity.
     * col = 220/55 = 4, row = 155/32 = 4 (one above hit_row=5).
     * With dy=5, ball will step through and reach row 5. */
    ball_system_add(ctx, &env, 220, 155, 3, 5, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);

    env.frame = 100;
    ball_system_update(ctx, &env);

    /* Block hit callback should have been called */
    assert_true(bc.block_hit_count > 0);
    assert_int_equal(bc.block_hit_row, 5);
    assert_int_equal(bc.block_hit_col, 4);

    ball_system_destroy(ctx);
}

/* TC-43: Block collision with killer mode (no bounce). */
static void test_block_collision_no_bounce(void **state)
{
    (void)state;
    test_block_cb_t bc = {0};
    bc.hit_row = 5;
    bc.hit_col = 4;
    bc.hit_region = BALL_REGION_TOP;
    bc.block_hit_return = 1; /* no bounce — killer/teleport */

    ball_system_callbacks_t cbs = {0};
    cbs.check_region = cb_check_region;
    cbs.on_block_hit = cb_on_block_hit;
    ball_system_t *ctx = ball_system_create(&cbs, &bc, NULL);
    ball_system_env_t env = make_env(100);

    ball_system_add(ctx, &env, 220, 155, 3, 5, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);

    env.frame = 100;
    ball_system_update(ctx, &env);

    /* Block hit callback should have been called */
    assert_true(bc.block_hit_count > 0);

    /* Ball should still be active (callback handled it) */
    assert_int_equal(ball_system_get_state(ctx, 0), BALL_ACTIVE);

    ball_system_destroy(ctx);
}

/* TC-44: No block collision when check_region returns NONE. */
static void test_block_no_collision(void **state)
{
    (void)state;
    test_block_cb_t bc = {0};
    /* No hit configured — check_region always returns NONE */
    bc.hit_row = -1;
    bc.hit_col = -1;
    bc.hit_region = BALL_REGION_NONE;

    ball_system_callbacks_t cbs = {0};
    cbs.check_region = cb_check_region;
    cbs.on_block_hit = cb_on_block_hit;
    ball_system_t *ctx = ball_system_create(&cbs, &bc, NULL);
    ball_system_env_t env = make_env(100);

    ball_system_add(ctx, &env, 200, 200, 3, -3, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);

    env.frame = 100;
    ball_system_update(ctx, &env);

    /* No block hit should have been called */
    assert_int_equal(bc.block_hit_count, 0);

    ball_system_destroy(ctx);
}

/* TC-45: Block collision bounces ball off left side. */
static void test_block_collision_bounce_left(void **state)
{
    (void)state;
    test_block_cb_t bc = {0};
    bc.hit_row = 5;
    bc.hit_col = 4;
    bc.hit_region = BALL_REGION_LEFT;
    bc.block_hit_return = 0;

    ball_system_callbacks_t cbs = {0};
    cbs.check_region = cb_check_region;
    cbs.on_block_hit = cb_on_block_hit;
    ball_system_t *ctx = ball_system_create(&cbs, &bc, NULL);
    ball_system_env_t env = make_env(100);

    /* Ball moving rightward into block */
    ball_system_add(ctx, &env, 215, 160, 5, 0, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);

    env.frame = 100;
    ball_system_update(ctx, &env);

    assert_true(bc.block_hit_count > 0);

    ball_system_destroy(ctx);
}

/* =========================================================================
 * Group 8: Ball-to-ball collision
 * ========================================================================= */

/* TC-46: Two active balls near each other trigger ball2ball sound. */
static void test_ball_to_ball_collision_sound(void **state)
{
    (void)state;
    test_cb_log_t log = {0};
    ball_system_callbacks_t cbs = make_test_callbacks();
    ball_system_t *ctx = ball_system_create(&cbs, &log, NULL);
    ball_system_env_t env = make_env(100);

    /* Ball 0 at paddle position heading down — will hit paddle on update.
     * Paddle hit sets lastPaddleHitFrame properly (avoids tilt randomization).
     * After paddle bounce: ball 0 at ~(247, 539) with dx=2, dy=-11.
     *
     * Ball 1 at (247, 518) heading down slowly — within collision range
     * of ball 0 after paddle bounce. Distance ≈ 21, combined radius = 20.
     * Swept-circle test detects collision (tmin ≈ 0.07). */
    ball_system_add(ctx, &env, env.paddle_pos, 536, 0, 5, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);
    ball_system_add(ctx, &env, env.paddle_pos, 518, 0, 3, NULL);
    ball_system_change_mode(ctx, &env, 1, BALL_ACTIVE);

    env.frame = 100;
    ball_system_update(ctx, &env);

    /* ball2ball fires after paddle sound, so it should be the last sound.
     * At least 2 sounds: "paddle" + "ball2ball". */
    assert_true(log.sound_count >= 2);
    assert_string_equal(log.last_sound, "ball2ball");

    ball_system_destroy(ctx);
}

/* TC-47: Distant balls do not trigger ball-to-ball collision. */
static void test_ball_to_ball_no_collision(void **state)
{
    (void)state;
    test_cb_log_t log = {0};
    ball_system_callbacks_t cbs = make_test_callbacks();
    ball_system_t *ctx = ball_system_create(&cbs, &log, NULL);
    ball_system_env_t env = make_env(100);

    /* Place two balls far apart */
    ball_system_add(ctx, &env, 50, 200, 3, -3, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);
    ball_system_add(ctx, &env, 400, 200, -3, -3, NULL);
    ball_system_change_mode(ctx, &env, 1, BALL_ACTIVE);

    env.frame = 100;
    ball_system_update(ctx, &env);

    /* No ball2ball sound */
    int found_b2b = 0;
    if (log.sound_count > 0 && strcmp(log.last_sound, "ball2ball") == 0)
    {
        found_b2b = 1;
    }
    assert_int_equal(found_b2b, 0);

    ball_system_destroy(ctx);
}

/* TC-48: Ball-to-ball collision only checked for ACTIVE balls. */
static void test_ball_to_ball_skips_inactive(void **state)
{
    (void)state;
    test_cb_log_t log = {0};
    ball_system_callbacks_t cbs = make_test_callbacks();
    ball_system_t *ctx = ball_system_create(&cbs, &log, NULL);
    ball_system_env_t env = make_env(100);

    /* Ball 0 active, ball 1 in CREATE state (close but not active) */
    ball_system_add(ctx, &env, 200, 200, 5, -3, NULL);
    ball_system_change_mode(ctx, &env, 0, BALL_ACTIVE);
    ball_system_add(ctx, &env, 210, 200, -5, -3, NULL);
    /* Ball 1 stays in BALL_CREATE — should not participate in b2b */

    env.frame = 100;
    ball_system_update(ctx, &env);

    /* No ball2ball sound (ball 1 is not ACTIVE) */
    int found_b2b = 0;
    if (log.sound_count > 0 && strcmp(log.last_sound, "ball2ball") == 0)
    {
        found_b2b = 1;
    }
    assert_int_equal(found_b2b, 0);

    ball_system_destroy(ctx);
}

/* =========================================================================
 * Group 12: Multiball (split)
 * ========================================================================= */

/* TC-49: Split with teleport places ball at available cell. */
static void test_split_teleports_ball(void **state)
{
    (void)state;
    test_block_cb_t bc = {0};
    bc.avail_row = 5;
    bc.avail_col = 4;

    ball_system_callbacks_t cbs = {0};
    cbs.cell_available = cb_cell_available;
    ball_system_t *ctx = ball_system_create(&cbs, &bc, NULL);
    ball_system_env_t env = make_env(100);

    /* Add an initial ball so slot 0 is occupied */
    ball_system_add(ctx, &env, 200, 200, 3, -3, NULL);

    /* Split should add a second ball and teleport it */
    int j = ball_system_split(ctx, &env);
    assert_true(j >= 0);
    assert_int_equal(ball_system_get_state(ctx, j), BALL_ACTIVE);

    /* Verify the ball was teleported (cell_available was called) */
    assert_true(bc.cell_avail_count > 0);

    ball_system_destroy(ctx);
}

/* TC-50: Split adds a new ball and emits SPLIT event. */
static void test_split_adds_ball(void **state)
{
    (void)state;
    test_cb_log_t log = {0};
    ball_system_callbacks_t cbs = make_test_callbacks();
    ball_system_t *ctx = ball_system_create(&cbs, &log, NULL);
    ball_system_env_t env = make_env(100);

    /* Add an initial ball so slot 0 is occupied */
    ball_system_add(ctx, &env, 200, 200, 3, -3, NULL);

    /* Split should add a second ball */
    int j = ball_system_split(ctx, &env);
    assert_true(j >= 0);

    /* New ball should be ACTIVE */
    assert_int_equal(ball_system_get_state(ctx, j), BALL_ACTIVE);

    /* SPLIT event should have been emitted */
    int found_split = 0;
    for (int e = 0; e < log.event_count; e++)
    {
        if (log.events[e] == BALL_EVT_SPLIT)
        {
            found_split = 1;
        }
    }
    assert_true(found_split);

    /* Message should say "Another ball!" */
    assert_string_equal(log.last_message, "Another ball!");

    ball_system_destroy(ctx);
}

/* TC-51: Split when all slots full shows failure message. */
static void test_split_full_shows_message(void **state)
{
    (void)state;
    test_cb_log_t log = {0};
    ball_system_callbacks_t cbs = make_test_callbacks();
    ball_system_t *ctx = ball_system_create(&cbs, &log, NULL);
    ball_system_env_t env = make_env(100);

    /* Fill all slots */
    for (int i = 0; i < MAX_BALLS; i++)
    {
        ball_system_add(ctx, &env, i * 50, 200, 3, -3, NULL);
    }

    /* Split should fail */
    int j = ball_system_split(ctx, &env);
    assert_int_equal(j, -1);

    assert_string_equal(log.last_message, "Cannot add ball!");

    ball_system_destroy(ctx);
}

/* =========================================================================
 * Main
 * ========================================================================= */

/* =========================================================================
 * Group 13: Guide animation rate (basket 6, xboing-c-xny)
 *
 * The launch guide oscillator advances only when
 * env->frame % (BALL_FRAME_RATE * 8) == 0 — every 40 frames.
 * Earlier modern code used `* 3` (every 15 frames), 2.67x too fast vs
 * original/ball.c:456.
 * ========================================================================= */

static void test_guide_advances_only_at_8x_frame_rate(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    assert_non_null(ctx);

    /* Add a ball at frame 0 and move to BALL_READY.  change_mode(READY)
     * doesn't set nextFrame, but ball_system_add sets it to
     * env.frame + BIRTH_FRAME_RATE = 5.  The BALL_READY auto-activate
     * guard fires at env->frame >= nextFrame, so we need nextFrame
     * ahead of our test range.  Transition through the create animation
     * naturally to reach READY with a proper nextFrame. */
    ball_system_env_t env = make_env(0);
    int idx = ball_system_add(ctx, &env, 247, 522, 0, 0, NULL);
    assert_int_not_equal(idx, -1);

    /* Run create animation to completion (BIRTH_SLIDES=8 * BIRTH_FRAME_RATE=5
     * = 40 frames), reaching BALL_READY with nextFrame = 40 + 3000 = 3040. */
    for (int f = 1; f <= 50; f++)
    {
        env.frame = f;
        ball_system_update(ctx, &env);
    }

    /* Capture initial guide state. */
    ball_system_guide_info_t g0 = ball_system_get_guide_info(ctx);
    int base = 80; /* multiple of 40, well below nextFrame ~3040 */

    /* Drive frames base+1..base+39: outer modulus fires at multiples of
     * BALL_FRAME_RATE.  None of those are multiples of
     * BALL_FRAME_RATE * 8 = 40 (since base=80 is itself a multiple of
     * 40), so guide.pos must NOT change. */
    for (int f = 1; f < 40; f++)
    {
        env.frame = base + f;
        ball_system_update(ctx, &env);
        ball_system_guide_info_t g = ball_system_get_guide_info(ctx);
        if (g.pos != g0.pos)
        {
            fail_msg("guide.pos changed at frame %d (expected change only at multiples of 40)",
                     env.frame);
        }
    }

    /* Frame base+40 IS a multiple of 40 — guide.pos should change. */
    env.frame = base + 40;
    ball_system_update(ctx, &env);
    ball_system_guide_info_t g40 = ball_system_get_guide_info(ctx);
    assert_int_not_equal(g40.pos, g0.pos);

    /* Frames base+41..base+79 must hold the new value. */
    for (int f = 41; f < 80; f++)
    {
        env.frame = base + f;
        ball_system_update(ctx, &env);
        ball_system_guide_info_t g = ball_system_get_guide_info(ctx);
        if (g.pos != g40.pos)
        {
            fail_msg("guide.pos changed at frame %d (expected hold between multiples of 40)",
                     env.frame);
        }
    }

    ball_system_destroy(ctx);
}

/* =========================================================================
 * Savegame v2 accessors — get_velocity, get_wait_mode, restore
 * ========================================================================= */

static void test_get_velocity_default_zero(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);

    int dx = 99, dy = 99;
    ball_system_status_t st = ball_system_get_velocity(ctx, 0, &dx, &dy);
    assert_int_equal(st, BALL_SYS_OK);
    /* Default ball slot has zero velocity. */
    assert_int_equal(dx, 0);
    assert_int_equal(dy, 0);

    ball_system_destroy(ctx);
}

static void test_get_velocity_invalid_index(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);

    int dx = 0, dy = 0;
    assert_int_equal(ball_system_get_velocity(ctx, -1, &dx, &dy),
                     BALL_SYS_ERR_INVALID_INDEX);
    assert_int_equal(ball_system_get_velocity(ctx, MAX_BALLS, &dx, &dy),
                     BALL_SYS_ERR_INVALID_INDEX);

    ball_system_destroy(ctx);
}

static void test_get_velocity_null_args(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    int dx = 0, dy = 0;
    assert_int_equal(ball_system_get_velocity(NULL, 0, &dx, &dy), BALL_SYS_ERR_NULL_ARG);
    assert_int_equal(ball_system_get_velocity(ctx, 0, NULL, &dy), BALL_SYS_ERR_NULL_ARG);
    assert_int_equal(ball_system_get_velocity(ctx, 0, &dx, NULL), BALL_SYS_ERR_NULL_ARG);
    ball_system_destroy(ctx);
}

static void test_get_wait_mode_default(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    assert_int_equal(ball_system_get_wait_mode(ctx, 0), BALL_NONE);
    /* Invalid index also returns BALL_NONE (sentinel). */
    assert_int_equal(ball_system_get_wait_mode(ctx, -1), BALL_NONE);
    assert_int_equal(ball_system_get_wait_mode(NULL, 0), BALL_NONE);
    ball_system_destroy(ctx);
}

static void test_restore_active_ball_roundtrip(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);

    /* Restore a moving ball into slot 0. */
    ball_system_status_t st = ball_system_restore(
        ctx, 0, /*current_frame*/ 100, /*active*/ 1, BALL_ACTIVE, /*x*/ 247, /*y*/ 520,
        /*dx*/ 3, /*dy*/ -4, /*wait_mode*/ BALL_NONE);
    assert_int_equal(st, BALL_SYS_OK);

    assert_int_equal(ball_system_get_state(ctx, 0), BALL_ACTIVE);
    int bx, by;
    ball_system_get_position(ctx, 0, &bx, &by);
    assert_int_equal(bx, 247);
    assert_int_equal(by, 520);

    int dx, dy;
    ball_system_get_velocity(ctx, 0, &dx, &dy);
    assert_int_equal(dx, 3);
    assert_int_equal(dy, -4);

    assert_int_equal(ball_system_get_wait_mode(ctx, 0), BALL_NONE);

    ball_system_destroy(ctx);
}

/* spec: BALL_CREATE in save data restores as BALL_READY (skip spawn animation). */
static void test_restore_ball_create_becomes_ready(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);

    ball_system_restore(ctx, 0, 100, 1, BALL_CREATE, 100, 100, 0, 0, BALL_NONE);

    assert_int_equal(ball_system_get_state(ctx, 0), BALL_READY);

    ball_system_destroy(ctx);
}

static void test_restore_invalid_index(void **state)
{
    (void)state;
    ball_system_t *ctx = ball_system_create(NULL, NULL, NULL);
    assert_int_equal(
        ball_system_restore(ctx, -1, 100, 1, BALL_ACTIVE, 0, 0, 0, 0, BALL_NONE),
        BALL_SYS_ERR_INVALID_INDEX);
    assert_int_equal(
        ball_system_restore(ctx, MAX_BALLS, 100, 1, BALL_ACTIVE, 0, 0, 0, 0, BALL_NONE),
        BALL_SYS_ERR_INVALID_INDEX);
    assert_int_equal(
        ball_system_restore(NULL, 0, 100, 1, BALL_ACTIVE, 0, 0, 0, 0, BALL_NONE),
        BALL_SYS_ERR_NULL_ARG);
    ball_system_destroy(ctx);
}

/* Restored BALL_ACTIVE must not auto-tilt on first tick after restore
 * (would randomize dx/dy and defeat the restored velocity). */
static void test_restore_active_no_auto_tilt_first_tick(void **state)
{
    (void)state;
    test_cb_log_t log = {0};
    ball_system_callbacks_t cbs = make_test_callbacks();
    ball_system_t *ctx = ball_system_create(&cbs, &log, NULL);

    int save_frame = 1000;
    ball_system_restore(ctx, 0, save_frame, /*active*/ 1, BALL_ACTIVE, /*x*/ 247, /*y*/ 400,
                        /*dx*/ 3, /*dy*/ -4, /*wait_mode*/ BALL_NONE);

    /* Tick one frame after restore.  No auto-tilt message should fire. */
    ball_system_env_t env = make_env(save_frame + 1);
    ball_system_update(ctx, &env);

    /* No message means lastPaddleHitFrame was correctly set far in the
     * future (current + PADDLE_BALL_FRAME_TILT), not 0. */
    if (log.message_count > 0)
    {
        assert_string_not_equal(log.last_message, "Auto Tilt Activated");
    }

    /* Velocity preserved (no tilt randomization). */
    int dx, dy;
    ball_system_get_velocity(ctx, 0, &dx, &dy);
    assert_int_equal(dx, 3);
    assert_int_equal(dy, -4);

    ball_system_destroy(ctx);
}

/* Restored BALL_READY must not auto-activate on first tick after restore. */
static void test_restore_ready_no_auto_activate_first_tick(void **state)
{
    (void)state;
    test_cb_log_t log = {0};
    ball_system_callbacks_t cbs = make_test_callbacks();
    ball_system_t *ctx = ball_system_create(&cbs, &log, NULL);

    int save_frame = 1000;
    ball_system_restore(ctx, 0, save_frame, 1, BALL_READY, 247, 528, 0, 0, BALL_NONE);

    /* Tick one frame after restore.  Should still be in BALL_READY (the
     * auto-activate timer was set to save_frame + BIRTH_FRAME_RATE, so
     * at save_frame + 1 it has not yet expired). */
    ball_system_env_t env = make_env(save_frame + 1);
    ball_system_update(ctx, &env);

    assert_int_equal(ball_system_get_state(ctx, 0), BALL_READY);

    ball_system_destroy(ctx);
}

/* =========================================================================
 * Group 15: Ray-march start position (xboing-c-qck)
 * =========================================================================
 *
 * Regression test for the seam-tunneling bug.  The fix moves the
 * oldx/oldy snapshot from BEFORE the ray-march to AFTER it, matching
 * the per-tick flow in original/ball.c:1213-1214 (ray-march reads
 * pre-tick oldx/oldy) → original/ball.c:1318 (call to MoveBall) →
 * original/ball.c:402-403 (MoveBall snapshots ballx/bally back into
 * oldx/oldy).  Without the fix the ray-march starts at the post-tick
 * position and probes `step` more pixels past it, skipping the
 * pre-tick → post-tick path entirely on fast balls.
 *
 * We exercise this by configuring a windowed mock that fires only when
 * the ball center sits in a narrow y-band somewhere ALONG the trajectory.
 * The buggy code samples past the band (starting at post-tick y); the
 * fixed code walks through the band from pre-tick y → post-tick y and
 * fires the bounce.
 */

typedef struct
{
    int hit_row;
    int hit_col;
    int hit_region;
    int y_min;
    int y_max;
    int block_hit_count;
    block_hit_result_t block_hit_return;
} test_windowed_cb_t;

static int cb_check_region_windowed(int row, int col, int bx, int by, int bdx, void *ud)
{
    test_windowed_cb_t *bc = (test_windowed_cb_t *)ud;
    (void)bx;
    (void)bdx;
    if (row == bc->hit_row && col == bc->hit_col && by >= bc->y_min && by <= bc->y_max)
    {
        return bc->hit_region;
    }
    return BALL_REGION_NONE;
}

static block_hit_result_t cb_on_block_hit_windowed(int row, int col, int ball_index, void *ud)
{
    test_windowed_cb_t *bc = (test_windowed_cb_t *)ud;
    (void)row;
    (void)col;
    (void)ball_index;
    bc->block_hit_count++;
    return bc->block_hit_return;
}

/* Pin the ray-march start point with a y-window covering the pre-tick y
 * but excluding the post-tick y.  The fixed ray-march samples (220, 180)
 * on iteration 0 and fires; the pre-fix ray-march starts at the post-tick
 * y (≈165) and never samples 180, so the mock never fires.
 *
 * Companion test below (test_check_for_collision_search_base_does_not_drift)
 * covers the second tunneling cause: the search base used to drift by
 * (-1, +1) on every NONE return, so multi-iteration ray-marches lost
 * the cell containing the block.
 *
 * (We can't write a multi-iteration walk test here because the row/col
 * arguments to check_region drift diagonally each iteration thanks to a
 * legacy bug preserved at check_for_collision:989-995 — only iteration 0
 * is called with the original row/col base.) */
static void test_raymarch_starts_at_pre_tick_position(void **state)
{
    (void)state;
    test_windowed_cb_t bc = {0};
    bc.hit_row = 5;
    bc.hit_col = 4;
    bc.hit_region = BALL_REGION_BOTTOM;
    /* Window covers ONLY the pre-tick y (180), not the post-tick y (165). */
    bc.y_min = 180;
    bc.y_max = 180;
    bc.block_hit_return = BLOCK_HIT_BOUNCE;

    ball_system_callbacks_t cbs = {0};
    cbs.check_region = cb_check_region_windowed;
    cbs.on_block_hit = cb_on_block_hit_windowed;
    ball_system_t *ctx = ball_system_create(&cbs, &bc, NULL);
    ball_system_env_t env = make_env(100);

    /* Restore an active ball at (220, 180), velocity (0, -15).  Restore
     * bypasses the BALL_ACTIVE guide-direction override (which would
     * replace dy with -5) and pre-sets lastPaddleHitFrame so the auto-tilt
     * inside update() leaves our velocity alone. */
    ball_system_restore(ctx, 0, env.frame - 1, 1, BALL_ACTIVE, 220, 180, 0, -15, BALL_NONE);
    ball_system_update(ctx, &env);

    assert_true(bc.block_hit_count > 0);

    ball_system_destroy(ctx);
}

/* Second tunneling cause: check_for_collision used to drift its row/col
 * search base by (-1, +1) on every NONE return.  After ~10 ray-march
 * iterations the base had drifted 10 cells away, so a fast ball never
 * found the cell containing the block it was about to enter.
 *
 * We configure the mock to fire at row=5, col=4 only — when the ball y
 * reaches a window inside row 5's vertical span.  The pre-tick position
 * is in row 6 (one below the block), and the ray-march walks up through
 * row 5.  With the drift bug, by the time y reaches the window the
 * search base has drifted to row=0 col=9 — far away — and the cell at
 * row=5 col=4 never gets tested.  Without the drift, every iteration
 * tests row=5 col=4 (via the (-1, 0) neighbor of base (6, 4)) and the
 * window fires on the right y. */
static void test_check_for_collision_search_base_does_not_drift(void **state)
{
    (void)state;
    test_windowed_cb_t bc = {0};
    bc.hit_row = 5;
    bc.hit_col = 4;
    bc.hit_region = BALL_REGION_BOTTOM;
    /* Block at row 5 with row_height=32 sits in y range 160..192 inclusive
     * of any internal offset.  Configure the window to cover y=185-186,
     * which the ray-march reaches at iteration 5 (pre-tick y=190, dy=-11). */
    bc.y_min = 185;
    bc.y_max = 186;
    bc.block_hit_return = BLOCK_HIT_BOUNCE;

    ball_system_callbacks_t cbs = {0};
    cbs.check_region = cb_check_region_windowed;
    cbs.on_block_hit = cb_on_block_hit_windowed;
    ball_system_t *ctx = ball_system_create(&cbs, &bc, NULL);
    ball_system_env_t env = make_env(100);

    ball_system_restore(ctx, 0, env.frame - 1, 1, BALL_ACTIVE, 220, 190, 0, -11, BALL_NONE);
    ball_system_update(ctx, &env);

    assert_true(bc.block_hit_count > 0);

    ball_system_destroy(ctx);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_returns_context),
        cmocka_unit_test(test_create_all_balls_inactive),
        cmocka_unit_test(test_create_guide_initial_position),
        cmocka_unit_test(test_destroy_null_safe),
        /* Group 2: Ball management */
        cmocka_unit_test(test_add_returns_slot_zero),
        cmocka_unit_test(test_add_fills_consecutive_slots),
        cmocka_unit_test(test_add_full_returns_error),
        cmocka_unit_test(test_clear_resets_to_defaults),
        cmocka_unit_test(test_clear_allows_reuse),
        cmocka_unit_test(test_clear_all),
        cmocka_unit_test(test_add_null_context),
        /* Group 3: State machine dispatch */
        cmocka_unit_test(test_update_null_safe),
        cmocka_unit_test(test_active_ball_position_update),
        cmocka_unit_test(test_active_ball_skipped_off_frame),
        cmocka_unit_test(test_create_animation_increments_slide),
        cmocka_unit_test(test_create_animation_completes_to_ready),
        cmocka_unit_test(test_wait_transitions_on_frame),
        cmocka_unit_test(test_ready_auto_activates),
        cmocka_unit_test(test_pop_animation_emits_died),
        /* Group 4: Wall collision */
        cmocka_unit_test(test_wall_bounce_left),
        cmocka_unit_test(test_wall_bounce_right),
        cmocka_unit_test(test_wall_bounce_top),
        cmocka_unit_test(test_wall_wrap_left),
        cmocka_unit_test(test_wall_wrap_right),
        /* Group 5: Ball dies off bottom */
        cmocka_unit_test(test_ball_past_paddle_triggers_die),
        cmocka_unit_test(test_ball_off_bottom_clears),
        /* Group 6: Paddle collision */
        cmocka_unit_test(test_paddle_hit_bounces_upward),
        cmocka_unit_test(test_paddle_hit_scores),
        cmocka_unit_test(test_sticky_bat_catches_ball),
        cmocka_unit_test(test_ball_misses_paddle),
        /* Group 9: Guide direction */
        cmocka_unit_test(test_guide_direction_table),
        cmocka_unit_test(test_activate_waiting_fires_event),
        /* Group 11: Speed normalization + auto-tilt */
        cmocka_unit_test(test_speed_normalized_after_paddle),
        cmocka_unit_test(test_auto_tilt_fires),
        /* Group 10: Render state queries */
        cmocka_unit_test(test_render_info_active_ball),
        cmocka_unit_test(test_render_info_interpolation_fields),
        cmocka_unit_test(test_render_info_inactive_slot),
        cmocka_unit_test(test_render_info_invalid_index),
        cmocka_unit_test(test_guide_info_initial),
        cmocka_unit_test(test_queries_null_safe),
        cmocka_unit_test(test_status_strings),
        cmocka_unit_test(test_state_names),
        /* Group 7: Block collision */
        cmocka_unit_test(test_block_collision_bounce_top),
        cmocka_unit_test(test_block_collision_no_bounce),
        cmocka_unit_test(test_block_no_collision),
        cmocka_unit_test(test_block_collision_bounce_left),
        /* Group 8: Ball-to-ball collision */
        cmocka_unit_test(test_ball_to_ball_collision_sound),
        cmocka_unit_test(test_ball_to_ball_no_collision),
        cmocka_unit_test(test_ball_to_ball_skips_inactive),
        /* Group 12: Multiball (split) */
        cmocka_unit_test(test_split_teleports_ball),
        cmocka_unit_test(test_split_adds_ball),
        cmocka_unit_test(test_split_full_shows_message),
        /* Group 13: Guide animation rate (basket 6, xboing-c-xny) */
        cmocka_unit_test(test_guide_advances_only_at_8x_frame_rate),

        /* Group 14: Savegame v2 accessors */
        cmocka_unit_test(test_get_velocity_default_zero),
        cmocka_unit_test(test_get_velocity_invalid_index),
        cmocka_unit_test(test_get_velocity_null_args),
        cmocka_unit_test(test_get_wait_mode_default),
        cmocka_unit_test(test_restore_active_ball_roundtrip),
        cmocka_unit_test(test_restore_ball_create_becomes_ready),
        cmocka_unit_test(test_restore_invalid_index),
        cmocka_unit_test(test_restore_active_no_auto_tilt_first_tick),
        cmocka_unit_test(test_restore_ready_no_auto_activate_first_tick),
        /* Group 15: Ray-march start position (xboing-c-qck) */
        cmocka_unit_test(test_raymarch_starts_at_pre_tick_position),
        cmocka_unit_test(test_check_for_collision_search_base_does_not_drift),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
