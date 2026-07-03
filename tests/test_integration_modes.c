/*
 * test_integration_modes.c — Mode handler crash test.
 *
 * Force-transitions to each of the registered game modes and ticks
 * N frames to verify no crashes, memory errors, or undefined behavior.
 * ASan catches any issues during the ticking.
 *
 * This is primarily a robustness (no-crash) suite — most tests only
 * verify that a mode's enter/update don't crash, not that they do the
 * right thing.  A small set of focused correctness regressions is mixed
 * in (e.g. the bonus-interstitial rank tests at the end), each clearly
 * marked.
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

#include "bonus_system.h"
#include "game_context.h"
#include "game_init.h"
#include "gun_system.h"
#include "highscore_io.h"
#include "score_system.h"
#include "sdl2_input.h"
#include "sdl2_renderer.h"
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

/* =========================================================================
 * Bonus interstitial rank — correctness (not just no-crash)
 *
 * The test process is never setgid and sets no XBOING_SCORE_FILE, so the
 * global board is inactive and the interstitial must rank against the
 * PERSONAL board — the one the player is actually placed on at game over.
 * ========================================================================= */

/* Replace the personal board with a single top entry of the given score. */
static void seed_personal_top(game_ctx_t *ctx, unsigned long top_score)
{
    highscore_io_init_table(&ctx->hs_personal);
    ctx->hs_personal.entries[0].score = top_score;
    snprintf(ctx->hs_personal.entries[0].name, HIGHSCORE_NAME_LEN, "Prior Best");
}

/* Set the running score and zero every bonus input so the projected
 * (post-bonus) score the interstitial ranks equals the running score —
 * bonus_system_compute_total returns 0 when the timer and bullet count are
 * both zero.  Keeps these table-selection assertions independent of the
 * bonus arithmetic. */
static void set_score_no_bonus(game_ctx_t *ctx, unsigned long score)
{
    ctx->time_remaining = 0;
    ctx->bonus_count = 0;
    gun_system_set_ammo(ctx->gun, 0);
    score_system_set(ctx->score, score);
}

/* Regression for "interstitial said #1, game over said #2": with a higher
 * prior personal score, the bonus screen must report rank 2, not the
 * spurious 1 that ranking against an empty global board produced. */
static void test_bonus_rank_uses_personal_board(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    seed_personal_top(ctx, 100000);
    ctx->level_number = 3;
    set_score_no_bonus(ctx, 5000);

    sdl2_state_status_t st = sdl2_state_transition(ctx->state, SDL2ST_BONUS);
    assert_int_equal(st, SDL2ST_OK);
    assert_int_equal(bonus_system_get_highscore_rank(ctx->bonus), 2);
}

/* A score tied with the board's top entry predicts placement BEHIND it
 * (insert semantics), so the interstitial never over-promises "1st" for a
 * tie that will land the player 2nd. */
static void test_bonus_rank_tie_lands_behind(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    seed_personal_top(ctx, 5000);
    ctx->level_number = 2;
    set_score_no_bonus(ctx, 5000);

    sdl2_state_status_t st = sdl2_state_transition(ctx->state, SDL2ST_BONUS);
    assert_int_equal(st, SDL2ST_OK);
    assert_int_equal(bonus_system_get_highscore_rank(ctx->bonus), 2);
}

/* =========================================================================
 * -grab wiring — game_create applies the CLI flag to the window
 * ========================================================================= */

static char arg_grab[] = "-grab";

/* With -grab, the window's mouse is grabbed after game_create. */
static void test_grab_flag_applied(void **vstate)
{
    (void)vstate;
    char *argv[] = {arg_prog, arg_grab, NULL};
    game_ctx_t *ctx = game_create(2, argv);
    assert_non_null(ctx);
    assert_true(sdl2_renderer_get_mouse_grab(ctx->renderer));
    game_destroy(ctx);
}

/* Without -grab, the mouse is not grabbed (default). */
static void test_grab_flag_absent_default(void **vstate)
{
    (void)vstate;
    char *argv[] = {arg_prog, NULL};
    game_ctx_t *ctx = game_create(1, argv);
    assert_non_null(ctx);
    assert_false(sdl2_renderer_get_mouse_grab(ctx->renderer));
    game_destroy(ctx);
}

static void test_mode_pause(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    /* Enter GAME first, then PAUSE (pause needs game to be active) */
    sdl2_state_status_t status = sdl2_state_transition(f->ctx->state, SDL2ST_GAME);
    assert_int_equal(status, SDL2ST_OK);
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
        cmocka_unit_test_setup_teardown(test_bonus_rank_uses_personal_board, setup, teardown),
        cmocka_unit_test_setup_teardown(test_bonus_rank_tie_lands_behind, setup, teardown),
        cmocka_unit_test(test_grab_flag_applied),
        cmocka_unit_test(test_grab_flag_absent_default),
        cmocka_unit_test_setup_teardown(test_mode_pause, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_edit, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_dialogue_push_pop, setup, teardown),
        cmocka_unit_test_setup_teardown(test_rapid_transitions, setup, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
