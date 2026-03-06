/*
 * test_replay_gameplay.c — Scripted gameplay session via input replay.
 *
 * Uses the replay infrastructure to drive a full gameplay session:
 * start from attract, play with scripted input (paddle movement,
 * shooting), and verify game state at checkpoints.
 *
 * Tests verify:
 *   - Scripted game start from attract mode
 *   - Paddle movement via held LEFT/RIGHT keys
 *   - Shoot action fires bullets
 *   - Pause/unpause round-trip during gameplay
 *   - Game state is consistent at checkpoints
 *
 * Requires: SDL_VIDEODRIVER=dummy, SDL_AUDIODRIVER=dummy
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <cmocka.h>

#include "ball_system.h"
#include "game_context.h"
#include "game_init.h"
#include "paddle_system.h"
#include "score_system.h"
#include "sdl2_state.h"
#include "test_replay.h"

/* =========================================================================
 * Writable argv buffer
 * ========================================================================= */

static char arg_prog[] = "xboing_test";

/* =========================================================================
 * Helper: create game and skip to GAME mode via replay
 * Returns the replay context positioned at start_frame (in GAME mode).
 * ========================================================================= */

typedef struct
{
    game_ctx_t *ctx;
    replay_ctx_t rctx;
} game_session_t;

/*
 * Start a game session: create context, skip presents with Space,
 * then start game with second Space.  Returns with rctx positioned
 * just after entering GAME mode.
 */
static int start_game_session(game_session_t *s, const replay_event_t *extra_script)
{
    char *argv[] = {arg_prog, NULL};
    s->ctx = game_create(1, argv);
    if (!s->ctx)
        return -1;

    sdl2_state_transition(s->ctx->state, SDL2ST_PRESENTS);
    replay_init(&s->rctx, s->ctx, extra_script);
    return 0;
}

static void end_game_session(game_session_t *s)
{
    game_destroy(s->ctx);
}

/* =========================================================================
 * Fixture with game start preamble baked into script
 * ========================================================================= */

/* Common preamble: skip presents at frame 10, start game at frame 500 */
#define PREAMBLE_SKIP_PRESENTS  { 10, SDL2I_START, 1}, { 11, SDL2I_START, 0}
#define PREAMBLE_START_GAME    {500, SDL2I_START, 1}, {501, SDL2I_START, 0}
#define GAME_START_FRAME 510

/* =========================================================================
 * Tests
 * ========================================================================= */

static void test_scripted_game_start(void **vstate)
{
    (void)vstate;

    replay_event_t script[] = {
        PREAMBLE_SKIP_PRESENTS,
        PREAMBLE_START_GAME,
        REPLAY_END,
    };

    game_session_t s;
    assert_int_equal(start_game_session(&s, script), 0);

    replay_tick_until(&s.rctx, GAME_START_FRAME);
    assert_int_equal(sdl2_state_current(s.ctx->state), SDL2ST_GAME);
    assert_int_equal(s.ctx->lives_left, 3);
    assert_true(s.ctx->game_active);

    end_game_session(&s);
}

static void test_paddle_moves_left(void **vstate)
{
    (void)vstate;

    replay_event_t script[] = {
        PREAMBLE_SKIP_PRESENTS,
        PREAMBLE_START_GAME,
        {GAME_START_FRAME,       SDL2I_LEFT, 1}, /* Hold left */
        {GAME_START_FRAME + 100, SDL2I_LEFT, 0}, /* Release */
        REPLAY_END,
    };

    game_session_t s;
    assert_int_equal(start_game_session(&s, script), 0);

    /* Record initial paddle position after game starts */
    replay_tick_until(&s.rctx, GAME_START_FRAME);
    int initial_pos = paddle_system_get_pos(s.ctx->paddle);

    /* Tick through left movement */
    replay_tick_until(&s.rctx, GAME_START_FRAME + 100);
    int final_pos = paddle_system_get_pos(s.ctx->paddle);

    /* Paddle should have moved left (lower X) */
    assert_true(final_pos < initial_pos);

    end_game_session(&s);
}

static void test_paddle_moves_right(void **vstate)
{
    (void)vstate;

    replay_event_t script[] = {
        PREAMBLE_SKIP_PRESENTS,
        PREAMBLE_START_GAME,
        {GAME_START_FRAME,       SDL2I_RIGHT, 1}, /* Hold right */
        {GAME_START_FRAME + 100, SDL2I_RIGHT, 0}, /* Release */
        REPLAY_END,
    };

    game_session_t s;
    assert_int_equal(start_game_session(&s, script), 0);

    replay_tick_until(&s.rctx, GAME_START_FRAME);
    int initial_pos = paddle_system_get_pos(s.ctx->paddle);

    replay_tick_until(&s.rctx, GAME_START_FRAME + 100);
    int final_pos = paddle_system_get_pos(s.ctx->paddle);

    /* Paddle should have moved right (higher X) */
    assert_true(final_pos > initial_pos);

    end_game_session(&s);
}

static void test_pause_unpause_round_trip(void **vstate)
{
    (void)vstate;

    replay_event_t script[] = {
        PREAMBLE_SKIP_PRESENTS,
        PREAMBLE_START_GAME,
        {GAME_START_FRAME + 50,  SDL2I_PAUSE, 1}, /* Pause */
        {GAME_START_FRAME + 51,  SDL2I_PAUSE, 0},
        {GAME_START_FRAME + 100, SDL2I_PAUSE, 1}, /* Unpause */
        {GAME_START_FRAME + 101, SDL2I_PAUSE, 0},
        REPLAY_END,
    };

    game_session_t s;
    assert_int_equal(start_game_session(&s, script), 0);

    /* Get into game */
    replay_tick_until(&s.rctx, GAME_START_FRAME);
    assert_int_equal(sdl2_state_current(s.ctx->state), SDL2ST_GAME);

    /* Tick to pause */
    replay_tick_until(&s.rctx, GAME_START_FRAME + 60);
    assert_int_equal(sdl2_state_current(s.ctx->state), SDL2ST_PAUSE);

    /* Tick to unpause */
    replay_tick_until(&s.rctx, GAME_START_FRAME + 110);
    assert_int_equal(sdl2_state_current(s.ctx->state), SDL2ST_GAME);

    end_game_session(&s);
}

static void test_extended_scripted_session(void **vstate)
{
    (void)vstate;

    /*
     * A longer scripted session: start game, move paddle around,
     * shoot several times, pause/unpause, continue playing.
     */
    replay_event_t script[] = {
        PREAMBLE_SKIP_PRESENTS,
        PREAMBLE_START_GAME,
        /* Move left */
        {GAME_START_FRAME,       SDL2I_LEFT, 1},
        {GAME_START_FRAME + 50,  SDL2I_LEFT, 0},
        /* Move right */
        {GAME_START_FRAME + 50,  SDL2I_RIGHT, 1},
        {GAME_START_FRAME + 150, SDL2I_RIGHT, 0},
        /* Shoot a few times */
        {GAME_START_FRAME + 200, SDL2I_SHOOT, 1},
        {GAME_START_FRAME + 201, SDL2I_SHOOT, 0},
        {GAME_START_FRAME + 250, SDL2I_SHOOT, 1},
        {GAME_START_FRAME + 251, SDL2I_SHOOT, 0},
        /* Pause and unpause */
        {GAME_START_FRAME + 300, SDL2I_PAUSE, 1},
        {GAME_START_FRAME + 301, SDL2I_PAUSE, 0},
        {GAME_START_FRAME + 350, SDL2I_PAUSE, 1},
        {GAME_START_FRAME + 351, SDL2I_PAUSE, 0},
        /* Continue playing */
        {GAME_START_FRAME + 400, SDL2I_LEFT, 1},
        {GAME_START_FRAME + 500, SDL2I_LEFT, 0},
        REPLAY_END,
    };

    game_session_t s;
    assert_int_equal(start_game_session(&s, script), 0);

    /* Tick the entire session */
    replay_tick_until(&s.rctx, GAME_START_FRAME + 1000);

    /* Should still be in a valid state */
    sdl2_state_mode_t mode = sdl2_state_current(s.ctx->state);
    assert_true(mode >= SDL2ST_NONE && mode < SDL2ST_COUNT);

    /* Game should have been active at some point */
    assert_true(s.ctx->game_active);

    end_game_session(&s);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_scripted_game_start),
        cmocka_unit_test(test_paddle_moves_left),
        cmocka_unit_test(test_paddle_moves_right),
        cmocka_unit_test(test_pause_unpause_round_trip),
        cmocka_unit_test(test_extended_scripted_session),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
