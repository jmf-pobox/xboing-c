/*
 * test_replay_smoke.c — Smoke tests for the input replay infrastructure.
 *
 * Verifies the replay system can:
 *   - Inject synthetic key events into the input system
 *   - Drive the game through mode transitions via scripted input
 *   - Record and verify state at specific frame checkpoints
 *
 * Requires: SDL_VIDEODRIVER=dummy, SDL_AUDIODRIVER=dummy
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <cmocka.h>

#include "game_context.h"
#include "game_init.h"
#include "sdl2_input.h"
#include "sdl2_state.h"
#include "test_replay.h"

/* =========================================================================
 * Writable argv buffer
 * ========================================================================= */

static char arg_prog[] = "xboing_test";

/* =========================================================================
 * Fixture
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
 * Tests
 * ========================================================================= */

static void test_replay_init(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    replay_event_t script[] = {
        {0, SDL2I_START, 1},
        {1, SDL2I_START, 0},
        REPLAY_END,
    };

    replay_ctx_t rctx;
    replay_init(&rctx, f->ctx, script);

    assert_int_equal(rctx.current_frame, 0);
    assert_int_equal(rctx.script_len, 2);
    assert_int_equal(rctx.script_idx, 0);
}

static void test_replay_tick_advances_frame(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    replay_event_t script[] = {REPLAY_END};
    replay_ctx_t rctx;
    replay_init(&rctx, f->ctx, script);

    replay_tick(&rctx);
    assert_int_equal(rctx.current_frame, 1);

    replay_tick(&rctx);
    assert_int_equal(rctx.current_frame, 2);
}

static void test_replay_tick_until(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    replay_event_t script[] = {REPLAY_END};
    replay_ctx_t rctx;
    replay_init(&rctx, f->ctx, script);

    int ticked = replay_tick_until(&rctx, 50);
    assert_int_equal(ticked, 50);
    assert_int_equal(rctx.current_frame, 50);
}

static void test_replay_space_starts_game(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /*
     * Script: press Space once to skip presents → intro,
     * then press Space again during intro to start the game.
     *
     * First Space during PRESENTS triggers presents_system_skip(),
     * which accelerates to on_finished → INTRO transition.
     * Second Space during INTRO triggers game start.
     */
    replay_event_t script[] = {
        { 10, SDL2I_START, 1}, /* Skip presents */
        { 11, SDL2I_START, 0},
        {500, SDL2I_START, 1}, /* Start game from intro */
        {501, SDL2I_START, 0},
        REPLAY_END,
    };

    replay_ctx_t rctx;
    replay_init(&rctx, f->ctx, script);

    /* Tick past the second Space press */
    replay_tick_until(&rctx, 510);

    /* Should be in GAME mode */
    sdl2_state_mode_t mode = sdl2_state_current(f->ctx->state);
    assert_int_equal(mode, SDL2ST_GAME);
}

static void test_replay_game_then_pause(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /*
     * Script: press Space to start game, then press P to pause.
     */
    replay_event_t script[] = {
        { 10, SDL2I_START, 1}, /* Skip presents */
        { 11, SDL2I_START, 0},
        {500, SDL2I_START, 1}, /* Start game */
        {501, SDL2I_START, 0},
        {550, SDL2I_PAUSE, 1}, /* Pause */
        {551, SDL2I_PAUSE, 0},
        REPLAY_END,
    };

    replay_ctx_t rctx;
    replay_init(&rctx, f->ctx, script);

    /* Tick past game start */
    replay_tick_until(&rctx, 510);
    assert_int_equal(sdl2_state_current(f->ctx->state), SDL2ST_GAME);

    /* Tick past pause */
    replay_tick_until(&rctx, 560);
    assert_int_equal(sdl2_state_current(f->ctx->state), SDL2ST_PAUSE);
}

static void test_replay_action_to_scancode(void **vstate)
{
    (void)vstate;

    assert_int_equal(replay_action_to_scancode(SDL2I_LEFT), SDL_SCANCODE_LEFT);
    assert_int_equal(replay_action_to_scancode(SDL2I_START), SDL_SCANCODE_SPACE);
    assert_int_equal(replay_action_to_scancode(SDL2I_SHOOT), SDL_SCANCODE_K);
    assert_int_equal(replay_action_to_scancode(SDL2I_QUIT), SDL_SCANCODE_Q);
}

static void test_replay_extended_gameplay(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /*
     * Script: start game, move paddle left for a bit, then right.
     * Verify no crashes during gameplay with active input.
     */
    replay_event_t script[] = {
        { 10, SDL2I_START, 1},  /* Skip presents */
        { 11, SDL2I_START, 0},
        {500, SDL2I_START, 1},  /* Start game */
        {501, SDL2I_START, 0},
        {600, SDL2I_LEFT, 1},   /* Hold left */
        {700, SDL2I_LEFT, 0},   /* Release left */
        {700, SDL2I_RIGHT, 1},  /* Hold right */
        {800, SDL2I_RIGHT, 0},  /* Release right */
        {850, SDL2I_SHOOT, 1},  /* Shoot */
        {851, SDL2I_SHOOT, 0},
        REPLAY_END,
    };

    replay_ctx_t rctx;
    replay_init(&rctx, f->ctx, script);

    /* Tick through entire script plus extra gameplay */
    replay_tick_until(&rctx, 1500);

    /* Should still be in a valid state */
    sdl2_state_mode_t mode = sdl2_state_current(f->ctx->state);
    assert_true(mode >= SDL2ST_NONE && mode < SDL2ST_COUNT);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_replay_init, setup, teardown),
        cmocka_unit_test_setup_teardown(test_replay_tick_advances_frame, setup, teardown),
        cmocka_unit_test_setup_teardown(test_replay_tick_until, setup, teardown),
        cmocka_unit_test_setup_teardown(test_replay_space_starts_game, setup, teardown),
        cmocka_unit_test_setup_teardown(test_replay_game_then_pause, setup, teardown),
        cmocka_unit_test(test_replay_action_to_scancode),
        cmocka_unit_test_setup_teardown(test_replay_extended_gameplay, setup, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
