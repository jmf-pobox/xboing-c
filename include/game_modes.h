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

/* Dialogue result pending flags — set by game_input.c after
 * sdl2_state_push_dialogue succeeds, consumed by mode enter/exit
 * handlers.  Follows the wisdom_pending pattern (game_modes.c:69). */
void game_modes_set_quit_pending(void);
void game_modes_set_abort_pending(void);
void game_modes_set_level_pending(void);

#endif /* GAME_MODES_H */
