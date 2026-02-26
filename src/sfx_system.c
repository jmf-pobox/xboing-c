/*
 * sfx_system.c — Pure C visual special effects state machine.
 *
 * Owns 5 effect modes (SHAKE, FADE, BLIND, SHATTER, STATIC), the
 * enable/disable toggle, and the BorderGlow ambient animation.
 *
 * All rendering is delegated to the integration layer via callbacks
 * and query functions.  This module computes coordinates, timing,
 * and tile order; it never draws anything itself.
 *
 * Opaque context pattern: no globals, fully testable with CMocka stubs.
 * See ADR-023 in docs/DESIGN.md for design rationale.
 */

#include "sfx_system.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal structure
 * ========================================================================= */

struct sfx_system
{
    /* Current effect */
    sfx_mode_t mode;
    int enabled; /* 1 = effects on, 0 = off */

    /* Timed effect expiry (absolute frame count) */
    int end_frame;

    /* SHAKE state */
    int shake_x; /* Current window X position */
    int shake_y; /* Current window Y position */

    /* FADE state */
    int fade_step; /* Current grid step (0..12) */
    int fade_w;    /* Target area width (set at activation) */
    int fade_h;    /* Target area height (set at activation) */

    /* STATIC state */
    int static_started; /* Whether SetSfxEndFrame has been called */

    /* SHATTER scatter permutation tables */
    int xscat[SFX_NUM_SCAT];
    int yscat[SFX_NUM_SCAT];

    /* BorderGlow state */
    int glow_index;     /* Color index (0..SFX_GLOW_STEPS-1) */
    int glow_direction; /* +1 = ascending, -1 = descending */
    int glow_phase;     /* +1 = red, -1 = green */

    /* Devil eyes state */
    int deveye_slide;  /* Current position in the 26-step blink sequence */
    int deveye_active; /* 1 = animation running, 0 = idle */
    int deveye_x;      /* Computed draw position X */
    int deveye_y;      /* Computed draw position Y */

    /* Callbacks */
    sfx_system_callbacks_t callbacks;
    void *user_data;

    /* Random function */
    sfx_rand_fn rand_fn;
};

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static int get_rand(const sfx_system_t *ctx)
{
    if (ctx->rand_fn)
    {
        return ctx->rand_fn();
    }
    return rand();
}

static void reset_effect(sfx_system_t *ctx)
{
    ctx->mode = SFX_MODE_NONE;

    /* Restore canonical window position (cleanup after SHAKE) */
    if (ctx->callbacks.on_move_window)
    {
        ctx->callbacks.on_move_window(SFX_WINDOW_X, SFX_WINDOW_Y, ctx->user_data);
    }
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

sfx_system_t *sfx_system_create(const sfx_system_callbacks_t *callbacks, void *user_data,
                                sfx_rand_fn rand_fn)
{
    sfx_system_t *ctx = calloc(1, sizeof(sfx_system_t));
    if (ctx == NULL)
    {
        return NULL;
    }

    ctx->enabled = 1; /* Effects on by default */
    ctx->user_data = user_data;
    ctx->rand_fn = rand_fn;

    if (callbacks)
    {
        ctx->callbacks = *callbacks;
    }

    /* Legacy scatter permutation tables from sfx.c:89-90 */
    static const int xscat_init[SFX_NUM_SCAT] = {1, 9, 3, 6, 2, 4, 0, 7, 5, 8};
    static const int yscat_init[SFX_NUM_SCAT] = {2, 1, 0, 8, 6, 4, 9, 3, 7, 5};
    memcpy(ctx->xscat, xscat_init, sizeof(ctx->xscat));
    memcpy(ctx->yscat, yscat_init, sizeof(ctx->yscat));

    /* BorderGlow initial state */
    ctx->glow_index = 0;
    ctx->glow_direction = 1;
    ctx->glow_phase = 1; /* Start with red */

    /* Shake starts at canonical position */
    ctx->shake_x = SFX_WINDOW_X;
    ctx->shake_y = SFX_WINDOW_Y;

    return ctx;
}

void sfx_system_destroy(sfx_system_t *ctx)
{
    free(ctx);
}

/* =========================================================================
 * Enable / disable
 * ========================================================================= */

void sfx_system_set_enabled(sfx_system_t *ctx, int enabled)
{
    if (ctx)
    {
        ctx->enabled = enabled ? 1 : 0;
    }
}

int sfx_system_get_enabled(const sfx_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    return ctx->enabled;
}

/* =========================================================================
 * Mode management
 * ========================================================================= */

void sfx_system_set_mode(sfx_system_t *ctx, sfx_mode_t mode)
{
    if (ctx == NULL)
    {
        return;
    }

    ctx->mode = mode;

    /* Reset per-effect state on activation */
    switch (mode)
    {
        case SFX_MODE_SHAKE:
            ctx->shake_x = SFX_WINDOW_X;
            ctx->shake_y = SFX_WINDOW_Y;
            break;

        case SFX_MODE_FADE:
            ctx->fade_step = 0;
            break;

        case SFX_MODE_STATIC:
            ctx->static_started = 0;
            break;

        case SFX_MODE_BLIND:
        case SFX_MODE_SHATTER:
        case SFX_MODE_NONE:
            break;
    }
}

sfx_mode_t sfx_system_get_mode(const sfx_system_t *ctx)
{
    if (ctx == NULL)
    {
        return SFX_MODE_NONE;
    }
    return ctx->mode;
}

void sfx_system_set_end_frame(sfx_system_t *ctx, int end_frame)
{
    if (ctx)
    {
        ctx->end_frame = end_frame;
    }
}

/* =========================================================================
 * Per-effect update handlers
 * ========================================================================= */

static int update_shake(sfx_system_t *ctx, int frame)
{
    if (!ctx->enabled)
    {
        reset_effect(ctx);
        return 0;
    }

    /* Check expiry */
    if (frame >= ctx->end_frame)
    {
        reset_effect(ctx);
        return 0;
    }

    /* Throttle: only move every SHAKE_DELAY frames */
    if ((frame % SFX_SHAKE_DELAY) != 0)
    {
        return 1;
    }

    /* Move window to current position */
    if (ctx->callbacks.on_move_window)
    {
        ctx->callbacks.on_move_window(ctx->shake_x, ctx->shake_y, ctx->user_data);
    }

    /* Compute next position: ±3 pixels around canonical */
    int xi = (get_rand(ctx) % 6) - 3;
    int yi = (get_rand(ctx) % 6) - 3;
    ctx->shake_x = xi + SFX_WINDOW_X;
    ctx->shake_y = yi + SFX_WINDOW_Y;

    return 1;
}

static int update_fade(sfx_system_t *ctx)
{
    if (!ctx->enabled)
    {
        reset_effect(ctx);
        return 0;
    }

    /* Check if we've completed all steps */
    if (ctx->fade_step > SFX_FADE_STRIDE)
    {
        ctx->fade_step = 0;
        reset_effect(ctx);
        return 0;
    }

    /* Advance step for next frame */
    ctx->fade_step++;

    return 1;
}

static int update_blind(sfx_system_t *ctx)
{
    if (!ctx->enabled)
    {
        reset_effect(ctx);
        return 0;
    }

    /* Blind is synchronous — completes in one call */
    reset_effect(ctx);
    return 0;
}

static int update_shatter(sfx_system_t *ctx)
{
    if (!ctx->enabled)
    {
        reset_effect(ctx);
        return 0;
    }

    /* Shatter is synchronous — completes in one call */
    reset_effect(ctx);
    return 0;
}

static int update_static(sfx_system_t *ctx, int frame)
{
    if (!ctx->static_started)
    {
        ctx->end_frame = frame + SFX_STATIC_DURATION;
        ctx->static_started = 1;
    }

    if (frame >= ctx->end_frame)
    {
        ctx->static_started = 0;
        reset_effect(ctx);
        return 0;
    }

    /* Legacy stub: no actual drawing — "Do somehting in here" */
    return 1;
}

/* =========================================================================
 * Main update dispatch
 * ========================================================================= */

int sfx_system_update(sfx_system_t *ctx, int frame)
{
    if (ctx == NULL)
    {
        return 0;
    }

    switch (ctx->mode)
    {
        case SFX_MODE_SHAKE:
            return update_shake(ctx, frame);

        case SFX_MODE_FADE:
            return update_fade(ctx);

        case SFX_MODE_BLIND:
            return update_blind(ctx);

        case SFX_MODE_SHATTER:
            return update_shatter(ctx);

        case SFX_MODE_STATIC:
            return update_static(ctx, frame);

        case SFX_MODE_NONE:
        default:
            return 0;
    }
}

/* =========================================================================
 * Effect state queries
 * ========================================================================= */

sfx_shake_pos_t sfx_system_get_shake_pos(const sfx_system_t *ctx)
{
    sfx_shake_pos_t pos = {SFX_WINDOW_X, SFX_WINDOW_Y};
    if (ctx != NULL && ctx->mode == SFX_MODE_SHAKE)
    {
        pos.x = ctx->shake_x;
        pos.y = ctx->shake_y;
    }
    return pos;
}

sfx_fade_frame_t sfx_system_get_fade_frame(const sfx_system_t *ctx)
{
    sfx_fade_frame_t info = {0, SFX_FADE_STRIDE, 0, 0};
    if (ctx != NULL && ctx->mode == SFX_MODE_FADE)
    {
        /* Return the step that was just processed (step-1) since
         * update_fade increments after processing */
        info.step = ctx->fade_step > 0 ? ctx->fade_step - 1 : 0;
        info.w = ctx->fade_w;
        info.h = ctx->fade_h;
    }
    return info;
}

int sfx_system_get_shatter_tiles(sfx_system_t *ctx, sfx_shatter_tile_t *tiles, int max_tiles,
                                 int play_w, int play_h)
{
    if (ctx == NULL || tiles == NULL || max_tiles <= 0)
    {
        return 0;
    }

    /* Legacy shatter: 200x200 chunks, 10x10 tiles per chunk, 5x6 chunk grid */
    int size_w = 200;
    int size_h = 200;
    int tile_w = size_w / SFX_NUM_SCAT; /* 20 */
    int tile_h = size_h / SFX_NUM_SCAT; /* 20 */

    int offx = get_rand(ctx) % SFX_NUM_SCAT;
    int offy = get_rand(ctx) % SFX_NUM_SCAT;

    int count = 0;

    for (int srcx = 0; srcx < SFX_NUM_SCAT && count < max_tiles; srcx++)
    {
        for (int srcy = 0; srcy < SFX_NUM_SCAT && count < max_tiles; srcy++)
        {
            /* destx covers 0..4 (5 chunks), desty covers 0..5 (6 chunks) */
            int dest_cols = (play_w + size_w - 1) / size_w;
            int dest_rows = (play_h + size_h - 1) / size_h;
            if (dest_cols > 5)
            {
                dest_cols = 5;
            }
            if (dest_rows > 6)
            {
                dest_rows = 6;
            }

            for (int destx = 0; destx < dest_cols && count < max_tiles; destx++)
            {
                for (int desty = 0; desty < dest_rows && count < max_tiles; desty++)
                {
                    int tx = ctx->xscat[(srcx + srcy + offx) % SFX_NUM_SCAT];
                    int ty = ctx->yscat[(srcy + offy) % SFX_NUM_SCAT];

                    tiles[count].x = destx * size_w + tx * tile_w;
                    tiles[count].y = desty * size_h + ty * tile_h;
                    tiles[count].w = tile_w;
                    tiles[count].h = tile_h;
                    count++;
                }
            }
        }
    }

    return count;
}

int sfx_system_get_blind_strips(const sfx_system_t *ctx, sfx_blind_strip_t *strips, int max_strips,
                                int play_w, int play_h)
{
    if (ctx == NULL || strips == NULL || max_strips <= 0)
    {
        return 0;
    }

    int strip_w = play_w / 8;
    if (strip_w <= 0)
    {
        return 0;
    }

    int count = 0;

    /* Legacy: outer loop i from 0 to strip_w, inner x in steps of strip_w */
    for (int i = 0; i <= strip_w && count < max_strips; i++)
    {
        for (int x = 0; x < play_w && count < max_strips; x += strip_w)
        {
            strips[count].x = x + i;
            strips[count].h = play_h;
            count++;
        }
    }

    return count;
}

/* =========================================================================
 * BorderGlow
 * ========================================================================= */

sfx_glow_state_t sfx_system_update_glow(sfx_system_t *ctx, int frame)
{
    sfx_glow_state_t state = {0, 0};
    if (ctx == NULL)
    {
        return state;
    }

    /* Only update every SFX_GLOW_FRAME_INTERVAL frames */
    if ((frame % SFX_GLOW_FRAME_INTERVAL) != 0)
    {
        state.color_index = ctx->glow_index;
        state.use_green = ctx->glow_phase < 0 ? 1 : 0;
        return state;
    }

    /* Advance index */
    ctx->glow_index += ctx->glow_direction;

    /* Bounds check and direction reversal */
    if (ctx->glow_index >= SFX_GLOW_STEPS)
    {
        ctx->glow_index = SFX_GLOW_STEPS - 1;
        ctx->glow_direction = -1;
        ctx->glow_phase = -ctx->glow_phase; /* Toggle red/green */
    }
    else if (ctx->glow_index < 0)
    {
        ctx->glow_index = 0;
        ctx->glow_direction = 1;
    }

    state.color_index = ctx->glow_index;
    state.use_green = ctx->glow_phase < 0 ? 1 : 0;
    return state;
}

void sfx_system_reset_glow(sfx_system_t *ctx)
{
    if (ctx)
    {
        ctx->glow_index = 0;
        ctx->glow_direction = 1;
        ctx->glow_phase = 1;
    }
}

/* =========================================================================
 * FadeAwayArea (stateless utility)
 * ========================================================================= */

int sfx_system_fadeaway_steps(int w)
{
    if (w <= 0)
    {
        return 0;
    }
    return w / SFX_FADEAWAY_STRIDE + 1;
}

/* =========================================================================
 * Devil eyes blink animation
 * ========================================================================= */

/* Legacy 26-step blink sequence from stage.c:126 */
static const int deveye_seq[SFX_DEVEYE_SEQ_LEN] = {0, 1, 2, 3, 4, 5, 5, 4, 3, 2, 1, 0, 0,
                                                   0, 0, 1, 2, 3, 4, 5, 5, 4, 3, 2, 1, 0};

void sfx_system_start_deveyes(sfx_system_t *ctx)
{
    if (ctx)
    {
        ctx->deveye_slide = 0;
        ctx->deveye_active = 1;
    }
}

int sfx_system_update_deveyes(sfx_system_t *ctx, int play_w, int play_h)
{
    if (ctx == NULL || !ctx->deveye_active)
    {
        return 0;
    }

    /* Compute position: bottom-right of play area, matching legacy
     * devilx = PLAY_WIDTH - DEVILEYE_WC - 5
     * devily = PLAY_HEIGHT - DEVILEYE_HC - 5
     * Then draw at (devilx - DEVILEYE_WC, devily - DEVILEYE_HC) */
    int half_w = SFX_DEVEYE_WIDTH / 2;
    int half_h = SFX_DEVEYE_HEIGHT / 2;
    int cx = play_w - half_w - SFX_DEVEYE_MARGIN;
    int cy = play_h - half_h - SFX_DEVEYE_MARGIN;
    ctx->deveye_x = cx - half_w;
    ctx->deveye_y = cy - half_h;

    ctx->deveye_slide++;
    if (ctx->deveye_slide >= SFX_DEVEYE_SEQ_LEN)
    {
        ctx->deveye_slide = 0;
        ctx->deveye_active = 0;
        return 0;
    }

    return 1;
}

sfx_deveye_info_t sfx_system_get_deveye_info(const sfx_system_t *ctx)
{
    sfx_deveye_info_t info = {0, 0, 0, 0};
    if (ctx == NULL)
    {
        return info;
    }

    info.active = ctx->deveye_active;
    if (ctx->deveye_active && ctx->deveye_slide < SFX_DEVEYE_SEQ_LEN)
    {
        info.frame_index = deveye_seq[ctx->deveye_slide];
        info.x = ctx->deveye_x;
        info.y = ctx->deveye_y;
    }

    return info;
}
