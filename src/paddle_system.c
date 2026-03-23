/*
 * paddle_system.c — Pure C paddle system implementation.
 *
 * Port of paddle.c with all X11 rendering removed.  Preserves the exact
 * movement formulas, clamping, reverse controls, and size transitions from
 * the legacy code.
 *
 * Key legacy behaviors preserved:
 *   - Mouse position formula: pos = mouse_x - (main_width/2) + half_width
 *   - Keyboard velocity: PADDLE_VEL (10 pixels per update)
 *   - Reverse: keyboard direction swap, mouse X mirror
 *   - Size clamp: shrinking SMALL stays SMALL, expanding HUGE stays HUGE
 *   - paddleDx is 0 for keyboard mode (legacy does not set it for keys)
 *   - Initial size is PADDLE_SIZE_HUGE (set by caller, not hardcoded here)
 *
 * See ADR-017 in docs/DESIGN.md for design rationale.
 */

#include "paddle_system.h"

#include <stdlib.h>

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/*
 * Return the half-width for a given size type.
 * Used for position clamping and mouse formula offset.
 */
static int half_width_for_size(int size_type)
{
    switch (size_type)
    {
        case PADDLE_SIZE_SMALL:
            return 20;
        case PADDLE_SIZE_MEDIUM:
            return 25;
        case PADDLE_SIZE_HUGE:
            return 35;
        default:
            return 0;
    }
}

/*
 * Return the pixel width for a given size type.
 * Matches legacy GetPaddleSize().
 */
static int pixel_width_for_size(int size_type)
{
    switch (size_type)
    {
        case PADDLE_SIZE_SMALL:
            return PADDLE_WIDTH_SMALL;
        case PADDLE_SIZE_MEDIUM:
            return PADDLE_WIDTH_MEDIUM;
        case PADDLE_SIZE_HUGE:
            return PADDLE_WIDTH_HUGE;
        default:
            return 0;
    }
}

/* =========================================================================
 * Internal struct
 * ========================================================================= */

struct paddle_system
{
    int pos;         /* Center X position in play-area coordinates */
    int prev_pos;    /* Previous tick position (for render interpolation) */
    int size_type;   /* PADDLE_SIZE_SMALL / MEDIUM / HUGE */
    int reverse_on;  /* Reverse controls flag */
    int sticky_on;   /* Sticky bat flag */
    int dx;          /* Movement delta from last update (mouse mode only) */
    int moving;      /* Nonzero if paddle moved during last update */
    int old_mouse_x; /* Previous mouse_x for dx calculation */
    int play_width;  /* Playfield width (495) */
    int play_height; /* Playfield height (580) */
    int main_width;  /* Info panel width (70) — for mouse formula */
};

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

paddle_system_t *paddle_system_create(int play_width, int play_height, int main_width,
                                      paddle_system_status_t *status)
{
    paddle_system_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
    {
        if (status)
        {
            *status = PADDLE_SYS_ERR_ALLOC_FAILED;
        }
        return NULL;
    }

    ctx->play_width = play_width;
    ctx->play_height = play_height;
    ctx->main_width = main_width;

    /* Initial state: centered, HUGE, flags off */
    ctx->pos = play_width / 2;
    ctx->prev_pos = ctx->pos;
    ctx->size_type = PADDLE_SIZE_HUGE;
    ctx->reverse_on = 0;
    ctx->sticky_on = 0;
    ctx->dx = 0;
    ctx->moving = 0;
    ctx->old_mouse_x = 0;

    if (status)
    {
        *status = PADDLE_SYS_OK;
    }
    return ctx;
}

void paddle_system_destroy(paddle_system_t *ctx)
{
    free(ctx);
}

/* =========================================================================
 * Update
 * ========================================================================= */

void paddle_system_update(paddle_system_t *ctx, int direction, int mouse_x)
{
    int half;
    int old_pos;
    int raw_mouse_x;

    if (ctx == NULL)
    {
        return;
    }

    ctx->prev_pos = ctx->pos;
    old_pos = ctx->pos;
    half = half_width_for_size(ctx->size_type);

    /*
     * Save raw mouse position BEFORE reverse transformation.
     * Legacy computes paddleDx = x - oldx from the raw mouse position
     * in main.c (line 207), before MovePaddle() applies the reverse.
     */
    raw_mouse_x = mouse_x;

    /*
     * Step 1: Apply direction (keyboard velocity).
     * Matches legacy MovePaddle() switch on direction.
     */
    switch (direction)
    {
        case PADDLE_DIR_LEFT:
            if (ctx->reverse_on)
            {
                ctx->pos += PADDLE_VELOCITY;
            }
            else
            {
                ctx->pos -= PADDLE_VELOCITY;
            }
            break;

        case PADDLE_DIR_RIGHT:
            if (ctx->reverse_on)
            {
                ctx->pos -= PADDLE_VELOCITY;
            }
            else
            {
                ctx->pos += PADDLE_VELOCITY;
            }
            break;

        case PADDLE_DIR_NONE:
            if (ctx->reverse_on)
            {
                mouse_x = ctx->play_width - mouse_x;
            }
            break;

        default:
            break;
    }

    /*
     * Step 2: Apply mouse position (if provided).
     * Matches legacy: paddlePos = xpos - (MAIN_WIDTH / 2) + half_width
     * Only applied when mouse_x > 0 (mouse mode).
     */
    if (mouse_x > 0)
    {
        ctx->pos = mouse_x - (ctx->main_width / 2) + half;
    }

    /*
     * Step 3: Clamp to playfield bounds.
     * Matches legacy: paddlePos in [half_width, PLAY_WIDTH - half_width]
     */
    if (ctx->pos < half)
    {
        ctx->pos = half;
    }
    if (ctx->pos > ctx->play_width - half)
    {
        ctx->pos = ctx->play_width - half;
    }

    /*
     * Step 4: Compute dx and motion state.
     *
     * Mouse mode: dx = raw_mouse_x - old_mouse_x.  Uses the RAW (pre-reverse)
     * mouse position, matching legacy main.c:207 where paddleDx = x - oldx
     * is computed before MovePaddle() applies the reverse transformation.
     * Keyboard mode: dx = 0 (legacy does not set paddleDx for keyboard).
     *
     * Motion flag: nonzero if position changed.
     */
    if (raw_mouse_x > 0)
    {
        if (ctx->old_mouse_x > 0)
        {
            ctx->dx = raw_mouse_x - ctx->old_mouse_x;
        }
        else
        {
            ctx->dx = 0;
        }
        ctx->old_mouse_x = raw_mouse_x;
    }
    else
    {
        ctx->dx = 0;
        ctx->old_mouse_x = 0;
    }

    ctx->moving = (ctx->pos != old_pos) ? 1 : 0;
}

/* =========================================================================
 * State changes
 * ========================================================================= */

void paddle_system_reset(paddle_system_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    ctx->pos = ctx->play_width / 2;
    ctx->prev_pos = ctx->pos;
    ctx->dx = 0;
    ctx->moving = 0;
    ctx->old_mouse_x = 0;
}

void paddle_system_change_size(paddle_system_t *ctx, int shrink)
{
    if (ctx == NULL)
    {
        return;
    }

    if (shrink)
    {
        /* Shrink one step: HUGE → MEDIUM → SMALL (clamp at SMALL) */
        if (ctx->size_type == PADDLE_SIZE_MEDIUM)
        {
            ctx->size_type = PADDLE_SIZE_SMALL;
        }
        else if (ctx->size_type == PADDLE_SIZE_HUGE)
        {
            ctx->size_type = PADDLE_SIZE_MEDIUM;
        }
    }
    else
    {
        /* Expand one step: SMALL → MEDIUM → HUGE (clamp at HUGE) */
        if (ctx->size_type == PADDLE_SIZE_SMALL)
        {
            ctx->size_type = PADDLE_SIZE_MEDIUM;
        }
        else if (ctx->size_type == PADDLE_SIZE_MEDIUM)
        {
            ctx->size_type = PADDLE_SIZE_HUGE;
        }
    }

    ctx->prev_pos = ctx->pos;
}

void paddle_system_set_size(paddle_system_t *ctx, int size_type)
{
    if (ctx == NULL)
    {
        return;
    }

    switch (size_type)
    {
        case PADDLE_SIZE_SMALL:
        case PADDLE_SIZE_MEDIUM:
        case PADDLE_SIZE_HUGE:
            ctx->size_type = size_type;
            ctx->prev_pos = ctx->pos;
            break;
        default:
            /* Ignore invalid size_type values. */
            break;
    }
}

void paddle_system_set_reverse(paddle_system_t *ctx, int on)
{
    if (ctx == NULL)
    {
        return;
    }

    ctx->reverse_on = on ? 1 : 0;
}

void paddle_system_toggle_reverse(paddle_system_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    ctx->reverse_on = ctx->reverse_on ? 0 : 1;
}

void paddle_system_set_sticky(paddle_system_t *ctx, int on)
{
    if (ctx == NULL)
    {
        return;
    }

    ctx->sticky_on = on ? 1 : 0;
}

/* =========================================================================
 * Queries
 * ========================================================================= */

int paddle_system_get_pos(const paddle_system_t *ctx)
{
    if (!ctx)
    {
        return 0;
    }

    return ctx->pos;
}

int paddle_system_get_dx(const paddle_system_t *ctx)
{
    if (!ctx)
    {
        return 0;
    }

    return ctx->dx;
}

int paddle_system_get_size(const paddle_system_t *ctx)
{
    if (!ctx)
    {
        return 0;
    }

    return pixel_width_for_size(ctx->size_type);
}

int paddle_system_get_size_type(const paddle_system_t *ctx)
{
    if (!ctx)
    {
        return 0;
    }

    return ctx->size_type;
}

int paddle_system_is_moving(const paddle_system_t *ctx)
{
    if (!ctx)
    {
        return 0;
    }

    return ctx->moving;
}

int paddle_system_get_reverse(const paddle_system_t *ctx)
{
    if (!ctx)
    {
        return 0;
    }

    return ctx->reverse_on;
}

int paddle_system_get_sticky(const paddle_system_t *ctx)
{
    if (!ctx)
    {
        return 0;
    }

    return ctx->sticky_on;
}

paddle_system_status_t paddle_system_get_render_info(const paddle_system_t *ctx,
                                                     paddle_system_render_info_t *info)
{
    if (ctx == NULL || info == NULL)
    {
        return PADDLE_SYS_ERR_NULL_ARG;
    }

    info->pos = ctx->pos;
    info->prev_pos = ctx->prev_pos;
    info->y = ctx->play_height - PADDLE_DIST_BASE;
    info->width = pixel_width_for_size(ctx->size_type);
    info->height = PADDLE_RENDER_HEIGHT;
    info->size_type = ctx->size_type;

    return PADDLE_SYS_OK;
}

/* =========================================================================
 * Utility
 * ========================================================================= */

const char *paddle_system_status_string(paddle_system_status_t status)
{
    switch (status)
    {
        case PADDLE_SYS_OK:
            return "OK";
        case PADDLE_SYS_ERR_NULL_ARG:
            return "NULL argument";
        case PADDLE_SYS_ERR_ALLOC_FAILED:
            return "allocation failed";
        default:
            return "unknown status";
    }
}
