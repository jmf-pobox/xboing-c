/*
 * sdl2_renderer.c — SDL2 window and renderer lifecycle.
 *
 * See include/sdl2_renderer.h for API documentation.
 * See ADR-004 in docs/DESIGN.md for design rationale.
 */

#include "sdl2_renderer.h"

#include <stdlib.h>
#include <string.h>

struct sdl2_renderer
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    int logical_width;
    int logical_height;
    bool fullscreen;
    bool sdl_video_owned; /* true if we called SDL_InitSubSystem(VIDEO) */
};

sdl2_renderer_config_t sdl2_renderer_config_defaults(void)
{
    sdl2_renderer_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.logical_width = SDL2R_LOGICAL_WIDTH;
    cfg.logical_height = SDL2R_LOGICAL_HEIGHT;
    cfg.scale = SDL2R_DEFAULT_SCALE;
    cfg.fullscreen = false;
    cfg.vsync = true;
    cfg.title = "XBoing";
    return cfg;
}

sdl2_renderer_t *sdl2_renderer_create(const sdl2_renderer_config_t *config)
{
    if (config == NULL)
    {
        return NULL;
    }

    sdl2_renderer_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
    {
        return NULL;
    }

    /* Initialize SDL video if not already active. */
    ctx->sdl_video_owned = false;
    if (!(SDL_WasInit(0) & SDL_INIT_VIDEO))
    {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        {
            free(ctx);
            return NULL;
        }
        ctx->sdl_video_owned = true;
    }

    ctx->logical_width = config->logical_width;
    ctx->logical_height = config->logical_height;
    ctx->fullscreen = config->fullscreen;

    int physical_w = config->logical_width * config->scale;
    int physical_h = config->logical_height * config->scale;

    Uint32 window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (config->fullscreen)
    {
        window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    const char *title = config->title != NULL ? config->title : "XBoing";

    ctx->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   physical_w, physical_h, window_flags);
    if (ctx->window == NULL)
    {
        if (ctx->sdl_video_owned)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
        free(ctx);
        return NULL;
    }

    Uint32 renderer_flags = SDL_RENDERER_ACCELERATED;
    if (config->vsync)
    {
        renderer_flags |= SDL_RENDERER_PRESENTVSYNC;
    }

    ctx->renderer = SDL_CreateRenderer(ctx->window, -1, renderer_flags);
    if (ctx->renderer == NULL)
    {
        /* Fall back to software renderer (e.g. CI dummy driver). */
        ctx->renderer = SDL_CreateRenderer(ctx->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (ctx->renderer == NULL)
    {
        SDL_DestroyWindow(ctx->window);
        if (ctx->sdl_video_owned)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
        free(ctx);
        return NULL;
    }

    SDL_RenderSetLogicalSize(ctx->renderer, ctx->logical_width, ctx->logical_height);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    return ctx;
}

void sdl2_renderer_destroy(sdl2_renderer_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    if (ctx->renderer != NULL)
    {
        SDL_DestroyRenderer(ctx->renderer);
    }
    if (ctx->window != NULL)
    {
        SDL_DestroyWindow(ctx->window);
    }
    if (ctx->sdl_video_owned)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    free(ctx);
}

void sdl2_renderer_clear(sdl2_renderer_t *ctx)
{
    if (ctx == NULL || ctx->renderer == NULL)
    {
        return;
    }
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx->renderer);
}

void sdl2_renderer_present(sdl2_renderer_t *ctx)
{
    if (ctx == NULL || ctx->renderer == NULL)
    {
        return;
    }
    SDL_RenderPresent(ctx->renderer);
}

bool sdl2_renderer_toggle_fullscreen(sdl2_renderer_t *ctx)
{
    if (ctx == NULL || ctx->window == NULL)
    {
        return false;
    }

    if (ctx->fullscreen)
    {
        SDL_SetWindowFullscreen(ctx->window, 0);
        ctx->fullscreen = false;
    }
    else
    {
        SDL_SetWindowFullscreen(ctx->window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        ctx->fullscreen = true;
    }

    return ctx->fullscreen;
}

bool sdl2_renderer_is_fullscreen(const sdl2_renderer_t *ctx)
{
    if (ctx == NULL)
    {
        return false;
    }
    return ctx->fullscreen;
}

SDL_Renderer *sdl2_renderer_get(const sdl2_renderer_t *ctx)
{
    if (ctx == NULL)
    {
        return NULL;
    }
    return ctx->renderer;
}

SDL_Window *sdl2_renderer_get_window(const sdl2_renderer_t *ctx)
{
    if (ctx == NULL)
    {
        return NULL;
    }
    return ctx->window;
}

void sdl2_renderer_get_logical_size(const sdl2_renderer_t *ctx, int *w, int *h)
{
    if (ctx == NULL)
    {
        if (w != NULL)
        {
            *w = 0;
        }
        if (h != NULL)
        {
            *h = 0;
        }
        return;
    }
    if (w != NULL)
    {
        *w = ctx->logical_width;
    }
    if (h != NULL)
    {
        *h = ctx->logical_height;
    }
}

void sdl2_renderer_get_window_size(const sdl2_renderer_t *ctx, int *w, int *h)
{
    if (ctx == NULL || ctx->window == NULL)
    {
        if (w != NULL)
        {
            *w = 0;
        }
        if (h != NULL)
        {
            *h = 0;
        }
        return;
    }
    SDL_GetWindowSize(ctx->window, w, h);
}
