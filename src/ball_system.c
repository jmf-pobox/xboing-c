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

#include <math.h>
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
 * Guide direction table — maps guide_pos (0-10) to (dx, dy)
 * Matches ChangeBallDirectionToGuide() in ball.c:1678-1752
 * ========================================================================= */

static const int guide_dx[11] = {-5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5};
static const int guide_dy[11] = {-1, -2, -3, -4, -5, -5, -5, -4, -3, -2, -1};

/* =========================================================================
 * Static helpers — forward declarations
 * ========================================================================= */

static void update_a_ball(ball_system_t *ctx, const ball_system_env_t *env, int i);
static int ball_hit_paddle(const ball_system_env_t *env, const BALL *b, int *hit_pos, int *hx,
                           int *hy);
static void animate_ball_pop(ball_system_t *ctx, const ball_system_env_t *env, int i);
static void animate_ball_create(ball_system_t *ctx, const ball_system_env_t *env, int i);
static void do_ball_wait(ball_system_t *ctx, const ball_system_env_t *env, int i);
static void randomise_velocity(ball_system_t *ctx, const ball_system_env_t *env, int i);
static void update_guide(ball_system_t *ctx, const ball_system_env_t *env);

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
 * Public API — Ball state control
 * ========================================================================= */

ball_system_status_t ball_system_change_mode(ball_system_t *ctx, const ball_system_env_t *env,
                                             int index, enum BallStates mode)
{
    if (ctx == NULL || env == NULL)
    {
        return BALL_SYS_ERR_NULL_ARG;
    }
    if (index < 0 || index >= MAX_BALLS)
    {
        return BALL_SYS_ERR_INVALID_INDEX;
    }

    ctx->balls[index].ballState = mode;

    /* Set up pop animation when entering BALL_POP */
    if (mode == BALL_POP)
    {
        ctx->balls[index].slide = BIRTH_SLIDES + 1;
        ctx->balls[index].nextFrame = env->frame + BIRTH_FRAME_RATE;
    }

    return BALL_SYS_OK;
}

int ball_system_activate_waiting(ball_system_t *ctx, const ball_system_env_t *env)
{
    if (ctx == NULL || env == NULL)
    {
        return -1;
    }

    for (int i = 0; i < MAX_BALLS; i++)
    {
        if (ctx->balls[i].ballState == BALL_READY)
        {
            ctx->balls[i].ballState = BALL_ACTIVE;
            ctx->balls[i].lastPaddleHitFrame = env->frame + PADDLE_BALL_FRAME_TILT;

            /* Apply guide direction */
            if (ctx->guide_pos >= 0 && ctx->guide_pos <= 10)
            {
                ctx->balls[i].dx = guide_dx[ctx->guide_pos];
                ctx->balls[i].dy = guide_dy[ctx->guide_pos];
            }

            /* Reset guide to middle */
            ctx->guide_pos = 6;

            if (ctx->callbacks.on_event != NULL)
            {
                ctx->callbacks.on_event(BALL_EVT_ACTIVATED, i, ctx->user_data);
            }

            return i;
        }
    }

    return -1;
}

int ball_system_reset_start(ball_system_t *ctx, const ball_system_env_t *env)
{
    if (ctx == NULL || env == NULL)
    {
        return -1;
    }

    int i = ball_system_add(ctx, env, 0, 0, 3, -3, NULL);
    if (i >= 0)
    {
        /* Position ball on paddle — matches updateBallVariables() ball.c:1608 */
        ctx->balls[i].ballx = env->paddle_pos;
        ctx->balls[i].bally = env->play_height - DIST_BALL_OF_PADDLE;
        ctx->balls[i].oldx = ctx->balls[i].ballx;
        ctx->balls[i].oldy = ctx->balls[i].bally;

        /* Set up BALL_WAIT → BALL_CREATE sequence — matches ball.c:1802 */
        ctx->balls[i].waitingFrame = env->frame + 1;
        ctx->balls[i].waitMode = BALL_CREATE;
        ctx->balls[i].ballState = BALL_WAIT;

        if (ctx->callbacks.on_event != NULL)
        {
            ctx->callbacks.on_event(BALL_EVT_RESET_START, i, ctx->user_data);
        }
    }

    return i;
}

void ball_system_do_tilt(ball_system_t *ctx, const ball_system_env_t *env, int index)
{
    if (ctx == NULL || env == NULL)
    {
        return;
    }
    if (index < 0 || index >= MAX_BALLS)
    {
        return;
    }

    if (ctx->balls[index].ballState == BALL_ACTIVE)
    {
        if (ctx->callbacks.on_message != NULL)
        {
            ctx->callbacks.on_message("Auto Tilt Activated", ctx->user_data);
        }
        if (ctx->callbacks.on_event != NULL)
        {
            ctx->callbacks.on_event(BALL_EVT_TILT, index, ctx->user_data);
        }

        randomise_velocity(ctx, env, index);
    }
}

/* =========================================================================
 * Public API — Per-frame update
 * ========================================================================= */

void ball_system_update(ball_system_t *ctx, const ball_system_env_t *env)
{
    if (ctx == NULL || env == NULL)
    {
        return;
    }

    for (int i = 0; i < MAX_BALLS; i++)
    {
        if (ctx->balls[i].active == 0)
        {
            continue;
        }

        switch (ctx->balls[i].ballState)
        {
            case BALL_POP:
                animate_ball_pop(ctx, env, i);
                break;

            case BALL_ACTIVE:
                if ((env->frame % BALL_FRAME_RATE) == 0)
                {
                    update_a_ball(ctx, env, i);
                }
                break;

            case BALL_READY:
                /* Follow paddle position */
                ctx->balls[i].ballx = env->paddle_pos;
                ctx->balls[i].bally = env->play_height - DIST_BALL_OF_PADDLE;
                ctx->balls[i].oldx = ctx->balls[i].ballx;
                ctx->balls[i].oldy = ctx->balls[i].bally;

                /* Update guide animation */
                if ((env->frame % BALL_FRAME_RATE) == 0)
                {
                    update_guide(ctx, env);
                }

                /* Auto-activate after delay */
                if (env->frame == ctx->balls[i].nextFrame)
                {
                    ctx->balls[i].ballState = BALL_ACTIVE;
                    ctx->balls[i].lastPaddleHitFrame = env->frame + PADDLE_BALL_FRAME_TILT;

                    if (ctx->guide_pos >= 0 && ctx->guide_pos <= 10)
                    {
                        ctx->balls[i].dx = guide_dx[ctx->guide_pos];
                        ctx->balls[i].dy = guide_dy[ctx->guide_pos];
                    }

                    ctx->guide_pos = 6;

                    if (ctx->callbacks.on_event != NULL)
                    {
                        ctx->callbacks.on_event(BALL_EVT_ACTIVATED, i, ctx->user_data);
                    }
                }
                break;

            case BALL_STOP:
                break;

            case BALL_CREATE:
                animate_ball_create(ctx, env, i);
                break;

            case BALL_WAIT:
                do_ball_wait(ctx, env, i);
                break;

            case BALL_DIE:
                if ((env->frame % BALL_FRAME_RATE) == 0)
                {
                    update_a_ball(ctx, env, i);
                }
                break;

            case BALL_NONE:
                break;
        }
    }
}

/* =========================================================================
 * Static helpers — state machine
 * ========================================================================= */

static void update_a_ball(ball_system_t *ctx, const ball_system_env_t *env, int i)
{
    /*
     * Main physics update for a single ball.
     * Handles wall collision, paddle collision, speed normalization.
     * Block collision and ball-to-ball are deferred to PR 3 via callbacks.
     * Matches UpdateABall() in ball.c:1023-1346.
     */

    BALL *b = &ctx->balls[i];

    /* Update ball position using dx and dy values */
    b->ballx = b->oldx + b->dx;
    b->bally = b->oldy + b->dy;

    /* Mark the ball to die if past the paddle — ball.c:1041 */
    if (b->bally > (env->play_height - DIST_BASE + BALL_HEIGHT))
    {
        b->ballState = BALL_DIE;
    }

    /* Left wall collision — ball.c:1045-1060 */
    if (b->ballx < BALL_WC && env->no_walls == 0)
    {
        b->dx = abs(b->dx);
        if (ctx->callbacks.on_sound != NULL)
        {
            ctx->callbacks.on_sound("boing", ctx->user_data);
        }
    }
    else if (env->no_walls != 0 && b->ballx < BALL_WC)
    {
        b->ballx = env->play_width - BALL_WC;
        b->oldx = b->ballx;
        b->oldy = b->bally;
        return;
    }

    /* Right wall collision — ball.c:1063-1078 */
    if (b->ballx > (env->play_width - BALL_WC) && env->no_walls == 0)
    {
        b->dx = -(abs(b->dx));
        if (ctx->callbacks.on_sound != NULL)
        {
            ctx->callbacks.on_sound("boing", ctx->user_data);
        }
    }
    else if (env->no_walls != 0 && b->ballx > (env->play_width - BALL_WC))
    {
        b->ballx = BALL_WC;
        b->oldx = b->ballx;
        b->oldy = b->bally;
        return;
    }

    /* Top wall collision — ball.c:1081-1086 */
    if (b->bally < BALL_HC)
    {
        b->dy = abs(b->dy);
        if (ctx->callbacks.on_sound != NULL)
        {
            ctx->callbacks.on_sound("boing", ctx->user_data);
        }
    }

    if (b->ballState != BALL_DIE)
    {
        int hit_pos, hx, hy;

        if (ball_hit_paddle(env, b, &hit_pos, &hx, &hy))
        {
            /* Paddle hit — ball.c:1093-1157 */
            b->lastPaddleHitFrame = env->frame + PADDLE_BALL_FRAME_TILT;

            if (ctx->callbacks.on_sound != NULL)
            {
                ctx->callbacks.on_sound("paddle", ctx->user_data);
            }
            if (ctx->callbacks.on_score != NULL)
            {
                ctx->callbacks.on_score(PADDLE_HIT_SCORE, ctx->user_data);
            }
            if (ctx->callbacks.on_event != NULL)
            {
                ctx->callbacks.on_event(BALL_EVT_PADDLE_HIT, i, ctx->user_data);
            }

            /* Compute paddle bounce using ball_math */
            int new_dx, new_dy;
            int pad_size = env->paddle_size + BALL_WC;
            ball_math_paddle_bounce(b->dx, b->dy, hit_pos, pad_size, env->paddle_dx, &new_dx,
                                    &new_dy);
            b->dx = new_dx;
            b->dy = new_dy;
            b->ballx = hx;
            b->bally = hy;

            /* Sticky bat — ball.c:1146-1157 */
            if (env->sticky_bat != 0)
            {
                b->ballState = BALL_READY;
                b->nextFrame = env->frame + BALL_AUTO_ACTIVE_DELAY;
                b->oldx = b->ballx;
                b->oldy = b->bally;
                return;
            }
        }
        else
        {
            /* Auto-tilt if paddle not hit recently — ball.c:1164-1165 */
            if (b->lastPaddleHitFrame <= env->frame)
            {
                ball_system_do_tilt(ctx, env, i);
            }
        }

        /* Speed normalization — ball.c:1168-1197 */
        ball_math_normalize_speed(&b->dx, &b->dy, env->speed_level);
    }

    /* Ball lost off bottom — ball.c:1199-1207 */
    if (b->bally > (env->play_height + BALL_HEIGHT * 2))
    {
        ball_system_clear(ctx, i);
        if (ctx->callbacks.on_event != NULL)
        {
            ctx->callbacks.on_event(BALL_EVT_DIED, i, ctx->user_data);
        }
        return;
    }

    /* Update old positions — replaces MoveBall() position tracking */
    b->oldx = b->ballx;
    b->oldy = b->bally;

    /* Ball animation slide — replaces MoveBall() animation, ball.c:417-422 */
    if ((env->frame % BALL_ANIM_RATE) == 0)
    {
        b->slide++;
    }
    if (b->slide == BALL_SLIDES - 1)
    {
        b->slide = 0;
    }

    /* Block collision and ball-to-ball handled via callbacks in PR 3 */
}

static int ball_hit_paddle(const ball_system_env_t *env, const BALL *b, int *hit_pos, int *hx,
                           int *hy)
{
    /*
     * Paddle intersection test.
     * Matches BallHitPaddle() in ball.c:697-786 exactly.
     */

    int paddle_line = env->play_height - DIST_BASE - 2;

    if (b->bally + BALL_HC > paddle_line)
    {
        float xP1 = (float)(env->paddle_pos - (env->paddle_size / 2) - BALL_WC);
        float xP2 = (float)(env->paddle_pos + (env->paddle_size / 2) + BALL_WC);

        if (b->dx == 0)
        {
            /* Vertical moving ball */
            if ((float)b->ballx > xP1 && (float)b->ballx < xP2)
            {
                *hit_pos = b->ballx - env->paddle_pos;
                *hx = b->ballx;
                *hy = paddle_line - BALL_HC;
                return 1;
            }
            return 0;
        }

        /* Compute line coefficients of ball trajectory */
        float alpha = (float)b->dy;
        float x1 = (float)(b->ballx - b->dx);
        float y1 = (float)(b->bally - b->dy);
        float x2 = (float)b->ballx;
        float y2 = (float)b->bally;
        float beta = ((y1 + y2) - alpha * (x1 + x2)) / 2.0f;

        float yH = (float)paddle_line;
        float xH = (yH - beta) / alpha;

        if (xH > xP1 && xH < xP2)
        {
            *hit_pos = (int)(xH + 0.5f) - env->paddle_pos;
            *hx = (int)(xH + 0.5f);
            *hy = paddle_line - BALL_HC;
            return 1;
        }
        return 0;
    }

    return 0;
}

static void animate_ball_pop(ball_system_t *ctx, const ball_system_env_t *env, int i)
{
    /*
     * Pop animation countdown. When complete, clears ball and emits DIED.
     * Matches AnimateBallPop() in ball.c:1808-1848.
     * Uses per-ball slide field instead of legacy static variable.
     */

    BALL *b = &ctx->balls[i];

    if (env->frame == b->nextFrame)
    {
        b->slide--;
        b->nextFrame += BIRTH_FRAME_RATE;

        /* First frame clears the ball visual (slide == BIRTH_SLIDES) */
        if (b->slide == BIRTH_SLIDES)
        {
            b->slide--;
        }

        if (b->slide < 0)
        {
            /* Pop animation complete — clear and emit death event */
            ball_system_clear(ctx, i);
            if (ctx->callbacks.on_event != NULL)
            {
                ctx->callbacks.on_event(BALL_EVT_DIED, i, ctx->user_data);
            }
        }
    }
}

static void animate_ball_create(ball_system_t *ctx, const ball_system_env_t *env, int i)
{
    /*
     * Create animation. When complete, transitions to BALL_READY.
     * Matches AnimateBallCreate() in ball.c:1850-1895.
     * Uses per-ball slide field instead of legacy static variable.
     */

    BALL *b = &ctx->balls[i];

    if (env->frame == b->nextFrame)
    {
        b->slide++;
        b->nextFrame += BIRTH_FRAME_RATE;

        if (b->slide == BIRTH_SLIDES)
        {
            b->slide = 0;

            /* Position ball on paddle */
            b->ballx = env->paddle_pos;
            b->bally = env->play_height - DIST_BALL_OF_PADDLE;
            b->oldx = b->ballx;
            b->oldy = b->bally;

            /* Transition to BALL_READY */
            b->ballState = BALL_READY;
            b->nextFrame = env->frame + BALL_AUTO_ACTIVE_DELAY;
        }
    }
}

static void do_ball_wait(ball_system_t *ctx, const ball_system_env_t *env, int i)
{
    /*
     * Wait state handler. Transitions to waitMode when waitingFrame reached.
     * Matches DoBallWait() in ball.c:1919-1931.
     */

    BALL *b = &ctx->balls[i];

    if (env->frame == b->waitingFrame)
    {
        b->nextFrame = env->frame + 10;
        b->ballState = b->waitMode;
    }
}

static void randomise_velocity(ball_system_t *ctx, const ball_system_env_t *env, int i)
{
    /*
     * Randomize ball velocity for tilt.
     * Matches RandomiseBallVelocity() in ball.c:469-488.
     */

    BALL *b = &ctx->balls[i];
    b->dx = 0;
    b->dy = 0;

    while (b->dx == 0 || b->dy == 0)
    {
        b->dx = (rand() % (MAX_X_VEL - 3)) + 3;
        b->dy = (rand() % (MAX_Y_VEL - 3)) + 3;

        if ((rand() % 10) < 5)
        {
            b->dx *= -1;
        }
        if ((rand() % 10) < 5)
        {
            b->dy *= -1;
        }

        b->lastPaddleHitFrame = env->frame + PADDLE_BALL_FRAME_TILT;
    }
}

static void update_guide(ball_system_t *ctx, const ball_system_env_t *env)
{
    /*
     * Animate guide direction indicator.
     * Matches MoveGuides() in ball.c:425-467 (animation timing only).
     */

    if ((env->frame % (BALL_FRAME_RATE * 8)) == 0)
    {
        ctx->guide_pos += ctx->guide_inc;
    }

    if (ctx->guide_pos >= 10)
    {
        ctx->guide_pos = 10;
        ctx->guide_inc = -1;
    }
    if (ctx->guide_pos <= 0)
    {
        ctx->guide_pos = 0;
        ctx->guide_inc = 1;
    }
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
