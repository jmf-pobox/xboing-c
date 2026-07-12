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
    cfg.title = "- XBoing II -";
    return cfg;
}

sdl2_renderer_t *sdl2_renderer_create(const sdl2_renderer_config_t *config)
{
    if (config == NULL)
    {
        return NULL;
    }

    if (config->logical_width <= 0 || config->logical_height <= 0 || config->scale <= 0)
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

    /* Fit the initial window to the display's usable area while
     * preserving the configured logical_width:logical_height aspect
     * ratio.  Without this, scale=2 on a 1920×1200 display creates a
     * 1150×1440 window — 240px taller than the screen.  The WM
     * silently shrinks the height, breaking the aspect ratio and
     * producing prominent letterbox bars.
     *
     * Strategy: try the configured scale first.  If that overflows,
     * compute the largest window that fits by capping to the usable
     * height and deriving width from the aspect ratio.  Falls back to
     * the configured scale if SDL_GetDisplayUsableBounds fails
     * (e.g., headless / dummy driver).  SDL_RenderSetLogicalSize
     * handles internal scaling; SDL_HINT_RENDER_SCALE_QUALITY =
     * "nearest" preserves pixel-art sharpness at non-integer scales.
     *
     * Design ref: docs/DESIGN.md ADR-004, point 2. */
    int physical_w = config->logical_width * config->scale;
    int physical_h = config->logical_height * config->scale;

    if (!config->fullscreen)
    {
        SDL_Rect usable;
        if (SDL_GetDisplayUsableBounds(0, &usable) == 0)
        {
            if (physical_w > usable.w || physical_h > usable.h)
            {
                /* Scale to fit: cap to usable height, derive width
                 * from aspect ratio.  If width overflows too, cap to
                 * usable width and derive height instead. */
                int fit_h = usable.h;
                int fit_w = fit_h * config->logical_width / config->logical_height;
                if (fit_w > usable.w)
                {
                    fit_w = usable.w;
                    fit_h = fit_w * config->logical_height / config->logical_width;
                }
                physical_w = fit_w;
                physical_h = fit_h;
            }
        }
    }

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
        if (SDL_SetWindowFullscreen(ctx->window, 0) == 0)
        {
            ctx->fullscreen = false;
        }
    }
    else
    {
        if (SDL_SetWindowFullscreen(ctx->window, SDL_WINDOW_FULLSCREEN_DESKTOP) == 0)
        {
            ctx->fullscreen = true;
        }
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

void sdl2_renderer_set_mouse_grab(sdl2_renderer_t *ctx, bool grab)
{
    if (ctx == NULL || ctx->window == NULL)
    {
        return;
    }
    SDL_SetWindowMouseGrab(ctx->window, grab ? SDL_TRUE : SDL_FALSE);
}

bool sdl2_renderer_is_mouse_grabbed(const sdl2_renderer_t *ctx)
{
    if (ctx == NULL || ctx->window == NULL)
    {
        return false;
    }
    return SDL_GetWindowMouseGrab(ctx->window) == SDL_TRUE;
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

int sdl2_renderer_set_logical_width(sdl2_renderer_t *ctx, int new_logical_width)
{
    if (ctx == NULL || ctx->window == NULL || ctx->renderer == NULL || ctx->logical_height <= 0 ||
        new_logical_width <= 0)
    {
        return -1;
    }
    if (new_logical_width == ctx->logical_width)
    {
        return 0;
    }

    /* Resize the physical window before SDL_RenderSetLogicalSize so the
     * logical-size call's scale computation reads the updated output size
     * (ADR-004). Capture the old window size so the operation stays
     * transactional: if SDL_RenderSetLogicalSize fails, revert the window
     * so both logical and physical sizing stay unchanged. */
    int old_win_w = 0;
    int win_h = 0;
    bool window_resized = false;
    if (!ctx->fullscreen)
    {
        SDL_GetWindowSize(ctx->window, &old_win_w, &win_h);
        int new_win_w = (new_logical_width * win_h) / ctx->logical_height;
        SDL_SetWindowSize(ctx->window, new_win_w, win_h);
        window_resized = true;
    }

    if (SDL_RenderSetLogicalSize(ctx->renderer, new_logical_width, ctx->logical_height) != 0)
    {
        if (window_resized)
        {
            SDL_SetWindowSize(ctx->window, old_win_w, win_h);
        }
        return -1;
    }
    ctx->logical_width = new_logical_width;
    return 0;
}

int sdl2_renderer_save_screenshot(const sdl2_renderer_t *ctx, const char *path)
{
    if (ctx == NULL || ctx->renderer == NULL || path == NULL)
    {
        return -1;
    }
    int w = ctx->logical_width;
    int h = ctx->logical_height;
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (surf == NULL)
    {
        return -1;
    }
    if (SDL_RenderReadPixels(ctx->renderer, NULL, SDL_PIXELFORMAT_RGBA32, surf->pixels,
                             surf->pitch) != 0)
    {
        SDL_FreeSurface(surf);
        return -1;
    }
    int rc = SDL_SaveBMP(surf, path);
    SDL_FreeSurface(surf);
    return rc;
}
