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
#include "block_system.h"
#include "editor_system.h"
#include "game_callbacks.h"
#include "game_context.h"
#include "game_init.h"
#include "game_input.h"
#include "message_system.h"
#include "paddle_system.h"
#include "sdl2_input.h"
#include "sdl2_loop.h"
#include "sdl2_audio.h"
#include "sdl2_renderer.h"
#include "sdl2_state.h"
#include "sfx_system.h"
#include "special_system.h"

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

/* Same as inject_key but sets the SDL key modifier mask -- needed to
 * distinguish Shift+'=' ('+', volume up) from bare '=' (debug skip-level
 * cheat), since both share SDL_SCANCODE_EQUALS on a US keyboard layout. */
static void inject_key_mod(game_ctx_t *ctx, SDL_Scancode sc, Uint16 mod)
{
    sdl2_input_begin_frame(ctx->input);
    SDL_Event e = {0};
    e.type = SDL_KEYDOWN;
    e.key.keysym.scancode = sc;
    e.key.keysym.mod = mod;
    e.key.repeat = 0;
    sdl2_input_process_event(ctx->input, &e);
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

/* I minimizes (iconifies) the window -- original/main.c:853-856
 * (XIconifyWindow) -- it must NOT toggle fullscreen (mission
 * m-2026-07-17-014, bead 2z1). Before the fix, I called
 * sdl2_renderer_toggle_fullscreen. */
static void test_iconify_does_not_toggle_fullscreen(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    assert_false(sdl2_renderer_is_fullscreen(ctx->renderer));
    inject_key(ctx, SDL_SCANCODE_I);
    game_input_global(ctx);
    /* No crash under the SDL dummy driver, and still not fullscreen. */
    assert_false(sdl2_renderer_is_fullscreen(ctx->renderer));
}

static void test_global_quit_opens_dialogue(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    inject_key(ctx, SDL_SCANCODE_Q);
    game_input_global(ctx);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_DIALOGUE);
}

static void test_quit_blocked_during_dialogue(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    inject_key(ctx, SDL_SCANCODE_Q);
    game_input_global(ctx);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_DIALOGUE);
    inject_key(ctx, SDL_SCANCODE_Q);
    game_input_global(ctx);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_DIALOGUE);
}

static void test_global_set_level_opens_dialogue(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    inject_key(ctx, SDL_SCANCODE_W);
    game_input_global(ctx);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_DIALOGUE);
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
    inject_key(ctx, SDL_SCANCODE_Q);
    game_input_global(ctx);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_EDIT);
}

/* =========================================================================
 * Group 3: Gameplay keys (sdl2_state_update in GAME mode)
 * ========================================================================= */

static void test_gameplay_pause(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    inject_key(ctx, SDL_SCANCODE_P);
    game_input_global(ctx);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_PAUSE);
}

/* E must NOT open the editor from live gameplay -- original binds E only
 * in handleIntroKeys (attract screens, original/main.c:676-681);
 * handleGameKeys (original/main.c:430-533) has no E case at all
 * (mission m-2026-07-17-014, bead 1xd). */
static void test_gameplay_editor_e_is_noop(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    inject_key(ctx, SDL_SCANCODE_E);
    game_input_global(ctx);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_GAME);
}

/* Positive sibling: E from an attract screen still enters the editor. */
static void test_attract_e_enters_editor(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_INTRO);
    inject_key(ctx, SDL_SCANCODE_E);
    game_input_global(ctx);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_EDIT);
}

static void test_gameplay_paddle_left(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    int before = paddle_system_get_pos(ctx->paddle);
    inject_key(ctx, SDL_SCANCODE_J);
    sdl2_state_update(ctx->state);
    int after = paddle_system_get_pos(ctx->paddle);
    assert_true(after < before);
}

static void test_gameplay_paddle_right(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    int before = paddle_system_get_pos(ctx->paddle);
    inject_key(ctx, SDL_SCANCODE_L);
    sdl2_state_update(ctx->state);
    int after = paddle_system_get_pos(ctx->paddle);
    assert_true(after > before);
}

static void test_gameplay_paddle_left_arrow(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    int before = paddle_system_get_pos(ctx->paddle);
    inject_key(ctx, SDL_SCANCODE_LEFT);
    sdl2_state_update(ctx->state);
    int after = paddle_system_get_pos(ctx->paddle);
    assert_true(after < before);
}

static void test_gameplay_paddle_right_arrow(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    int before = paddle_system_get_pos(ctx->paddle);
    inject_key(ctx, SDL_SCANCODE_RIGHT);
    sdl2_state_update(ctx->state);
    int after = paddle_system_get_pos(ctx->paddle);
    assert_true(after > before);
}

static void tick_until_ball_ready(game_ctx_t *ctx)
{
    for (int i = 0; i < 100; i++)
    {
        sdl2_input_begin_frame(ctx->input);
        sdl2_state_update(ctx->state);
        if (ball_system_get_active_index(ctx->ball) == -1 &&
            ball_system_is_ball_waiting(ctx->ball))
            break;
    }
}

static void launch_ball(game_ctx_t *ctx)
{
    tick_until_ball_ready(ctx);
    ball_system_env_t env = game_callbacks_ball_env(ctx);
    int rc = ball_system_activate_waiting(ctx->ball, &env);
    assert_true(rc >= 0);
}

static void test_gameplay_kill_ball(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    launch_ball(ctx);
    assert_true(ball_system_get_active_count(ctx->ball) > 0);
    inject_key(ctx, SDL_SCANCODE_D);
    game_input_global(ctx);
    assert_int_equal(ball_system_get_active_index(ctx->ball), -1);
}

static void test_gameplay_tilt(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    launch_ball(ctx);
    int before_tilts = ctx->user_tilts;
    inject_key(ctx, SDL_SCANCODE_T);
    game_input_global(ctx);
    assert_int_equal(ctx->user_tilts, before_tilts + 1);
}

static void test_gameplay_tilt_max_reached(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    launch_ball(ctx);
    ctx->user_tilts = GAME_MAX_TILTS;
    inject_key(ctx, SDL_SCANCODE_T);
    game_input_global(ctx);
    assert_int_equal(ctx->user_tilts, GAME_MAX_TILTS);
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

static void test_global_audio_toggle(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    if (!ctx->audio)
    {
        skip();
        return;
    }
    assert_false(sdl2_audio_is_muted(ctx->audio));
    inject_key(ctx, SDL_SCANCODE_A);
    game_input_global(ctx);
    assert_true(sdl2_audio_is_muted(ctx->audio));
    inject_key(ctx, SDL_SCANCODE_A);
    game_input_global(ctx);
    assert_false(sdl2_audio_is_muted(ctx->audio));
}

static void test_global_highscore_h_global(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    inject_key(ctx, SDL_SCANCODE_H);
    game_input_global(ctx);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_HIGHSCORE);
    assert_int_equal(ctx->highscore_request_type, HIGHSCORE_TYPE_GLOBAL);
}

static void test_global_highscore_H_personal(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    sdl2_input_begin_frame(ctx->input);
    SDL_Event e = {0};
    e.type = SDL_KEYDOWN;
    e.key.keysym.scancode = SDL_SCANCODE_H;
    e.key.keysym.mod = KMOD_LSHIFT;
    e.key.repeat = 0;
    sdl2_input_process_event(ctx->input, &e);
    game_input_global(ctx);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_HIGHSCORE);
    assert_int_equal(ctx->highscore_request_type, HIGHSCORE_TYPE_PERSONAL);
}

static void test_attract_cycle_table(void **vstate)
{
    (void)vstate;
    assert_int_equal(game_callbacks_attract_next(SDL2ST_INTRO), SDL2ST_INSTRUCT);
    assert_int_equal(game_callbacks_attract_next(SDL2ST_INSTRUCT), SDL2ST_DEMO);
    assert_int_equal(game_callbacks_attract_next(SDL2ST_DEMO), SDL2ST_KEYS);
    assert_int_equal(game_callbacks_attract_next(SDL2ST_KEYS), SDL2ST_KEYSEDIT);
    assert_int_equal(game_callbacks_attract_next(SDL2ST_KEYSEDIT), SDL2ST_PREVIEW);
    assert_int_equal(game_callbacks_attract_next(SDL2ST_PREVIEW), SDL2ST_HIGHSCORE);
    assert_int_equal(game_callbacks_attract_next(SDL2ST_HIGHSCORE), SDL2ST_INTRO);
    assert_int_equal(game_callbacks_attract_next(SDL2ST_GAME), SDL2ST_NONE);
    assert_int_equal(game_callbacks_attract_next(SDL2ST_NONE), SDL2ST_NONE);
}

static void test_attract_c_cycles_screen(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_INTRO);
    inject_key(ctx, SDL_SCANCODE_C);
    game_input_global(ctx);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_INSTRUCT);
}

/* The C cycle key out of a game-over Highscore must clear game_active — the
 * same single-source clear (mode_intro_enter) the finish-timer relies on — so
 * the flag does not leak into the attract screens (SafeAttract).  Without it,
 * the next Highscore's Space would return to Intro instead of starting a game.
 * Proof: docs/specs/2026-07-04-screen-state-machine.tex, invariant SafeAttract. */
static void test_highscore_c_cycle_clears_game_active(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    /* Enter Highscore via the real game-over path: game_active true and
     * score already submitted (so the enter handler skips submission). */
    ctx->game_active = true;
    ctx->score_submitted = true;
    sdl2_state_transition(ctx->state, SDL2ST_HIGHSCORE);

    inject_key(ctx, SDL_SCANCODE_C);
    game_input_global(ctx);

    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_INTRO);
    assert_false(ctx->game_active);
}

/* Space on a game-over Highscore returns to the title and must clear
 * game_active.  Since ADR-055 removed the handler's own clear (game_input.c),
 * this now regression-guards the sole clear site (mode_intro_enter) for the
 * Space-return path, alongside the timer and C-key tests. */
static void test_highscore_space_return_clears_game_active(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    /* Enter Highscore via the real game-over path: game_active true and
     * score already submitted (so the enter handler skips submission). */
    ctx->game_active = true;
    ctx->score_submitted = true;
    sdl2_state_transition(ctx->state, SDL2ST_HIGHSCORE);

    inject_key(ctx, SDL_SCANCODE_SPACE);
    game_input_global(ctx);

    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_INTRO);
    assert_false(ctx->game_active);
}

/* Space on the title advances to the instructions screen, not into the game.
 * Handled once per frame in game_input_global (like the C-cycle key). */
static void test_title_space_goes_to_instructions(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_INTRO);
    inject_key(ctx, SDL_SCANCODE_SPACE);
    game_input_global(ctx);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_INSTRUCT);
}

/* A single space press must NOT cascade past instructions into the game, even
 * across many state-machine ticks in one frame (the first-frame catch-up
 * burst).  Regression for the per-tick multi-fire bug. */
static void test_title_space_single_press_no_cascade(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_INTRO);
    inject_key(ctx, SDL_SCANCODE_SPACE);
    game_input_global(ctx);
    /* Same held edge, many ticks in the frame: must stay on instructions. */
    for (int i = 0; i < 20; i++)
        sdl2_state_update(ctx->state);
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
 * Group 5: Editor window width round-trip (bead xboing-di8)
 *
 * mode_edit_enter widens the logical canvas to
 * SDL2R_LOGICAL_WIDTH + EDITOR_TOOL_WIDTH (695) to reveal the tool
 * palette; mode_edit_exit restores SDL2R_LOGICAL_WIDTH (575).  Every
 * transition into or out of EDIT goes through sdl2_state_transition's
 * on_exit/on_enter pair (src/sdl2_state.c), so this exercises the
 * production callback path directly — no keystroke injection needed.
 * See docs/specs/2026-07-11-editor-window-width.md "Verification plan".
 * ========================================================================= */

static int logical_width(const game_ctx_t *ctx)
{
    int w = 0, h = 0;
    sdl2_renderer_get_logical_size(ctx->renderer, &w, &h);
    (void)h;
    return w;
}

/* Editor entry (setup_editor: PRESENTS -> EDIT) widens to 695. */
static void test_editor_enter_widens_logical_width(void **vstate)
{
    const game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    assert_int_equal(logical_width(ctx), SDL2R_LOGICAL_WIDTH + EDITOR_TOOL_WIDTH);
}

/* EDIT -> INTRO restores to 575. */
static void test_editor_exit_restores_logical_width(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    assert_int_equal(logical_width(ctx), SDL2R_LOGICAL_WIDTH + EDITOR_TOOL_WIDTH);

    sdl2_state_transition(ctx->state, SDL2ST_INTRO);
    assert_int_equal(logical_width(ctx), SDL2R_LOGICAL_WIDTH);
}

/* Full round trip: INTRO -> EDIT -> GAME (playtest) -> EDIT (playtest end)
 * -> INTRO, asserting the logical width at every hop. */
static void test_editor_playtest_round_trip_logical_width(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;

    /* setup_editor already performed PRESENTS -> EDIT; confirm widened. */
    assert_int_equal(logical_width(ctx), SDL2R_LOGICAL_WIDTH + EDITOR_TOOL_WIDTH);

    /* EDIT -> GAME (playtest start): shrinks back to 575, matching
     * normal gameplay. */
    sdl2_state_transition(ctx->state, SDL2ST_GAME);
    assert_int_equal(logical_width(ctx), SDL2R_LOGICAL_WIDTH);

    /* GAME -> EDIT (playtest end): widens back to 695. */
    sdl2_state_transition(ctx->state, SDL2ST_EDIT);
    assert_int_equal(logical_width(ctx), SDL2R_LOGICAL_WIDTH + EDITOR_TOOL_WIDTH);

    /* EDIT -> INTRO (finish): restores to 575. */
    sdl2_state_transition(ctx->state, SDL2ST_INTRO);
    assert_int_equal(logical_width(ctx), SDL2R_LOGICAL_WIDTH);
}

/* =========================================================================
 * Group 6: Play-test P/Escape must end play-test, not pause/abort
 * (bead xboing-lpi) -- drives the REAL game_input.c handlers where the
 * bug lived, not a direct editor_system_key_input(EDITOR_KEY_PLAYTEST)
 * call.  The original regression test called editor_system_key_input
 * directly, so it never exercised game_input_global's SDL2I_PAUSE/
 * SDL2I_ABORT branches (src/game_input.c:145-153, :328-340) -- the
 * exact live-input path that had to check ctx->play_test_active before
 * routing to SDL2ST_PAUSE/the abort confirmation dialogue.
 * ========================================================================= */

/* Draws one block, enters play-test (setup, not the assertion), then
 * injects 'p' through the SAME path a real player's keypress takes:
 * inject_key + game_input_global.  Before the fix this fell through to
 * the "mode == SDL2ST_GAME" branch and transitioned to SDL2ST_PAUSE
 * instead of ending play-test. */
static void test_playtest_p_ends_via_live_game_input(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;

    editor_system_select_palette(ctx->editor, 0);
    editor_system_mouse_button(ctx->editor, 192, 80, 1, 1);
    editor_system_mouse_button(ctx->editor, 192, 80, 1, 0);
    assert_true(block_system_is_occupied(ctx->block, 2, 3));

    editor_system_key_input(ctx->editor, EDITOR_KEY_PLAYTEST);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_GAME);
    assert_true(ctx->play_test_active);

    inject_key(ctx, SDL_SCANCODE_P);
    game_input_global(ctx);

    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_EDIT);
    assert_false(ctx->play_test_active);
    /* Pre-test board restored -- proves editor_cb_on_playtest_end ran,
     * not a silent fall-through that left play-test state untouched. */
    assert_true(block_system_is_occupied(ctx->block, 2, 3));
}

/* Sibling case: a REAL game (play_test_active == false) must still pause
 * on 'p' -- the fix must not regress ordinary in-game pause. */
static void test_real_game_p_still_pauses(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    assert_false(ctx->play_test_active);

    inject_key(ctx, SDL_SCANCODE_P);
    game_input_global(ctx);

    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_PAUSE);
}

/* Escape during play-test must also end it via the live abort path
 * (src/game_input.c SDL2I_ABORT), not open the "Abort current game?"
 * confirmation dialogue. */
static void test_playtest_escape_ends_via_live_game_input(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;

    editor_system_key_input(ctx->editor, EDITOR_KEY_PLAYTEST);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_GAME);
    assert_true(ctx->play_test_active);

    inject_key(ctx, SDL_SCANCODE_ESCAPE);
    game_input_global(ctx);

    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_EDIT);
    assert_false(ctx->play_test_active);
}

/* =========================================================================
 * Group 7: FULL-FRAME regression for the same-frame double-fire
 * (bead xboing-1ir) -- the shape that actually catches the bug.
 *
 * Group 6's tests above call only game_input_global and never run the
 * SAME frame's editor tick, so they could NOT have caught xboing-1ir:
 * just_pressed stays true for the rest of the frame once set, and the
 * real loop (src/game_main.c:88-158) always runs game_input_global THEN
 * the per-tick update (sdl2_loop_update -> ... -> sdl2_state_update)
 * before the next begin_frame clears edge state.  Before the fix, ending
 * play-test in game_input_global left SDL2I_PAUSE/SDL2I_ABORT still
 * just_pressed; the following mode_edit_update (current mode is already
 * SDL2ST_EDIT by then) re-read the still-pressed key and re-fired --
 * P restarted play-test (-> SDL2ST_GAME, src/game_modes.c:1588-1589),
 * Escape hit the editor's quit path (-> SDL2ST_INTRO,
 * src/game_modes.c:1563-1581).  The fix (sdl2_input_consume,
 * src/game_input.c:152 and :340) clears the action right after it ends
 * play-test so the following mode_edit_update sees just_pressed == false.
 *
 * Per-frame call order below matches src/game_main.c exactly: ONE
 * begin_frame (inside inject_key) + process, THEN game_input_global,
 * THEN sdl2_state_update -- no begin_frame between the last two calls,
 * because a real frame only clears edge triggers once, at the top.
 * ========================================================================= */

/* 'p' full-frame: draw a block, enter play-test, then in the SAME frame
 * end it via game_input_global and immediately run the editor's per-tick
 * update.  Without sdl2_input_consume this regresses to SDL2ST_GAME (P
 * re-read by mode_edit_update's "P = playtest" branch,
 * src/game_modes.c:1588-1589) -- a flash restart of play-test the player
 * never asked for. */
static void test_playtest_p_full_frame_no_restart(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;

    editor_system_select_palette(ctx->editor, 0);
    editor_system_mouse_button(ctx->editor, 192, 80, 1, 1);
    editor_system_mouse_button(ctx->editor, 192, 80, 1, 0);
    assert_true(block_system_is_occupied(ctx->block, 2, 3));

    editor_system_key_input(ctx->editor, EDITOR_KEY_PLAYTEST);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_GAME);
    assert_true(ctx->play_test_active);

    /* ONE frame: begin_frame + process 'p' (inject_key), then the two
     * real per-frame calls in the real order -- no begin_frame between
     * them, so just_pressed(P) is still whatever game_input_global left
     * it as when mode_edit_update runs. */
    inject_key(ctx, SDL_SCANCODE_P);
    game_input_global(ctx);
    sdl2_state_update(ctx->state);

    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_EDIT);
    assert_false(ctx->play_test_active);
    /* Pre-test board still restored -- the same-frame editor tick did not
     * re-enter play-test and re-hide it. */
    assert_true(block_system_is_occupied(ctx->block, 2, 3));
}

/* Escape full-frame sibling: without sdl2_input_consume this regresses to
 * SDL2ST_INTRO (Escape re-read by mode_edit_update's QUIT/ABORT branch,
 * src/game_modes.c:1563-1581, which force-exits to INTRO when no dialogue
 * opened and the editor isn't already FINISH) instead of staying on the
 * editor screen the player just returned to. */
static void test_playtest_escape_full_frame_no_reopen(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;

    editor_system_key_input(ctx->editor, EDITOR_KEY_PLAYTEST);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_GAME);
    assert_true(ctx->play_test_active);

    inject_key(ctx, SDL_SCANCODE_ESCAPE);
    game_input_global(ctx);
    sdl2_state_update(ctx->state);

    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_EDIT);
    assert_false(ctx->play_test_active);
}

/* =========================================================================
 * Group 8: Q must do nothing during play-test (bead xboing-3h3)
 *
 * game_input.c:397-398 guards the SDL2I_QUIT dialogue open with
 * `!ctx->play_test_active` (comment cites original/editor.c:992-1030 --
 * EDIT_TEST's key switch only handles p/paddle/shoot and returns before
 * reaching the Q exit-dialogue case, so the original silently swallows Q
 * during play-test too).  Drives the same full-frame shape as
 * test_playtest_p_full_frame_no_restart: ONE begin_frame (inside
 * inject_key) + game_input_global + sdl2_state_update, no begin_frame in
 * between, matching src/game_main.c's real per-frame call order.
 * ========================================================================= */

static void test_playtest_q_full_frame_no_dialogue(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;

    editor_system_key_input(ctx->editor, EDITOR_KEY_PLAYTEST);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_GAME);
    assert_true(ctx->play_test_active);

    inject_key(ctx, SDL_SCANCODE_Q);
    game_input_global(ctx);
    sdl2_state_update(ctx->state);

    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_GAME);
    assert_true(ctx->play_test_active);
}

/* Sibling: a REAL game (play_test_active == false) must still open the
 * quit confirmation dialogue on Q -- proves the play-test guard didn't
 * regress ordinary in-game quit. */
static void test_real_game_q_opens_dialogue(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    assert_false(ctx->play_test_active);

    inject_key(ctx, SDL_SCANCODE_Q);
    game_input_global(ctx);

    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_DIALOGUE);
}

/* =========================================================================
 * Group 9: '=' debug skip-level cheat + Shift-aware keymap
 * (mission m-2026-07-16-015) -- SDL2I_DEBUG_SKIP shares SDL_SCANCODE_EQUALS
 * with Shift+'=' ('+'); game_input_global branches on
 * sdl2_input_shift_held(): unshifted '=' in SDL2ST_GAME explodes required
 * blocks only when ctx->debug_mode is set (src/game_input.c:248-339);
 * Shift+'=' always routes to volume up, never the cheat.
 *
 * Each test clears the grid and seeds a deterministic board (independent
 * of whichever level the fixture happened to load), so the assertions do
 * not depend on level-file content.
 * ========================================================================= */

static void seed_required_and_special_blocks(game_ctx_t *ctx)
{
    assert_int_equal(block_system_clear_all(ctx->block), BLOCK_SYS_OK);
    assert_int_equal(block_system_add(ctx->block, 0, 0, RED_BLK, 0, 0), BLOCK_SYS_OK);
    assert_int_equal(block_system_add(ctx->block, 0, 1, BLUE_BLK, 0, 0), BLOCK_SYS_OK);
    /* BLACK_BLK is not a "required" type (block_system_still_active only
     * counts color blocks, COUNTER_BLK, DROP_BLK) and is absent from the
     * cheat's explode switch (src/game_input.c:289-296) -- it must survive
     * the skip-level cheat untouched. */
    assert_int_equal(block_system_add(ctx->block, 0, 2, BLACK_BLK, 0, 0), BLOCK_SYS_OK);
    assert_true(block_system_still_active(ctx->block));
    assert_int_equal(block_system_get_exploding_count(ctx->block), 0);
}

/* Unshifted '=' with debug_mode on: required blocks (RED, BLUE) enter the
 * explosion animation; the BLACK_BLK special survives untouched. Driving
 * the explosion state machine to finalize (frame, +10, +20, +30 -- see
 * test_update_advances_one_stage_per_tick_at_delay in
 * tests/test_block_system_explosion.c) proves the cheat completes the
 * level through the normal per-tick path, not a forced state change. */
static void test_debug_skip_cheats_when_debug(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    ctx->debug_mode = true;
    seed_required_and_special_blocks(ctx);

    int frame = (int)sdl2_state_frame(ctx->state);
    inject_key(ctx, SDL_SCANCODE_EQUALS);
    game_input_global(ctx);

    assert_int_equal(block_system_get_exploding_count(ctx->block), 2);
    assert_true(block_system_is_occupied(ctx->block, 0, 0));
    assert_true(block_system_is_occupied(ctx->block, 0, 1));
    assert_true(block_system_is_occupied(ctx->block, 0, 2));
    assert_int_equal(block_system_get_type(ctx->block, 0, 2), BLACK_BLK);

    block_system_update_explosions(ctx->block, frame, NULL, NULL);
    block_system_update_explosions(ctx->block, frame + 10, NULL, NULL);
    block_system_update_explosions(ctx->block, frame + 20, NULL, NULL);
    block_system_update_explosions(ctx->block, frame + 30, NULL, NULL);

    assert_int_equal(block_system_get_exploding_count(ctx->block), 0);
    assert_false(block_system_is_occupied(ctx->block, 0, 0));
    assert_false(block_system_is_occupied(ctx->block, 0, 1));
    /* Required blocks are gone; only the surviving special remains, so the
     * level no longer reports required blocks outstanding. */
    assert_false(block_system_still_active(ctx->block));
    assert_true(block_system_is_occupied(ctx->block, 0, 2));
}

/* The discriminating debug-gate test: same setup, ctx->debug_mode == false.
 * Unshifted '=' must not touch the grid at all -- no explosions armed, no
 * blocks disturbed. */
static void test_debug_skip_blocked_without_debug(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    ctx->debug_mode = false;
    seed_required_and_special_blocks(ctx);

    inject_key(ctx, SDL_SCANCODE_EQUALS);
    game_input_global(ctx);

    assert_int_equal(block_system_get_exploding_count(ctx->block), 0);
    assert_true(block_system_is_occupied(ctx->block, 0, 0));
    assert_int_equal(block_system_get_type(ctx->block, 0, 0), RED_BLK);
    assert_true(block_system_is_occupied(ctx->block, 0, 1));
    assert_int_equal(block_system_get_type(ctx->block, 0, 1), BLUE_BLK);
    assert_true(block_system_is_occupied(ctx->block, 0, 2));
    assert_int_equal(block_system_get_type(ctx->block, 0, 2), BLACK_BLK);
    assert_true(block_system_still_active(ctx->block));
}

/* Shift+'=' ('+') must always route to volume up, even with debug_mode on
 * -- it must never fall into the skip-level branch. */
static void test_shift_equals_is_volume_up(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    if (!ctx->audio)
    {
        skip();
        return;
    }
    ctx->debug_mode = true;
    seed_required_and_special_blocks(ctx);
    sdl2_audio_set_volume_percent(ctx->audio, 50);
    int before = sdl2_audio_get_volume_percent(ctx->audio);

    inject_key_mod(ctx, SDL_SCANCODE_EQUALS, KMOD_LSHIFT);
    game_input_global(ctx);

    assert_int_equal(sdl2_audio_get_volume_percent(ctx->audio), before + 1);
    assert_int_equal(block_system_get_exploding_count(ctx->block), 0);
    assert_true(block_system_is_occupied(ctx->block, 0, 0));
    assert_true(block_system_is_occupied(ctx->block, 0, 1));
}

/* Numpad '+' is the sole VOLUME_UP binding (no debug-skip ambiguity) and
 * must behave identically to Shift+'=': volume up, grid untouched. */
static void test_numpad_plus_is_volume_up(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;
    if (!ctx->audio)
    {
        skip();
        return;
    }
    ctx->debug_mode = true;
    seed_required_and_special_blocks(ctx);
    sdl2_audio_set_volume_percent(ctx->audio, 50);
    int before = sdl2_audio_get_volume_percent(ctx->audio);

    inject_key(ctx, SDL_SCANCODE_KP_PLUS);
    game_input_global(ctx);

    assert_int_equal(sdl2_audio_get_volume_percent(ctx->audio), before + 1);
    assert_int_equal(block_system_get_exploding_count(ctx->block), 0);
    assert_true(block_system_is_occupied(ctx->block, 0, 0));
    assert_true(block_system_is_occupied(ctx->block, 0, 1));
}

/* =========================================================================
 * Group 10: Sticky-paddle single-launch per press
 * (mission m-2026-07-17-014, bead c0q)
 *
 * input_launch_ball (src/game_input.c:70-87) runs per tick via
 * game_input_update -> mode_game_update, but just_pressed(SDL2I_START)
 * only clears once per FRAME (sdl2_input_begin_frame).  At high warp
 * sdl2_loop_update drives up to SDL2L_MAX_TICKS_PER_UPDATE (10) ticks
 * per real frame before the next begin_frame runs.  Without
 * sdl2_input_consume, one space press would call
 * ball_system_activate_waiting on every one of those ticks, and since
 * activate_waiting always promotes the FIRST BALL_READY slot it finds,
 * a sticky-paddle player holding 2+ balls would see them ALL launch off
 * one press instead of one at a time.
 *
 * Seeds BALL_READY balls directly via ball_system_restore (the same
 * seam savegame_system_restore uses -- see docs/TESTING.md's savegame
 * fixture pattern) rather than driving a full multiball sequence.
 * ball_system_restore sets nextFrame = current_frame + BIRTH_FRAME_RATE
 * (5) to suppress auto-activation on the immediate post-restore tick
 * (ball_system.h:292-298) -- so the burst below is capped at
 * BIRTH_FRAME_RATE-1 (4) ticks, not the full 10-tick real-world warp
 * burst, to keep the auto-activate timer from firing mid-test and
 * confounding the assertion.  4 ticks with 3 seeded balls is still
 * enough to distinguish "exactly one" from "all of them."
 * ========================================================================= */

static void seed_ready_ball(game_ctx_t *ctx, int index, int x)
{
    int frame = (int)sdl2_state_frame(ctx->state);
    ball_system_status_t rc =
        ball_system_restore(ctx->ball, index, frame, 1, BALL_READY, x, 500, 3, -3, BALL_NONE);
    assert_int_equal(rc, BALL_SYS_OK);
}

static void test_sticky_paddle_one_press_launches_exactly_one_ball(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;

    ball_system_clear_all(ctx->ball);
    special_system_set(ctx->special, SPECIAL_STICKY, 1);
    seed_ready_ball(ctx, 0, 200);
    seed_ready_ball(ctx, 1, 260);
    seed_ready_ball(ctx, 2, 320);

    assert_int_equal(ball_system_get_state(ctx->ball, 0), BALL_READY);
    assert_int_equal(ball_system_get_state(ctx->ball, 1), BALL_READY);
    assert_int_equal(ball_system_get_state(ctx->ball, 2), BALL_READY);

    /* ONE space press: begin_frame + process (inject_key), then several
     * per-tick updates in the SAME frame -- no begin_frame between them,
     * matching the real high-warp burst (src/game_main.c: one
     * begin_frame/game_input_global per real frame, N ticks via
     * sdl2_loop_update -> stub_tick -> sdl2_state_update). */
    inject_key(ctx, SDL_SCANCODE_SPACE);
    for (int i = 0; i < 4; i++)
        sdl2_state_update(ctx->state);

    int ready = 0;
    int active = 0;
    for (int i = 0; i < 3; i++)
    {
        enum BallStates s = ball_system_get_state(ctx->ball, i);
        if (s == BALL_READY)
            ready++;
        else if (s == BALL_ACTIVE)
            active++;
    }

    /* Pre-fix this activated all 3; the fix must leave exactly one
     * active and the other two still waiting. */
    assert_int_equal(active, 1);
    assert_int_equal(ready, 2);
}

/* Sibling: a single waiting ball still launches normally on one press --
 * the fix must not suppress the ordinary non-sticky single-ball case. */
static void test_single_waiting_ball_still_launches_on_one_press(void **vstate)
{
    game_ctx_t *ctx = ((kb_fixture_t *)*vstate)->ctx;

    ball_system_clear_all(ctx->ball);
    seed_ready_ball(ctx, 0, 200);
    assert_int_equal(ball_system_get_state(ctx->ball, 0), BALL_READY);

    inject_key(ctx, SDL_SCANCODE_SPACE);
    sdl2_state_update(ctx->state);

    assert_int_equal(ball_system_get_state(ctx->ball, 0), BALL_ACTIVE);
}

/* =========================================================================
 * Group 11: dialogue_key_from_sdl — pure keysym -> dialogue-action seam
 * (mission m-2026-07-17-016, extracted under m-2026-07-17-015 from the
 * inline switch in game_main.c's event loop).  Pure function, no game_ctx
 * needed.  Covers xboing-47p's Delete=Backspace alias
 * (original/dialogue.c:327-328).
 * ========================================================================= */

static void test_dialogue_key_delete_maps_to_backspace(void **vstate)
{
    (void)vstate;
    dialogue_key_type_t out = DIALOGUE_KEY_CHAR;
    bool rc = dialogue_key_from_sdl(SDLK_DELETE, &out);
    assert_true(rc);
    assert_int_equal(out, DIALOGUE_KEY_BACKSPACE);
}

/* Delete and Backspace must be indistinguishable to the dialogue system --
 * the alias is total, not partial. */
static void test_dialogue_key_backspace_maps_to_backspace(void **vstate)
{
    (void)vstate;
    dialogue_key_type_t out = DIALOGUE_KEY_CHAR;
    bool rc = dialogue_key_from_sdl(SDLK_BACKSPACE, &out);
    assert_true(rc);
    assert_int_equal(out, DIALOGUE_KEY_BACKSPACE);
}

static void test_dialogue_key_return(void **vstate)
{
    (void)vstate;
    dialogue_key_type_t out = DIALOGUE_KEY_CHAR;
    bool rc = dialogue_key_from_sdl(SDLK_RETURN, &out);
    assert_true(rc);
    assert_int_equal(out, DIALOGUE_KEY_RETURN);
}

static void test_dialogue_key_escape(void **vstate)
{
    (void)vstate;
    dialogue_key_type_t out = DIALOGUE_KEY_CHAR;
    bool rc = dialogue_key_from_sdl(SDLK_ESCAPE, &out);
    assert_true(rc);
    assert_int_equal(out, DIALOGUE_KEY_ESCAPE);
}

/* An unmapped key must return false and leave *out untouched -- callers
 * gate dispatch on the return value, not the out-param's contents. */
static void test_dialogue_key_unmapped_returns_false(void **vstate)
{
    (void)vstate;
    dialogue_key_type_t out = DIALOGUE_KEY_CHAR;
    bool rc = dialogue_key_from_sdl(SDLK_a, &out);
    assert_false(rc);
    assert_int_equal(out, DIALOGUE_KEY_CHAR);
}

/* NULL out must be rejected, not crash -- the function NULL-guards its
 * only pointer parameter. */
static void test_dialogue_key_null_out_returns_false(void **vstate)
{
    (void)vstate;
    bool rc = dialogue_key_from_sdl(SDLK_RETURN, NULL);
    assert_false(rc);
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
        cmocka_unit_test_setup_teardown(test_iconify_does_not_toggle_fullscreen, setup_attract,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_global_quit_opens_dialogue, setup_attract, teardown),
        cmocka_unit_test_setup_teardown(test_quit_blocked_during_dialogue, setup_attract, teardown),
        cmocka_unit_test_setup_teardown(test_global_set_level_opens_dialogue, setup_attract,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_global_audio_toggle, setup_attract, teardown),
        cmocka_unit_test_setup_teardown(test_global_highscore_h_global, setup_attract, teardown),
        cmocka_unit_test_setup_teardown(test_global_highscore_H_personal, setup_attract, teardown),
    };

    const struct CMUnitTest scoping_tests[] = {
        cmocka_unit_test_setup_teardown(test_sfx_blocked_in_game, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_sfx_blocked_in_editor, setup_editor, teardown),
        cmocka_unit_test_setup_teardown(test_speed_blocked_in_game, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_quit_blocked_in_editor, setup_editor, teardown),
    };

    const struct CMUnitTest gameplay_tests[] = {
        cmocka_unit_test_setup_teardown(test_gameplay_pause, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_gameplay_editor_e_is_noop, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_gameplay_paddle_left, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_gameplay_paddle_right, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_gameplay_paddle_left_arrow, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_gameplay_paddle_right_arrow, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_gameplay_paddle_blocked_in_mouse_mode, setup_game,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_gameplay_kill_ball, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_gameplay_tilt, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_gameplay_tilt_max_reached, setup_game, teardown),
    };

    const struct CMUnitTest attract_tests[] = {
        cmocka_unit_test(test_attract_cycle_table),
        cmocka_unit_test_setup_teardown(test_title_space_goes_to_instructions, setup_attract,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_title_space_single_press_no_cascade, setup_attract,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_attract_c_cycles_screen, setup_attract, teardown),
        cmocka_unit_test_setup_teardown(test_attract_e_enters_editor, setup_attract, teardown),
        cmocka_unit_test_setup_teardown(test_highscore_c_cycle_clears_game_active, setup_attract,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_highscore_space_return_clears_game_active,
                                        setup_attract, teardown),
        cmocka_unit_test_setup_teardown(test_attract_c_full_cycle_order, setup_attract, teardown),
    };

    const struct CMUnitTest editor_width_tests[] = {
        cmocka_unit_test_setup_teardown(test_editor_enter_widens_logical_width, setup_editor,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_editor_exit_restores_logical_width, setup_editor,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_editor_playtest_round_trip_logical_width,
                                        setup_editor, teardown),
    };

    const struct CMUnitTest playtest_live_input_tests[] = {
        cmocka_unit_test_setup_teardown(test_playtest_p_ends_via_live_game_input, setup_editor,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_real_game_p_still_pauses, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_playtest_escape_ends_via_live_game_input,
                                        setup_editor, teardown),
    };

    const struct CMUnitTest playtest_full_frame_tests[] = {
        cmocka_unit_test_setup_teardown(test_playtest_p_full_frame_no_restart, setup_editor,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_playtest_escape_full_frame_no_reopen, setup_editor,
                                        teardown),
    };

    const struct CMUnitTest playtest_quit_tests[] = {
        cmocka_unit_test_setup_teardown(test_playtest_q_full_frame_no_dialogue, setup_editor,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_real_game_q_opens_dialogue, setup_game, teardown),
    };

    const struct CMUnitTest debug_skip_tests[] = {
        cmocka_unit_test_setup_teardown(test_debug_skip_cheats_when_debug, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_debug_skip_blocked_without_debug, setup_game,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_shift_equals_is_volume_up, setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_numpad_plus_is_volume_up, setup_game, teardown),
    };

    const struct CMUnitTest sticky_launch_tests[] = {
        cmocka_unit_test_setup_teardown(test_sticky_paddle_one_press_launches_exactly_one_ball,
                                        setup_game, teardown),
        cmocka_unit_test_setup_teardown(test_single_waiting_ball_still_launches_on_one_press,
                                        setup_game, teardown),
    };

    const struct CMUnitTest dialogue_key_seam_tests[] = {
        cmocka_unit_test(test_dialogue_key_delete_maps_to_backspace),
        cmocka_unit_test(test_dialogue_key_backspace_maps_to_backspace),
        cmocka_unit_test(test_dialogue_key_return),
        cmocka_unit_test(test_dialogue_key_escape),
        cmocka_unit_test(test_dialogue_key_unmapped_returns_false),
        cmocka_unit_test(test_dialogue_key_null_out_returns_false),
    };

    int rc = 0;
    rc |= cmocka_run_group_tests_name("global keys", global_tests, NULL, NULL);
    rc |= cmocka_run_group_tests_name("mode scoping", scoping_tests, NULL, NULL);
    rc |= cmocka_run_group_tests_name("gameplay keys", gameplay_tests, NULL, NULL);
    rc |= cmocka_run_group_tests_name("attract navigation", attract_tests, NULL, NULL);
    rc |= cmocka_run_group_tests_name("editor window width", editor_width_tests, NULL, NULL);
    rc |= cmocka_run_group_tests_name("playtest live input (xboing-lpi)", playtest_live_input_tests,
                                      NULL, NULL);
    rc |= cmocka_run_group_tests_name("playtest full-frame double-fire (xboing-1ir)",
                                      playtest_full_frame_tests, NULL, NULL);
    rc |= cmocka_run_group_tests_name("playtest quit blocked (xboing-3h3)", playtest_quit_tests,
                                      NULL, NULL);
    rc |= cmocka_run_group_tests_name("debug skip-level cheat + shift keymap (m-2026-07-16-015)",
                                      debug_skip_tests, NULL, NULL);
    rc |= cmocka_run_group_tests_name("sticky-paddle single-launch per press (m-2026-07-17-014)",
                                      sticky_launch_tests, NULL, NULL);
    rc |= cmocka_run_group_tests_name("dialogue_key_from_sdl seam (m-2026-07-17-016)",
                                      dialogue_key_seam_tests, NULL, NULL);
    return rc;
}
