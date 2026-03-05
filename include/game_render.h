/*
 * game_render.h -- Rendering dispatch for the SDL2 game.
 */

#ifndef GAME_RENDER_H
#define GAME_RENDER_H

#include "game_context.h"

/* Render the complete game frame (background + playfield + blocks + UI). */
void game_render_frame(const game_ctx_t *ctx);

/* Render the play area background. */
void game_render_background(const game_ctx_t *ctx);

/* Render the play area border and all blocks. */
void game_render_playfield(const game_ctx_t *ctx);

/* Render all occupied blocks in the grid. */
void game_render_blocks(const game_ctx_t *ctx);

/* Render the paddle at its current position. */
void game_render_paddle(const game_ctx_t *ctx);

/* Render all active balls. */
void game_render_balls(const game_ctx_t *ctx);

/* Render the score digit display. */
void game_render_score(const game_ctx_t *ctx);

/* Render lives remaining and level number. */
void game_render_lives(const game_ctx_t *ctx);

#endif /* GAME_RENDER_H */
