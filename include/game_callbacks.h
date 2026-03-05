/*
 * game_callbacks.h -- Callback implementations for the SDL2 game.
 *
 * Provides concrete callback functions that wire game modules together.
 * These are registered at module creation time in game_init.c.
 */

#ifndef GAME_CALLBACKS_H
#define GAME_CALLBACKS_H

#include "ball_system.h"
#include "game_context.h"
#include "gun_system.h"
#include "intro_system.h"
#include "presents_system.h"

/*
 * Build and return the ball_system callback table.
 * All callbacks use game_ctx_t* as their user_data pointer.
 */
ball_system_callbacks_t game_callbacks_ball(void);

/*
 * Build a ball_system_env_t snapshot from current game state.
 * Called per-frame to pass environment to ball_system_update().
 */
ball_system_env_t game_callbacks_ball_env(const game_ctx_t *ctx);

/*
 * Build and return the gun_system callback table.
 */
gun_system_callbacks_t game_callbacks_gun(void);

/*
 * Build a gun_system_env_t snapshot from current game state.
 */
gun_system_env_t game_callbacks_gun_env(const game_ctx_t *ctx);

/* Presents system callback table. */
presents_system_callbacks_t game_callbacks_presents(void);

/* Intro system callback table. */
intro_system_callbacks_t game_callbacks_intro(void);

#endif /* GAME_CALLBACKS_H */
