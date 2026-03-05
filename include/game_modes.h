/*
 * game_modes.h -- Mode handlers for the SDL2 game state machine.
 *
 * Registers on_enter/on_update/on_exit handlers for each game mode
 * with the sdl2_state module.
 */

#ifndef GAME_MODES_H
#define GAME_MODES_H

#include "game_context.h"

/*
 * Register all mode handlers with the state machine.
 * Must be called after game_create() has initialized ctx->state.
 */
void game_modes_register(game_ctx_t *ctx);

#endif /* GAME_MODES_H */
