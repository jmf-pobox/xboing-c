/*
 * game_input.h -- Input dispatch for the SDL2 game.
 *
 * Translates sdl2_input action queries into game module calls.
 * Called once per frame from the event loop.
 */

#ifndef GAME_INPUT_H
#define GAME_INPUT_H

#include "game_context.h"

/*
 * Process gameplay input for the current frame.
 *
 * Reads action state from sdl2_input and dispatches to the
 * appropriate game modules (paddle, ball, gun, etc.).
 *
 * Must be called after sdl2_input_begin_frame() + event processing,
 * before the game loop tick.
 */
void game_input_update(game_ctx_t *ctx);

/*
 * Process mode-independent (global) input for the current frame.
 *
 * Handles mode-independent keys: SFX toggle (S), volume (+/-),
 * fullscreen toggle (I), control toggle (G), quit (Q).  Speed keys
 * (1-9) are attract-mode only per original handleSpeedKeys scope.
 * Mirrors original/main.c handleMiscKeys + handleSpeedKeys.
 *
 * Must be called once per visual frame in game_main.c, after
 * sdl2_input_begin_frame() + event processing and before
 * sdl2_loop_update().  NOT in stub_tick — that fires multiple times
 * per visual frame at high speeds, causing toggle keys to multi-fire.
 */
void game_input_global(game_ctx_t *ctx);

#endif /* GAME_INPUT_H */
