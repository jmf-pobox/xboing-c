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

/*
 * Seed the process-global RNG (srand) with the production policy:
 * time(NULL).  game_create itself does NOT call srand — the library
 * never mutates rand() state silently.  Callers choose their own
 * seeding policy:
 *
 *   - Production main() calls this once before game_create().
 *   - Determinism-sensitive tests call srand(known_seed) directly.
 *   - Other tests call neither and inherit whatever rand() state
 *     existed at process start.
 *
 * If the production seeding policy ever needs to change (e.g.,
 * combine time() with getpid() for parallel-test safety), this is
 * the one place to edit.
 */
void game_seed_rng_default(void);

#endif /* GAME_INIT_H */
