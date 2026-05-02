/*
 * game_render.h -- Rendering dispatch for the SDL2 game.
 */

#ifndef GAME_RENDER_H
#define GAME_RENDER_H

#include "game_context.h"
#include "special_system.h"

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

/* Render all active bullets and tink impact effects. */
void game_render_bullets(const game_ctx_t *ctx);

/* Render the score digit display. */
void game_render_score(const game_ctx_t *ctx);

/* Render lives remaining and level number. */
void game_render_lives(const game_ctx_t *ctx);

/* Render the ammo belt (bullet strip) in the level panel. */
void game_render_ammo_belt(const game_ctx_t *ctx);

/* Render the EyeDude character. */
void game_render_eyedude(const game_ctx_t *ctx);

/* Render devil eyes blink animation. */
void game_render_deveyes(const game_ctx_t *ctx);

/* Render border glow color cycling. */
void game_render_border_glow(const game_ctx_t *ctx);

/* Render the editor palette sidebar. */
void game_render_editor_palette(const game_ctx_t *ctx);

/* Render the message bar below the play area. */
void game_render_messages(const game_ctx_t *ctx);

/* Render the timer display (seconds remaining). */
void game_render_timer(const game_ctx_t *ctx);

/* Render the specials panel (8 power-up labels, active=yellow / inactive=white). */
void game_render_specials(const game_ctx_t *ctx);

/*
 * Pure coordinate helper — compute absolute pixel position of specials label i.
 *
 * lh:     line height from sdl2_font_line_height (used as the row y-step).
 * labels: array of SPECIAL_COUNT entries from special_system_get_labels().
 * i:      label index in [0, SPECIAL_COUNT).
 * abs_x:  receives SPECIAL_PANEL_ORIGIN_X + labels[i].col_x.
 * abs_y:  receives SPECIAL_PANEL_ORIGIN_Y + SPECIAL_ROW0_Y + labels[i].row * (lh + SPECIAL_GAP).
 *
 * No SDL2 dependency — safe to call from CMocka tests without a renderer.
 */
void game_render_specials_coords(int lh, const special_label_info_t *labels, int i, int *abs_x,
                                 int *abs_y);

#endif /* GAME_RENDER_H */
