/*
 * ball_system.c — Pure C ball physics system with callback-based side effects.
 *
 * See include/ball_system.h for API documentation.
 * See ADR-015 in docs/DESIGN.md for design rationale.
 *
 * PR 1: lifecycle, ball management, queries, render info, guide info.
 * PR 2: state machine dispatch, wall/paddle collision.
 * PR 3: block collision, ball-to-ball collision, multiball.
 */

#include "ball_system.h"
#include "ball_math.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal data structures
 * ========================================================================= */

struct ball_system
{
    BALL balls[MAX_BALLS];
    int guide_pos; /* 0-10, guide direction indicator */
    int guide_inc; /* +1 or -1, guide animation direction */
    float machine_eps;
    ball_system_callbacks_t callbacks;
    void *user_data;
};

/* =========================================================================
 * Public API — Lifecycle
 * ========================================================================= */

ball_system_t *ball_system_create(const ball_system_callbacks_t *callbacks, void *user_data,
                                  ball_system_status_t *status)
{
    ball_system_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
    {
        if (status != NULL)
        {
            *status = BALL_SYS_ERR_ALLOC_FAILED;
        }
        return NULL;
    }

    ctx->guide_pos = 6; /* Start in middle of guider — matches ball.c:166 */
    ctx->guide_inc = -1;
    ctx->machine_eps = ball_math_init();

    if (callbacks != NULL)
    {
        ctx->callbacks = *callbacks;
    }
    ctx->user_data = user_data;

    /* Clear all ball slots to defaults */
    for (int i = 0; i < MAX_BALLS; i++)
    {
        ctx->balls[i].active = 0;
        ctx->balls[i].ballState = BALL_CREATE;
        ctx->balls[i].radius = BALL_WC;
        ctx->balls[i].mass = MIN_BALL_MASS;
        ctx->balls[i].waitMode = BALL_NONE;
        ctx->balls[i].newMode = BALL_NONE;
    }

    if (status != NULL)
    {
        *status = BALL_SYS_OK;
    }
    return ctx;
}

void ball_system_destroy(ball_system_t *ctx)
{
    free(ctx);
}

/* =========================================================================
 * Public API — Ball management
 * ========================================================================= */

int ball_system_add(ball_system_t *ctx, const ball_system_env_t *env, int x, int y, int dx, int dy,
                    ball_system_status_t *status)
{
    if (ctx == NULL || env == NULL)
    {
        if (status != NULL)
        {
            *status = BALL_SYS_ERR_NULL_ARG;
        }
        return -1;
    }

    for (int i = 0; i < MAX_BALLS; i++)
    {
        if (ctx->balls[i].active == 0)
        {
            /* Clear the slot first — matches ball.c:1951 */
            ball_system_clear(ctx, i);

            ctx->balls[i].active = 1;
            ctx->balls[i].ballx = x;
            ctx->balls[i].bally = y;
            ctx->balls[i].oldx = x;
            ctx->balls[i].oldy = y;
            ctx->balls[i].dx = dx;
            ctx->balls[i].dy = dy;
            ctx->balls[i].ballState = BALL_CREATE;
            ctx->balls[i].mass = (float)((rand() % (int)MAX_BALL_MASS) + (int)MIN_BALL_MASS);
            ctx->balls[i].slide = 0;
            ctx->balls[i].nextFrame = env->frame + BIRTH_FRAME_RATE;

            if (status != NULL)
            {
                *status = BALL_SYS_OK;
            }
            return i;
        }
    }

    if (status != NULL)
    {
        *status = BALL_SYS_ERR_FULL;
    }
    return -1;
}

ball_system_status_t ball_system_clear(ball_system_t *ctx, int index)
{
    if (ctx == NULL)
    {
        return BALL_SYS_ERR_NULL_ARG;
    }
    if (index < 0 || index >= MAX_BALLS)
    {
        return BALL_SYS_ERR_INVALID_INDEX;
    }

    /* Matches ClearBall() in ball.c:1979-2001 exactly */
    BALL *b = &ctx->balls[index];
    b->waitMode = BALL_NONE;
    b->waitingFrame = 0;
    b->lastPaddleHitFrame = 0;
    b->nextFrame = 0;
    b->newMode = BALL_NONE;
    b->active = 0;
    b->oldx = 0;
    b->oldy = 0;
    b->ballx = 0;
    b->bally = 0;
    b->dx = 0;
    b->dy = 0;
    b->slide = 0;
    b->radius = BALL_WC;
    b->mass = MIN_BALL_MASS;
    b->ballState = BALL_CREATE;

    return BALL_SYS_OK;
}

ball_system_status_t ball_system_clear_all(ball_system_t *ctx)
{
    if (ctx == NULL)
    {
        return BALL_SYS_ERR_NULL_ARG;
    }

    for (int i = 0; i < MAX_BALLS; i++)
    {
        ball_system_clear(ctx, i);
    }

    return BALL_SYS_OK;
}

/* =========================================================================
 * Public API — Per-frame update (stub — implementation lands in PR 2)
 * ========================================================================= */

void ball_system_update(ball_system_t *ctx, const ball_system_env_t *env)
{
    (void)ctx;
    (void)env;
}

/* =========================================================================
 * Public API — Queries
 * ========================================================================= */

int ball_system_get_active_count(const ball_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < MAX_BALLS; i++)
    {
        if (ctx->balls[i].ballState == BALL_ACTIVE)
        {
            count++;
        }
    }
    return count;
}

int ball_system_get_active_index(const ball_system_t *ctx)
{
    if (ctx == NULL)
    {
        return -1;
    }

    for (int i = 0; i < MAX_BALLS; i++)
    {
        if (ctx->balls[i].ballState == BALL_ACTIVE)
        {
            return i;
        }
    }
    return -1;
}

int ball_system_is_ball_waiting(const ball_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }

    for (int i = 0; i < MAX_BALLS; i++)
    {
        if (ctx->balls[i].ballState == BALL_READY)
        {
            return 1;
        }
    }
    return 0;
}

enum BallStates ball_system_get_state(const ball_system_t *ctx, int index)
{
    if (ctx == NULL || index < 0 || index >= MAX_BALLS)
    {
        return BALL_NONE;
    }
    return ctx->balls[index].ballState;
}

ball_system_status_t ball_system_get_position(const ball_system_t *ctx, int index, int *x, int *y)
{
    if (ctx == NULL || x == NULL || y == NULL)
    {
        return BALL_SYS_ERR_NULL_ARG;
    }
    if (index < 0 || index >= MAX_BALLS)
    {
        return BALL_SYS_ERR_INVALID_INDEX;
    }

    *x = ctx->balls[index].ballx;
    *y = ctx->balls[index].bally;
    return BALL_SYS_OK;
}

ball_system_status_t ball_system_get_render_info(const ball_system_t *ctx, int index,
                                                 ball_system_render_info_t *info)
{
    if (ctx == NULL || info == NULL)
    {
        return BALL_SYS_ERR_NULL_ARG;
    }
    if (index < 0 || index >= MAX_BALLS)
    {
        return BALL_SYS_ERR_INVALID_INDEX;
    }

    const BALL *b = &ctx->balls[index];
    info->active = b->active;
    info->x = b->ballx;
    info->y = b->bally;
    info->slide = b->slide;
    info->state = b->ballState;

    return BALL_SYS_OK;
}

ball_system_guide_info_t ball_system_get_guide_info(const ball_system_t *ctx)
{
    ball_system_guide_info_t info = {0};
    if (ctx == NULL)
    {
        return info;
    }
    info.pos = ctx->guide_pos;
    info.inc = ctx->guide_inc;
    return info;
}

/* =========================================================================
 * Public API — Utility
 * ========================================================================= */

const char *ball_system_status_string(ball_system_status_t status)
{
    switch (status)
    {
        case BALL_SYS_OK:
            return "OK";
        case BALL_SYS_ERR_NULL_ARG:
            return "NULL argument";
        case BALL_SYS_ERR_ALLOC_FAILED:
            return "allocation failed";
        case BALL_SYS_ERR_FULL:
            return "all ball slots full";
        case BALL_SYS_ERR_INVALID_INDEX:
            return "invalid ball index";
    }
    return "unknown status";
}

const char *ball_system_state_name(enum BallStates state)
{
    switch (state)
    {
        case BALL_POP:
            return "pop";
        case BALL_ACTIVE:
            return "active";
        case BALL_STOP:
            return "stop";
        case BALL_CREATE:
            return "create";
        case BALL_DIE:
            return "die";
        case BALL_WAIT:
            return "wait";
        case BALL_READY:
            return "ready";
        case BALL_NONE:
            return "none";
    }
    return "unknown";
}
