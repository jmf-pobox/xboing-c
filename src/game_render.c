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

#include "ball_system.h"
#include "ball_types.h"
#include "block_system.h"
#include "block_types.h"
#include "game_context.h"
#include "level_system.h"
#include "paddle_system.h"
#include "score_system.h"
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
 * Ball rendering
 * ========================================================================= */

void game_render_balls(const game_ctx_t *ctx)
{
    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);

    for (int i = 0; i < MAX_BALLS; i++)
    {
        ball_system_render_info_t info;
        if (ball_system_get_render_info(ctx->ball, i, &info) != BALL_SYS_OK)
            continue;
        if (!info.active)
            continue;

        const char *key = NULL;

        switch (info.state)
        {
            case BALL_CREATE:
            {
                /* Birth animation: slide is 1-8 */
                key = sprite_ball_birth_key(info.slide);
                break;
            }

            case BALL_ACTIVE:
            case BALL_READY:
            case BALL_DIE:
            case BALL_WAIT:
                key = sprite_ball_key(info.slide);
                break;

            case BALL_POP:
                /* Pop animation reuses birth frames in reverse */
                key = sprite_ball_birth_key(info.slide);
                break;

            default:
                continue;
        }

        if (!key)
            continue;

        sdl2_texture_info_t tex;
        if (sdl2_texture_get(ctx->texture, key, &tex) != SDL2T_OK)
            continue;

        /* Ball position is center — convert to top-left */
        SDL_Rect dst = {
            .x = PLAY_AREA_X + info.x - BALL_WC,
            .y = PLAY_AREA_Y + info.y - BALL_HC,
            .w = BALL_WIDTH,
            .h = BALL_HEIGHT,
        };
        SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
    }
}

/* =========================================================================
 * Paddle rendering
 * ========================================================================= */

void game_render_paddle(const game_ctx_t *ctx)
{
    paddle_system_render_info_t info;
    if (paddle_system_get_render_info(ctx->paddle, &info) != PADDLE_SYS_OK)
        return;

    const char *key = sprite_paddle_key(info.width);
    sdl2_texture_info_t tex;
    if (sdl2_texture_get(ctx->texture, key, &tex) != SDL2T_OK)
        return;

    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);

    /* Paddle position is center X in play-area coordinates.
     * Convert to top-left corner for SDL_RenderCopy. */
    SDL_Rect dst = {
        .x = PLAY_AREA_X + info.pos - info.width / 2,
        .y = PLAY_AREA_Y + info.y,
        .w = info.width,
        .h = info.height,
    };
    SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
}

/* =========================================================================
 * Playfield rendering
 * ========================================================================= */

void game_render_playfield(const game_ctx_t *ctx)
{
    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);

    /* Draw play area border (red rectangle).
     * Top border sits just above the play area; left/right extend from the
     * top border down past the play area bottom (bottom is open for ball death). */
    SDL_SetRenderDrawColor(sdl, 200, 0, 0, 255);

    int bx = PLAY_AREA_X - BORDER_THICKNESS;
    int by = PLAY_AREA_Y; /* top border at y=0, not negative */
    int bw = PLAY_AREA_W + 2 * BORDER_THICKNESS;
    int bh = PLAY_AREA_H + BORDER_THICKNESS; /* extends from top to bottom */

    SDL_Rect top = {bx, by, bw, BORDER_THICKNESS};
    SDL_Rect left = {bx, by, BORDER_THICKNESS, bh};
    SDL_Rect right = {bx + bw - BORDER_THICKNESS, by, BORDER_THICKNESS, bh};
    SDL_RenderFillRect(sdl, &top);
    SDL_RenderFillRect(sdl, &left);
    SDL_RenderFillRect(sdl, &right);

    /* Render blocks */
    game_render_blocks(ctx);

    /* Render paddle */
    game_render_paddle(ctx);

    /* Render balls */
    game_render_balls(ctx);
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
 * Score digit rendering
 * ========================================================================= */

/* Score window position in the main window (from legacy stage.c) */
#define SCORE_AREA_X 247
#define SCORE_AREA_Y 10

void game_render_score(const game_ctx_t *ctx)
{
    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);
    unsigned long score_val = score_system_get(ctx->score);

    score_system_digit_layout_t layout;
    score_system_get_digit_layout(score_val, &layout);

    for (int i = 0; i < layout.count; i++)
    {
        const char *key = sprite_digit_key(layout.digits[i]);
        sdl2_texture_info_t tex;
        if (sdl2_texture_get(ctx->texture, key, &tex) != SDL2T_OK)
            continue;

        SDL_Rect dst = {
            .x = SCORE_AREA_X + layout.x_positions[i],
            .y = SCORE_AREA_Y + layout.y,
            .w = SCORE_DIGIT_WIDTH,
            .h = SCORE_WINDOW_HEIGHT,
        };
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

    /* Score display */
    game_render_score(ctx);

    sdl2_renderer_present(ctx->renderer);
}
