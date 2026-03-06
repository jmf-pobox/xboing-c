/*
 * test_integration_autocycle.c — Integration test for the attract-mode auto-cycle.
 *
 * Creates a full game context with dummy SDL drivers, then ticks the state
 * machine repeatedly to verify the attract-mode sequence advances correctly:
 *
 *   PRESENTS → INTRO → INSTRUCT → DEMO → KEYS → KEYSEDIT → PREVIEW → HIGHSCORE → INTRO
 *
 * Each sequencer module auto-advances via on_finished callbacks after their
 * internal frame counters expire.  No user input is injected — this tests
 * the fully autonomous attract loop.
 *
 * The test records every mode transition and verifies both:
 *   1. That specific expected transitions occur in order
 *   2. That the game does not crash during thousands of update ticks
 *
 * Requires: SDL_VIDEODRIVER=dummy, SDL_AUDIODRIVER=dummy
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <cmocka.h>

#include "game_context.h"
#include "game_init.h"
#include "sdl2_input.h"
#include "sdl2_state.h"

/* =========================================================================
 * Writable argv buffers (avoid UB from string literal mutation)
 * ========================================================================= */

static char arg_prog[] = "xboing_test";

/* =========================================================================
 * Transition log — records mode changes during ticking
 * ========================================================================= */

#define MAX_TRANSITIONS 64

typedef struct
{
    sdl2_state_mode_t from;
    sdl2_state_mode_t to;
    int at_tick;
} transition_t;

typedef struct
{
    game_ctx_t *ctx;
    transition_t log[MAX_TRANSITIONS];
    int count;
    sdl2_state_mode_t last_mode;
} test_state_t;

/* Record a transition if the mode changed */
static void record_transition(test_state_t *ts, int tick)
{
    sdl2_state_mode_t current = sdl2_state_current(ts->ctx->state);
    if (current != ts->last_mode && ts->count < MAX_TRANSITIONS)
    {
        ts->log[ts->count].from = ts->last_mode;
        ts->log[ts->count].to = current;
        ts->log[ts->count].at_tick = tick;
        ts->count++;
        ts->last_mode = current;
    }
}

/* Tick N frames, recording transitions.
 * Calls sdl2_input_begin_frame() each tick so that mode handlers
 * using sdl2_input_just_pressed() see correct edge-triggered state. */
static void tick_frames(test_state_t *ts, int n, int *tick_counter)
{
    for (int i = 0; i < n; i++)
    {
        sdl2_input_begin_frame(ts->ctx->input);
        sdl2_state_update(ts->ctx->state);
        (*tick_counter)++;
        record_transition(ts, *tick_counter);
    }
}

/* Find the first transition TO a given mode, starting at log index `from_idx` */
static int find_transition_to(const test_state_t *ts, sdl2_state_mode_t target, int from_idx)
{
    for (int i = from_idx; i < ts->count; i++)
    {
        if (ts->log[i].to == target)
            return i;
    }
    return -1;
}

/* =========================================================================
 * Fixture
 * ========================================================================= */

static int setup(void **vstate)
{
    test_state_t *ts = calloc(1, sizeof(*ts));
    assert_non_null(ts);

    char *argv[] = {arg_prog, NULL};
    ts->ctx = game_create(1, argv);
    assert_non_null(ts->ctx);

    /* Transition to PRESENTS — mirrors game_main.c:33 */
    sdl2_state_transition(ts->ctx->state, SDL2ST_PRESENTS);

    ts->last_mode = sdl2_state_current(ts->ctx->state);
    ts->count = 0;

    *vstate = ts;
    return 0;
}

static int teardown(void **vstate)
{
    test_state_t *ts = (test_state_t *)*vstate;
    game_destroy(ts->ctx);
    free(ts);
    return 0;
}

/* =========================================================================
 * Helper: dump transition log for debugging
 * ========================================================================= */

static void dump_transitions(const test_state_t *ts)
{
    for (int i = 0; i < ts->count; i++)
    {
        fprintf(stderr, "  [%3d] tick %5d: %s -> %s\n", i, ts->log[i].at_tick,
                sdl2_state_mode_name(ts->log[i].from), sdl2_state_mode_name(ts->log[i].to));
    }
}

/* =========================================================================
 * Tests
 * ========================================================================= */

static void test_initial_mode_is_presents(void **vstate)
{
    const test_state_t *ts = (const test_state_t *)*vstate;
    sdl2_state_mode_t mode = sdl2_state_current(ts->ctx->state);
    assert_int_equal(mode, SDL2ST_PRESENTS);
}

static void test_presents_transitions_to_intro(void **vstate)
{
    test_state_t *ts = (test_state_t *)*vstate;
    int tick = 0;

    /* Presents has multiple phases with delays totaling ~3000-5000 frames.
     * With ATTRACT_FRAME_MULTIPLIER=6, each tick advances 6 virtual frames.
     * Tick up to 2000 real ticks (12000 virtual frames) — generous margin. */
    tick_frames(ts, 2000, &tick);

    int idx = find_transition_to(ts, SDL2ST_INTRO, 0);
    if (idx < 0)
    {
        fprintf(stderr, "FAIL: No PRESENTS→INTRO transition found after %d ticks.\n", tick);
        fprintf(stderr, "Transitions recorded:\n");
        dump_transitions(ts);
        fail();
        return;
    }
    assert_int_equal(ts->log[idx].from, SDL2ST_PRESENTS);
}

static void test_intro_transitions_to_instruct(void **vstate)
{
    test_state_t *ts = (test_state_t *)*vstate;
    int tick = 0;

    /* Tick enough to get through presents + intro */
    tick_frames(ts, 4000, &tick);

    /* Find INTRO first */
    int intro_idx = find_transition_to(ts, SDL2ST_INTRO, 0);
    assert_true(intro_idx >= 0);

    /* Then INSTRUCT after INTRO */
    int instruct_idx = find_transition_to(ts, SDL2ST_INSTRUCT, intro_idx + 1);
    if (instruct_idx < 0)
    {
        fprintf(stderr, "FAIL: No INTRO→INSTRUCT transition found.\n");
        dump_transitions(ts);
    }
    assert_true(instruct_idx >= 0);
}

static void test_full_attract_cycle(void **vstate)
{
    test_state_t *ts = (test_state_t *)*vstate;
    int tick = 0;

    /*
     * The full attract cycle is:
     *   PRESENTS → INTRO → INSTRUCT → DEMO → KEYS → KEYSEDIT → PREVIEW → HIGHSCORE → INTRO
     *
     * Each sequencer runs for a few hundred to a few thousand virtual frames.
     * With 6x multiplier, 10000 real ticks = 60000 virtual frames.
     * That should be enough for one full cycle.
     */
    tick_frames(ts, 10000, &tick);

    /* Verify the expected sequence of modes was visited */
    sdl2_state_mode_t expected[] = {
        SDL2ST_INTRO,     /* presents → intro */
        SDL2ST_INSTRUCT,  /* intro → instruct */
        SDL2ST_DEMO,      /* instruct → demo */
        SDL2ST_KEYS,      /* demo → keys */
        SDL2ST_KEYSEDIT,  /* keys → keysedit */
        SDL2ST_PREVIEW,   /* keysedit → preview */
        SDL2ST_HIGHSCORE, /* preview → highscore */
        SDL2ST_INTRO,     /* highscore → intro (cycle restart) */
    };
    int expected_count = (int)(sizeof(expected) / sizeof(expected[0]));

    int log_idx = 0;
    for (int i = 0; i < expected_count; i++)
    {
        int found = find_transition_to(ts, expected[i], log_idx);
        if (found < 0)
        {
            fprintf(stderr,
                    "FAIL: Expected transition to %s (step %d/%d) not found "
                    "after log index %d.\n",
                    sdl2_state_mode_name(expected[i]), i + 1, expected_count, log_idx);
            fprintf(stderr, "Full transition log (%d entries):\n", ts->count);
            dump_transitions(ts);
        }
        assert_true(found >= 0);
        log_idx = found + 1;
    }
}

static void test_no_crash_extended_ticking(void **vstate)
{
    test_state_t *ts = (test_state_t *)*vstate;
    int tick = 0;

    /* Tick for 20000 frames — should complete 2+ full cycles without crashing.
     * ASan will catch any memory errors. */
    tick_frames(ts, 20000, &tick);

    /* Just verify we're still in a valid mode */
    sdl2_state_mode_t mode = sdl2_state_current(ts->ctx->state);
    assert_true(mode >= SDL2ST_NONE && mode < SDL2ST_COUNT);

    /* And that at least one full cycle happened */
    int second_intro = -1;
    int first_intro = find_transition_to(ts, SDL2ST_INTRO, 0);
    if (first_intro >= 0)
        second_intro = find_transition_to(ts, SDL2ST_INTRO, first_intro + 1);
    assert_true(second_intro >= 0);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_initial_mode_is_presents, setup, teardown),
        cmocka_unit_test_setup_teardown(test_presents_transitions_to_intro, setup, teardown),
        cmocka_unit_test_setup_teardown(test_intro_transitions_to_instruct, setup, teardown),
        cmocka_unit_test_setup_teardown(test_full_attract_cycle, setup, teardown),
        cmocka_unit_test_setup_teardown(test_no_crash_extended_ticking, setup, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
