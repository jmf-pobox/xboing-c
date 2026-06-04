/*
 * game_render_ui.c -- UI screen rendering for SDL2-based XBoing.
 *
 * Renders attract-mode screens by querying module render info structs
 * and drawing sprites/text via SDL2 texture cache and font system.
 *
 * Each screen has its own render function that reads the module's
 * current state and draws the appropriate elements.
 */

#include "game_render_ui.h"

#include <time.h>

#include <SDL2/SDL.h>

#include "block_types.h"
#include "bonus_system.h"
#include "demo_system.h"
#include "game_context.h"
#include "game_render.h"
#include "highscore_io.h"
#include "highscore_system.h"
#include "intro_system.h"
#include "keys_system.h"
#include "level_system.h"
#include "presents_system.h"
#include "score_system.h"
#include "sdl2_font.h"
#include "sdl2_renderer.h"
#include "sdl2_state.h"
#include "sdl2_texture.h"
#include "sprite_catalog.h"

/* Layout constants from legacy stage.c */
#define OFFSET_X 35
#define PLAY_AREA_X OFFSET_X
#define PLAY_AREA_Y 60
#define PLAY_AREA_W GAME_PLAY_WIDTH
#define PLAY_AREA_H GAME_PLAY_HEIGHT

/* =========================================================================
 * Helper: render a sparkle (star) frame at a position
 * ========================================================================= */

/* Shared play-area frame: stone background tile + border.
 * When glow is true, uses game_render_border_glow (SFX system state).
 * When false, draws a static green border. */
static void render_play_area_frame(const game_ctx_t *ctx, bool glow)
{
    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);
    sdl2_texture_info_t bg;
    if (sdl2_texture_get(ctx->texture, SPR_BGRND_MAIN, &bg) == SDL2T_OK && bg.width > 0 &&
        bg.height > 0)
    {
        int tw = bg.width;
        int th = bg.height;
        for (int ty = PLAY_AREA_Y; ty < PLAY_AREA_Y + PLAY_AREA_H; ty += th)
        {
            for (int tx = PLAY_AREA_X; tx < PLAY_AREA_X + PLAY_AREA_W; tx += tw)
            {
                int dw = tw;
                int dh = th;
                if (tx + dw > PLAY_AREA_X + PLAY_AREA_W)
                    dw = PLAY_AREA_X + PLAY_AREA_W - tx;
                if (ty + dh > PLAY_AREA_Y + PLAY_AREA_H)
                    dh = PLAY_AREA_Y + PLAY_AREA_H - ty;
                SDL_Rect src = {0, 0, dw, dh};
                SDL_Rect dst = {tx, ty, dw, dh};
                SDL_RenderCopy(sdl, bg.texture, &src, &dst);
            }
        }
    }

    if (glow)
    {
        game_render_border_glow(ctx);
    }
    else
    {
        SDL_SetRenderDrawColor(sdl, 0, 200, 0, 255);
        int bx = PLAY_AREA_X - 2;
        int by = PLAY_AREA_Y - 2;
        int bw = PLAY_AREA_W + 4;
        int bh = PLAY_AREA_H + 4;
        SDL_Rect top = {bx, by, bw, 2};
        SDL_Rect bottom = {bx, by + bh - 2, bw, 2};
        SDL_Rect left = {bx, by, 2, bh};
        SDL_Rect right = {bx + bw - 2, by, 2, bh};
        SDL_RenderFillRect(sdl, &top);
        SDL_RenderFillRect(sdl, &bottom);
        SDL_RenderFillRect(sdl, &left);
        SDL_RenderFillRect(sdl, &right);
    }
}

static void render_sparkle(const game_ctx_t *ctx, int x, int y, int frame_index)
{
    if (frame_index < 1 || frame_index > 11)
        return;

    const char *key = sprite_star_key(frame_index);
    sdl2_texture_info_t tex;
    if (sdl2_texture_get(ctx->texture, key, &tex) != SDL2T_OK)
        return;

    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);
    SDL_Rect dst = {x, y, tex.width, tex.height};
    SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
}

/* =========================================================================
 * Presents screen — flag, letters, sparkle, typewriter, wipe
 * ========================================================================= */

void game_render_presents(const game_ctx_t *ctx)
{
    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);

    if (presents_system_is_finished(ctx->presents))
        return;

    /*
     * The presents module uses a WAIT state between content states.
     * Content data persists in the module after each state runs, so we
     * query and render unconditionally — the query functions return the
     * data that was set during the most recent content state.
     *
     * Rendering layers bottom-to-top: flag → credits → letters → sparkle
     * → typewriter → wipe.
     */

    /*
     * All presents content uses mainWindow-absolute coordinates — NO
     * PLAY_AREA_X / PLAY_AREA_Y offset.  The original calls
     * Presents(display, mainWindow) at original/main.c:1190, so all
     * drawing lands at mainWindow (x, y) not playWindow (x, y).
     * Only gameplay rendering (blocks, paddle, ball) uses the play-area
     * offset.
     */

    /* Flag + earth — always render once set (persists from FLAG state) */
    {
        presents_flag_info_t fi;
        presents_system_get_flag_info(ctx->presents, &fi);

        if (fi.flag_x > 0 || fi.flag_y > 0)
        {
            sdl2_texture_info_t tex;
            if (sdl2_texture_get(ctx->texture, SPR_PRESENTS_FLAG, &tex) == SDL2T_OK)
            {
                SDL_Rect dst = {fi.flag_x, fi.flag_y, tex.width, tex.height};
                SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
            }
            if (sdl2_texture_get(ctx->texture, SPR_PRESENTS_EARTH, &tex) == SDL2T_OK)
            {
                SDL_Rect dst = {fi.earth_x, fi.earth_y, tex.width, tex.height};
                SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
            }

            SDL_Color white = {255, 255, 255, 255};
            sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, "Made in Australia", 65,
                                          white, SDL2R_LOGICAL_WIDTH);
            sdl2_font_draw_shadow_centred(
                ctx->font, SDL2F_FONT_COPY,
                "\xc2\xa9 Copyright 1993-1997, Justin C. Kibell, All Rights Reserved",
                fi.copyright_y, white, SDL2R_LOGICAL_WIDTH);
        }
    }

    /* Author credits — bitmap sprites sequenced per original/presents.c.
     * Stage 1: "JUSTIN" at (140, 530).  Stage 2: adds "KIBELL" at (152, 584).
     * Stage 3: replaces both with "presents" at (77, 562).
     * Positions: center = (MAIN_WIDTH+PLAY_WIDTH)/2 = 282. */
    {
        int credits_stage = presents_system_get_credits_stage(ctx->presents);
        sdl2_texture_info_t tex;

        if (credits_stage == 1 || credits_stage == 2)
        {
            if (sdl2_texture_get(ctx->texture, SPR_PRESENTS_JUSTIN, &tex) == SDL2T_OK)
            {
                SDL_Rect dst = {140, 530, tex.width, tex.height};
                SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
            }
        }
        if (credits_stage == 2)
        {
            if (sdl2_texture_get(ctx->texture, SPR_PRESENTS_KIBELL, &tex) == SDL2T_OK)
            {
                SDL_Rect dst = {152, 584, tex.width, tex.height};
                SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
            }
        }
        if (credits_stage == 3)
        {
            if (sdl2_texture_get(ctx->texture, SPR_PRESENTS, &tex) == SDL2T_OK)
            {
                SDL_Rect dst = {77, 562, tex.width, tex.height};
                SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
            }
        }
    }

    /* XBOING letter stamps — variable per-letter widths matching
     * original/presents.c:319-354 (mainWindow coordinates). */
    {
        presents_letter_info_t li;
        if (presents_system_get_letter_info(ctx->presents, &li))
        {
            static const char *const letter_keys[] = {SPR_TITLE_X, SPR_TITLE_B, SPR_TITLE_O,
                                                      SPR_TITLE_I, SPR_TITLE_N, SPR_TITLE_G};
            static const int letter_widths[] = {71, 73, 83, 41, 85, 88};
            int lx = 40;
            for (int i = 0; i <= li.letter_index && i < 6; i++)
            {
                sdl2_texture_info_t tex;
                if (sdl2_texture_get(ctx->texture, letter_keys[i], &tex) == SDL2T_OK)
                {
                    SDL_Rect dst = {lx, 220, tex.width, tex.height};
                    SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
                }
                lx += 10 + letter_widths[i];
            }
        }

        /* "II" suffix after all 6 letters — original/presents.c:344-348
         * draws two 'I' glyphs below the XBOING title. */
        presents_ii_info_t ii;
        if (presents_system_get_ii_info(ctx->presents, &ii))
        {
            sdl2_texture_info_t tex;
            if (sdl2_texture_get(ctx->texture, SPR_TITLE_I, &tex) == SDL2T_OK)
            {
                SDL_Rect d1 = {ii.i1_x, ii.y, tex.width, tex.height};
                SDL_RenderCopy(sdl, tex.texture, NULL, &d1);
                SDL_Rect d2 = {ii.i2_x, ii.y, tex.width, tex.height};
                SDL_RenderCopy(sdl, tex.texture, NULL, &d2);
            }
        }
    }

    /* Sparkle — render when active */
    {
        presents_sparkle_info_t si;
        presents_system_get_sparkle_info(ctx->presents, &si);
        if (si.active)
            render_sparkle(ctx, si.x, si.y, si.frame_index);
    }

    /* Typewriter text lines — centered within PRESENTS_TOTAL_WIDTH.
     * ti.x_offset from the system is 0 (the system notes the
     * integration layer should compute centering).  We measure the
     * visible substring and center it horizontally. */
    {
        SDL_Color red = {255, 0, 0, 255};
        for (int line = 0; line < 3; line++)
        {
            presents_typewriter_info_t ti;
            if (presents_system_get_typewriter_info(ctx->presents, line, &ti))
            {
                if (ti.chars_visible > 0 && ti.text)
                {
                    char buf[256];
                    int n = ti.chars_visible;
                    if (n > 255)
                        n = 255;
                    snprintf(buf, sizeof(buf), "%.*s", n, ti.text);
                    sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_DATA, buf, ti.y, red,
                                                  PRESENTS_TOTAL_WIDTH);
                }
            }
        }
    }

    /* Curtain wipe — draw black rectangles closing from top and bottom.
     * Coordinates from the presents system use PRESENTS_TOTAL
     * dimensions (565×710). */
    {
        presents_wipe_info_t wi;
        presents_system_get_wipe_info(ctx->presents, &wi);
        if (wi.top_y > 0 || wi.complete)
        {
            SDL_SetRenderDrawColor(sdl, 0, 0, 0, 255);
            SDL_Rect top_rect = {0, 0, PRESENTS_TOTAL_WIDTH, wi.top_y};
            SDL_Rect bot_rect = {0, wi.bottom_y, PRESENTS_TOTAL_WIDTH,
                                 PRESENTS_TOTAL_HEIGHT - wi.bottom_y};
            SDL_RenderFillRect(sdl, &top_rect);
            SDL_RenderFillRect(sdl, &bot_rect);

            /* Red lines at the wipe edges per original/presents.c:529-532 */
            SDL_SetRenderDrawColor(sdl, 255, 0, 0, 255);
            SDL_RenderDrawLine(sdl, 2, wi.top_y, PRESENTS_TOTAL_WIDTH - 2, wi.top_y);
            SDL_RenderDrawLine(sdl, 2, wi.bottom_y - 1, PRESENTS_TOTAL_WIDTH - 2, wi.bottom_y - 1);
        }
    }
}

/* =========================================================================
 * Intro screen — block descriptions table + sparkle
 * ========================================================================= */

void game_render_intro(const game_ctx_t *ctx)
{
    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);
    intro_state_t state = intro_system_get_state(ctx->intro);

    if (state == INTRO_STATE_NONE)
        return;

    render_play_area_frame(ctx, state == INTRO_STATE_EXPLODE);

    /* Title */
    if (state >= INTRO_STATE_TITLE)
    {
        sdl2_texture_info_t tex;
        if (sdl2_texture_get(ctx->texture, SPR_TITLE_BIG, &tex) == SDL2T_OK)
        {
            int tx = PLAY_AREA_X + (PLAY_AREA_W - tex.width) / 2;
            SDL_Rect dst = {tx, PLAY_AREA_Y + 10, tex.width, tex.height};
            SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
        }
    }

    /* Block descriptions table.  Each entry's .type is an
     * intro_block_type_t enum (0..21), NOT a block_types.h constant.
     * The mapping table below translates to the correct sprite key.
     * PADDLE and BULLET_ITEM are not block types and get their own
     * sprite keys directly. */
    if (state >= INTRO_STATE_BLOCKS)
    {
        /* Map INTRO_BLK_* → sprite key.  Order matches the
         * intro_block_type_t enum in include/intro_system.h. */
        static const char *const intro_sprite_keys[] = {
            SPR_BLOCK_RED,        /* INTRO_BLK_RED         → RED_BLK */
            SPR_BLOCK_ROAMER,     /* INTRO_BLK_ROAMER      → ROAMER_BLK */
            SPR_BLOCK_PAD_EXPAND, /* INTRO_BLK_PAD_EXPAND  → PAD_EXPAND_BLK */
            SPR_BLOCK_X2_1,       /* INTRO_BLK_BONUSX2     → BONUSX2_BLK */
            SPR_BLOCK_LOTSAMMO,   /* INTRO_BLK_MAXAMMO     → MAXAMMO_BLK */
            SPR_BLOCK_GREEN,      /* INTRO_BLK_DROP        → DROP_BLK */
            SPR_BLOCK_YELLOW,     /* INTRO_BLK_BULLET      → BULLET_BLK */
            SPR_BLOCK_HYPERSPACE, /* INTRO_BLK_HYPERSPACE  → HYPERSPACE_BLK */
            SPR_BLOCK_REVERSE,    /* INTRO_BLK_REVERSE     → REVERSE_BLK */
            SPR_BLOCK_MACHGUN,    /* INTRO_BLK_MGUN        → MGUN_BLK */
            SPR_BLOCK_MULTIBALL,  /* INTRO_BLK_MULTIBALL   → MULTIBALL_BLK */
            SPR_BLOCK_BONUS_1,    /* INTRO_BLK_BONUS       → BONUS_BLK */
            SPR_BLOCK_COUNTER_5,  /* INTRO_BLK_COUNTER     → COUNTER_BLK (slide=5) */
            SPR_BLOCK_CLOCK,      /* INTRO_BLK_TIMER       → TIMER_BLK */
            SPR_BLOCK_BLACK,      /* INTRO_BLK_BLACK       → BLACK_BLK */
            SPR_BLOCK_BOMB,       /* INTRO_BLK_BOMB        → BOMB_BLK */
            SPR_PADDLE_SMALL,     /* INTRO_BLK_PADDLE      → paddle sprite */
            SPR_BULLET,           /* INTRO_BLK_BULLET_ITEM → bullet sprite */
            SPR_BLOCK_DEATH_1,    /* INTRO_BLK_DEATH       → DEATH_BLK */
            SPR_BLOCK_EXTRABALL,  /* INTRO_BLK_EXTRABALL   → EXTRABALL_BLK */
            SPR_BLOCK_WALLOFF,    /* INTRO_BLK_WALLOFF     → WALLOFF_BLK */
            SPR_BLOCK_STICKY,     /* INTRO_BLK_STICKY      → STICKY_BLK */
        };
        _Static_assert(sizeof(intro_sprite_keys) / sizeof(intro_sprite_keys[0]) ==
                           INTRO_BLOCK_TOTAL,
                       "intro_sprite_keys must match intro_block_type_t enum count");

        const intro_block_entry_t *entries = NULL;
        int count = intro_system_get_block_table(ctx->intro, &entries);

        /* Original uses green for all block descriptions
         * (original/intro.c:207 etc.). */
        SDL_Color green = {0, 255, 0, 255};

        for (int i = 0; i < count; i++)
        {
            /* Draw block sprite using the corrected mapping. */
            int type_idx = (int)entries[i].type;
            const char *key = NULL;
            if (type_idx >= 0 &&
                type_idx < (int)(sizeof(intro_sprite_keys) / sizeof(intro_sprite_keys[0])))
            {
                key = intro_sprite_keys[type_idx];
            }
            if (key)
            {
                sdl2_texture_info_t tex;
                if (sdl2_texture_get(ctx->texture, key, &tex) == SDL2T_OK)
                {
                    SDL_Rect dst = {PLAY_AREA_X + entries[i].x + entries[i].x_adjust,
                                    PLAY_AREA_Y + entries[i].y + entries[i].y_adjust, tex.width,
                                    tex.height};
                    SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
                }
            }

            /* Draw description text — all green per original. */
            if (entries[i].description)
            {
                sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, entries[i].description,
                                      PLAY_AREA_X + entries[i].x + 60,
                                      PLAY_AREA_Y + entries[i].y + 2, green);
            }
        }

        /* "Insert coin to start the game" — inside the play area
         * at the bottom, matching original/intro.c:302-305 which
         * draws at y = PLAY_HEIGHT - 27 inside playWindow.  Original
         * uses X11 "tann" color (tan). */
        SDL_Color tan_clr = {210, 180, 140, 255};
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, "Insert coin to start the game",
                                      PLAY_AREA_Y + PLAY_AREA_H - 27, tan_clr,
                                      PLAY_AREA_W + 2 * PLAY_AREA_X);
    }

    /* Sparkle */
    if (state == INTRO_STATE_EXPLODE)
    {
        intro_sparkle_info_t si;
        intro_system_get_sparkle_info(ctx->intro, &si);
        if (si.active)
            render_sparkle(ctx, PLAY_AREA_X + si.x, PLAY_AREA_Y + si.y, si.frame_index);
    }
}

/* =========================================================================
 * Instructions screen — text lines + sparkle
 * ========================================================================= */

void game_render_instruct(const game_ctx_t *ctx)
{
    intro_state_t state = intro_system_get_state(ctx->intro);

    if (state == INTRO_STATE_NONE)
        return;

    render_play_area_frame(ctx, state == INTRO_STATE_EXPLODE);

    /* XBOING title image — original/inst.c:219 calls DoIntroTitle
     * which draws the same title sprite as the intro blocks screen. */
    if (state >= INTRO_STATE_TITLE)
    {
        sdl2_texture_info_t tex;
        if (sdl2_texture_get(ctx->texture, SPR_TITLE_BIG, &tex) == SDL2T_OK)
        {
            int tx = PLAY_AREA_X + (PLAY_AREA_W - tex.width) / 2;
            SDL_Rect dst = {tx, PLAY_AREA_Y + 10, tex.width, tex.height};
            SDL_RenderCopy(sdl2_renderer_get(ctx->renderer), tex.texture, NULL, &dst);
        }
    }

    /* "- Instructions -" title + body text + "Insert coin" —
     * all drawn during INTRO_STATE_TEXT per original/inst.c:134-163. */
    if (state >= INTRO_STATE_TEXT)
    {
        /* Title: titleFont, y=110, red, centered across PLAY_WIDTH. */
        SDL_Color red = {255, 0, 0, 255};
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TITLE, "- Instructions -",
                                      PLAY_AREA_Y + 110, red, PLAY_AREA_W + 2 * PLAY_AREA_X);

        const intro_instruct_line_t *lines = NULL;
        int count = intro_system_get_instruct_text(ctx->intro, &lines);

        SDL_Color green_bright = {0, 255, 0, 255};
        SDL_Color green_medium = {0, 187, 0, 255};

        int y = PLAY_AREA_Y + 150;
        for (int i = 0; i < count; i++)
        {
            if (lines[i].is_spacer)
            {
                y += 18;
                continue;
            }

            SDL_Color clr = lines[i].color_index % 2 ? green_bright : green_medium;
            sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_DATA, lines[i].text, y, clr,
                                          PLAY_AREA_W + 2 * PLAY_AREA_X);
            y += 18;
        }

        /* "Insert coin" — original/inst.c:163: textFont, tann,
         * y = PLAY_HEIGHT - 40, centred across PLAY_WIDTH. */
        SDL_Color tan_clr = {210, 180, 140, 255};
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, "Insert coin to start the game",
                                      PLAY_AREA_Y + PLAY_AREA_H - 40, tan_clr,
                                      PLAY_AREA_W + 2 * PLAY_AREA_X);
    }

    /* Sparkle */
    if (state == INTRO_STATE_EXPLODE)
    {
        intro_sparkle_info_t si;
        intro_system_get_sparkle_info(ctx->intro, &si);
        if (si.active)
            render_sparkle(ctx, PLAY_AREA_X + si.x, PLAY_AREA_Y + si.y, si.frame_index);
    }
}

/* =========================================================================
 * Demo screen — ball trail + descriptive text
 * ========================================================================= */

void game_render_demo(const game_ctx_t *ctx)
{
    demo_state_t state = demo_system_get_state(ctx->demo);
    if (state == DEMO_STATE_NONE)
        return;

    render_play_area_frame(ctx, state == DEMO_STATE_SPARKLE);

    /* Blocks from demo.data — rendered after background, before title/trail */
    if (state >= DEMO_STATE_BLOCKS)
        game_render_blocks(ctx);

    /* XBOING title image — same as intro/instruct per original/demo.c:117
     * DrawIntroTitle(display, window, 10, 10). */
    if (state >= DEMO_STATE_TITLE)
    {
        sdl2_texture_info_t tex;
        if (sdl2_texture_get(ctx->texture, SPR_TITLE_BIG, &tex) == SDL2T_OK)
        {
            int tx = PLAY_AREA_X + (PLAY_AREA_W - tex.width) / 2;
            SDL_Rect dst = {tx, PLAY_AREA_Y + 10, tex.width, tex.height};
            SDL_RenderCopy(sdl2_renderer_get(ctx->renderer), tex.texture, NULL, &dst);
        }
    }

    /* Ball trail + decorations + paddle — all drawn during BLOCKS state.
     * Order matches original/demo.c DoBlocks: trail → half-block → paddle. */
    if (state >= DEMO_STATE_BLOCKS)
    {
        SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);

        /* Ball trail animation */
        const demo_ball_pos_t *trail = NULL;
        int count = demo_system_get_ball_trail(ctx->demo, &trail);
        for (int i = 0; i < count; i++)
        {
            const char *key = sprite_ball_key(trail[i].frame_index);
            sdl2_texture_info_t tex;
            if (sdl2_texture_get(ctx->texture, key, &tex) == SDL2T_OK)
            {
                SDL_Rect dst = {PLAY_AREA_X + trail[i].x, PLAY_AREA_Y + trail[i].y, tex.width,
                                tex.height};
                SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
            }
        }

        /* Half-disintegrated block at (col=2, row=12) per original/demo.c:173-176 */
        const char *exkey = sprite_block_explode_key(YELLOW_BLK, 1);
        sdl2_texture_info_t etex;
        if (sdl2_texture_get(ctx->texture, exkey, &etex) == SDL2T_OK)
        {
            SDL_Rect dst = {PLAY_AREA_X + 110, PLAY_AREA_Y + 384, etex.width, etex.height};
            SDL_RenderCopy(sdl, etex.texture, NULL, &dst);
        }

        /* Paddle + left arrow per original/demo.c:178-182 */
        int px = PLAY_AREA_X + PLAY_AREA_W / 2;
        int py = PLAY_AREA_Y + PLAY_AREA_H - 90;

        sdl2_texture_info_t ptex;
        if (sdl2_texture_get(ctx->texture, SPR_PADDLE_HUGE, &ptex) == SDL2T_OK)
        {
            SDL_Rect dst = {px - 35, py, ptex.width, ptex.height};
            SDL_RenderCopy(sdl, ptex.texture, NULL, &dst);
        }

        sdl2_texture_info_t atex;
        if (sdl2_texture_get(ctx->texture, SPR_LEFT_ARROW, &atex) == SDL2T_OK)
        {
            SDL_Rect dst = {px - 75, py - 1, atex.width, atex.height};
            SDL_RenderCopy(sdl, atex.texture, NULL, &dst);
        }
    }

    /* Descriptive text */
    if (state >= DEMO_STATE_TEXT)
    {
        const demo_text_line_t *lines = NULL;
        int count = demo_system_get_demo_text(ctx->demo, &lines);

        SDL_Color yellow = {255, 255, 0, 255};
        for (int i = 0; i < count; i++)
        {
            if (lines[i].text)
                sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_DATA, lines[i].text,
                                      PLAY_AREA_X + lines[i].x, PLAY_AREA_Y + lines[i].y, yellow);
        }

        /* "Insert coin" per original/demo.c:241 */
        SDL_Color tan_clr = {210, 180, 140, 255};
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, "Insert coin to start the game",
                                      PLAY_AREA_Y + PLAY_AREA_H - 27, tan_clr,
                                      PLAY_AREA_W + 2 * PLAY_AREA_X);
    }

    /* Sparkle */
    if (state == DEMO_STATE_SPARKLE)
    {
        demo_sparkle_info_t si;
        demo_system_get_sparkle_info(ctx->demo, &si);
        if (si.active)
            render_sparkle(ctx, PLAY_AREA_X + si.x, PLAY_AREA_Y + si.y, si.frame_index);
    }
}

/* =========================================================================
 * Preview screen — random level display
 * ========================================================================= */

void game_render_preview(const game_ctx_t *ctx)
{
    demo_state_t state = demo_system_get_state(ctx->demo);
    if (state == DEMO_STATE_NONE)
        return;

    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);

    /* Background tile — original/preview.c:118 cycles backgrounds 2-5 */
    int level = demo_system_get_preview_level(ctx->demo);
    int bgrnd = (level % 4) + 2;
    const char *bg_key = sprite_background_key(bgrnd);
    sdl2_texture_info_t bg;
    if (sdl2_texture_get(ctx->texture, bg_key, &bg) == SDL2T_OK && bg.width > 0 && bg.height > 0)
    {
        for (int ty = PLAY_AREA_Y; ty < PLAY_AREA_Y + PLAY_AREA_H; ty += bg.height)
        {
            for (int tx = PLAY_AREA_X; tx < PLAY_AREA_X + PLAY_AREA_W; tx += bg.width)
            {
                int dw = bg.width;
                int dh = bg.height;
                if (tx + dw > PLAY_AREA_X + PLAY_AREA_W)
                    dw = PLAY_AREA_X + PLAY_AREA_W - tx;
                if (ty + dh > PLAY_AREA_Y + PLAY_AREA_H)
                    dh = PLAY_AREA_Y + PLAY_AREA_H - ty;
                SDL_Rect src = {0, 0, dw, dh};
                SDL_Rect dst = {tx, ty, dw, dh};
                SDL_RenderCopy(sdl, bg.texture, &src, &dst);
            }
        }
    }

    /* Blocks on top of background — original/preview.c:130 */
    if (state >= DEMO_STATE_BLOCKS)
        game_render_blocks(ctx);

    /* Red border — original/preview.c uses red XSetWindowBorder */
    if (state == DEMO_STATE_SPARKLE)
    {
        game_render_border_glow(ctx);
    }
    else
    {
        SDL_SetRenderDrawColor(sdl, 200, 0, 0, 255);
        int bx = PLAY_AREA_X - 2;
        int by = PLAY_AREA_Y - 2;
        int bw = PLAY_AREA_W + 4;
        int bh = PLAY_AREA_H + 4;
        SDL_Rect top = {bx, by, bw, 2};
        SDL_Rect bottom = {bx, by + bh - 2, bw, 2};
        SDL_Rect left = {bx, by, 2, bh};
        SDL_Rect right = {bx + bw - 2, by, 2, bh};
        SDL_RenderFillRect(sdl, &top);
        SDL_RenderFillRect(sdl, &bottom);
        SDL_RenderFillRect(sdl, &left);
        SDL_RenderFillRect(sdl, &right);
    }

    /* Level name at bottom — original/preview.c:133-134 */
    const char *level_title = level_system_get_title(ctx->level);
    if (level_title && level_title[0] != '\0')
    {
        SDL_Color red = {255, 0, 0, 255};
        char name_buf[80];
        snprintf(name_buf, sizeof(name_buf), "- %s -", level_title);
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, name_buf,
                                      PLAY_AREA_Y + PLAY_AREA_H - 60, red,
                                      PLAY_AREA_W + 2 * PLAY_AREA_X);
    }

    /* "Insert coin to start the game" — original/preview.c:130 */
    SDL_Color tan_clr = {210, 180, 140, 255};
    sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, "Insert coin to start the game",
                                  PLAY_AREA_Y + PLAY_AREA_H - 35, tan_clr,
                                  PLAY_AREA_W + 2 * PLAY_AREA_X);

    if (state == DEMO_STATE_SPARKLE)
    {
        demo_sparkle_info_t si;
        demo_system_get_sparkle_info(ctx->demo, &si);
        if (si.active)
            render_sparkle(ctx, PLAY_AREA_X + si.x, PLAY_AREA_Y + si.y, si.frame_index);
    }
}

/* =========================================================================
 * Keys screen — game controls binding table
 * ========================================================================= */

void game_render_keys(const game_ctx_t *ctx)
{
    keys_state_t state = keys_system_get_state(ctx->keys);
    if (state == KEYS_STATE_NONE)
        return;

    render_play_area_frame(ctx, state == KEYS_STATE_SPARKLE);

    /* XBOING title image per original/keys.c:309 DoIntroTitle */
    if (state >= KEYS_STATE_TITLE)
    {
        sdl2_texture_info_t tex;
        if (sdl2_texture_get(ctx->texture, SPR_TITLE_BIG, &tex) == SDL2T_OK)
        {
            int tx = PLAY_AREA_X + (PLAY_AREA_W - tex.width) / 2;
            SDL_Rect dst = {tx, PLAY_AREA_Y + 10, tex.width, tex.height};
            SDL_RenderCopy(sdl2_renderer_get(ctx->renderer), tex.texture, NULL, &dst);
        }
    }

    if (state >= KEYS_STATE_TEXT)
    {
        SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);
        SDL_Color red = {255, 0, 0, 255};
        SDL_Color yellow = {255, 255, 0, 255};
        SDL_Color green = {0, 255, 0, 255};
        SDL_Color tan_clr = {210, 180, 140, 255};

        /* "- Game Controls -" title per original/keys.c:143 */
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TITLE, "- Game Controls -",
                                      PLAY_AREA_Y + 120, red, PLAY_AREA_W + 2 * PLAY_AREA_X);

        /* Horizontal separator line per original/keys.c:147-148 */
        int line_y = PLAY_AREA_Y + 160;
        SDL_SetRenderDrawColor(sdl, 0, 0, 0, 255);
        SDL_RenderDrawLine(sdl, PLAY_AREA_X + 32, line_y + 2, PLAY_AREA_X + PLAY_AREA_W - 28,
                           line_y + 2);
        SDL_SetRenderDrawColor(sdl, 255, 255, 255, 255);
        SDL_RenderDrawLine(sdl, PLAY_AREA_X + 30, line_y, PLAY_AREA_X + PLAY_AREA_W - 30, line_y);

        /* Mouse sprite + arrows + paddle labels per original/keys.c:151-162.
         * mouse_y already includes PLAY_AREA_Y via line_y. */
        int mouse_y = line_y + 18;
        int cx = PLAY_AREA_X + PLAY_AREA_W / 2;

        sdl2_texture_info_t mtex;
        if (sdl2_texture_get(ctx->texture, SPR_MOUSE, &mtex) == SDL2T_OK)
        {
            SDL_Rect dst = {cx - 17, mouse_y, mtex.width, mtex.height};
            SDL_RenderCopy(sdl, mtex.texture, NULL, &dst);
        }

        sdl2_texture_info_t latex;
        if (sdl2_texture_get(ctx->texture, SPR_LEFT_ARROW, &latex) == SDL2T_OK)
        {
            SDL_Rect dst = {cx - 17 - 10 - 35, mouse_y + 28, latex.width, latex.height};
            SDL_RenderCopy(sdl, latex.texture, NULL, &dst);
        }
        sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, "Paddle left",
                              cx - 17 - 10 - 35 - 40 - 60, mouse_y + 28, green);

        sdl2_texture_info_t ratex;
        if (sdl2_texture_get(ctx->texture, SPR_RIGHT_ARROW, &ratex) == SDL2T_OK)
        {
            SDL_Rect dst = {cx + 17 + 10, mouse_y + 28, ratex.width, ratex.height};
            SDL_RenderCopy(sdl, ratex.texture, NULL, &dst);
        }
        sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, "Paddle right", cx + 17 + 10 + 40,
                              mouse_y + 28, green);

        /* Key bindings — two columns, yellow, textFont with shadow
         * per original/keys.c:164-246 */
        const keys_binding_entry_t *bindings = NULL;
        int count = keys_system_get_game_bindings(ctx->keys, &bindings);

        for (int i = 0; i < count; i++)
        {
            int x = (bindings[i].column == 0) ? PLAY_AREA_X + 30 : PLAY_AREA_X + 280;
            int row = (bindings[i].column == 0) ? i : i - 10;
            sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, bindings[i].text, x,
                                  PLAY_AREA_Y + 250 + row * 27, yellow);
        }

        /* Bottom separator line per original/keys.c:249-250 */
        int bot_y = PLAY_AREA_Y + 250 + 10 * 27 + 10;
        SDL_SetRenderDrawColor(sdl, 0, 0, 0, 255);
        SDL_RenderDrawLine(sdl, PLAY_AREA_X + 32, bot_y + 2, PLAY_AREA_X + PLAY_AREA_W - 28,
                           bot_y + 2);
        SDL_SetRenderDrawColor(sdl, 255, 255, 255, 255);
        SDL_RenderDrawLine(sdl, PLAY_AREA_X + 30, bot_y, PLAY_AREA_X + PLAY_AREA_W - 30, bot_y);

        /* "Insert coin" per original/keys.c:252-253 */
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, "Insert coin to start the game",
                                      PLAY_AREA_Y + PLAY_AREA_H - 30, tan_clr,
                                      PLAY_AREA_W + 2 * PLAY_AREA_X);
    }

    if (state == KEYS_STATE_SPARKLE)
    {
        keys_sparkle_info_t si;
        keys_system_get_sparkle_info(ctx->keys, &si);
        if (si.active)
            render_sparkle(ctx, PLAY_AREA_X + si.x, PLAY_AREA_Y + si.y, si.frame_index);
    }
}

/* =========================================================================
 * Editor keys screen — editor controls
 * ========================================================================= */

void game_render_keysedit(const game_ctx_t *ctx)
{
    keys_state_t state = keys_system_get_state(ctx->keys);
    if (state == KEYS_STATE_NONE)
        return;

    render_play_area_frame(ctx, state == KEYS_STATE_SPARKLE);

    /* XBOING title image — same as KEYS screen, original/keysedit.c:147 */
    if (state >= KEYS_STATE_TITLE)
    {
        sdl2_texture_info_t tex;
        if (sdl2_texture_get(ctx->texture, SPR_TITLE_BIG, &tex) == SDL2T_OK)
        {
            int tx = PLAY_AREA_X + (PLAY_AREA_W - tex.width) / 2;
            SDL_Rect dst = {tx, PLAY_AREA_Y + 10, tex.width, tex.height};
            SDL_RenderCopy(sdl2_renderer_get(ctx->renderer), tex.texture, NULL, &dst);
        }
    }

    if (state >= KEYS_STATE_TEXT)
    {
        SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);
        SDL_Color red = {255, 0, 0, 255};
        SDL_Color yellow = {255, 255, 0, 255};

        /* "- Level Editor Controls -" in red — original/keysedit.c:153 */
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TITLE, "- Level Editor Controls -",
                                      PLAY_AREA_Y + 120, red, PLAY_AREA_W + 2 * PLAY_AREA_X);

        /* Separator line — original/keysedit.c:157 */
        int line_y = PLAY_AREA_Y + 160;
        SDL_SetRenderDrawColor(sdl, 0, 0, 0, 255);
        SDL_RenderDrawLine(sdl, PLAY_AREA_X + 32, line_y + 2, PLAY_AREA_X + PLAY_AREA_W - 28,
                           line_y + 2);
        SDL_SetRenderDrawColor(sdl, 255, 255, 255, 255);
        SDL_RenderDrawLine(sdl, PLAY_AREA_X + 30, line_y, PLAY_AREA_X + PLAY_AREA_W - 30, line_y);

        /* Description text — original/keysedit.c:138-146, alternating greens */
        SDL_Color green_bright = {0, 255, 0, 255};
        SDL_Color green_dark = {0, 187, 0, 255};
        const keys_info_line_t *info = NULL;
        int info_count = keys_system_get_editor_info(ctx->keys, &info);
        int text_y = line_y + 20;
        int j = 0;
        for (int i = 0; i < info_count; i++)
        {
            if (info[i].text)
            {
                SDL_Color clr = (j % 2) ? green_bright : green_dark;
                sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_COPY, info[i].text, text_y, clr,
                                              PLAY_AREA_W + 2 * PLAY_AREA_X);
                j++;
            }
            text_y += 24;
        }

        /* Key bindings — original/keysedit.c:186-210 */
        const keys_binding_entry_t *bindings = NULL;
        int bind_count = keys_system_get_editor_bindings(ctx->keys, &bindings);
        int start_y = text_y + 20;
        for (int i = 0; i < bind_count; i++)
        {
            int x = (bindings[i].column == 0) ? PLAY_AREA_X + 30 : PLAY_AREA_X + 270;
            int row = (bindings[i].column == 0) ? i : i - 5;
            sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, bindings[i].text, x,
                                  start_y + row * 30, yellow);
        }

        /* Bottom separator — original/keysedit.c:215 */
        int bot_y = start_y + 5 * 30 + 15;
        SDL_SetRenderDrawColor(sdl, 0, 0, 0, 255);
        SDL_RenderDrawLine(sdl, PLAY_AREA_X + 32, bot_y + 2, PLAY_AREA_X + PLAY_AREA_W - 28,
                           bot_y + 2);
        SDL_SetRenderDrawColor(sdl, 255, 255, 255, 255);
        SDL_RenderDrawLine(sdl, PLAY_AREA_X + 30, bot_y, PLAY_AREA_X + PLAY_AREA_W - 30, bot_y);

        /* "Insert coin to start the game" — original/keysedit.c:201-202 */
        SDL_Color tan_clr = {210, 180, 140, 255};
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, "Insert coin to start the game",
                                      PLAY_AREA_Y + PLAY_AREA_H - 30, tan_clr,
                                      PLAY_AREA_W + 2 * PLAY_AREA_X);
    }

    if (state == KEYS_STATE_SPARKLE)
    {
        keys_sparkle_info_t si;
        keys_system_get_sparkle_info(ctx->keys, &si);
        if (si.active)
            render_sparkle(ctx, PLAY_AREA_X + si.x, PLAY_AREA_Y + si.y, si.frame_index);
    }
}

/* =========================================================================
 * Bonus tally screen — score breakdown between levels
 * ========================================================================= */

/* Ordinal suffix for high-score ranking — "st"/"nd"/"rd"/"th".
 * Matches original/bonus.c:DoHighScore (lines 546-571). */
static const char *rank_suffix(int rank)
{
    switch (rank)
    {
        case 1:
            return "st";
        case 2:
            return "nd";
        case 3:
            return "rd";
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
            return "th";
        default:
            return "";
    }
}

/* Ball-border decoration around the bonus screen.  Mirrors
 * original/bonus.c:DrawBallBorder (lines 164-198): rows of animated
 * ball sprites along all four edges of the main window, 22px
 * stride, cycling through the ball-animation frames.  The original
 * uses BALL_SLIDES=5 frames; modern catalog has SPR_BALL_1..4 (4
 * frames) — same visual effect, one fewer phase.
 *
 * Border positions mirror the original constants: BORDER_LEFT=55,
 * BORDER_TOP=73, BORDER_RIGHT/BOTTOM at TOTAL_W/H - 50/85.  Modern
 * canvas is 575x720 vs original 565x710; the 10px tolerance leaves
 * the border visually identical. */
static void draw_ball_border(const game_ctx_t *ctx)
{
    static const char *const ball_frames[] = {SPR_BALL_1, SPR_BALL_2, SPR_BALL_3, SPR_BALL_4};
    const int n_frames = (int)(sizeof(ball_frames) / sizeof(ball_frames[0]));
    sdl2_texture_info_t balls[4];
    for (int i = 0; i < n_frames; i++)
    {
        if (sdl2_texture_get(ctx->texture, ball_frames[i], &balls[i]) != SDL2T_OK)
        {
            return;
        }
    }

    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);
    const int BORDER_LEFT = 55;
    const int BORDER_TOP = 73;
    const int BORDER_RIGHT = SDL2R_LOGICAL_WIDTH - 50;
    const int BORDER_BOTTOM = SDL2R_LOGICAL_HEIGHT - 85;
    const int STRIDE = 22;
    int slide = 0;

    /* Top + bottom rows. */
    for (int x = BORDER_LEFT; x < BORDER_RIGHT; x += STRIDE)
    {
        const sdl2_texture_info_t *f = &balls[slide % n_frames];
        SDL_Rect top = {.x = x, .y = BORDER_TOP, .w = f->width, .h = f->height};
        SDL_Rect bot = {.x = x, .y = BORDER_BOTTOM, .w = f->width, .h = f->height};
        SDL_RenderCopy(sdl, f->texture, NULL, &top);
        SDL_RenderCopy(sdl, f->texture, NULL, &bot);
        slide++;
    }
    /* Left + right columns. */
    for (int y = BORDER_TOP; y < BORDER_BOTTOM; y += STRIDE)
    {
        const sdl2_texture_info_t *f = &balls[slide % n_frames];
        SDL_Rect lhs = {.x = BORDER_LEFT, .y = y, .w = f->width, .h = f->height};
        SDL_Rect rhs = {.x = BORDER_RIGHT, .y = y, .w = f->width, .h = f->height};
        SDL_RenderCopy(sdl, f->texture, NULL, &lhs);
        SDL_RenderCopy(sdl, f->texture, NULL, &rhs);
        slide++;
    }
}

void game_render_bonus(const game_ctx_t *ctx)
{
    bonus_state_t state = bonus_system_get_state(ctx->bonus);
    if (state == BONUS_STATE_FINISH)
        return;

    /* Original uses pure colors: white text, red headers, yellow
     * bonus lines, blue messages (Sorry/Bonus void), tan footer.
     * Match those choices — the modern's softened red/green palette
     * was a guess. */
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color yellow = {255, 255, 0, 255};
    SDL_Color blue = {127, 127, 255, 255};
    SDL_Color red = {255, 0, 0, 255};
    SDL_Color tan = {210, 180, 140, 255};

    const int W = SDL2R_LOGICAL_WIDTH;
    const int center_x = W / 2;

    /* Decorative ball border around the full window — matches the
     * original's DrawBallBorder. */
    draw_ball_border(ctx);

    /* Small XBOING title pixmap at top.  Replaces the previous "BONUS"
     * text — the original (bonus.c:218 DrawSmallIntroTitle) uses the
     * gold pixmap from the presents/intro asset set. */
    sdl2_texture_info_t title;
    int title_bottom = 60;
    if (sdl2_texture_get(ctx->texture, SPR_TITLE_SMALL, &title) == SDL2T_OK)
    {
        SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);
        SDL_Rect dst = {
            .x = center_x - title.width / 2, .y = 60, .w = title.width, .h = title.height};
        SDL_RenderCopy(sdl, title.texture, NULL, &dst);
        title_bottom = dst.y + dst.h;
    }

    /* Per-line ypos accumulator — mirrors the original's `ypos`
     * walker.  Start below the title with a comfortable gap. */
    int ypos = title_bottom + 30;
    char buf[80];

    /* "- Level N -" — red header.  Always drawn once BONUS mode
     * is active. */
    snprintf(buf, sizeof(buf), "- Level %d -", ctx->level_number);
    sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TITLE, buf, ypos, red, W);
    ypos += 40;

    if (state >= BONUS_STATE_SCORE)
    {
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT,
                                      "Congratulations on finishing this level.", ypos, white, W);
        ypos += 35;
    }

    /* Bonus-coin tally line (one of: super-bonus / no-coins / coins-void
     * / animated row).  Mirrors original/bonus.c:280-389 DoBonuses. */
    if (state >= BONUS_STATE_BONUS)
    {
        int initial = bonus_system_get_initial_coins(ctx->bonus);
        int live = bonus_system_get_coins(ctx->bonus);
        int drawn = initial - live;
        int time_secs = bonus_system_get_time_bonus_secs(ctx->bonus);
        sdl2_texture_info_t coin_tex;
        if (time_secs == 0 && initial > 0)
        {
            sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT,
                                          "Bonus coins void - Timer ran out!", ypos, blue, W);
        }
        else if (initial == 0)
        {
            sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT,
                                          "Sorry, no bonus coins collected.", ypos, blue, W);
        }
        else if (initial > 8)
        {
            snprintf(buf, sizeof(buf), "Super Bonus - %d", initial);
            sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TITLE, buf, ypos, yellow, W);
        }
        else if (drawn > 0 &&
                 sdl2_texture_get(ctx->texture, SPR_BLOCK_BONUS_1, &coin_tex) == SDL2T_OK)
        {
            SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);
            for (int i = 0; i < drawn && i < initial; i++)
            {
                SDL_Rect dst = {.x = bonus_row_item_x(center_x, initial, BONUS_COIN_STRIDE,
                                                      BONUS_COIN_PADDING, i),
                                .y = ypos,
                                .w = coin_tex.width,
                                .h = coin_tex.height};
                SDL_RenderCopy(sdl, coin_tex.texture, NULL, &dst);
            }
        }
        ypos += 35;
    }

    /* "Level bonus - level N x 100 = M points" — yellow, with the
     * total computed.  Adjusts for start_level via the original's
     * formula (original/bonus.c:400-413): theLevel = level -
     * starting_level + 1.  If the timer ran out (time_secs == 0),
     * the original shows "No level bonus - Timer ran out." instead
     * (original/bonus.c:415-422). */
    if (state >= BONUS_STATE_LEVEL)
    {
        int time_secs = bonus_system_get_time_bonus_secs(ctx->bonus);
        if (time_secs > 0)
        {
            int adj_level = ctx->level_number - ctx->start_level + 1;
            if (adj_level < 1)
                adj_level = 1;
            snprintf(buf, sizeof(buf), "Level bonus - level %d x 100 = %d points", adj_level,
                     adj_level * 100);
        }
        else
        {
            snprintf(buf, sizeof(buf), "No level bonus - Timer ran out.");
        }
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, buf, ypos, yellow, W);
        ypos += 30;
    }

    /* Bullet-bonus row.  Mirrors original/bonus.c:431-490 DoBullets. */
    if (state >= BONUS_STATE_BULLET)
    {
        int initial_b = bonus_system_get_initial_bullets(ctx->bonus);
        int live_b = bonus_system_get_bullets(ctx->bonus);
        int drawn_b = initial_b - live_b;
        sdl2_texture_info_t bullet_tex;
        if (initial_b == 0)
        {
            sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT,
                                          "You have used all your bullets. No bonus!", ypos, blue,
                                          W);
        }
        else if (drawn_b > 0 && sdl2_texture_get(ctx->texture, SPR_BULLET, &bullet_tex) == SDL2T_OK)
        {
            SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);
            for (int i = 0; i < drawn_b && i < initial_b; i++)
            {
                SDL_Rect dst = {.x = bonus_row_item_x(center_x, initial_b, BONUS_BULLET_STRIDE,
                                                      BONUS_BULLET_PADDING, i),
                                .y = ypos,
                                .w = bullet_tex.width,
                                .h = bullet_tex.height};
                SDL_RenderCopy(sdl, bullet_tex.texture, NULL, &dst);
            }
        }
        ypos += 35;
    }

    /* "Time bonus - X seconds x 100 = N points" — yellow, computed
     * total (original/bonus.c:501-510). */
    if (state >= BONUS_STATE_TIME)
    {
        int secs = bonus_system_get_time_bonus_secs(ctx->bonus);
        if (secs > 0)
        {
            snprintf(buf, sizeof(buf), "Time bonus - %d seconds x 100 = %d points", secs,
                     secs * 100);
            sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, buf, ypos, yellow, W);
        }
        else
        {
            sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT,
                                          "No time bonus - not quick enough!", ypos, yellow, W);
        }
        ypos += 30;
    }

    /* High-score ranking — red text.  Matches DoHighScore (bonus.c:528-586).
     * Personal table is the reference per original (PERSONAL precedes GLOBAL
     * in the legacy ranking calculation). */
    if (state >= BONUS_STATE_HSCORE)
    {
        unsigned long current_score = ctx->score ? score_system_get(ctx->score) : 0UL;
        int rank = highscore_io_get_ranking(&ctx->hs_personal, current_score);
        if (rank == 1)
        {
            snprintf(buf, sizeof(buf), "You are ranked 1st. Well done!");
        }
        else if (rank > 0)
        {
            snprintf(buf, sizeof(buf), "You are currently ranked %d%s.", rank, rank_suffix(rank));
        }
        else
        {
            snprintf(buf, sizeof(buf), "Keep on trying!");
        }
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, buf, ypos, red, W);
        ypos += 30;
    }

    /* "Prepare for level N" — yellow.  No trailing exclamation mark
     * (the original is sober: "Prepare for level 4"). */
    if (state >= BONUS_STATE_END_TEXT)
    {
        snprintf(buf, sizeof(buf), "Prepare for level %d", ctx->level_number + 1);
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, buf, ypos, yellow, W);
        /* This is the last accumulating line; the footer + floppy
         * below use fixed positions, so no further ypos increment. */
    }

    /* Persistent footer — "Press space for next level" in tan,
     * always shown.  Original draws this from DrawTitleText at
     * mode entry (bonus.c:239-240). */
    sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, "Press space for next level",
                                  SDL2R_LOGICAL_HEIGHT - 60, tan, W);

    /* Save-token floppy icon — bottom-right corner.  Drawn when
     * the current run grants a save token: (level - starting_level
     * + 1) % SAVE_LEVEL == 0 (original/bonus.c:243).  Using
     * ctx->start_level honours -startlevel N runs that begin
     * higher than 1. */
    if (bonus_system_should_save(ctx->level_number, ctx->start_level))
    {
        sdl2_texture_info_t floppy;
        if (sdl2_texture_get(ctx->texture, SPR_FLOPPY, &floppy) == SDL2T_OK)
        {
            SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);
            SDL_Rect dst = {.x = W - 80 - floppy.width,
                            .y = SDL2R_LOGICAL_HEIGHT - 60 - floppy.height,
                            .w = floppy.width,
                            .h = floppy.height};
            SDL_RenderCopy(sdl, floppy.texture, NULL, &dst);
        }
    }
}

/* =========================================================================
 * High score table display
 * ========================================================================= */

void game_render_highscore(const game_ctx_t *ctx)
{
    highscore_state_t state = highscore_system_get_state(ctx->highscore_display);
    if (state == HIGHSCORE_STATE_NONE)
        return;

    SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);

    /* Space background — original/highscore.c:188 BACKGROUND_SPACE */
    sdl2_texture_info_t space_bg;
    if (sdl2_texture_get(ctx->texture, SPR_BGRND_SPACE, &space_bg) == SDL2T_OK &&
        space_bg.width > 0 && space_bg.height > 0)
    {
        for (int ty = PLAY_AREA_Y; ty < PLAY_AREA_Y + PLAY_AREA_H; ty += space_bg.height)
        {
            for (int tx = PLAY_AREA_X; tx < PLAY_AREA_X + PLAY_AREA_W; tx += space_bg.width)
            {
                int dw = space_bg.width;
                int dh = space_bg.height;
                if (tx + dw > PLAY_AREA_X + PLAY_AREA_W)
                    dw = PLAY_AREA_X + PLAY_AREA_W - tx;
                if (ty + dh > PLAY_AREA_Y + PLAY_AREA_H)
                    dh = PLAY_AREA_Y + PLAY_AREA_H - ty;
                SDL_Rect src = {0, 0, dw, dh};
                SDL_Rect dst = {tx, ty, dw, dh};
                SDL_RenderCopy(sdl, space_bg.texture, &src, &dst);
            }
        }
    }

    /* Earth globe — original/highscore.c:192-193 */
    sdl2_texture_info_t earth;
    if (sdl2_texture_get(ctx->texture, SPR_PRESENTS_EARTH, &earth) == SDL2T_OK)
    {
        int ex = PLAY_AREA_X + PLAY_AREA_W / 2 - earth.width / 2;
        int ey = PLAY_AREA_Y + PLAY_AREA_H / 2 - earth.height / 2 + 40;
        SDL_Rect dst = {ex, ey, earth.width, earth.height};
        SDL_RenderCopy(sdl, earth.texture, NULL, &dst);
    }

    /* Red border — original/highscore.c uses red XSetWindowBorder */
    if (state == HIGHSCORE_STATE_SPARKLE)
    {
        game_render_border_glow(ctx);
    }
    else
    {
        SDL_SetRenderDrawColor(sdl, 200, 0, 0, 255);
        int bx = PLAY_AREA_X - 2;
        int by = PLAY_AREA_Y - 2;
        int bw = PLAY_AREA_W + 4;
        int bh = PLAY_AREA_H + 4;
        SDL_Rect top = {bx, by, bw, 2};
        SDL_Rect bottom = {bx, by + bh - 2, bw, 2};
        SDL_Rect left = {bx, by, 2, bh};
        SDL_Rect right = {bx + bw - 2, by, 2, bh};
        SDL_RenderFillRect(sdl, &top);
        SDL_RenderFillRect(sdl, &bottom);
        SDL_RenderFillRect(sdl, &left);
        SDL_RenderFillRect(sdl, &right);
    }

    /* "HIGH SCORES" title bitmap — original/highscore.c:191 at (59,20) */
    sdl2_texture_info_t title_tex;
    if (sdl2_texture_get(ctx->texture, SPR_HIGHSCORE, &title_tex) == SDL2T_OK)
    {
        SDL_Rect dst = {PLAY_AREA_X + 59 - 35, PLAY_AREA_Y + 20, title_tex.width, title_tex.height};
        SDL_RenderCopy(sdl, title_tex.texture, NULL, &dst);
    }

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color yellow = {255, 255, 0, 255};
    SDL_Color green = {0, 255, 0, 255};
    SDL_Color red = {255, 80, 80, 255};
    SDL_Color tan_clr = {210, 180, 140, 255};

    /* "Boing Master" + name + wisdom — original/highscore.c:229-241 */
    int ym = PLAY_AREA_Y + 75;
    int fw = PLAY_AREA_W + 2 * PLAY_AREA_X;
    sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, "Boing Master", ym, red, fw);
    ym += 31;

    const highscore_table_t *table = highscore_system_get_table(ctx->highscore_display);
    if (table && table->entries[0].name[0] != '\0')
    {
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TITLE, table->entries[0].name, ym,
                                      yellow, fw);
        ym += 31;
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_COPY, table->master_text, ym, green,
                                      fw);
        ym += 44;
    }
    else
    {
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TITLE, "To be announced!", ym, yellow,
                                      fw);
        ym += 31;
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_COPY, "Anyone play this game?", ym,
                                      green, fw);
        ym += 44;
    }

    /* Section title — original/highscore.c:248-252 */
    highscore_type_t type = highscore_system_get_type(ctx->highscore_display);
    const char *section =
        (type == HIGHSCORE_TYPE_GLOBAL) ? "- The Roll of Honour -" : "- Personal Best -";
    sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, section, ym, red, fw);

    /* Column headers + table — original/highscore.c:254-381 */
    if (state >= HIGHSCORE_STATE_SHOW)
    {
        int xr = PLAY_AREA_X + 30;
        int xs = xr + 30;
        int xl = xs + 75;
        int xt = xl + 40;
        int xg = xt + 65;
        int xn = xg + 95;
        int y = PLAY_AREA_Y + 200;

        sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, "#", xr, y, yellow);
        sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, "Score", xs, y, yellow);
        sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, "L", xl, y, yellow);
        sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, "Time", xt, y, yellow);
        sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, "Date", xg, y, yellow);
        sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, "Player", xn, y, yellow);

        y += 24;
        SDL_SetRenderDrawColor(sdl, 0, 0, 0, 255);
        SDL_RenderDrawLine(sdl, PLAY_AREA_X + 22, y + 2, PLAY_AREA_X + PLAY_AREA_W - 18, y + 2);
        SDL_SetRenderDrawColor(sdl, 255, 255, 255, 255);
        SDL_RenderDrawLine(sdl, PLAY_AREA_X + 20, y, PLAY_AREA_X + PLAY_AREA_W - 20, y);
        y += 18;

        if (!table)
            return;

        unsigned long current = highscore_system_get_current_score(ctx->highscore_display);

        for (int i = 0; i < HIGHSCORE_NUM_ENTRIES; i++)
        {
            char buf[80];
            SDL_Color rank_clr = tan_clr;
            SDL_Color score_clr = red;
            SDL_Color time_clr = tan_clr;
            SDL_Color date_clr = white;
            SDL_Color name_clr = yellow;

            if (table->entries[i].score == current && current > 0)
            {
                rank_clr = green;
                score_clr = green;
                time_clr = green;
                date_clr = green;
                name_clr = green;
            }

            if (table->entries[i].score > 0)
            {
                snprintf(buf, sizeof(buf), "%d", i + 1);
                sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, buf, xr, y, rank_clr);

                snprintf(buf, sizeof(buf), "%lu", table->entries[i].score);
                sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, buf, xs, y, score_clr);

                snprintf(buf, sizeof(buf), "%lu", table->entries[i].level);
                sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, buf, xl, y, green);

                if (table->entries[i].game_time > 0)
                {
                    unsigned long gt = table->entries[i].game_time;
                    snprintf(buf, sizeof(buf), "%lu'%lu'%lu\"", gt / 3600, gt / 60, gt % 60);
                }
                else
                {
                    snprintf(buf, sizeof(buf), "--");
                }
                sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, buf, xt, y, time_clr);

                if (table->entries[i].timestamp > 0)
                {
                    time_t t = (time_t)table->entries[i].timestamp;
                    const struct tm *tm = localtime(&t);
                    if (tm)
                    {
                        static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
                        snprintf(buf, sizeof(buf), "%02d %s %02d", tm->tm_mday, months[tm->tm_mon],
                                 tm->tm_year % 100);
                    }
                    else
                    {
                        snprintf(buf, sizeof(buf), "--");
                    }
                }
                else
                {
                    snprintf(buf, sizeof(buf), "--");
                }
                sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, buf, xg, y, date_clr);

                sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, table->entries[i].name, xn, y,
                                      name_clr);
            }
            else
            {
                snprintf(buf, sizeof(buf), "%d", i + 1);
                sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, buf, xr, y, tan_clr);
                sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, "--", xs, y, red);
                sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, "--", xl, y, green);
                sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, "--", xt, y, tan_clr);
                sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, "--", xg, y, white);
                sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_TEXT, "--", xn, y, yellow);
            }

            y += 28;
        }

        /* Bottom separator — original/highscore.c:379-381 */
        SDL_SetRenderDrawColor(sdl, 0, 0, 0, 255);
        SDL_RenderDrawLine(sdl, PLAY_AREA_X + 22, y + 2, PLAY_AREA_X + PLAY_AREA_W - 18, y + 2);
        SDL_SetRenderDrawColor(sdl, 255, 255, 255, 255);
        SDL_RenderDrawLine(sdl, PLAY_AREA_X + 20, y, PLAY_AREA_X + PLAY_AREA_W - 20, y);
    }

    /* Title sparkle */
    if (state == HIGHSCORE_STATE_SPARKLE)
    {
        highscore_title_sparkle_t ts;
        highscore_system_get_title_sparkle(ctx->highscore_display, &ts);
        if (ts.active)
        {
            render_sparkle(ctx, PLAY_AREA_X + 20, PLAY_AREA_Y + 20, ts.frame_index);
            render_sparkle(ctx, PLAY_AREA_X + PLAY_AREA_W - 40, PLAY_AREA_Y + 20, ts.mirror_index);
        }
    }

    /* "Insert coin to start the game" — original/highscore.c:196 */
    sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, "Insert coin to start the game",
                                  PLAY_AREA_Y + PLAY_AREA_H - 40, tan_clr,
                                  PLAY_AREA_W + 2 * PLAY_AREA_X);
}
