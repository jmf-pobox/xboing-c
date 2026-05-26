/*
 * test_attract_screenshots.c — Visual regression: direct vs C-key transitions.
 *
 * Captures each attract screen entered two ways:
 *   1. Direct sdl2_state_transition (canonical enter path)
 *   2. C-key from the prior screen (user fast-forward)
 *
 * Saves BMP pairs to .tmp/ and compares pixel-by-pixel.  Differences
 * prove that the C-key path leaves the screen in a different visual
 * state than the direct path — a structural initialization bug.
 *
 * Requires SDL_VIDEODRIVER=offscreen for real pixel rendering.
 * Uses -nosound to avoid SDL_mixer teardown crash with dummy audio.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

#include <SDL2/SDL.h>
#include <cmocka.h>

#include "game_context.h"
#include "game_init.h"
#include "game_input.h"
#include "sdl2_input.h"
#include "sdl2_loop.h"
#include "sdl2_renderer.h"
#include "sdl2_state.h"

/* =========================================================================
 * Helpers
 * ========================================================================= */

static void inject_key(game_ctx_t *ctx, SDL_Scancode sc)
{
    sdl2_input_begin_frame(ctx->input);
    SDL_Event e = {0};
    e.type = SDL_KEYDOWN;
    e.key.keysym.scancode = sc;
    e.key.repeat = 0;
    sdl2_input_process_event(ctx->input, &e);
}

static void advance_ticks(game_ctx_t *ctx, int count)
{
    for (int i = 0; i < count; i++)
    {
        sdl2_input_begin_frame(ctx->input);
        sdl2_loop_update(ctx->loop, 8);
    }
}

typedef struct
{
    uint8_t *pixels;
    int w;
    int h;
    int pitch;
} framebuf_t;

static framebuf_t capture_and_save(const game_ctx_t *ctx, const char *bmp_path)
{
    framebuf_t fb = {0};
    SDL_Renderer *r = sdl2_renderer_get(ctx->renderer);
    assert_non_null(r);
    sdl2_renderer_get_logical_size(ctx->renderer, &fb.w, &fb.h);
    fb.pitch = fb.w * 4;

    SDL_Surface *surf =
        SDL_CreateRGBSurfaceWithFormat(0, fb.w, fb.h, 32, SDL_PIXELFORMAT_RGBA32);
    assert_non_null(surf);
    int rc = SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_RGBA32, surf->pixels, surf->pitch);
    assert_int_equal(rc, 0);

    if (bmp_path)
    {
        int save_rc = SDL_SaveBMP(surf, bmp_path);
        if (save_rc != 0)
            fprintf(stderr, "WARNING: SDL_SaveBMP(%s) failed: %s\n", bmp_path, SDL_GetError());
    }

    fb.pixels = calloc(1, (size_t)(fb.pitch * fb.h));
    assert_non_null(fb.pixels);
    memcpy(fb.pixels, surf->pixels, (size_t)(fb.pitch * fb.h));
    SDL_FreeSurface(surf);

    return fb;
}

static double compare_framebuffers(const framebuf_t *a, const framebuf_t *b)
{
    assert_int_equal(a->w, b->w);
    assert_int_equal(a->h, b->h);
    int total = a->w * a->h;
    if (total == 0)
    {
        return 1.0;
    }
    int matching = 0;
    for (int y = 0; y < a->h; y++)
    {
        const uint8_t *ra = a->pixels + y * a->pitch;
        const uint8_t *rb = b->pixels + y * b->pitch;
        for (int x = 0; x < a->w; x++)
        {
            int off = x * 4;
            if (ra[off] == rb[off] && ra[off + 1] == rb[off + 1] && ra[off + 2] == rb[off + 2])
            {
                matching++;
            }
        }
    }
    return (double)matching / (double)total;
}

/* =========================================================================
 * Shared game context
 * ========================================================================= */

static game_ctx_t *g_ctx;

static int group_setup(void **vstate)
{
    char arg0[] = "xboing_test";
    char arg1[] = "-nosound";
    char *argv[] = {arg0, arg1, NULL};
    g_ctx = game_create(2, argv);
    assert_non_null(g_ctx);
    mkdir(".tmp", 0755);
    sdl2_state_transition(g_ctx->state, SDL2ST_PRESENTS);
    *vstate = g_ctx;
    return 0;
}

static int group_teardown(void **vstate)
{
    (void)vstate;
    game_destroy(g_ctx);
    g_ctx = NULL;
    return 0;
}

/* =========================================================================
 * Screen definitions — C-key cycle order per game_input.c
 * ========================================================================= */

typedef struct
{
    sdl2_state_mode_t prior;
    sdl2_state_mode_t target;
    const char *label;
} screen_pair_t;

static const screen_pair_t attract_screens[] = {
    {SDL2ST_INTRO, SDL2ST_INSTRUCT, "instruct"},
    {SDL2ST_INSTRUCT, SDL2ST_DEMO, "demo"},
    {SDL2ST_DEMO, SDL2ST_KEYS, "keys"},
    {SDL2ST_KEYS, SDL2ST_KEYSEDIT, "keysedit"},
    {SDL2ST_KEYSEDIT, SDL2ST_PREVIEW, "preview"},
    {SDL2ST_PREVIEW, SDL2ST_HIGHSCORE, "highscore"},
};

#define NUM_SCREENS (sizeof(attract_screens) / sizeof(attract_screens[0]))
#define STABILIZE_TICKS 100

/* =========================================================================
 * Test: compare direct vs C-key entry for every attract screen
 *
 * Captures 12 screenshots (6 direct, 6 C-key) and reports pixel
 * similarity.  Screens with >5% pixel difference FAIL.
 * ========================================================================= */

static void test_all_screens(void **vstate)
{
    (void)vstate;
    game_ctx_t *ctx = g_ctx;
    int failures = 0;

    /* Phase 1: capture direct-entry screenshots */
    framebuf_t direct_fbs[NUM_SCREENS];
    for (size_t i = 0; i < NUM_SCREENS; i++)
    {
        char path[128];
        snprintf(path, sizeof(path), ".tmp/direct_%s.bmp", attract_screens[i].label);
        sdl2_state_transition(ctx->state, attract_screens[i].target);
        advance_ticks(ctx, STABILIZE_TICKS);
        direct_fbs[i] = capture_and_save(ctx, path);
    }

    /* Phase 2: capture C-key screenshots */
    framebuf_t ckey_fbs[NUM_SCREENS];
    for (size_t i = 0; i < NUM_SCREENS; i++)
    {
        char path[128];
        snprintf(path, sizeof(path), ".tmp/ckey_%s.bmp", attract_screens[i].label);
        sdl2_state_transition(ctx->state, attract_screens[i].prior);
        advance_ticks(ctx, 5);
        inject_key(ctx, SDL_SCANCODE_C);
        game_input_global(ctx);

        if (sdl2_state_current(ctx->state) != attract_screens[i].target)
        {
            fprintf(stderr, "FAIL %s: C-key from %d did not reach %d (got %d)\n",
                    attract_screens[i].label, attract_screens[i].prior, attract_screens[i].target,
                    sdl2_state_current(ctx->state));
            memset(&ckey_fbs[i], 0, sizeof(ckey_fbs[i]));
            failures++;
            continue;
        }

        advance_ticks(ctx, STABILIZE_TICKS);
        ckey_fbs[i] = capture_and_save(ctx, path);
    }

    /* Phase 3: compare and report */
    fprintf(stderr, "\n--- Direct vs C-key similarity ---\n");
    for (size_t i = 0; i < NUM_SCREENS; i++)
    {
        if (!ckey_fbs[i].pixels)
        {
            fprintf(stderr, "  %-12s  SKIP (transition failed)\n", attract_screens[i].label);
            continue;
        }
        double sim = compare_framebuffers(&direct_fbs[i], &ckey_fbs[i]);
        const char *verdict = sim > 0.95 ? "PASS" : "FAIL";
        fprintf(stderr, "  %-12s  %.1f%% match  %s\n", attract_screens[i].label, sim * 100.0,
                verdict);
        if (sim <= 0.95)
        {
            failures++;
        }
    }

    /* Cleanup */
    for (size_t i = 0; i < NUM_SCREENS; i++)
    {
        free(direct_fbs[i].pixels);
        free(ckey_fbs[i].pixels);
    }

    if (failures > 0)
    {
        fprintf(stderr,
                "\n%d screen(s) differ.  Inspect .tmp/direct_*.bmp vs .tmp/ckey_*.bmp\n",
                failures);
    }
    assert_int_equal(failures, 0);
}

/* =========================================================================
 * Runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_all_screens),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
