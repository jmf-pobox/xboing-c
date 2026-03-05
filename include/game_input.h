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

#endif /* GAME_INPUT_H */
