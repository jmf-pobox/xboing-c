/*
 * sdl2_font.c — SDL2 TTF font rendering.
 *
 * See include/sdl2_font.h for API documentation.
 * See ADR-006 in docs/DESIGN.md for design rationale.
 */

#include "sdl2_font.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL_ttf.h>

/* =========================================================================
 * Internal data structures
 * ========================================================================= */

/* Static specification for each font slot: filename and point size. */
struct sdl2_font_spec
{
    const char *filename;
    int ptsize;
};

/* Runtime state for a loaded font. */
struct sdl2_font_slot
{
    TTF_Font *font;
    int line_height;
};

struct sdl2_font
{
    SDL_Renderer *renderer; /* borrowed, not owned */
    struct sdl2_font_slot slots[SDL2F_FONT_COUNT];
    bool ttf_initialized;
};

/* Font specifications: maps each enum slot to a TTF file and point size. */
static const struct sdl2_font_spec font_specs[SDL2F_FONT_COUNT] = {
    [SDL2F_FONT_TITLE] = {"LiberationSans-Bold.ttf", 24},
    [SDL2F_FONT_TEXT] = {"LiberationSans-Regular.ttf", 18},
    [SDL2F_FONT_DATA] = {"LiberationSans-Bold.ttf", 14},
    [SDL2F_FONT_COPY] = {"LiberationSans-Regular.ttf", 12},
};

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static bool is_valid_font_id(sdl2_font_id_t id)
{
    return id >= 0 && id < SDL2F_FONT_COUNT;
}

/*
 * Render text to the renderer at (x, y).
 * Empty/NULL text is a no-op (returns OK).
 * Creates a transient surface → texture → RenderCopy → cleanup.
 */
static sdl2_font_status_t render_text(sdl2_font_t *ctx, TTF_Font *font, const char *text, int x,
                                      int y, SDL_Color color)
{
    if (text == NULL || text[0] == '\0')
    {
        return SDL2F_OK;
    }

    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, color);
    if (surface == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sdl2_font: TTF_RenderUTF8_Blended failed: %s",
                     TTF_GetError());
        return SDL2F_ERR_RENDER_FAILED;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(ctx->renderer, surface);
    int w = surface->w;
    int h = surface->h;
    SDL_FreeSurface(surface);

    if (texture == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "sdl2_font: SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
        return SDL2F_ERR_RENDER_FAILED;
    }

    SDL_Rect dst = {x, y, w, h};
    int rc = SDL_RenderCopy(ctx->renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);

    if (rc != 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sdl2_font: SDL_RenderCopy failed: %s",
                     SDL_GetError());
        return SDL2F_ERR_RENDER_FAILED;
    }

    return SDL2F_OK;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

sdl2_font_config_t sdl2_font_config_defaults(void)
{
    sdl2_font_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.renderer = NULL;
    cfg.font_dir = SDL2F_DEFAULT_FONT_DIR;
    return cfg;
}

sdl2_font_t *sdl2_font_create(const sdl2_font_config_t *config, sdl2_font_status_t *status)
{
    if (config == NULL)
    {
        if (status != NULL)
        {
            *status = SDL2F_ERR_NULL_ARG;
        }
        return NULL;
    }

    if (config->renderer == NULL)
    {
        if (status != NULL)
        {
            *status = SDL2F_ERR_RENDERER;
        }
        return NULL;
    }

    sdl2_font_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
    {
        if (status != NULL)
        {
            *status = SDL2F_ERR_FONT_LOAD;
        }
        return NULL;
    }

    ctx->renderer = config->renderer;
    ctx->ttf_initialized = false;

    /* Initialize SDL2_ttf if not already active. */
    if (!TTF_WasInit())
    {
        if (TTF_Init() != 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sdl2_font: TTF_Init failed: %s",
                         TTF_GetError());
            free(ctx);
            if (status != NULL)
            {
                *status = SDL2F_ERR_TTF_INIT;
            }
            return NULL;
        }
        ctx->ttf_initialized = true;
    }

    /* Load all four fonts (all-or-nothing). */
    const char *font_dir = config->font_dir != NULL ? config->font_dir : SDL2F_DEFAULT_FONT_DIR;

    for (int i = 0; i < SDL2F_FONT_COUNT; i++)
    {
        char path[1024];
        int n = snprintf(path, sizeof(path), "%s/%s", font_dir, font_specs[i].filename);
        if (n < 0 || (size_t)n >= sizeof(path))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sdl2_font: font path too long for slot %d",
                         i);
            sdl2_font_destroy(ctx);
            if (status != NULL)
            {
                *status = SDL2F_ERR_FONT_LOAD;
            }
            return NULL;
        }

        TTF_Font *font = TTF_OpenFont(path, font_specs[i].ptsize);
        if (font == NULL)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sdl2_font: failed to open '%s' at %dpt: %s",
                         path, font_specs[i].ptsize, TTF_GetError());
            sdl2_font_destroy(ctx);
            if (status != NULL)
            {
                *status = SDL2F_ERR_FONT_LOAD;
            }
            return NULL;
        }

        ctx->slots[i].font = font;
        ctx->slots[i].line_height = TTF_FontHeight(font);
    }

    if (status != NULL)
    {
        *status = SDL2F_OK;
    }
    return ctx;
}

void sdl2_font_destroy(sdl2_font_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    for (int i = 0; i < SDL2F_FONT_COUNT; i++)
    {
        if (ctx->slots[i].font != NULL)
        {
            TTF_CloseFont(ctx->slots[i].font);
        }
    }

    if (ctx->ttf_initialized)
    {
        TTF_Quit();
    }

    free(ctx);
}

sdl2_font_status_t sdl2_font_draw(sdl2_font_t *ctx, sdl2_font_id_t font_id, const char *text, int x,
                                  int y, SDL_Color color)
{
    if (ctx == NULL || text == NULL)
    {
        return SDL2F_ERR_NULL_ARG;
    }
    if (!is_valid_font_id(font_id))
    {
        return SDL2F_ERR_INVALID_FONT_ID;
    }

    return render_text(ctx, ctx->slots[font_id].font, text, x, y, color);
}

sdl2_font_status_t sdl2_font_draw_shadow(sdl2_font_t *ctx, sdl2_font_id_t font_id, const char *text,
                                         int x, int y, SDL_Color color)
{
    if (ctx == NULL || text == NULL)
    {
        return SDL2F_ERR_NULL_ARG;
    }
    if (!is_valid_font_id(font_id))
    {
        return SDL2F_ERR_INVALID_FONT_ID;
    }

    SDL_Color shadow = {0, 0, 0, 255};
    sdl2_font_status_t st = render_text(ctx, ctx->slots[font_id].font, text,
                                        x + SDL2F_SHADOW_OFFSET, y + SDL2F_SHADOW_OFFSET, shadow);
    if (st != SDL2F_OK)
    {
        return st;
    }

    return render_text(ctx, ctx->slots[font_id].font, text, x, y, color);
}

sdl2_font_status_t sdl2_font_draw_shadow_centred(sdl2_font_t *ctx, sdl2_font_id_t font_id,
                                                 const char *text, int y, SDL_Color color,
                                                 int width)
{
    if (ctx == NULL || text == NULL)
    {
        return SDL2F_ERR_NULL_ARG;
    }
    if (!is_valid_font_id(font_id))
    {
        return SDL2F_ERR_INVALID_FONT_ID;
    }

    /* Measure text width for centring. */
    int text_w = 0;
    if (text[0] != '\0')
    {
        int text_h = 0;
        if (TTF_SizeUTF8(ctx->slots[font_id].font, text, &text_w, &text_h) != 0)
        {
            return SDL2F_ERR_RENDER_FAILED;
        }
    }

    int x = (width / 2) - (text_w / 2);

    SDL_Color shadow = {0, 0, 0, 255};
    sdl2_font_status_t st = render_text(ctx, ctx->slots[font_id].font, text,
                                        x + SDL2F_SHADOW_OFFSET, y + SDL2F_SHADOW_OFFSET, shadow);
    if (st != SDL2F_OK)
    {
        return st;
    }

    return render_text(ctx, ctx->slots[font_id].font, text, x, y, color);
}

sdl2_font_status_t sdl2_font_measure(sdl2_font_t *ctx, sdl2_font_id_t font_id, const char *text,
                                     sdl2_font_metrics_t *metrics)
{
    if (ctx == NULL || text == NULL || metrics == NULL)
    {
        return SDL2F_ERR_NULL_ARG;
    }
    if (!is_valid_font_id(font_id))
    {
        return SDL2F_ERR_INVALID_FONT_ID;
    }

    if (text[0] == '\0')
    {
        metrics->width = 0;
        metrics->height = ctx->slots[font_id].line_height;
        return SDL2F_OK;
    }

    int w = 0;
    int h = 0;
    if (TTF_SizeUTF8(ctx->slots[font_id].font, text, &w, &h) != 0)
    {
        return SDL2F_ERR_RENDER_FAILED;
    }

    metrics->width = w;
    metrics->height = h;
    return SDL2F_OK;
}

int sdl2_font_line_height(const sdl2_font_t *ctx, sdl2_font_id_t font_id)
{
    if (ctx == NULL || !is_valid_font_id(font_id))
    {
        return 0;
    }
    return ctx->slots[font_id].line_height;
}

const char *sdl2_font_status_string(sdl2_font_status_t status)
{
    switch (status)
    {
        case SDL2F_OK:
            return "OK";
        case SDL2F_ERR_NULL_ARG:
            return "NULL argument";
        case SDL2F_ERR_RENDERER:
            return "NULL renderer";
        case SDL2F_ERR_TTF_INIT:
            return "SDL2_ttf initialization failed";
        case SDL2F_ERR_FONT_LOAD:
            return "failed to load font";
        case SDL2F_ERR_RENDER_FAILED:
            return "text rendering failed";
        case SDL2F_ERR_INVALID_FONT_ID:
            return "invalid font ID";
    }
    return "unknown status";
}
