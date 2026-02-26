/*
 * score_system.c — Pure C score management with callback-based side effects.
 *
 * Owns the game score value, multiplier application, extra life tracking,
 * and digit layout computation for rendering.  Delegates pure arithmetic
 * to score_logic.h functions.  Zero dependency on SDL2 or X11.
 *
 * Opaque context pattern: no globals, fully testable with CMocka stubs.
 * See ADR-019 in docs/DESIGN.md for design rationale.
 */

#include "score_system.h"
#include "score_logic.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal structure
 * ========================================================================= */

struct score_system
{
    u_long score;
    int life_threshold; /* Previous extra life threshold index */
    score_system_callbacks_t callbacks;
    void *user_data;
};

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static void check_extra_life(score_system_t *ctx)
{
    int new_threshold = score_extra_life_threshold((long)ctx->score);
    if (new_threshold > ctx->life_threshold)
    {
        ctx->life_threshold = new_threshold;
        if (ctx->callbacks.on_extra_life)
        {
            ctx->callbacks.on_extra_life(ctx->score, ctx->user_data);
        }
    }
}

static void notify_score_changed(score_system_t *ctx)
{
    if (ctx->callbacks.on_score_changed)
    {
        ctx->callbacks.on_score_changed(ctx->score, ctx->user_data);
    }
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

score_system_t *score_system_create(const score_system_callbacks_t *callbacks, void *user_data,
                                    score_system_status_t *status)
{
    score_system_t *ctx = calloc(1, sizeof(score_system_t));
    if (ctx == NULL)
    {
        if (status)
        {
            *status = SCORE_SYS_ERR_ALLOC_FAILED;
        }
        return NULL;
    }

    ctx->user_data = user_data;

    if (callbacks)
    {
        ctx->callbacks = *callbacks;
    }

    ctx->score = 0;
    ctx->life_threshold = 0;

    if (status)
    {
        *status = SCORE_SYS_OK;
    }
    return ctx;
}

void score_system_destroy(score_system_t *ctx)
{
    free(ctx);
}

/* =========================================================================
 * Score operations
 * ========================================================================= */

void score_system_set(score_system_t *ctx, u_long value)
{
    if (ctx == NULL)
    {
        return;
    }
    ctx->score = value;
    /* Reset extra life tracking to current threshold */
    ctx->life_threshold = score_extra_life_threshold((long)value);
    notify_score_changed(ctx);
}

u_long score_system_get(const score_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    return ctx->score;
}

u_long score_system_add(score_system_t *ctx, u_long increment, const score_system_env_t *env)
{
    if (ctx == NULL)
    {
        return 0;
    }

    int x2 = 0;
    int x4 = 0;
    if (env)
    {
        x2 = env->x2_active;
        x4 = env->x4_active;
    }

    u_long adjusted = score_apply_multiplier(increment, x2, x4);
    ctx->score += adjusted;

    check_extra_life(ctx);
    notify_score_changed(ctx);

    return ctx->score;
}

u_long score_system_add_raw(score_system_t *ctx, u_long increment)
{
    if (ctx == NULL)
    {
        return 0;
    }

    ctx->score += increment;

    check_extra_life(ctx);
    notify_score_changed(ctx);

    return ctx->score;
}

/* =========================================================================
 * Digit layout computation
 * ========================================================================= */

void score_system_get_digit_layout(u_long score_value, score_system_digit_layout_t *layout)
{
    if (layout == NULL)
    {
        return;
    }

    memset(layout, 0, sizeof(*layout));
    layout->y = 0;

    if (score_value == 0)
    {
        /* Legacy: single "0" at x=192 (SCORE_WINDOW_WIDTH - SCORE_DIGIT_STRIDE) */
        layout->count = 1;
        layout->digits[0] = 0;
        layout->x_positions[0] = SCORE_WINDOW_WIDTH - SCORE_DIGIT_STRIDE;
        return;
    }

    /* Decompose score into digits, least-significant first */
    int temp_digits[SCORE_MAX_DIGITS];
    int n = 0;
    u_long remaining = score_value;
    while (remaining > 0 && n < SCORE_MAX_DIGITS)
    {
        temp_digits[n] = (int)(remaining % 10);
        remaining /= 10;
        n++;
    }

    layout->count = n;

    /* Reverse into most-significant-first order and compute x positions.
     * Rightmost digit: x = SCORE_WINDOW_WIDTH - SCORE_DIGIT_STRIDE = 192
     * Each digit to the left: x -= SCORE_DIGIT_STRIDE */
    for (int i = 0; i < n; i++)
    {
        int msi = n - 1 - i; /* most-significant index */
        layout->digits[i] = temp_digits[msi];
        /* Position: rightmost digit at x=192, leftward by stride */
        layout->x_positions[i] = SCORE_WINDOW_WIDTH - SCORE_DIGIT_STRIDE * (msi + 1);
    }
}

/* =========================================================================
 * Extra life tracking
 * ========================================================================= */

int score_system_get_life_threshold(const score_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    return ctx->life_threshold;
}

/* =========================================================================
 * Utility
 * ========================================================================= */

const char *score_system_status_string(score_system_status_t status)
{
    switch (status)
    {
        case SCORE_SYS_OK:
            return "SCORE_SYS_OK";
        case SCORE_SYS_ERR_NULL_ARG:
            return "SCORE_SYS_ERR_NULL_ARG";
        case SCORE_SYS_ERR_ALLOC_FAILED:
            return "SCORE_SYS_ERR_ALLOC_FAILED";
        default:
            return "SCORE_SYS_UNKNOWN";
    }
}
