/*
 * game_render.h -- Rendering dispatch for the SDL2 game.
 */

#ifndef GAME_RENDER_H
#define GAME_RENDER_H

#include "game_context.h"
#include "score_system.h" /* SCORE_DIGIT_STRIDE — shared with level number layout */
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

/*
 * Pure helper — compute the absolute pixel position of life icon i in the
 * level info panel.  Mirrors original/level.c:223-224:
 *
 *   DrawLife(display, window, 175 - (i * 30), 21);
 *
 * which calls RenderShape at (x - sprite_w/2, y - sprite_h/2).  The (175, 21)
 * pair is the SPRITE CENTER in window-local coordinates with stride 30 between
 * lives.  In modern absolute coords:
 *
 *   center_x = level_area_x + LEVEL_LIFE_ANCHOR_X - i * LEVEL_LIFE_STRIDE
 *   center_y = level_area_y + LEVEL_LIFE_ANCHOR_Y
 *   out_x    = center_x - sprite_w / 2
 *   out_y    = center_y - sprite_h / 2
 *
 * Lives render right-to-left starting at i=0 (the rightmost icon).
 * NULL out_x or out_y skips that axis.
 */
#define LEVEL_LIFE_ANCHOR_X 175
#define LEVEL_LIFE_ANCHOR_Y 21
#define LEVEL_LIFE_STRIDE 30

static inline void level_life_position(int level_area_x, int level_area_y, int i, int sprite_w,
                                       int sprite_h, int *out_x, int *out_y)
{
    int center_x = level_area_x + LEVEL_LIFE_ANCHOR_X - i * LEVEL_LIFE_STRIDE;
    int center_y = level_area_y + LEVEL_LIFE_ANCHOR_Y;
    if (out_x)
        *out_x = center_x - sprite_w / 2;
    if (out_y)
        *out_y = center_y - sprite_h / 2;
}

/*
 * Pure helper — compute the absolute pixel position of level-number digit
 * `digit_index` (0=rightmost) when rendering right-anchored.  Mirrors
 * original/score.c:155-167 DrawOutNumber recursion which lays digits at
 * x - 32, x - 64, ... from the anchor.
 *
 * Original anchor for level number is x=260 in level-window-local coordinates
 * (original/level.c:210); rightmost digit lands at x=228 (anchor - 32) and
 * each preceding digit at -32 from its successor.
 */
#define LEVEL_NUM_ANCHOR_X 260
#define LEVEL_NUM_ANCHOR_Y 5

static inline void level_number_digit_position(int level_area_x, int level_area_y, int digit_index,
                                               int *out_x, int *out_y)
{
    if (out_x)
        *out_x = level_area_x + LEVEL_NUM_ANCHOR_X - (digit_index + 1) * SCORE_DIGIT_STRIDE;
    if (out_y)
        *out_y = level_area_y + LEVEL_NUM_ANCHOR_Y;
}

/*
 * Pure helper — compute the x-coordinate of one item in a centred sprite
 * row used by the bonus screen for coin and bullet animations.
 * Mirrors the formulas at original/bonus.c:354-361 and 462-469:
 *
 *   max_len = total * stride + padding
 *   x[i]    = center_x + max_len / 2 - (total - i) * stride
 *
 * Layout: items 0..total-1 fill in left-to-right with `stride` pixels
 * between successive items (so item 0 is leftmost, item total-1 is
 * rightmost).  The padding matches the original's per-row centering.
 *
 * Coin row (BONUS_BLK): stride = 37 (27-px sprite + 10-px gap),
 *   padding = 5 (original/bonus.c:354 `+ 5`).
 * Bullet row: stride = 10 (7-px sprite + 3-px gap),
 *   padding = 0 (original/bonus.c:462 has no `+ N` term).
 *
 * Caller selects how many of these positions to actually render based
 * on the bonus_system live counters: drawn = initial_count - live_count.
 */
#define BONUS_COIN_STRIDE 37
#define BONUS_COIN_PADDING 5
#define BONUS_BULLET_STRIDE 10
#define BONUS_BULLET_PADDING 0

static inline int bonus_row_item_x(int center_x, int total_count, int stride, int padding,
                                   int item_index)
{
    return center_x + (total_count * stride + padding) / 2 - (total_count - item_index) * stride;
}

/*
 * Pure helper — compute the top-left position to center a text glyph within a
 * block-sized bounding box. Used for DROP_BLK hit-points digit, RANDOM_BLK
 * "- R -" overlay, and any future composite text rendering that needs to
 * center within a block.
 *
 *   *out_x = block_x + (block_w / 2) - (text_w / 2)
 *   *out_y = block_y + (block_h / 2) - (text_h / 2)
 *
 * Matches original/blocks.c:1706 and original/blocks.c:1733 centering math.
 *
 * Static inline — no SDL2 dependency, safe to call from CMocka tests.
 * NULL out_x or out_y skips that axis (allows partial use).
 */
static inline void block_overlay_text_pos(int block_x, int block_y, int block_w, int block_h,
                                          int text_w, int text_h, int *out_x, int *out_y)
{
    if (out_x)
        *out_x = block_x + (block_w / 2) - (text_w / 2);
    if (out_y)
        *out_y = block_y + (block_h / 2) - (text_h / 2);
}

#endif /* GAME_RENDER_H */
