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

#include <SDL2/SDL.h>

#include "game_context.h"
#include "intro_system.h"
#include "presents_system.h"
#include "sdl2_font.h"
#include "sdl2_renderer.h"
#include "sdl2_texture.h"
#include "sprite_catalog.h"

/* Layout constants from legacy stage.c */
#define OFFSET_X 35
#define PLAY_AREA_X OFFSET_X
#define PLAY_AREA_Y 60
#define PLAY_AREA_W 495
#define PLAY_AREA_H 580

/* =========================================================================
 * Helper: render a sparkle (star) frame at a position
 * ========================================================================= */

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
    presents_state_t state = presents_system_get_state(ctx->presents);

    if (state == PRESENTS_STATE_NONE || state == PRESENTS_STATE_FINISH)
        return;

    /* Flag + earth (shown in FLAG state) */
    if (state == PRESENTS_STATE_FLAG)
    {
        presents_flag_info_t fi;
        presents_system_get_flag_info(ctx->presents, &fi);

        sdl2_texture_info_t tex;
        if (sdl2_texture_get(ctx->texture, SPR_PRESENTS_FLAG, &tex) == SDL2T_OK)
        {
            SDL_Rect dst = {PLAY_AREA_X + fi.flag_x, PLAY_AREA_Y + fi.flag_y, tex.width,
                            tex.height};
            SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
        }
        if (sdl2_texture_get(ctx->texture, SPR_PRESENTS_EARTH, &tex) == SDL2T_OK)
        {
            SDL_Rect dst = {PLAY_AREA_X + fi.earth_x, PLAY_AREA_Y + fi.earth_y, tex.width,
                            tex.height};
            SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
        }
    }

    /* Author credits (TEXT1-TEXT3 states) */
    if (state >= PRESENTS_STATE_TEXT1 && state <= PRESENTS_STATE_TEXT3)
    {
        SDL_Color white = {255, 255, 255, 255};
        SDL_Color yellow = {255, 255, 0, 255};

        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, "Justin C. Kibell",
                                      PLAY_AREA_Y + 200, white, PLAY_AREA_W);
        if (state >= PRESENTS_STATE_TEXT2)
            sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_COPY, "presents",
                                          PLAY_AREA_Y + 230, yellow, PLAY_AREA_W);
        if (state >= PRESENTS_STATE_TEXT3)
            sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TITLE, "XBoing II",
                                          PLAY_AREA_Y + 270, white, PLAY_AREA_W);
    }

    /* Letter stamps (LETTERS state) */
    if (state == PRESENTS_STATE_LETTERS || state == PRESENTS_STATE_SHINE)
    {
        presents_letter_info_t li;
        if (presents_system_get_letter_info(ctx->presents, &li))
        {
            /* Draw individual XBOING letters */
            static const char *const letter_keys[] = {SPR_TITLE_X, SPR_TITLE_B, SPR_TITLE_O,
                                                      SPR_TITLE_I, SPR_TITLE_N, SPR_TITLE_G};
            for (int i = 0; i <= li.letter_index && i < 6; i++)
            {
                sdl2_texture_info_t tex;
                if (sdl2_texture_get(ctx->texture, letter_keys[i], &tex) == SDL2T_OK)
                {
                    /* Letters are positioned sequentially across the play area */
                    int lx = PLAY_AREA_X + 10 + i * 80;
                    SDL_Rect dst = {lx, PLAY_AREA_Y + 250, tex.width, tex.height};
                    SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
                }
            }
        }
    }

    /* Sparkle animation (SHINE state) */
    if (state == PRESENTS_STATE_SHINE)
    {
        presents_sparkle_info_t si;
        presents_system_get_sparkle_info(ctx->presents, &si);
        if (si.active)
            render_sparkle(ctx, PLAY_AREA_X + si.x, PLAY_AREA_Y + si.y, si.frame_index);
    }

    /* Typewriter text (SPECIAL_TEXT1-3 states) */
    if (state >= PRESENTS_STATE_SPECIAL_TEXT1 && state <= PRESENTS_STATE_SPECIAL_TEXT3)
    {
        SDL_Color green = {0, 255, 0, 255};
        for (int line = 0; line < 3; line++)
        {
            presents_typewriter_info_t ti;
            if (presents_system_get_typewriter_info(ctx->presents, line, &ti))
            {
                if (ti.chars_visible > 0 && ti.text)
                {
                    /* Draw the visible portion of the typewriter text */
                    char buf[256];
                    int n = ti.chars_visible;
                    if (n > 255)
                        n = 255;
                    snprintf(buf, sizeof(buf), "%.*s", n, ti.text);
                    sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_DATA, buf,
                                          PLAY_AREA_X + ti.x_offset, PLAY_AREA_Y + ti.y, green);
                }
            }
        }
    }

    /* Curtain wipe (CLEAR state) — draw black rectangles from top and bottom */
    if (state == PRESENTS_STATE_CLEAR)
    {
        presents_wipe_info_t wi;
        presents_system_get_wipe_info(ctx->presents, &wi);

        SDL_SetRenderDrawColor(sdl, 0, 0, 0, 255);
        SDL_Rect top_rect = {PLAY_AREA_X, PLAY_AREA_Y, PLAY_AREA_W, wi.top_y};
        SDL_Rect bot_rect = {PLAY_AREA_X, PLAY_AREA_Y + wi.bottom_y, PLAY_AREA_W,
                             PLAY_AREA_H - wi.bottom_y};
        SDL_RenderFillRect(sdl, &top_rect);
        SDL_RenderFillRect(sdl, &bot_rect);
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

    /* Block descriptions table */
    if (state >= INTRO_STATE_BLOCKS)
    {
        const intro_block_entry_t *entries = NULL;
        int count = intro_system_get_block_table(ctx->intro, &entries);

        SDL_Color white = {255, 255, 255, 255};
        SDL_Color yellow = {255, 255, 0, 255};

        for (int i = 0; i < count; i++)
        {
            /* Draw block sprite */
            const char *key = sprite_block_key(entries[i].type);
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

            /* Draw description text */
            if (entries[i].description)
            {
                SDL_Color clr = (i % 2 == 0) ? white : yellow;
                sdl2_font_draw(ctx->font, SDL2F_FONT_COPY, entries[i].description,
                               PLAY_AREA_X + entries[i].x + 50, PLAY_AREA_Y + entries[i].y + 2,
                               clr);
            }
        }
    }

    /* Sparkle */
    if (state == INTRO_STATE_SPARKLE)
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

    /* Title */
    if (state >= INTRO_STATE_TITLE)
    {
        SDL_Color white = {255, 255, 255, 255};
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TITLE, "Instructions", PLAY_AREA_Y + 20,
                                      white, PLAY_AREA_W);
    }

    /* Instruction text lines */
    if (state >= INTRO_STATE_TEXT)
    {
        const intro_instruct_line_t *lines = NULL;
        int count = intro_system_get_instruct_text(ctx->intro, &lines);

        SDL_Color white = {255, 255, 255, 255};
        SDL_Color yellow = {255, 255, 0, 255};
        SDL_Color green = {0, 255, 0, 255};

        for (int i = 0; i < count; i++)
        {
            if (lines[i].is_spacer)
                continue;

            SDL_Color clr = white;
            if (lines[i].color_index == 1)
                clr = yellow;
            else if (lines[i].color_index == 2)
                clr = green;

            sdl2_font_draw(ctx->font, SDL2F_FONT_COPY, lines[i].text, PLAY_AREA_X + 30,
                           PLAY_AREA_Y + 90 + i * 24, clr);
        }
    }

    /* Sparkle */
    if (state == INTRO_STATE_SPARKLE)
    {
        intro_sparkle_info_t si;
        intro_system_get_sparkle_info(ctx->intro, &si);
        if (si.active)
            render_sparkle(ctx, PLAY_AREA_X + si.x, PLAY_AREA_Y + si.y, si.frame_index);
    }
}
