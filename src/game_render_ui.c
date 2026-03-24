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

#include "bonus_system.h"
#include "demo_system.h"
#include "game_context.h"
#include "game_render.h"
#include "highscore_system.h"
#include "intro_system.h"
#include "keys_system.h"
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

    /* Flag + earth — always render once set (persists from FLAG state) */
    {
        presents_flag_info_t fi;
        presents_system_get_flag_info(ctx->presents, &fi);

        /* flag_x is set to non-zero when FLAG state runs */
        if (fi.flag_x > 0 || fi.flag_y > 0)
        {
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

            /* Copyright text at bottom */
            SDL_Color white = {255, 255, 255, 255};
            sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_COPY,
                                          "(c) 1993-1996 Justin C. Kibell",
                                          PLAY_AREA_Y + fi.copyright_y, white, PLAY_AREA_W);
        }
    }

    /* Author credits — query the active typewriter line to know progress.
     * TEXT1/TEXT2/TEXT3 states each set one line of credits. After TEXT3,
     * all three are visible until TEXT_CLEAR. We render based on whether
     * the typewriter has been used (active_line >= 0 means past flag). */
    {
        int active_line = presents_system_get_active_typewriter_line(ctx->presents);
        presents_state_t state = presents_system_get_state(ctx->presents);

        /* Credits text appears after FLAG, before LETTERS */
        if (active_line < 0 && state != PRESENTS_STATE_FLAG && state != PRESENTS_STATE_NONE)
        {
            /* Between FLAG and LETTERS — show credits */
            SDL_Color white = {255, 255, 255, 255};
            SDL_Color yellow = {255, 255, 0, 255};

            sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, "Justin C. Kibell",
                                          PLAY_AREA_Y + 200, white, PLAY_AREA_W);
            sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_COPY, "presents", PLAY_AREA_Y + 230,
                                          yellow, PLAY_AREA_W);
            sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TITLE, "XBoing II",
                                          PLAY_AREA_Y + 270, white, PLAY_AREA_W);
        }
    }

    /* XBOING letter stamps — render if any letters have been placed */
    {
        presents_letter_info_t li;
        if (presents_system_get_letter_info(ctx->presents, &li))
        {
            static const char *const letter_keys[] = {SPR_TITLE_X, SPR_TITLE_B, SPR_TITLE_O,
                                                      SPR_TITLE_I, SPR_TITLE_N, SPR_TITLE_G};
            for (int i = 0; i <= li.letter_index && i < 6; i++)
            {
                sdl2_texture_info_t tex;
                if (sdl2_texture_get(ctx->texture, letter_keys[i], &tex) == SDL2T_OK)
                {
                    int lx = PLAY_AREA_X + 10 + i * 80;
                    SDL_Rect dst = {lx, PLAY_AREA_Y + 250, tex.width, tex.height};
                    SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
                }
            }
        }
    }

    /* Sparkle — render when active */
    {
        presents_sparkle_info_t si;
        presents_system_get_sparkle_info(ctx->presents, &si);
        if (si.active)
            render_sparkle(ctx, PLAY_AREA_X + si.x, PLAY_AREA_Y + si.y, si.frame_index);
    }

    /* Typewriter text lines — render visible chars for each line */
    {
        SDL_Color green = {0, 255, 0, 255};
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
                    sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_DATA, buf,
                                          PLAY_AREA_X + ti.x_offset, PLAY_AREA_Y + ti.y, green);
                }
            }
        }
    }

    /* Curtain wipe — draw black rectangles closing from top and bottom */
    {
        presents_wipe_info_t wi;
        presents_system_get_wipe_info(ctx->presents, &wi);
        if (wi.top_y > 0 || wi.complete)
        {
            SDL_SetRenderDrawColor(sdl, 0, 0, 0, 255);
            SDL_Rect top_rect = {PLAY_AREA_X, PLAY_AREA_Y, PLAY_AREA_W, wi.top_y};
            SDL_Rect bot_rect = {PLAY_AREA_X, PLAY_AREA_Y + wi.bottom_y, PLAY_AREA_W,
                                 PLAY_AREA_H - wi.bottom_y};
            SDL_RenderFillRect(sdl, &top_rect);
            SDL_RenderFillRect(sdl, &bot_rect);
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

/* =========================================================================
 * Demo screen — ball trail + descriptive text
 * ========================================================================= */

void game_render_demo(const game_ctx_t *ctx)
{
    demo_state_t state = demo_system_get_state(ctx->demo);
    if (state == DEMO_STATE_NONE)
        return;

    /* Title */
    if (state >= DEMO_STATE_TITLE)
    {
        SDL_Color white = {255, 255, 255, 255};
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TITLE, "Demonstration",
                                      PLAY_AREA_Y + 20, white, PLAY_AREA_W);
    }

    /* Ball trail animation */
    if (state >= DEMO_STATE_BLOCKS)
    {
        const demo_ball_pos_t *trail = NULL;
        int count = demo_system_get_ball_trail(ctx->demo, &trail);

        for (int i = 0; i < count; i++)
        {
            const char *key = sprite_ball_key(trail[i].frame_index);
            sdl2_texture_info_t tex;
            if (sdl2_texture_get(ctx->texture, key, &tex) == SDL2T_OK)
            {
                SDL_Renderer *sdl = sdl2_renderer_get(ctx->renderer);
                SDL_Rect dst = {PLAY_AREA_X + trail[i].x, PLAY_AREA_Y + trail[i].y, tex.width,
                                tex.height};
                SDL_RenderCopy(sdl, tex.texture, NULL, &dst);
            }
        }
    }

    /* Descriptive text */
    if (state >= DEMO_STATE_TEXT)
    {
        const demo_text_line_t *lines = NULL;
        int count = demo_system_get_demo_text(ctx->demo, &lines);

        SDL_Color green = {0, 255, 0, 255};
        for (int i = 0; i < count; i++)
        {
            if (lines[i].text)
                sdl2_font_draw(ctx->font, SDL2F_FONT_DATA, lines[i].text, PLAY_AREA_X + lines[i].x,
                               PLAY_AREA_Y + lines[i].y, green);
        }
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

    SDL_Color white = {255, 255, 255, 255};
    int level = demo_system_get_preview_level(ctx->demo);
    char title[64];
    snprintf(title, sizeof(title), "Preview - Level %d", level);
    sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TITLE, title, PLAY_AREA_Y + 20, white,
                                  PLAY_AREA_W);

    if (state >= DEMO_STATE_BLOCKS)
        game_render_blocks(ctx);

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

    if (state >= KEYS_STATE_TITLE)
    {
        SDL_Color white = {255, 255, 255, 255};
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TITLE, "Game Controls",
                                      PLAY_AREA_Y + 20, white, PLAY_AREA_W);
    }

    if (state >= KEYS_STATE_TEXT)
    {
        const keys_binding_entry_t *bindings = NULL;
        int count = keys_system_get_game_bindings(ctx->keys, &bindings);

        SDL_Color yellow = {255, 255, 0, 255};
        for (int i = 0; i < count; i++)
        {
            int x = (bindings[i].column == 0) ? PLAY_AREA_X + 30 : PLAY_AREA_X + 280;
            int row = (bindings[i].column == 0) ? i : i - 10;
            sdl2_font_draw(ctx->font, SDL2F_FONT_COPY, bindings[i].text, x,
                           PLAY_AREA_Y + 90 + row * 24, yellow);
        }
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

    if (state >= KEYS_STATE_TITLE)
    {
        SDL_Color white = {255, 255, 255, 255};
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TITLE, "Editor Controls",
                                      PLAY_AREA_Y + 20, white, PLAY_AREA_W);
    }

    if (state >= KEYS_STATE_TEXT)
    {
        const keys_info_line_t *info = NULL;
        int info_count = keys_system_get_editor_info(ctx->keys, &info);

        SDL_Color green = {0, 255, 0, 255};
        for (int i = 0; i < info_count; i++)
        {
            if (info[i].text)
                sdl2_font_draw(ctx->font, SDL2F_FONT_COPY, info[i].text, PLAY_AREA_X + 30,
                               PLAY_AREA_Y + 90 + i * 22, green);
        }

        const keys_binding_entry_t *bindings = NULL;
        int bind_count = keys_system_get_editor_bindings(ctx->keys, &bindings);

        SDL_Color yellow = {255, 255, 0, 255};
        int start_y = PLAY_AREA_Y + 90 + info_count * 22 + 20;
        for (int i = 0; i < bind_count; i++)
        {
            int x = (bindings[i].column == 0) ? PLAY_AREA_X + 30 : PLAY_AREA_X + 270;
            int row = (bindings[i].column == 0) ? i : i - 5;
            sdl2_font_draw(ctx->font, SDL2F_FONT_COPY, bindings[i].text, x, start_y + row * 24,
                           yellow);
        }
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

void game_render_bonus(const game_ctx_t *ctx)
{
    bonus_state_t state = bonus_system_get_state(ctx->bonus);
    if (state == BONUS_STATE_FINISH)
        return;

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color yellow = {255, 255, 0, 255};
    SDL_Color green = {0, 255, 0, 255};
    SDL_Color red = {255, 80, 80, 255};

    /* Title */
    sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TITLE, "BONUS", PLAY_AREA_Y + 40, red,
                                  PLAY_AREA_W);

    /* Display score running total */
    unsigned long dscore = bonus_system_get_display_score(ctx->bonus);
    char score_buf[64];
    snprintf(score_buf, sizeof(score_buf), "Score: %lu", dscore);
    sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, score_buf, PLAY_AREA_Y + 100, white,
                                  PLAY_AREA_W);

    if (state >= BONUS_STATE_SCORE)
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_DATA, "Congratulations!",
                                      PLAY_AREA_Y + 160, yellow, PLAY_AREA_W);

    if (state >= BONUS_STATE_BONUS)
    {
        int coins = bonus_system_get_coins(ctx->bonus);
        char buf[64];
        snprintf(buf, sizeof(buf), "Bonus Coins: %d x 3000 = %d", coins, coins * 3000);
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_DATA, buf, PLAY_AREA_Y + 200, green,
                                      PLAY_AREA_W);
    }

    if (state >= BONUS_STATE_LEVEL)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "Level Bonus: %d x 100", ctx->level_number);
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_DATA, buf, PLAY_AREA_Y + 240, green,
                                      PLAY_AREA_W);
    }

    if (state >= BONUS_STATE_BULLET)
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_DATA, "Bullet Bonus...",
                                      PLAY_AREA_Y + 280, green, PLAY_AREA_W);

    if (state >= BONUS_STATE_TIME)
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_DATA, "Time Bonus...",
                                      PLAY_AREA_Y + 320, green, PLAY_AREA_W);

    if (state >= BONUS_STATE_END_TEXT)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "Prepare for level %d!", ctx->level_number + 1);
        sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TEXT, buf, PLAY_AREA_Y + 400, yellow,
                                      PLAY_AREA_W);
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

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color yellow = {255, 255, 0, 255};
    SDL_Color green = {0, 255, 0, 255};
    SDL_Color red = {255, 80, 80, 255};

    /* Title */
    highscore_type_t type = highscore_system_get_type(ctx->highscore_display);
    const char *title = (type == HIGHSCORE_TYPE_GLOBAL) ? "Hall of Fame" : "Personal Best";
    sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TITLE, title, PLAY_AREA_Y + 20, red,
                                  PLAY_AREA_W);

    /* Score table */
    if (state >= HIGHSCORE_STATE_SHOW)
    {
        const highscore_table_t *table = highscore_system_get_table(ctx->highscore_display);
        if (!table)
            return;

        /* Boing master name and words of wisdom (above table) */
        int ym = PLAY_AREA_Y + 55;
        if (table->entries[0].score > 0)
        {
            sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_TITLE, table->entries[0].name, ym,
                                          yellow, PLAY_AREA_W);
            ym += 25;

            if (table->master_text[0] != '\0')
            {
                sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_DATA, table->master_text, ym,
                                              green, PLAY_AREA_W);
            }
        }
        ym += 25;

        /* Column positions for fixed alignment */
        int col_rank = PLAY_AREA_X + 20;
        int col_name = PLAY_AREA_X + 55;
        int col_score = PLAY_AREA_X + 280;
        int col_level = PLAY_AREA_X + 380;

        unsigned long current = highscore_system_get_current_score(ctx->highscore_display);

        int row = 0;
        for (int i = 0; i < HIGHSCORE_NUM_ENTRIES; i++)
        {
            if (table->entries[i].score == 0)
                continue;

            int y = ym + row * 32;
            row++;

            /* Highlight the player's score */
            SDL_Color clr = (table->entries[i].score == current) ? green : white;

            /* Rank */
            char rank_buf[8];
            snprintf(rank_buf, sizeof(rank_buf), "%2d.", i + 1);
            sdl2_font_draw(ctx->font, SDL2F_FONT_DATA, rank_buf, col_rank, y, clr);

            /* Name (truncated to fit) */
            sdl2_font_draw(ctx->font, SDL2F_FONT_DATA, table->entries[i].name, col_name, y, clr);

            /* Score (right-aligned) */
            char score_buf[16];
            snprintf(score_buf, sizeof(score_buf), "%lu", table->entries[i].score);
            sdl2_font_metrics_t sm;
            int score_w = 0;
            if (sdl2_font_measure(ctx->font, SDL2F_FONT_DATA, score_buf, &sm) == SDL2F_OK)
                score_w = sm.width;
            sdl2_font_draw(ctx->font, SDL2F_FONT_DATA, score_buf, col_score + 80 - score_w, y, clr);

            /* Level */
            char level_buf[16];
            snprintf(level_buf, sizeof(level_buf), "Lv.%lu", table->entries[i].level);
            sdl2_font_draw(ctx->font, SDL2F_FONT_DATA, level_buf, col_level, y, clr);
        }
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

    /* "Press SPACE to continue" */
    sdl2_font_draw_shadow_centred(ctx->font, SDL2F_FONT_COPY, "Press SPACE to continue",
                                  PLAY_AREA_Y + PLAY_AREA_H - 40, yellow, PLAY_AREA_W);
}
