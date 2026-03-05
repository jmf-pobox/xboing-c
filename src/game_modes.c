/*
 * game_modes.c -- Mode handlers for the SDL2 game state machine.
 *
 * Each game mode (presents, intro, game, pause, bonus, highscore, etc.)
 * has on_enter/on_update/on_exit handlers registered with sdl2_state.
 *
 * The on_update handler is called every frame by sdl2_state_update().
 * It drives the module's update function and checks for transitions.
 */

#include "game_modes.h"

#include "ball_system.h"
#include "game_callbacks.h"
#include "game_context.h"
#include "game_input.h"
#include "game_render.h"
#include "game_rules.h"
#include "gun_system.h"
#include "intro_system.h"
#include "message_system.h"
#include "presents_system.h"
#include "sdl2_input.h"
#include "sdl2_state.h"

/* =========================================================================
 * MODE_GAME — core gameplay
 * ========================================================================= */

static void mode_game_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    if (!ctx->game_active)
    {
        /* New game — reset state */
        ctx->game_active = true;
    }
}

static void mode_game_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    /* Input dispatch */
    game_input_update(ctx);

    /* Ball physics */
    ball_system_env_t benv = game_callbacks_ball_env(ctx);
    ball_system_update(ctx->ball, &benv);

    /* Gun physics */
    gun_system_env_t genv = game_callbacks_gun_env(ctx);
    gun_system_update(ctx->gun, &genv);

    /* Message timer */
    int frame = (int)sdl2_state_frame(ctx->state);
    message_system_update(ctx->message, frame);

    /* Game rules (level completion, bonus spawning) */
    game_rules_check(ctx);
}

/* =========================================================================
 * MODE_PAUSE — freeze everything
 * ========================================================================= */

static void mode_pause_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    int frame = (int)sdl2_state_frame(ctx->state);
    message_system_set(ctx->message, "- Game paused -", 0, frame);
}

static void mode_pause_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    /* Only check for unpause */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_PAUSE))
        sdl2_state_transition(ctx->state, SDL2ST_GAME);
}

static void mode_pause_exit(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    int frame = (int)sdl2_state_frame(ctx->state);
    message_system_set(ctx->message, "", 1, frame);
}

/* =========================================================================
 * MODE_PRESENTS — splash screen sequence
 * ========================================================================= */

static void mode_presents_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    int frame = (int)sdl2_state_frame(ctx->state);
    presents_system_begin(ctx->presents, frame);
}

static void mode_presents_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    int frame = (int)sdl2_state_frame(ctx->state);

    presents_system_update(ctx->presents, frame);

    /* Space skips presents */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_START))
        presents_system_skip(ctx->presents, frame);

    /* on_finished callback handles the transition to intro */
}

/* =========================================================================
 * MODE_INTRO — block descriptions + sparkle
 * ========================================================================= */

static void mode_intro_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    int frame = (int)sdl2_state_frame(ctx->state);
    intro_system_begin(ctx->intro, INTRO_MODE_INTRO, frame);
}

static void mode_intro_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    int frame = (int)sdl2_state_frame(ctx->state);

    intro_system_update(ctx->intro, frame);

    /* Space starts the game from intro */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_START))
    {
        sdl2_state_transition(ctx->state, SDL2ST_GAME);
        return;
    }

    /* on_finished callback handles cycling to next attract screen */
}

/* =========================================================================
 * MODE_INSTRUCT — instructions text + sparkle
 * ========================================================================= */

static void mode_instruct_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    int frame = (int)sdl2_state_frame(ctx->state);
    intro_system_begin(ctx->intro, INTRO_MODE_INSTRUCT, frame);
}

static void mode_instruct_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    int frame = (int)sdl2_state_frame(ctx->state);

    intro_system_update(ctx->intro, frame);

    if (sdl2_input_just_pressed(ctx->input, SDL2I_START))
    {
        sdl2_state_transition(ctx->state, SDL2ST_GAME);
        return;
    }
}

/* =========================================================================
 * Registration
 * ========================================================================= */

void game_modes_register(game_ctx_t *ctx)
{
    /* MODE_GAME */
    {
        sdl2_state_mode_def_t def = {
            .on_enter = mode_game_enter,
            .on_update = mode_game_update,
        };
        sdl2_state_register(ctx->state, SDL2ST_GAME, &def);
    }

    /* MODE_PAUSE */
    {
        sdl2_state_mode_def_t def = {
            .on_enter = mode_pause_enter,
            .on_update = mode_pause_update,
            .on_exit = mode_pause_exit,
        };
        sdl2_state_register(ctx->state, SDL2ST_PAUSE, &def);
    }

    /* MODE_PRESENTS */
    {
        sdl2_state_mode_def_t def = {
            .on_enter = mode_presents_enter,
            .on_update = mode_presents_update,
        };
        sdl2_state_register(ctx->state, SDL2ST_PRESENTS, &def);
    }

    /* MODE_INTRO */
    {
        sdl2_state_mode_def_t def = {
            .on_enter = mode_intro_enter,
            .on_update = mode_intro_update,
        };
        sdl2_state_register(ctx->state, SDL2ST_INTRO, &def);
    }

    /* MODE_INSTRUCT */
    {
        sdl2_state_mode_def_t def = {
            .on_enter = mode_instruct_enter,
            .on_update = mode_instruct_update,
        };
        sdl2_state_register(ctx->state, SDL2ST_INSTRUCT, &def);
    }

    /* Remaining modes (demo, keys, bonus, highscore, etc.)
     * are registered in later beads. */
}
