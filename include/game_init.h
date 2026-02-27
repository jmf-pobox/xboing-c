/*
 * game_init.h -- Create and destroy the game context.
 */

#ifndef GAME_INIT_H
#define GAME_INIT_H

#include "game_context.h"

/*
 * Create a fully initialized game context.
 * Parses CLI, loads config, initializes all SDL2 and game modules.
 * Returns NULL on failure or if an exit-early flag was found (-help, -version).
 */
game_ctx_t *game_create(int argc, char *argv[]);

/*
 * Destroy the game context and all owned modules.
 * Safe to call with NULL.
 */
void game_destroy(game_ctx_t *ctx);

#endif /* GAME_INIT_H */
