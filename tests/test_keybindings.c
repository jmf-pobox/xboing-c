/*
 * test_keybindings.c — Keybinding integration tests.
 *
 * Injects synthetic SDL key events via sdl2_input_process_event,
 * calls the appropriate handler (game_input_global for global keys,
 * sdl2_state_update for gameplay keys), and asserts observable
 * game state changes.
 *
 * Covers every key binding shown on the Game Controls screen.
 *
 * Requires: SDL_VIDEODRIVER=dummy, SDL_AUDIODRIVER=dummy
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <cmocka.h>

#include "ball_system.h"
#include "game_context.h"
#include "game_init.h"
#include "game_input.h"
#include "message_system.h"
#include "paddle_system.h"
#include "sdl2_input.h"
#include "sdl2_loop.h"
#include "sdl2_state.h"
#include "sfx_system.h"

/* =========================================================================
 * Helpers
 * ========================================================================= */

static char arg_prog[] = "xboing_test";

static void inject_key(game_ctx_t *ctx, SDL_Scancode sc)
{
    sdl2_input_begin_frame(ctx->input);
    SDL_Event e = {0};
    e.type = SDL_KEYDOWN;
    e.key.keysym.scancode = sc;
    e.key.repeat = 0;
    sdl2_input_process_event(ctx->input, &e);
}

static void drain_sdl_quit_events(void)
{
    SDL_Event discard;
    SDL_PumpEvents();
    while (SDL_PeepEvents(&discard, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) > 0)
    {
    }
}

/* =========================================================================
 * Fixtures
 * ========================================================================= */

typedef struct
{
    game_ctx_t *ctx;
} kb_fixture_t;

static int setup_attract(void **vstate)
{
    kb_fixture_t *f = calloc(1, sizeof(*f));
    assert_non_null(f);
    char *argv[] = {arg_prog, NULL};
    f->ctx = game_create(1, argv);
    assert_non_null(f->ctx);
    sdl2_state_transition(f->ctx->state, SDL2ST_PRESENTS);
    sdl2_state_transition(f->ctx->state, SDL2ST_INTRO);
    *vstate = f;
    return 0;
}

static int setup_game(void **vstate)
{
    kb_fixture_t *f = calloc(1, sizeof(*f));
    assert_non_null(f);
    char *argv[] = {arg_prog, NULL};
    f->ctx = game_create(1, argv);
    assert_non_null(f->ctx);
    f->ctx->config.use_keys = true;
    sdl2_state_transition(f->ctx->state, SDL2ST_PRESENTS);
    sdl2_state_transition(f->ctx->state, SDL2ST_GAME);
    *vstate = f;
    return 0;
}

static int setup_editor(void **vstate)
{
    kb_fixture_t *f = calloc(1, sizeof(*f));
    assert_non_null(f);
    char *argv[] = {arg_prog, NULL};
    f->ctx = game_create(1, argv);
    assert_non_null(f->ctx);
    sdl2_state_transition(f->ctx->state, SDL2ST_PRESENTS);
    sdl2_state_transition(f->ctx->state, SDL2ST_EDIT);
    *vstate = f;
    return 0;
}

static int teardown(void **vstate)
{
    kb_fixture_t *f = (kb_fixture_t *)*vstate;
    game_destroy(f->ctx);
    free(f);
    return 0;
}

/* =========================================================================
 * Group 1: Global keys (game_input_global, attract mode)
 * ========================================================================= */

static void test_global_sfx_toggle_off(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    assert_int_equal(sfx_system_get_enabled(ctx->sfx), 1);
    inject_key(ctx, SDL_SCANCODE_S);
    game_input_global(ctx);
    assert_int_equal(sfx_system_get_enabled(ctx->sfx), 0);
}

static void test_global_sfx_toggle_on(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    sfx_system_set_enabled(ctx->sfx, 0);
    inject_key(ctx, SDL_SCANCODE_S);
    game_input_global(ctx);
    assert_int_equal(sfx_system_get_enabled(ctx->sfx), 1);
}

static void test_global_speed_1(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    inject_key(ctx, SDL_SCANCODE_1);
    game_input_global(ctx);
    assert_int_equal(sdl2_loop_get_speed(ctx->loop), 1);
}

static void test_global_speed_5(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    inject_key(ctx, SDL_SCANCODE_5);
    game_input_global(ctx);
    assert_int_equal(sdl2_loop_get_speed(ctx->loop), 5);
}

static void test_global_speed_9(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    inject_key(ctx, SDL_SCANCODE_9);
    game_input_global(ctx);
    assert_int_equal(sdl2_loop_get_speed(ctx->loop), 9);
}

static void test_global_control_toggle(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    bool before = ctx->config.use_keys;
    inject_key(ctx, SDL_SCANCODE_G);
    game_input_global(ctx);
    assert_int_not_equal(ctx->config.use_keys, before);
}

static void test_global_fullscreen_no_crash(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    inject_key(ctx, SDL_SCANCODE_I);
    game_input_global(ctx);
    /* SDL dummy driver — no observable fullscreen change, just no crash */
}

static void test_global_quit_pushes_event(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    drain_sdl_quit_events();
    inject_key(ctx, SDL_SCANCODE_Q);
    game_input_global(ctx);
    SDL_PumpEvents();
    SDL_Event found;
    int count = SDL_PeepEvents(&found, 1, SDL_PEEKEVENT, SDL_QUIT, SDL_QUIT);
    assert_true(count > 0);
    drain_sdl_quit_events();
}

/* =========================================================================
 * Group 2: Mode scoping (keys that must NOT fire outside attract)
 * ========================================================================= */

static void test_sfx_blocked_in_game(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    assert_int_equal(sfx_system_get_enabled(ctx->sfx), 1);
    inject_key(ctx, SDL_SCANCODE_S);
    game_input_global(ctx);
    assert_int_equal(sfx_system_get_enabled(ctx->sfx), 1);
}

static void test_sfx_blocked_in_editor(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    assert_int_equal(sfx_system_get_enabled(ctx->sfx), 1);
    inject_key(ctx, SDL_SCANCODE_S);
    game_input_global(ctx);
    assert_int_equal(sfx_system_get_enabled(ctx->sfx), 1);
}

static void test_speed_blocked_in_game(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    int before = sdl2_loop_get_speed(ctx->loop);
    inject_key(ctx, SDL_SCANCODE_3);
    game_input_global(ctx);
    assert_int_equal(sdl2_loop_get_speed(ctx->loop), before);
}

static void test_quit_blocked_in_editor(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    drain_sdl_quit_events();
    inject_key(ctx, SDL_SCANCODE_Q);
    game_input_global(ctx);
    SDL_PumpEvents();
    SDL_Event found;
    int count = SDL_PeepEvents(&found, 1, SDL_PEEKEVENT, SDL_QUIT, SDL_QUIT);
    assert_int_equal(count, 0);
}

/* =========================================================================
 * Group 3: Gameplay keys (sdl2_state_update in GAME mode)
 * ========================================================================= */

static void test_gameplay_pause(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    inject_key(ctx, SDL_SCANCODE_P);
    sdl2_state_update(ctx->state);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_PAUSE);
}

static void test_gameplay_editor(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    inject_key(ctx, SDL_SCANCODE_E);
    sdl2_state_update(ctx->state);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_EDIT);
}

static void test_gameplay_paddle_left(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    int before = paddle_system_get_pos(ctx->paddle);
    inject_key(ctx, SDL_SCANCODE_J);
    for (int i = 0; i < 6; i++)
        sdl2_state_update(ctx->state);
    int after = paddle_system_get_pos(ctx->paddle);
    assert_true(after < before);
}

static void test_gameplay_paddle_right(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    int before = paddle_system_get_pos(ctx->paddle);
    inject_key(ctx, SDL_SCANCODE_L);
    for (int i = 0; i < 6; i++)
        sdl2_state_update(ctx->state);
    int after = paddle_system_get_pos(ctx->paddle);
    assert_true(after > before);
}

static void test_gameplay_paddle_left_arrow(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    int before = paddle_system_get_pos(ctx->paddle);
    inject_key(ctx, SDL_SCANCODE_LEFT);
    for (int i = 0; i < 6; i++)
        sdl2_state_update(ctx->state);
    int after = paddle_system_get_pos(ctx->paddle);
    assert_true(after < before);
}

static void test_gameplay_paddle_right_arrow(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    int before = paddle_system_get_pos(ctx->paddle);
    inject_key(ctx, SDL_SCANCODE_RIGHT);
    for (int i = 0; i < 6; i++)
        sdl2_state_update(ctx->state);
    int after = paddle_system_get_pos(ctx->paddle);
    assert_true(after > before);
}

static void test_gameplay_paddle_blocked_in_mouse_mode(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    ctx->config.use_keys = false;
    int before = paddle_system_get_pos(ctx->paddle);
    inject_key(ctx, SDL_SCANCODE_J);
    sdl2_state_update(ctx->state);
    int after = paddle_system_get_pos(ctx->paddle);
    assert_int_equal(after, before);
    ctx->config.use_keys = true;
}

/* =========================================================================
 * Group 4: Attract navigation
 * ========================================================================= */

static void test_attract_c_cycles_screen(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_INTRO);
    inject_key(ctx, SDL_SCANCODE_C);
    game_input_global(ctx);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_INSTRUCT);
}

static void test_attract_c_full_cycle_order(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;

    /* Matches natural callback chain: game_callbacks.c keys_cb_on_finished,
     * demo_cb_on_finished(PREVIEW), highscore_cb_on_finished. */
    static const sdl2_state_mode_t expected[] = {
        SDL2ST_INSTRUCT, SDL2ST_DEMO,      SDL2ST_KEYS,    SDL2ST_KEYSEDIT,
        SDL2ST_PREVIEW,  SDL2ST_HIGHSCORE,  SDL2ST_INTRO,
    };

    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_INTRO);
    for (int i = 0; i < 7; i++)
    {
        inject_key(ctx, SDL_SCANCODE_C);
        game_input_global(ctx);
        assert_int_equal(sdl2_state_current(ctx->state), expected[i]);
    }
}

/* =========================================================================
 * Test registration
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest global_tests[] = {
        cmocka_unit_test_setup_teardown(test_global_sfx_toggle_off, setup_attract, teardown),
        cmocka_unit_test_setup_teardown(test_global_sfx_toggle_on, setup_attract, teardown),
        cmocka_unit_test_setup_teardown(test_global_speed_1, setup_attract, teardown),
        cmocka_unit_test_setup_teardown(test_global_speed_5, setup_attract, teardown),
        cmocka_unit_test_setup_teardown(test_global_speed_9, setup_attract, teardown),
        cmocka_unit_test_setup_teardown(test_global_control_toggle, setup_attract, teardown),
        cmocka_unit_test_setup_teardown(test_global_fullscreen_no_crash, setup_attract, teardown),
        cmocka_unit_test_setup_teardown(test_global_quit_pushes_event, setup_attract, teardown),
    };

    const struct CMUnitTest scoping_tests[] = {
        cmocka_unit_test_setup_teardown(test_sfx_blocked_in_game, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_sfx_blocked_in_editor, setup_editor, teardown),
        cmocka_unit_test_setup_teardown(test_speed_blocked_in_game, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_quit_blocked_in_editor, setup_editor, teardown),
    };

    const struct CMUnitTest gameplay_tests[] = {
        cmocka_unit_test_setup_teardown(test_gameplay_pause, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_gameplay_editor, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_gameplay_paddle_left, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_gameplay_paddle_right, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_gameplay_paddle_left_arrow, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_gameplay_paddle_right_arrow, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_gameplay_paddle_blocked_in_mouse_mode, setup_game,
                                        teardown),
    };

    const struct CMUnitTest attract_tests[] = {
        cmocka_unit_test_setup_teardown(test_attract_c_cycles_screen, setup_attract, teardown),
        cmocka_unit_test_setup_teardown(test_attract_c_full_cycle_order, setup_attract, teardown),
    };

    int rc = 0;
    rc |= cmocka_run_group_tests_name("global keys", global_tests, NULL, NULL);
    rc |= cmocka_run_group_tests_name("mode scoping", scoping_tests, NULL, NULL);
    rc |= cmocka_run_group_tests_name("gameplay keys", gameplay_tests, NULL, NULL);
    rc |= cmocka_run_group_tests_name("attract navigation", attract_tests, NULL, NULL);
    return rc;
}
