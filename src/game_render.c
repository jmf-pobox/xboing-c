/*
 * game_render.c -- Rendering dispatch for SDL2-based XBoing.
 *
 * Draws all visible game elements: playfield border, blocks, paddle,
 * balls, bullets, score digits, lives, specials panel, and messages.
 *
 * Each game element has its own render function that queries the
 * corresponding module for render info and draws textures from the
 * sprite catalog.
 *
 * This file starts with block rendering (Bead 1.4) and grows as
 * later beads wire more elements.
 */

#include "game_render.h"

#include <SDL2/SDL.h>

#include "block_system.h"
#include "block_types.h"
#include "game_context.h"
#include "level_system.h"
#include "sdl2_renderer.h"
#include "sdl2_texture.h"
#include "sprite_catalog.h"

/* =========================================================================
 * Play area geometry (from stage.h constants)
 * ========================================================================= */

/* The play area is the right-side region of the window.
 * Origin is at (MAIN_WIDTH, 0) = (70, 0). */
#define PLAY_AREA_X 70
#define PLAY_AREA_Y 0
#define PLAY_AREA_W 495
#define PLAY_AREA_H 580

/* Border thickness around the play area */
#define BORDER_THICKNESS 3

/* =========================================================================
 * Block rendering
 * ========================================================================= */

void game_render_blocks(const game_ctx_t *ctx)
{
    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);

    for (int row = 0; row < MAX_ROW; row++)
    {
        for (int col = 0; col < MAX_COL; col++)
        {
            block_system_render_info_t info;
            if (block_system_get_render_info(ctx->block, row, col, &info) != BLOCK_SYS_OK)
                continue;
            if (!info.occupied)
                continue;

            /* Look up the sprite for this block type */
            const char *key = sprite_block_key(info.block_type);
            if (!key)
                continue;

            sdl2_texture_info_t tex;
            if (sdl2_texture_get(ctx->texture, key, &tex) != SDL2T_OK)
                continue;

            /* Draw at the block's pixel position, offset by play area origin */
            SDL_Rect dst = {
                .x = PLAY_AREA_X + info.x,
                .y = PLAY_AREA_Y + info.y,
                .w = info.width,
                .h = info.height,
            };
            SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
        }
    }
}

/* =========================================================================
 * Playfield rendering
 * ========================================================================= */

void game_render_playfield(const game_ctx_t *ctx)
{
    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);

    /* Draw play area border (red rectangle) */
    SDL_SetRenderDrawColor(sdl, 200, 0, 0, 255);
    SDL_Rect border = {
        .x = PLAY_AREA_X - BORDER_THICKNESS,
        .y = PLAY_AREA_Y - BORDER_THICKNESS,
        .w = PLAY_AREA_W + 2 * BORDER_THICKNESS,
        .h = PLAY_AREA_H + 2 * BORDER_THICKNESS,
    };
    /* Draw top, left, right borders (bottom is open for ball death) */
    SDL_Rect top = {border.x, border.y, border.w, BORDER_THICKNESS};
    SDL_Rect left = {border.x, border.y, BORDER_THICKNESS, border.h};
    SDL_Rect right = {border.x + border.w - BORDER_THICKNESS, border.y, BORDER_THICKNESS, border.h};
    SDL_RenderFillRect(sdl, &top);
    SDL_RenderFillRect(sdl, &left);
    SDL_RenderFillRect(sdl, &right);

    /* Render blocks */
    game_render_blocks(ctx);
}

/* =========================================================================
 * Background rendering
 * ========================================================================= */

void game_render_background(const game_ctx_t *ctx)
{
    int bg_num = level_system_get_background(ctx->level);
    const char *key = sprite_background_key(bg_num);

    sdl2_texture_info_t tex;
    if (sdl2_texture_get(ctx->texture, key, &tex) == SDL2T_OK)
    {
        SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);
        SDL_Rect dst = {PLAY_AREA_X, PLAY_AREA_Y, PLAY_AREA_W, PLAY_AREA_H};
        SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
    }
}

/* =========================================================================
 * Full frame rendering
 * ========================================================================= */

void game_render_frame(const game_ctx_t *ctx)
{
    sdl2_renderer_clear(ctx->renderer);

    /* Background fills the play area */
    game_render_background(ctx);

    /* Playfield border + blocks */
    game_render_playfield(ctx);

    sdl2_renderer_present(ctx->renderer);
}
