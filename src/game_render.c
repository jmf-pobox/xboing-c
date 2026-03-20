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
#include "game_render_ui.h"

#include <SDL2/SDL.h>

#include "ball_system.h"
#include "ball_types.h"
#include "block_system.h"
#include "block_types.h"
#include "editor_system.h"
#include "eyedude_system.h"
#include "game_context.h"
#include "gun_system.h"
#include "level_system.h"
#include "message_system.h"
#include "paddle_system.h"
#include "score_system.h"
#include "sdl2_font.h"
#include "sdl2_renderer.h"
#include "sdl2_state.h"
#include "sdl2_texture.h"
#include "sfx_system.h"
#include "sprite_catalog.h"

/* =========================================================================
 * Play area geometry (from stage.h constants)
 * ========================================================================= */

/*
 * Layout constants from legacy stage.c:CreateAllWindows().
 *
 * offsetX = MAIN_WIDTH / 2 = 35
 * mainWindow: 575 x 720 (PLAY_WIDTH + MAIN_WIDTH + 10) x (PLAY_HEIGHT + MAIN_HEIGHT + 10)
 * scoreWindow:  x=35,  y=10,  w=224, h=42
 * levelWindow:  x=284, y=5,   w=286, h=52
 * playWindow:   x=35,  y=60,  w=495, h=580 (border=2)
 * messWindow:   x=35,  y=655, w=247, h=30
 * specialWindow: x=292, y=655, w=180, h=35
 * timeWindow:   x=477, y=655, w=61,  h=35
 */
#define OFFSET_X 35
#define PLAY_AREA_X OFFSET_X
#define PLAY_AREA_Y 60
#define PLAY_AREA_W 495
#define PLAY_AREA_H 580

/* Border thickness — matches legacy playWindow border_width=2 */
#define BORDER_THICKNESS 2

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
            if (!info.occupied && !info.exploding)
                continue;

            /* Select sprite key based on block state */
            const char *key = NULL;

            if (info.exploding)
            {
                /* Explosion animation overrides normal appearance */
                key = sprite_block_explode_key(info.block_type, info.explode_slide);
            }
            else if (info.block_type == COUNTER_BLK && info.counter_slide > 0)
            {
                /* Counter blocks show their hit count */
                key = sprite_counter_slide_key(info.counter_slide);
            }
            else
            {
                /* Normal static block sprite */
                key = sprite_block_key(info.block_type);
            }

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

        /* Draw launch direction guide above BALL_READY balls */
        if (info.state == BALL_READY)
        {
            ball_system_guide_info_t guide = ball_system_get_guide_info(ctx->ball);
            const char *gkey = sprite_guide_key(guide.pos);
            sdl2_texture_info_t gtex;
            if (sdl2_texture_get(ctx->texture, gkey, &gtex) == SDL2T_OK)
            {
                /* Legacy top-left: (ballx-14, bally-22) for 29x12 sprite.
                 * X: center on ball. Y: 16px gap + half sprite height above ball. */
                SDL_Rect gdst = {
                    .x = PLAY_AREA_X + info.x - gtex.width / 2,
                    .y = PLAY_AREA_Y + info.y - 16 - gtex.height / 2,
                    .w = gtex.width,
                    .h = gtex.height,
                };
                SDL_RenderCopy(sdl, gtex.texture, NULL, &gdst);
            }
        }
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

    /* Draw play area border — matches legacy playWindow border_width=2, color=red.
     * X11 draws the border outside the window content area, so we draw a 2px
     * border around all 4 sides of the play area. */
    SDL_SetRenderDrawColor(sdl, 200, 0, 0, 255);

    int bx = PLAY_AREA_X - BORDER_THICKNESS;
    int by = PLAY_AREA_Y - BORDER_THICKNESS;
    int bw = PLAY_AREA_W + 2 * BORDER_THICKNESS;
    int bh = PLAY_AREA_H + 2 * BORDER_THICKNESS;

    SDL_Rect top = {bx, by, bw, BORDER_THICKNESS};
    SDL_Rect bottom = {bx, by + bh - BORDER_THICKNESS, bw, BORDER_THICKNESS};
    SDL_Rect left = {bx, by, BORDER_THICKNESS, bh};
    SDL_Rect right = {bx + bw - BORDER_THICKNESS, by, BORDER_THICKNESS, bh};
    SDL_RenderFillRect(sdl, &top);
    SDL_RenderFillRect(sdl, &bottom);
    SDL_RenderFillRect(sdl, &left);
    SDL_RenderFillRect(sdl, &right);

    /* Clip all play area content to the border bounds */
    SDL_Rect clip = {PLAY_AREA_X, PLAY_AREA_Y, PLAY_AREA_W, PLAY_AREA_H};
    SDL_RenderSetClipRect(sdl, &clip);

    /* Render blocks */
    game_render_blocks(ctx);

    /* Render paddle */
    game_render_paddle(ctx);

    /* Render balls */
    game_render_balls(ctx);

    /* Render bullets and tinks */
    game_render_bullets(ctx);

    /* Remove clip rect */
    SDL_RenderSetClipRect(sdl, NULL);
}

/* =========================================================================
 * Background rendering
 * ========================================================================= */

void game_render_background(const game_ctx_t *ctx)
{
    int bg_num = level_system_get_background(ctx->level);
    const char *key = sprite_background_key(bg_num);

    sdl2_texture_info_t tex;
    if (sdl2_texture_get(ctx->texture, key, &tex) != SDL2T_OK)
        return;

    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);

    /* Background PNGs are small tiles (e.g., 32x32) — tile them across the play area */
    int tw = tex.width;
    int th = tex.height;
    if (tw <= 0 || th <= 0)
        return;

    for (int ty = PLAY_AREA_Y; ty < PLAY_AREA_Y + PLAY_AREA_H; ty += th)
    {
        for (int tx = PLAY_AREA_X; tx < PLAY_AREA_X + PLAY_AREA_W; tx += tw)
        {
            int dw = tw;
            int dh = th;

            /* Clip partial tiles at play area edges */
            if (tx + dw > PLAY_AREA_X + PLAY_AREA_W)
                dw = PLAY_AREA_X + PLAY_AREA_W - tx;
            if (ty + dh > PLAY_AREA_Y + PLAY_AREA_H)
                dh = PLAY_AREA_Y + PLAY_AREA_H - ty;

            SDL_Rect src = {0, 0, dw, dh};
            SDL_Rect dst = {tx, ty, dw, dh};
            SDL_RenderCopy(sdl, tex.texture, &src, &dst);
        }
    }
}

/* =========================================================================
 * Bullet and tink rendering
 * ========================================================================= */

void game_render_bullets(const game_ctx_t *ctx)
{
    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);

    /* Render bullets */
    sdl2_texture_info_t btex;
    int have_bullet_tex = (sdl2_texture_get(ctx->texture, SPR_BULLET, &btex) == SDL2T_OK);

    for (int i = 0; i < GUN_MAX_BULLETS; i++)
    {
        gun_system_bullet_info_t info;
        if (gun_system_get_bullet_info(ctx->gun, i, &info) != GUN_SYS_OK)
            continue;
        if (!info.active)
            continue;

        if (have_bullet_tex)
        {
            SDL_Rect dst = {
                .x = PLAY_AREA_X + info.x - GUN_BULLET_WC,
                .y = PLAY_AREA_Y + info.y - GUN_BULLET_HC,
                .w = GUN_BULLET_WIDTH,
                .h = GUN_BULLET_HEIGHT,
            };
            SDL_RenderCopy(sdl, btex.texture, NULL, &dst);
        }
    }

    /* Render tinks (impact effects) */
    sdl2_texture_info_t ttex;
    int have_tink_tex = (sdl2_texture_get(ctx->texture, SPR_TINK, &ttex) == SDL2T_OK);

    for (int i = 0; i < GUN_MAX_TINKS; i++)
    {
        gun_system_tink_info_t info;
        if (gun_system_get_tink_info(ctx->gun, i, &info) != GUN_SYS_OK)
            continue;
        if (!info.active)
            continue;

        if (have_tink_tex)
        {
            /* Tinks render at top of play area at the bullet's X position */
            SDL_Rect dst = {
                .x = PLAY_AREA_X + info.x - GUN_TINK_WC,
                .y = PLAY_AREA_Y + 2,
                .w = GUN_TINK_WIDTH,
                .h = GUN_TINK_HEIGHT,
            };
            SDL_RenderCopy(sdl, ttex.texture, NULL, &dst);
        }
    }
}

/* =========================================================================
 * Lives display — small ball sprites below the score area
 * ========================================================================= */

/* Level window position (from legacy stage.c: scoreWidth + offsetX + 25 = 284) */
#define LEVEL_AREA_X 284
#define LEVEL_AREA_Y 5

void game_render_lives(const game_ctx_t *ctx)
{
    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);

    sdl2_texture_info_t tex;
    if (sdl2_texture_get(ctx->texture, SPR_BALL_LIFE, &tex) != SDL2T_OK)
        return;

    /* Draw one life ball for each life remaining */
    int lives = ctx->lives_left;
    if (lives > 5)
        lives = 5; /* Cap display at 5 */

    for (int i = 0; i < lives; i++)
    {
        SDL_Rect dst = {
            .x = LEVEL_AREA_X + 5 + i * 18,
            .y = LEVEL_AREA_Y + 30,
            .w = 16,
            .h = 16,
        };
        SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
    }

    /* Draw level number as digits */
    int level = ctx->level_number;
    int d1 = level / 10;
    int d0 = level % 10;

    sdl2_texture_info_t dtex;
    if (d1 > 0 && sdl2_texture_get(ctx->texture, sprite_digit_key(d1), &dtex) == SDL2T_OK)
    {
        SDL_Rect dst = {LEVEL_AREA_X + 5, LEVEL_AREA_Y + 5, 20, 25};
        SDL_RenderCopy(sdl, dtex.texture, NULL, &dst);
    }
    if (sdl2_texture_get(ctx->texture, sprite_digit_key(d0), &dtex) == SDL2T_OK)
    {
        SDL_Rect dst = {LEVEL_AREA_X + 27, LEVEL_AREA_Y + 5, 20, 25};
        SDL_RenderCopy(sdl, dtex.texture, NULL, &dst);
    }
}

/* =========================================================================
 * Score digit rendering
 * ========================================================================= */

/* Score window position (from legacy stage.c: offsetX=35, y=10) */
#define SCORE_AREA_X OFFSET_X
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

/* Tile the space background across the entire window */
static void render_main_background(const game_ctx_t *ctx)
{
    sdl2_texture_info_t tex;
    if (sdl2_texture_get(ctx->texture, SPR_BGRND_SPACE, &tex) != SDL2T_OK)
        return;

    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);
    int tw = tex.width;
    int th = tex.height;
    if (tw <= 0 || th <= 0)
        return;

    for (int ty = 0; ty < SDL2R_LOGICAL_HEIGHT; ty += th)
    {
        for (int tx = 0; tx < SDL2R_LOGICAL_WIDTH; tx += tw)
        {
            int dw = tw;
            int dh = th;
            if (tx + dw > SDL2R_LOGICAL_WIDTH)
                dw = SDL2R_LOGICAL_WIDTH - tx;
            if (ty + dh > SDL2R_LOGICAL_HEIGHT)
                dh = SDL2R_LOGICAL_HEIGHT - ty;
            SDL_Rect src = {0, 0, dw, dh};
            SDL_Rect dst = {tx, ty, dw, dh};
            SDL_RenderCopy(sdl, tex.texture, &src, &dst);
        }
    }
}

/* =========================================================================
 * Editor palette rendering — sidebar with block type selection
 * ========================================================================= */

/* Palette renders to the right of the play area */
#define PALETTE_X (PLAY_AREA_X + PLAY_AREA_W + 15)
#define PALETTE_Y PLAY_AREA_Y
#define PALETTE_ENTRY_H 25
#define PALETTE_W 100

void game_render_editor_palette(const game_ctx_t *ctx)
{
    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);
    int count = editor_system_get_palette_count(ctx->editor);
    int selected = editor_system_get_selected_palette(ctx->editor);

    for (int i = 0; i < count && i < 20; i++) /* Show up to 20 entries */
    {
        const editor_palette_entry_t *entry = editor_system_get_palette_entry(ctx->editor, i);
        if (!entry)
            continue;

        int ey = PALETTE_Y + i * PALETTE_ENTRY_H;

        /* Highlight selected entry */
        if (i == selected)
        {
            SDL_SetRenderDrawColor(sdl, 255, 255, 0, 80);
            SDL_Rect hl = {PALETTE_X - 2, ey - 2, PALETTE_W + 4, PALETTE_ENTRY_H};
            SDL_RenderFillRect(sdl, &hl);
        }

        /* Draw block sprite */
        const char *key = sprite_block_key(entry->block_type);
        if (key)
        {
            sdl2_texture_info_t tex;
            if (sdl2_texture_get(ctx->texture, key, &tex) == SDL2T_OK)
            {
                SDL_Rect dst = {PALETTE_X, ey, tex.width, tex.height};
                SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
            }
        }
    }

    /* Editor status text */
    SDL_Color white = {255, 255, 255, 255};
    const char *title = editor_system_get_level_title(ctx->editor);
    if (title)
        sdl2_font_draw(ctx->font, SDL2F_FONT_COPY, title, PLAY_AREA_X, PLAY_AREA_Y - 15, white);

    int level = editor_system_get_level_number(ctx->editor);
    if (level > 0)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "Level %d", level);
        sdl2_font_draw(ctx->font, SDL2F_FONT_COPY, buf, PLAY_AREA_X + 200, PLAY_AREA_Y - 15, white);
    }
}

/* =========================================================================
 * EyeDude rendering
 * ========================================================================= */

void game_render_eyedude(const game_ctx_t *ctx)
{
    eyedude_render_info_t info = eyedude_system_get_render_info(ctx->eyedude);
    if (!info.visible)
        return;

    /* Select sprite based on direction and frame */
    const char *key = NULL;
    if (info.dir == EYEDUDE_DIR_LEFT)
    {
        static const char *const left_keys[] = {SPR_GUY_LEFT_1, SPR_GUY_LEFT_2, SPR_GUY_LEFT_3,
                                                SPR_GUY_LEFT_4, SPR_GUY_LEFT_5, SPR_GUY_LEFT_6};
        key = left_keys[info.frame_index % 6];
    }
    else if (info.dir == EYEDUDE_DIR_RIGHT)
    {
        static const char *const right_keys[] = {SPR_GUY_RIGHT_1, SPR_GUY_RIGHT_2, SPR_GUY_RIGHT_3,
                                                 SPR_GUY_RIGHT_4, SPR_GUY_RIGHT_5, SPR_GUY_RIGHT_6};
        key = right_keys[info.frame_index % 6];
    }
    else if (info.dir == EYEDUDE_DIR_DEAD)
    {
        key = SPR_EYEDUDE_DEAD;
    }

    if (!key)
        return;

    sdl2_texture_info_t tex;
    if (sdl2_texture_get(ctx->texture, key, &tex) != SDL2T_OK)
        return;

    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);
    SDL_Rect dst = {
        .x = PLAY_AREA_X + info.x - EYEDUDE_WC,
        .y = PLAY_AREA_Y + info.y - EYEDUDE_HC,
        .w = EYEDUDE_WIDTH,
        .h = EYEDUDE_HEIGHT,
    };
    SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
}

/* =========================================================================
 * Border glow rendering — ambient color cycling on the play area border
 * ========================================================================= */

void game_render_border_glow(const game_ctx_t *ctx)
{
    /* Read-only — animation is advanced in mode_game_update() */
    sfx_glow_state_t glow = sfx_system_get_glow_state(ctx->sfx);

    /* Map color_index (0-6) to intensity (100-255) */
    int intensity = 100 + (glow.color_index * 155 / (SFX_GLOW_STEPS - 1));

    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);
    if (glow.use_green)
        SDL_SetRenderDrawColor(sdl, 0, (Uint8)intensity, 0, 255);
    else
        SDL_SetRenderDrawColor(sdl, (Uint8)intensity, 0, 0, 255);

    int bx = PLAY_AREA_X - BORDER_THICKNESS;
    int by = PLAY_AREA_Y - BORDER_THICKNESS;
    int bw = PLAY_AREA_W + 2 * BORDER_THICKNESS;
    int bh = PLAY_AREA_H + 2 * BORDER_THICKNESS;

    SDL_Rect top = {bx, by, bw, BORDER_THICKNESS};
    SDL_Rect bottom = {bx, by + bh - BORDER_THICKNESS, bw, BORDER_THICKNESS};
    SDL_Rect left = {bx, by, BORDER_THICKNESS, bh};
    SDL_Rect right = {bx + bw - BORDER_THICKNESS, by, BORDER_THICKNESS, bh};
    SDL_RenderFillRect(sdl, &top);
    SDL_RenderFillRect(sdl, &bottom);
    SDL_RenderFillRect(sdl, &left);
    SDL_RenderFillRect(sdl, &right);
}

/* =========================================================================
 * Devil eyes rendering
 * ========================================================================= */

void game_render_deveyes(const game_ctx_t *ctx)
{
    sfx_deveye_info_t info = sfx_system_get_deveye_info(ctx->sfx);
    if (!info.active)
        return;

    static const char *const eye_keys[] = {SPR_EYEDUDE,   SPR_EYEDUDE_1, SPR_EYEDUDE_2,
                                           SPR_EYEDUDE_3, SPR_EYEDUDE_4, SPR_EYEDUDE_5};
    int fi = info.frame_index % SFX_DEVEYE_FRAMES;
    sdl2_texture_info_t tex;
    if (sdl2_texture_get(ctx->texture, eye_keys[fi], &tex) != SDL2T_OK)
        return;

    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);
    SDL_Rect dst = {
        .x = PLAY_AREA_X + info.x,
        .y = PLAY_AREA_Y + info.y,
        .w = SFX_DEVEYE_WIDTH,
        .h = SFX_DEVEYE_HEIGHT,
    };
    SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
}

/* =========================================================================
 * Message bar rendering — status text below the play area
 * ========================================================================= */

/* messWindow: x=35, y=655, w=247, h=30 */
#define MESSAGE_AREA_X OFFSET_X
#define MESSAGE_AREA_Y 655
#define MESSAGE_AREA_W 247

void game_render_messages(const game_ctx_t *ctx)
{
    const char *text = message_system_get_text(ctx->message);
    if (!text || text[0] == '\0')
        return;

    SDL_Color yellow = {255, 255, 50, 255};
    sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_COPY, text, MESSAGE_AREA_X + 5, MESSAGE_AREA_Y + 8,
                          yellow);
}

/* =========================================================================
 * Timer display — seconds remaining, right side below play area
 * ========================================================================= */

/* timeWindow: x=477, y=655, w=61, h=35 */
#define TIMER_AREA_X 477
#define TIMER_AREA_Y 655

void game_render_timer(const game_ctx_t *ctx)
{
    if (ctx->time_bonus_total <= 0)
        return; /* No timer for this level */

    /* Format as MM:SS to match legacy DrawLevelTimeBonus */
    int minutes = ctx->time_remaining / 60;
    int seconds = ctx->time_remaining % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);

    /* Color thresholds match legacy: <=10s red, <=60s yellow, else green */
    SDL_Color color;
    if (ctx->time_remaining <= 10)
        color = (SDL_Color){255, 50, 50, 255}; /* Red — critical */
    else if (ctx->time_remaining <= 60)
        color = (SDL_Color){255, 255, 50, 255}; /* Yellow — getting low */
    else
        color = (SDL_Color){50, 255, 50, 255}; /* Green — plenty of time */

    /* Legacy uses titleFont (bold 24pt) with shadow at (2,7) then color at (0,5) */
    sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TITLE, buf, TIMER_AREA_X, TIMER_AREA_Y + 5, color);
}

void game_render_frame(const game_ctx_t *ctx)
{
    sdl2_renderer_clear(ctx->renderer);

    /* Main window background (dark texture tiles across entire window) */
    render_main_background(ctx);

    sdl2_state_mode_t mode = sdl2_state_current(ctx->state);

    switch (mode)
    {
        case SDL2ST_PRESENTS:
            game_render_presents(ctx);
            break;

        case SDL2ST_INTRO:
            game_render_intro(ctx);
            break;

        case SDL2ST_INSTRUCT:
            game_render_instruct(ctx);
            break;

        case SDL2ST_DEMO:
            game_render_demo(ctx);
            break;

        case SDL2ST_PREVIEW:
            game_render_background(ctx);
            game_render_preview(ctx);
            break;

        case SDL2ST_KEYS:
            game_render_keys(ctx);
            break;

        case SDL2ST_KEYSEDIT:
            game_render_keysedit(ctx);
            break;

        case SDL2ST_BONUS:
            game_render_bonus(ctx);
            break;

        case SDL2ST_HIGHSCORE:
            game_render_highscore(ctx);
            break;

        case SDL2ST_GAME:
        case SDL2ST_PAUSE:
            /* Play area background (level-specific tile) */
            game_render_background(ctx);
            /* Playfield border + blocks + paddle + balls + bullets (clipped) */
            game_render_playfield(ctx);
            /* Animated border glow (overwrites static red border) */
            game_render_border_glow(ctx);
            /* EyeDude character (inside play area, after clip removed) */
            game_render_eyedude(ctx);
            /* Devil eyes blink animation */
            game_render_deveyes(ctx);
            /* Score and status */
            game_render_score(ctx);
            /* Lives and level */
            game_render_lives(ctx);
            /* Message bar and timer */
            game_render_messages(ctx);
            game_render_timer(ctx);
            break;

        case SDL2ST_EDIT:
            /* Editor: show grid background + blocks + palette */
            game_render_background(ctx);
            game_render_playfield(ctx);
            game_render_editor_palette(ctx);
            break;

        default:
            break;
    }

    sdl2_renderer_present(ctx->renderer);
}
