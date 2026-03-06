/*
 * test_integration_modes.c — Mode handler crash test.
 *
 * Force-transitions to each of the registered game modes and ticks
 * N frames to verify no crashes, memory errors, or undefined behavior.
 * ASan catches any issues during the ticking.
 *
 * This is a robustness test, not a correctness test.  We don't verify
 * that modes do the right thing — only that they don't crash.
 *
 * Modes tested:
 *   PRESENTS, INTRO, INSTRUCT, DEMO, PREVIEW, KEYS, KEYSEDIT,
 *   HIGHSCORE, BONUS, GAME, PAUSE, EDIT
 *
 * Modes NOT directly tested:
 *   NONE (no handler), BALL_WAIT / WAIT (legacy, never assigned),
 *   DIALOGUE (requires push/pop semantics)
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
 * Writable argv buffer
 * ========================================================================= */

static char arg_prog[] = "xboing_test";

/* =========================================================================
 * Fixture — creates full game context, starts in PRESENTS
 * ========================================================================= */

typedef struct
{
    game_ctx_t *ctx;
} test_fixture_t;

static int setup(void **vstate)
{
    test_fixture_t *f = calloc(1, sizeof(*f));
    assert_non_null(f);

    char *argv[] = {arg_prog, NULL};
    f->ctx = game_create(1, argv);
    assert_non_null(f->ctx);

    /* Start in PRESENTS like game_main.c */
    sdl2_state_transition(f->ctx->state, SDL2ST_PRESENTS);

    *vstate = f;
    return 0;
}

static int teardown(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_destroy(f->ctx);
    free(f);
    return 0;
}

/* =========================================================================
 * Helper: transition to mode and tick N frames
 * ========================================================================= */

#define TICK_COUNT 100

static void enter_and_tick(game_ctx_t *ctx, sdl2_state_mode_t mode)
{
    sdl2_state_status_t status = sdl2_state_transition(ctx->state, mode);
    assert_int_equal(status, SDL2ST_OK);
    assert_int_equal(sdl2_state_current(ctx->state), mode);

    for (int i = 0; i < TICK_COUNT; i++)
    {
        sdl2_input_begin_frame(ctx->input);
        sdl2_state_update(ctx->state);
    }

    /* Mode may have auto-transitioned — that's fine, just verify no crash */
}

/* =========================================================================
 * Tests — one per mode
 * ========================================================================= */

static void test_mode_presents(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_PRESENTS);
}

static void test_mode_intro(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_INTRO);
}

static void test_mode_instruct(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_INSTRUCT);
}

static void test_mode_demo(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_DEMO);
}

static void test_mode_preview(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_PREVIEW);
}

static void test_mode_keys(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_KEYS);
}

static void test_mode_keysedit(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_KEYSEDIT);
}

static void test_mode_highscore(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_HIGHSCORE);
}

static void test_mode_bonus(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_BONUS);
}

static void test_mode_game(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_GAME);
}

static void test_mode_pause(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    /* Enter GAME first, then PAUSE (pause needs game to be active) */
    sdl2_state_transition(f->ctx->state, SDL2ST_GAME);
    enter_and_tick(f->ctx, SDL2ST_PAUSE);
}

static void test_mode_edit(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_EDIT);
}

static void test_mode_dialogue_push_pop(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* Dialogue uses push/pop, not direct transition */
    sdl2_state_transition(ctx->state, SDL2ST_INTRO);

    sdl2_state_status_t status = sdl2_state_push_dialogue(ctx->state);
    assert_int_equal(status, SDL2ST_OK);
    assert_true(sdl2_state_is_dialogue(ctx->state));

    /* Tick in dialogue mode */
    for (int i = 0; i < TICK_COUNT; i++)
    {
        sdl2_input_begin_frame(ctx->input);
        sdl2_state_update(ctx->state);
    }

    /* Pop back to INTRO */
    status = sdl2_state_pop_dialogue(ctx->state);
    assert_int_equal(status, SDL2ST_OK);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_INTRO);
}

static void test_rapid_transitions(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* Rapidly cycle through all modes — stress test transition logic */
    sdl2_state_mode_t modes[] = {
        SDL2ST_PRESENTS, SDL2ST_INTRO,     SDL2ST_INSTRUCT, SDL2ST_DEMO,
        SDL2ST_PREVIEW,  SDL2ST_KEYS,      SDL2ST_KEYSEDIT, SDL2ST_HIGHSCORE,
        SDL2ST_BONUS,    SDL2ST_GAME,      SDL2ST_EDIT,     SDL2ST_PAUSE,
    };
    int mode_count = (int)(sizeof(modes) / sizeof(modes[0]));

    for (int cycle = 0; cycle < 3; cycle++)
    {
        for (int i = 0; i < mode_count; i++)
        {
            sdl2_state_transition(ctx->state, modes[i]);
            sdl2_input_begin_frame(ctx->input);
            sdl2_state_update(ctx->state);
        }
    }

    /* Just verify we're still alive */
    sdl2_state_mode_t mode = sdl2_state_current(ctx->state);
    assert_true(mode >= SDL2ST_NONE && mode < SDL2ST_COUNT);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_mode_presents, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_intro, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_instruct, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_demo, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_preview, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_keys, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_keysedit, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_highscore, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_bonus, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_game, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_pause, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_edit, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_dialogue_push_pop, setup, teardown),
        cmocka_unit_test_setup_teardown(test_rapid_transitions, setup, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
