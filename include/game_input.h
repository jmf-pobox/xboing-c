/*
 * game_input.h -- Input dispatch for the SDL2 game.
 *
 * Translates sdl2_input action queries into game module calls.
 * Called once per frame from the event loop.
 */

#ifndef GAME_INPUT_H
#define GAME_INPUT_H

#include <stdbool.h>

#include <SDL2/SDL.h>

#include "dialogue_system.h"
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

/*
 * Map an SDL keycode to a dialogue key action.
 *
 * Pure keycode -> dialogue-action mapping, no side effects.  Returns
 * true and sets *out for RETURN, BACKSPACE, DELETE, and ESCAPE.
 * Returns false (leaving *out untouched) for anything else.  Delete
 * aliases to Backspace per original/dialogue.c:327-328.
 */
bool dialogue_key_from_sdl(SDL_Keycode sym, dialogue_key_type_t *out);

#endif /* GAME_INPUT_H */
