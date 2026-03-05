/*
 * game_render_ui.h -- UI screen rendering for the SDL2 game.
 *
 * Renders attract-mode screens (presents, intro, demo, keys, bonus,
 * highscore) by querying module render info and drawing sprites/text.
 */

#ifndef GAME_RENDER_UI_H
#define GAME_RENDER_UI_H

#include "game_context.h"

/* Render the presents splash screen (flag, letters, sparkle, typewriter). */
void game_render_presents(const game_ctx_t *ctx);

/* Render the intro screen (block descriptions table, sparkle). */
void game_render_intro(const game_ctx_t *ctx);

/* Render the instructions screen (text, sparkle). */
void game_render_instruct(const game_ctx_t *ctx);

#endif /* GAME_RENDER_UI_H */
