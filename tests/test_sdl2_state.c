/*
 * test_sdl2_state.c — CMocka tests for SDL2 state machine module.
 *
 * Pure logic tests — no SDL2 or video/audio driver needed.
 * Tests verify mode transitions, enter/exit hooks, dialogue push/pop,
 * frame counter behavior, and error handling.
 *
 * Bead: xboing-cks.2
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "sdl2_state.h"

/* =========================================================================
 * Callback tracking — records handler invocations for verification
 * ========================================================================= */

#define MAX_CALLS 64

typedef struct
{
    sdl2_state_mode_t mode;
    int type; /* 0=enter, 1=update, 2=exit */
} call_record_t;

typedef struct
{
    call_record_t calls[MAX_CALLS];
    int count;
} call_log_t;

static void log_enter(sdl2_state_mode_t mode, void *user_data)
{
    call_log_t *log = (call_log_t *)user_data;
    if (log->count < MAX_CALLS)
    {
        log->calls[log->count].mode = mode;
        log->calls[log->count].type = 0;
        log->count++;
    }
}

static void log_update(sdl2_state_mode_t mode, void *user_data)
{
    call_log_t *log = (call_log_t *)user_data;
    if (log->count < MAX_CALLS)
    {
        log->calls[log->count].mode = mode;
        log->calls[log->count].type = 1;
        log->count++;
    }
}

static void log_exit(sdl2_state_mode_t mode, void *user_data)
{
    call_log_t *log = (call_log_t *)user_data;
    if (log->count < MAX_CALLS)
    {
        log->calls[log->count].mode = mode;
        log->calls[log->count].type = 2;
        log->count++;
    }
}

static const sdl2_state_mode_def_t logging_def = {
    .on_enter = log_enter,
    .on_update = log_update,
    .on_exit = log_exit,
};

/* =========================================================================
 * Per-test fixtures
 * ========================================================================= */

typedef struct
{
    sdl2_state_t *ctx;
    call_log_t log;
} test_state_t;

static int setup_state(void **state)
{
    test_state_t *ts = calloc(1, sizeof(*ts));
    if (ts == NULL)
    {
        return -1;
    }

    sdl2_state_status_t st;
    ts->ctx = sdl2_state_create(&ts->log, &st);
    if (ts->ctx == NULL)
    {
        fprintf(stderr, "sdl2_state_create failed: %s\n", sdl2_state_status_string(st));
        free(ts);
        return -1;
    }

    *state = ts;
    return 0;
}

static int teardown_state(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_destroy(ts->ctx);
    free(ts);
    *state = NULL;
    return 0;
}

/* =========================================================================
 * Group 1: Lifecycle and initial state
 * ========================================================================= */

static void test_create_destroy(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    assert_non_null(ts->ctx);
}

static void test_initial_mode_is_none(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    assert_int_equal(sdl2_state_current(ts->ctx), SDL2ST_NONE);
    assert_int_equal(sdl2_state_previous(ts->ctx), SDL2ST_NONE);
}

static void test_initial_frame_is_zero(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    assert_int_equal(sdl2_state_frame(ts->ctx), 0);
}

static void test_create_status_ok(void **state)
{
    (void)state;
    sdl2_state_status_t st;
    sdl2_state_t *ctx = sdl2_state_create(NULL, &st);
    assert_int_equal(st, SDL2ST_OK);
    assert_non_null(ctx);
    sdl2_state_destroy(ctx);
}

static void test_create_null_status_ptr(void **state)
{
    (void)state;
    sdl2_state_t *ctx = sdl2_state_create(NULL, NULL);
    assert_non_null(ctx);
    sdl2_state_destroy(ctx);
}

static void test_destroy_null(void **state)
{
    (void)state;
    sdl2_state_destroy(NULL); /* must not crash */
}

/* =========================================================================
 * Group 2: Mode registration
 * ========================================================================= */

static void test_register_mode(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_status_t st = sdl2_state_register(ts->ctx, SDL2ST_GAME, &logging_def);
    assert_int_equal(st, SDL2ST_OK);
}

static void test_register_null_clears(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_register(ts->ctx, SDL2ST_GAME, &logging_def);
    sdl2_state_status_t st = sdl2_state_register(ts->ctx, SDL2ST_GAME, NULL);
    assert_int_equal(st, SDL2ST_OK);
    /* Transition to GAME — should not call any handlers since cleared */
    sdl2_state_transition(ts->ctx, SDL2ST_GAME);
    assert_int_equal(ts->log.count, 0);
}

static void test_register_invalid_mode(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_status_t st = sdl2_state_register(ts->ctx, SDL2ST_COUNT, &logging_def);
    assert_int_equal(st, SDL2ST_ERR_INVALID_MODE);
}

static void test_register_null_ctx(void **state)
{
    (void)state;
    sdl2_state_status_t st = sdl2_state_register(NULL, SDL2ST_GAME, &logging_def);
    assert_int_equal(st, SDL2ST_ERR_NULL_ARG);
}

/* =========================================================================
 * Group 3: Basic transitions
 * ========================================================================= */

static void test_transition_calls_enter(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_register(ts->ctx, SDL2ST_INTRO, &logging_def);
    sdl2_state_transition(ts->ctx, SDL2ST_INTRO);

    assert_int_equal(sdl2_state_current(ts->ctx), SDL2ST_INTRO);
    assert_int_equal(ts->log.count, 1);
    assert_int_equal(ts->log.calls[0].mode, SDL2ST_INTRO);
    assert_int_equal(ts->log.calls[0].type, 0); /* enter */
}

static void test_transition_calls_exit_then_enter(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_register(ts->ctx, SDL2ST_INTRO, &logging_def);
    sdl2_state_register(ts->ctx, SDL2ST_GAME, &logging_def);

    sdl2_state_transition(ts->ctx, SDL2ST_INTRO);
    ts->log.count = 0; /* reset log */

    sdl2_state_transition(ts->ctx, SDL2ST_GAME);

    assert_int_equal(sdl2_state_current(ts->ctx), SDL2ST_GAME);
    assert_int_equal(sdl2_state_previous(ts->ctx), SDL2ST_INTRO);
    assert_int_equal(ts->log.count, 2);
    /* Exit from INTRO first */
    assert_int_equal(ts->log.calls[0].mode, SDL2ST_INTRO);
    assert_int_equal(ts->log.calls[0].type, 2); /* exit */
    /* Then enter GAME */
    assert_int_equal(ts->log.calls[1].mode, SDL2ST_GAME);
    assert_int_equal(ts->log.calls[1].type, 0); /* enter */
}

static void test_transition_same_mode_is_noop(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_register(ts->ctx, SDL2ST_INTRO, &logging_def);
    sdl2_state_transition(ts->ctx, SDL2ST_INTRO);
    ts->log.count = 0;

    sdl2_state_status_t st = sdl2_state_transition(ts->ctx, SDL2ST_INTRO);

    assert_int_equal(st, SDL2ST_OK);
    assert_int_equal(ts->log.count, 0); /* no callbacks fired */
}

static void test_transition_invalid_mode(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_status_t st = sdl2_state_transition(ts->ctx, SDL2ST_COUNT);
    assert_int_equal(st, SDL2ST_ERR_INVALID_MODE);
}

static void test_transition_null_ctx(void **state)
{
    (void)state;
    sdl2_state_status_t st = sdl2_state_transition(NULL, SDL2ST_GAME);
    assert_int_equal(st, SDL2ST_ERR_NULL_ARG);
}

static void test_transition_tracks_previous(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_transition(ts->ctx, SDL2ST_PRESENTS);
    sdl2_state_transition(ts->ctx, SDL2ST_INTRO);
    sdl2_state_transition(ts->ctx, SDL2ST_GAME);

    assert_int_equal(sdl2_state_current(ts->ctx), SDL2ST_GAME);
    assert_int_equal(sdl2_state_previous(ts->ctx), SDL2ST_INTRO);
}

/* =========================================================================
 * Group 4: Menu cycle (legacy C-key sequence)
 * ========================================================================= */

static void test_menu_cycle(void **state)
{
    test_state_t *ts = (test_state_t *)*state;

    /* Register all menu modes with logging. */
    sdl2_state_mode_t cycle[] = {
        SDL2ST_INTRO,    SDL2ST_INSTRUCT,  SDL2ST_DEMO,    SDL2ST_KEYS,
        SDL2ST_KEYSEDIT, SDL2ST_HIGHSCORE, SDL2ST_PREVIEW, SDL2ST_INTRO,
    };
    for (int i = 0; i < 7; i++)
    {
        sdl2_state_register(ts->ctx, cycle[i], &logging_def);
    }

    /* Start in INTRO. */
    sdl2_state_transition(ts->ctx, SDL2ST_INTRO);

    /* Cycle through all modes. */
    for (int i = 1; i < 8; i++)
    {
        sdl2_state_status_t st = sdl2_state_transition(ts->ctx, cycle[i]);
        assert_int_equal(st, SDL2ST_OK);
        assert_int_equal(sdl2_state_current(ts->ctx), cycle[i]);
    }

    /* Should be back at INTRO. */
    assert_int_equal(sdl2_state_current(ts->ctx), SDL2ST_INTRO);
}

/* =========================================================================
 * Group 5: Dialogue push/pop
 * ========================================================================= */

static void test_push_dialogue(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_register(ts->ctx, SDL2ST_GAME, &logging_def);
    sdl2_state_register(ts->ctx, SDL2ST_DIALOGUE, &logging_def);

    sdl2_state_transition(ts->ctx, SDL2ST_GAME);
    ts->log.count = 0;

    sdl2_state_status_t st = sdl2_state_push_dialogue(ts->ctx);

    assert_int_equal(st, SDL2ST_OK);
    assert_int_equal(sdl2_state_current(ts->ctx), SDL2ST_DIALOGUE);
    assert_true(sdl2_state_is_dialogue(ts->ctx));
    assert_int_equal(sdl2_state_saved_mode(ts->ctx), SDL2ST_GAME);

    /* Verify exit(GAME), enter(DIALOGUE) callback order. */
    assert_int_equal(ts->log.count, 2);
    assert_int_equal(ts->log.calls[0].mode, SDL2ST_GAME);
    assert_int_equal(ts->log.calls[0].type, 2); /* exit */
    assert_int_equal(ts->log.calls[1].mode, SDL2ST_DIALOGUE);
    assert_int_equal(ts->log.calls[1].type, 0); /* enter */
}

static void test_pop_dialogue_restores_mode(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_register(ts->ctx, SDL2ST_GAME, &logging_def);
    sdl2_state_register(ts->ctx, SDL2ST_DIALOGUE, &logging_def);

    sdl2_state_transition(ts->ctx, SDL2ST_GAME);
    sdl2_state_push_dialogue(ts->ctx);
    ts->log.count = 0;

    sdl2_state_status_t st = sdl2_state_pop_dialogue(ts->ctx);

    assert_int_equal(st, SDL2ST_OK);
    assert_int_equal(sdl2_state_current(ts->ctx), SDL2ST_GAME);
    assert_false(sdl2_state_is_dialogue(ts->ctx));
    assert_int_equal(sdl2_state_saved_mode(ts->ctx), SDL2ST_NONE);

    /* Verify exit(DIALOGUE), enter(GAME) callback order. */
    assert_int_equal(ts->log.count, 2);
    assert_int_equal(ts->log.calls[0].mode, SDL2ST_DIALOGUE);
    assert_int_equal(ts->log.calls[0].type, 2); /* exit */
    assert_int_equal(ts->log.calls[1].mode, SDL2ST_GAME);
    assert_int_equal(ts->log.calls[1].type, 0); /* enter */
}

static void test_push_dialogue_already_in_dialogue(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_transition(ts->ctx, SDL2ST_GAME);
    sdl2_state_push_dialogue(ts->ctx);

    sdl2_state_status_t st = sdl2_state_push_dialogue(ts->ctx);
    assert_int_equal(st, SDL2ST_ERR_ALREADY_IN_DIALOGUE);
}

static void test_pop_dialogue_not_in_dialogue(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_status_t st = sdl2_state_pop_dialogue(ts->ctx);
    assert_int_equal(st, SDL2ST_ERR_NOT_IN_DIALOGUE);
}

static void test_push_pop_null_ctx(void **state)
{
    (void)state;
    assert_int_equal(sdl2_state_push_dialogue(NULL), SDL2ST_ERR_NULL_ARG);
    assert_int_equal(sdl2_state_pop_dialogue(NULL), SDL2ST_ERR_NULL_ARG);
}

static void test_dialogue_from_intro(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_transition(ts->ctx, SDL2ST_INTRO);
    sdl2_state_push_dialogue(ts->ctx);

    assert_int_equal(sdl2_state_saved_mode(ts->ctx), SDL2ST_INTRO);

    sdl2_state_pop_dialogue(ts->ctx);
    assert_int_equal(sdl2_state_current(ts->ctx), SDL2ST_INTRO);
}

/* =========================================================================
 * Group 6: Frame counter
 * ========================================================================= */

static void test_frame_increments_on_update(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_transition(ts->ctx, SDL2ST_GAME);

    assert_int_equal(sdl2_state_frame(ts->ctx), 0);
    sdl2_state_update(ts->ctx);
    assert_int_equal(sdl2_state_frame(ts->ctx), 1);
    sdl2_state_update(ts->ctx);
    assert_int_equal(sdl2_state_frame(ts->ctx), 2);
}

static void test_frame_frozen_in_pause(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_transition(ts->ctx, SDL2ST_GAME);
    sdl2_state_update(ts->ctx); /* frame = 1 */

    sdl2_state_transition(ts->ctx, SDL2ST_PAUSE);
    sdl2_state_update(ts->ctx);
    sdl2_state_update(ts->ctx);

    assert_int_equal(sdl2_state_frame(ts->ctx), 1); /* unchanged */
}

static void test_frame_frozen_in_dialogue(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_transition(ts->ctx, SDL2ST_GAME);
    sdl2_state_update(ts->ctx); /* frame = 1 */
    sdl2_state_update(ts->ctx); /* frame = 2 */

    sdl2_state_push_dialogue(ts->ctx);
    sdl2_state_update(ts->ctx);

    assert_int_equal(sdl2_state_frame(ts->ctx), 2); /* unchanged */

    sdl2_state_pop_dialogue(ts->ctx);
    sdl2_state_update(ts->ctx); /* frame = 3 */
    assert_int_equal(sdl2_state_frame(ts->ctx), 3);
}

static void test_frame_increments_in_menu_modes(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_transition(ts->ctx, SDL2ST_INTRO);
    sdl2_state_update(ts->ctx);
    assert_int_equal(sdl2_state_frame(ts->ctx), 1);

    sdl2_state_transition(ts->ctx, SDL2ST_BONUS);
    sdl2_state_update(ts->ctx);
    assert_int_equal(sdl2_state_frame(ts->ctx), 2);
}

static void test_update_dispatches_handler(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_register(ts->ctx, SDL2ST_GAME, &logging_def);
    sdl2_state_transition(ts->ctx, SDL2ST_GAME);
    ts->log.count = 0;

    sdl2_state_update(ts->ctx);

    assert_int_equal(ts->log.count, 1);
    assert_int_equal(ts->log.calls[0].mode, SDL2ST_GAME);
    assert_int_equal(ts->log.calls[0].type, 1); /* update */
}

static void test_update_null_ctx(void **state)
{
    (void)state;
    sdl2_state_update(NULL); /* must not crash */
}

/* =========================================================================
 * Group 7: Query functions
 * ========================================================================= */

static void test_is_paused(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    assert_false(sdl2_state_is_paused(ts->ctx));
    sdl2_state_transition(ts->ctx, SDL2ST_PAUSE);
    assert_true(sdl2_state_is_paused(ts->ctx));
}

static void test_is_gameplay(void **state)
{
    test_state_t *ts = (test_state_t *)*state;

    sdl2_state_transition(ts->ctx, SDL2ST_GAME);
    assert_true(sdl2_state_is_gameplay(ts->ctx));

    sdl2_state_transition(ts->ctx, SDL2ST_PAUSE);
    assert_true(sdl2_state_is_gameplay(ts->ctx));

    sdl2_state_transition(ts->ctx, SDL2ST_BALL_WAIT);
    assert_true(sdl2_state_is_gameplay(ts->ctx));

    sdl2_state_transition(ts->ctx, SDL2ST_WAIT);
    assert_true(sdl2_state_is_gameplay(ts->ctx));

    /* Non-gameplay modes. */
    sdl2_state_transition(ts->ctx, SDL2ST_INTRO);
    assert_false(sdl2_state_is_gameplay(ts->ctx));

    sdl2_state_transition(ts->ctx, SDL2ST_BONUS);
    assert_false(sdl2_state_is_gameplay(ts->ctx));
}

static void test_query_null_ctx(void **state)
{
    (void)state;
    assert_int_equal(sdl2_state_current(NULL), SDL2ST_NONE);
    assert_int_equal(sdl2_state_previous(NULL), SDL2ST_NONE);
    assert_int_equal(sdl2_state_saved_mode(NULL), SDL2ST_NONE);
    assert_int_equal(sdl2_state_frame(NULL), 0);
    assert_false(sdl2_state_is_paused(NULL));
    assert_false(sdl2_state_is_dialogue(NULL));
    assert_false(sdl2_state_is_gameplay(NULL));
}

/* =========================================================================
 * Group 8: Mode names and status strings
 * ========================================================================= */

static void test_all_modes_have_names(void **state)
{
    (void)state;
    for (int i = 0; i < SDL2ST_COUNT; i++)
    {
        const char *name = sdl2_state_mode_name((sdl2_state_mode_t)i);
        assert_non_null(name);
        assert_string_not_equal(name, "unknown");
    }
}

static void test_unknown_mode_name(void **state)
{
    (void)state;
    const char *name = sdl2_state_mode_name(SDL2ST_COUNT);
    assert_string_equal(name, "unknown");
}

static void test_specific_mode_names(void **state)
{
    (void)state;
    assert_string_equal(sdl2_state_mode_name(SDL2ST_NONE), "none");
    assert_string_equal(sdl2_state_mode_name(SDL2ST_GAME), "game");
    assert_string_equal(sdl2_state_mode_name(SDL2ST_INTRO), "intro");
    assert_string_equal(sdl2_state_mode_name(SDL2ST_PAUSE), "pause");
    assert_string_equal(sdl2_state_mode_name(SDL2ST_DIALOGUE), "dialogue");
    assert_string_equal(sdl2_state_mode_name(SDL2ST_EDIT), "edit");
}

static void test_all_statuses_have_strings(void **state)
{
    (void)state;
    sdl2_state_status_t codes[] = {
        SDL2ST_OK,
        SDL2ST_ERR_NULL_ARG,
        SDL2ST_ERR_INVALID_MODE,
        SDL2ST_ERR_ALLOC_FAILED,
        SDL2ST_ERR_ALREADY_IN_DIALOGUE,
        SDL2ST_ERR_NOT_IN_DIALOGUE,
    };
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
    {
        const char *s = sdl2_state_status_string(codes[i]);
        assert_non_null(s);
        assert_true(strlen(s) > 0);
    }
}

static void test_unknown_status_string(void **state)
{
    (void)state;
    const char *s = sdl2_state_status_string((sdl2_state_status_t)999);
    assert_string_equal(s, "unknown status");
}

/* =========================================================================
 * Group 9: Pause toggle pattern (legacy ToggleGamePaused)
 * ========================================================================= */

static void test_pause_toggle(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_register(ts->ctx, SDL2ST_GAME, &logging_def);
    sdl2_state_register(ts->ctx, SDL2ST_PAUSE, &logging_def);

    /* Start in GAME. */
    sdl2_state_transition(ts->ctx, SDL2ST_GAME);
    assert_false(sdl2_state_is_paused(ts->ctx));

    /* Pause. */
    sdl2_state_transition(ts->ctx, SDL2ST_PAUSE);
    assert_true(sdl2_state_is_paused(ts->ctx));
    assert_int_equal(sdl2_state_previous(ts->ctx), SDL2ST_GAME);

    /* Unpause — return to GAME. */
    sdl2_state_transition(ts->ctx, SDL2ST_GAME);
    assert_false(sdl2_state_is_paused(ts->ctx));
    assert_int_equal(sdl2_state_previous(ts->ctx), SDL2ST_PAUSE);
}

/* =========================================================================
 * Group 10: Game startup sequence (legacy handleEventLoop)
 * ========================================================================= */

static void test_startup_sequence(void **state)
{
    test_state_t *ts = (test_state_t *)*state;
    sdl2_state_register(ts->ctx, SDL2ST_PRESENTS, &logging_def);
    sdl2_state_register(ts->ctx, SDL2ST_INTRO, &logging_def);
    sdl2_state_register(ts->ctx, SDL2ST_GAME, &logging_def);

    /* Game starts in PRESENTS (splash screen). */
    sdl2_state_transition(ts->ctx, SDL2ST_PRESENTS);
    assert_int_equal(sdl2_state_current(ts->ctx), SDL2ST_PRESENTS);

    /* After splash, auto-transition to INTRO. */
    sdl2_state_transition(ts->ctx, SDL2ST_INTRO);
    assert_int_equal(sdl2_state_current(ts->ctx), SDL2ST_INTRO);

    /* Player presses space — start game. */
    sdl2_state_transition(ts->ctx, SDL2ST_GAME);
    assert_int_equal(sdl2_state_current(ts->ctx), SDL2ST_GAME);
    assert_int_equal(sdl2_state_previous(ts->ctx), SDL2ST_INTRO);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest lifecycle_tests[] = {
        cmocka_unit_test_setup_teardown(test_create_destroy, setup_state, teardown_state),
        cmocka_unit_test_setup_teardown(test_initial_mode_is_none, setup_state, teardown_state),
        cmocka_unit_test_setup_teardown(test_initial_frame_is_zero, setup_state, teardown_state),
        cmocka_unit_test(test_create_status_ok),
        cmocka_unit_test(test_create_null_status_ptr),
        cmocka_unit_test(test_destroy_null),
    };

    const struct CMUnitTest registration_tests[] = {
        cmocka_unit_test_setup_teardown(test_register_mode, setup_state, teardown_state),
        cmocka_unit_test_setup_teardown(test_register_null_clears, setup_state, teardown_state),
        cmocka_unit_test_setup_teardown(test_register_invalid_mode, setup_state, teardown_state),
        cmocka_unit_test(test_register_null_ctx),
    };

    const struct CMUnitTest transition_tests[] = {
        cmocka_unit_test_setup_teardown(test_transition_calls_enter, setup_state, teardown_state),
        cmocka_unit_test_setup_teardown(test_transition_calls_exit_then_enter, setup_state,
                                        teardown_state),
        cmocka_unit_test_setup_teardown(test_transition_same_mode_is_noop, setup_state,
                                        teardown_state),
        cmocka_unit_test_setup_teardown(test_transition_invalid_mode, setup_state, teardown_state),
        cmocka_unit_test(test_transition_null_ctx),
        cmocka_unit_test_setup_teardown(test_transition_tracks_previous, setup_state,
                                        teardown_state),
    };

    const struct CMUnitTest cycle_tests[] = {
        cmocka_unit_test_setup_teardown(test_menu_cycle, setup_state, teardown_state),
    };

    const struct CMUnitTest dialogue_tests[] = {
        cmocka_unit_test_setup_teardown(test_push_dialogue, setup_state, teardown_state),
        cmocka_unit_test_setup_teardown(test_pop_dialogue_restores_mode, setup_state,
                                        teardown_state),
        cmocka_unit_test_setup_teardown(test_push_dialogue_already_in_dialogue, setup_state,
                                        teardown_state),
        cmocka_unit_test_setup_teardown(test_pop_dialogue_not_in_dialogue, setup_state,
                                        teardown_state),
        cmocka_unit_test(test_push_pop_null_ctx),
        cmocka_unit_test_setup_teardown(test_dialogue_from_intro, setup_state, teardown_state),
    };

    const struct CMUnitTest frame_tests[] = {
        cmocka_unit_test_setup_teardown(test_frame_increments_on_update, setup_state,
                                        teardown_state),
        cmocka_unit_test_setup_teardown(test_frame_frozen_in_pause, setup_state, teardown_state),
        cmocka_unit_test_setup_teardown(test_frame_frozen_in_dialogue, setup_state, teardown_state),
        cmocka_unit_test_setup_teardown(test_frame_increments_in_menu_modes, setup_state,
                                        teardown_state),
        cmocka_unit_test_setup_teardown(test_update_dispatches_handler, setup_state,
                                        teardown_state),
        cmocka_unit_test(test_update_null_ctx),
    };

    const struct CMUnitTest query_tests[] = {
        cmocka_unit_test_setup_teardown(test_is_paused, setup_state, teardown_state),
        cmocka_unit_test_setup_teardown(test_is_gameplay, setup_state, teardown_state),
        cmocka_unit_test(test_query_null_ctx),
    };

    const struct CMUnitTest name_tests[] = {
        cmocka_unit_test(test_all_modes_have_names),
        cmocka_unit_test(test_unknown_mode_name),
        cmocka_unit_test(test_specific_mode_names),
        cmocka_unit_test(test_all_statuses_have_strings),
        cmocka_unit_test(test_unknown_status_string),
    };

    const struct CMUnitTest pause_tests[] = {
        cmocka_unit_test_setup_teardown(test_pause_toggle, setup_state, teardown_state),
    };

    const struct CMUnitTest startup_tests[] = {
        cmocka_unit_test_setup_teardown(test_startup_sequence, setup_state, teardown_state),
    };

    int failed = 0;
    failed += cmocka_run_group_tests_name("lifecycle", lifecycle_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("registration", registration_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("transitions", transition_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("menu cycle", cycle_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("dialogue", dialogue_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("frame counter", frame_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("queries", query_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("names and strings", name_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("pause toggle", pause_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("startup sequence", startup_tests, NULL, NULL);
    return failed;
}
