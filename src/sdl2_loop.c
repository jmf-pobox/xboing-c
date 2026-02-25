/*
 * sdl2_loop.c — Fixed-timestep game loop timing module.
 *
 * See include/sdl2_loop.h for API documentation.
 * See ADR-013 in docs/DESIGN.md for design rationale.
 */

#include "sdl2_loop.h"

#include <stdlib.h>

/* =========================================================================
 * Internal data structures
 * ========================================================================= */

struct sdl2_loop
{
    /* Callbacks and user data. */
    sdl2_loop_tick_fn tick_fn;
    sdl2_loop_render_fn render_fn;
    void *user_data;

    /* Current speed level (1-9) and derived tick interval in microseconds. */
    int speed;
    uint64_t tick_interval_us;

    /* Accumulator in microseconds — sub-millisecond precision. */
    uint64_t accumulator_us;

    /* Pause state. */
    bool paused;

    /* Statistics. */
    uint64_t total_ticks;
    double alpha;
};

/* Microseconds per millisecond. */
#define US_PER_MS 1000U

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static uint64_t compute_tick_interval(int speed_level)
{
    return (uint64_t)SDL2L_TICK_UNIT_US * (uint64_t)(10 - speed_level);
}

/* =========================================================================
 * Public API — Lifecycle
 * ========================================================================= */

sdl2_loop_t *sdl2_loop_create(sdl2_loop_tick_fn tick_fn, sdl2_loop_render_fn render_fn,
                              void *user_data, sdl2_loop_status_t *status)
{
    sdl2_loop_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
    {
        if (status != NULL)
        {
            *status = SDL2L_ERR_ALLOC_FAILED;
        }
        return NULL;
    }

    ctx->tick_fn = tick_fn;
    ctx->render_fn = render_fn;
    ctx->user_data = user_data;
    ctx->speed = SDL2L_DEFAULT_SPEED;
    ctx->tick_interval_us = compute_tick_interval(SDL2L_DEFAULT_SPEED);
    ctx->accumulator_us = 0;
    ctx->paused = false;
    ctx->total_ticks = 0;
    ctx->alpha = 0.0;

    if (status != NULL)
    {
        *status = SDL2L_OK;
    }
    return ctx;
}

void sdl2_loop_destroy(sdl2_loop_t *ctx)
{
    free(ctx);
}

/* =========================================================================
 * Public API — Per-frame update
 * ========================================================================= */

int sdl2_loop_update(sdl2_loop_t *ctx, uint64_t elapsed_ms)
{
    if (ctx == NULL)
    {
        return 0;
    }

    /* When paused, do not accumulate time or dispatch callbacks. */
    if (ctx->paused)
    {
        return 0;
    }

    /* Clamp elapsed_ms to prevent overflow in the ms→us conversion.
     * 2^53 us ≈ 285 years — far beyond any real frame delta. */
    if (elapsed_ms > UINT64_MAX / US_PER_MS)
    {
        elapsed_ms = UINT64_MAX / US_PER_MS;
    }

    /* Add elapsed time to accumulator (ms → us). */
    ctx->accumulator_us += elapsed_ms * US_PER_MS;

    /* Consume fixed-timestep ticks from the accumulator. */
    int ticks = 0;
    while (ctx->accumulator_us >= ctx->tick_interval_us && ticks < SDL2L_MAX_TICKS_PER_UPDATE)
    {
        if (ctx->tick_fn != NULL)
        {
            ctx->tick_fn(ctx->user_data);
        }
        ctx->accumulator_us -= ctx->tick_interval_us;
        ticks++;
        ctx->total_ticks++;
    }

    /* Clamp: if we hit the max, discard leftover accumulator to prevent
     * a spiral of death on the next frame. */
    if (ticks == SDL2L_MAX_TICKS_PER_UPDATE)
    {
        ctx->accumulator_us = 0;
    }

    /* Compute interpolation alpha for smooth rendering. */
    if (ctx->tick_interval_us > 0)
    {
        ctx->alpha = (double)ctx->accumulator_us / (double)ctx->tick_interval_us;
    }
    else
    {
        ctx->alpha = 0.0;
    }

    /* Dispatch render callback exactly once. */
    if (ctx->render_fn != NULL)
    {
        ctx->render_fn(ctx->alpha, ctx->user_data);
    }

    return ticks;
}

/* =========================================================================
 * Public API — Speed control
 * ========================================================================= */

sdl2_loop_status_t sdl2_loop_set_speed(sdl2_loop_t *ctx, int level)
{
    if (ctx == NULL)
    {
        return SDL2L_ERR_NULL_ARG;
    }
    if (level < SDL2L_MIN_SPEED || level > SDL2L_MAX_SPEED)
    {
        return SDL2L_ERR_INVALID_SPEED;
    }

    ctx->speed = level;
    ctx->tick_interval_us = compute_tick_interval(level);
    return SDL2L_OK;
}

int sdl2_loop_get_speed(const sdl2_loop_t *ctx)
{
    if (ctx == NULL)
    {
        return SDL2L_DEFAULT_SPEED;
    }
    return ctx->speed;
}

uint64_t sdl2_loop_tick_interval_us(int speed_level)
{
    if (speed_level < SDL2L_MIN_SPEED || speed_level > SDL2L_MAX_SPEED)
    {
        return 0;
    }
    return compute_tick_interval(speed_level);
}

/* =========================================================================
 * Public API — Pause
 * ========================================================================= */

void sdl2_loop_set_paused(sdl2_loop_t *ctx, bool paused)
{
    if (ctx == NULL)
    {
        return;
    }

    bool was_paused = ctx->paused;
    ctx->paused = paused;

    /* Clear accumulator on unpause to prevent a burst of catch-up ticks. */
    if (was_paused && !paused)
    {
        ctx->accumulator_us = 0;
    }
}

bool sdl2_loop_is_paused(const sdl2_loop_t *ctx)
{
    if (ctx == NULL)
    {
        return false;
    }
    return ctx->paused;
}

/* =========================================================================
 * Public API — Statistics
 * ========================================================================= */

uint64_t sdl2_loop_total_ticks(const sdl2_loop_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    return ctx->total_ticks;
}

double sdl2_loop_alpha(const sdl2_loop_t *ctx)
{
    if (ctx == NULL)
    {
        return 0.0;
    }
    return ctx->alpha;
}

/* =========================================================================
 * Public API — Utility
 * ========================================================================= */

const char *sdl2_loop_status_string(sdl2_loop_status_t status)
{
    switch (status)
    {
        case SDL2L_OK:
            return "OK";
        case SDL2L_ERR_NULL_ARG:
            return "NULL argument";
        case SDL2L_ERR_INVALID_SPEED:
            return "invalid speed level (must be 1-9)";
        case SDL2L_ERR_ALLOC_FAILED:
            return "allocation failed";
    }
    return "unknown status";
}
