/*
 * eyedude_system.c — Pure C EyeDude animated character system.
 *
 * Owns the EyeDude state machine: reset, walk, turn, die.
 * The character walks across the top row of the play area.
 *
 * Opaque context pattern: no globals, fully testable with CMocka stubs.
 * See ADR-024 in docs/DESIGN.md for design rationale.
 */

#include "eyedude_system.h"

#include <stdlib.h>

/* =========================================================================
 * Internal structure
 * ========================================================================= */

struct eyedude_system
{
    eyedude_state_t state;
    eyedude_dir_t direction;

    int x, y;       /* Current center position */
    int oldx, oldy; /* Previous center position (for erase) */
    int slide;      /* Animation frame index (0..5) */
    int inc;        /* Movement increment per step (+5 or -5) */
    int turn;       /* 1 = will turn at midpoint */

    eyedude_system_callbacks_t callbacks;
    void *user_data;
    eyedude_rand_fn rand_fn;
};

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static int get_rand(const eyedude_system_t *ctx)
{
    if (ctx->rand_fn)
    {
        return ctx->rand_fn();
    }
    return rand();
}

static void fire_sound(const eyedude_system_t *ctx, const char *name)
{
    if (ctx->callbacks.on_sound)
    {
        ctx->callbacks.on_sound(name, ctx->user_data);
    }
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

eyedude_system_t *eyedude_system_create(const eyedude_system_callbacks_t *callbacks,
                                        void *user_data, eyedude_rand_fn rand_fn)
{
    eyedude_system_t *ctx = calloc(1, sizeof(eyedude_system_t));
    if (ctx == NULL)
    {
        return NULL;
    }

    ctx->user_data = user_data;
    ctx->rand_fn = rand_fn;

    if (callbacks)
    {
        ctx->callbacks = *callbacks;
    }

    /* calloc zeroes: state=NONE, position=0, slide=0 */
    return ctx;
}

void eyedude_system_destroy(eyedude_system_t *ctx)
{
    free(ctx);
}

/* =========================================================================
 * State management
 * ========================================================================= */

void eyedude_system_set_state(eyedude_system_t *ctx, eyedude_state_t state)
{
    if (ctx)
    {
        ctx->state = state;
    }
}

eyedude_state_t eyedude_system_get_state(const eyedude_system_t *ctx)
{
    if (ctx == NULL)
    {
        return EYEDUDE_STATE_NONE;
    }
    return ctx->state;
}

/* =========================================================================
 * Per-state handlers
 * ========================================================================= */

static void do_reset(eyedude_system_t *ctx, int play_w)
{
    ctx->slide = 0;
    ctx->turn = 0;

    /* Check if path is clear */
    if (ctx->callbacks.is_path_clear)
    {
        if (!ctx->callbacks.is_path_clear(ctx->user_data))
        {
            ctx->state = EYEDUDE_STATE_NONE;
            return;
        }
    }

    /* 30% chance to turn at midpoint */
    if ((get_rand(ctx) % 100) < EYEDUDE_TURN_CHANCE)
    {
        ctx->turn = 1;
    }

    /* Random direction */
    if ((get_rand(ctx) % 2) == 1)
    {
        /* Walk left: start from right edge */
        ctx->x = play_w + EYEDUDE_WC;
        ctx->y = EYEDUDE_HC;
        ctx->direction = EYEDUDE_DIR_LEFT;
    }
    else
    {
        /* Walk right: start from left edge */
        ctx->x = -EYEDUDE_WC;
        ctx->y = EYEDUDE_HC;
        ctx->direction = EYEDUDE_DIR_RIGHT;
    }
    ctx->oldx = ctx->x;
    ctx->oldy = ctx->y;

    ctx->state = EYEDUDE_STATE_WALK;
    fire_sound(ctx, "hithere");
}

static void do_walk(eyedude_system_t *ctx, int frame, int play_w)
{
    /* Only update on frame rate interval */
    if ((frame % EYEDUDE_FRAME_RATE) != 0)
    {
        return;
    }

    /* Store old position for erase */
    ctx->oldx = ctx->x;
    ctx->oldy = ctx->y;

    /* Advance animation frame */
    ctx->slide++;
    if (ctx->slide >= EYEDUDE_WALK_FRAMES)
    {
        ctx->slide = 0;
    }

    /* Direction logic */
    switch (ctx->direction)
    {
        case EYEDUDE_DIR_LEFT:
            if (ctx->x <= (play_w / 2) && ctx->turn)
            {
                ctx->direction = EYEDUDE_DIR_RIGHT;
                ctx->turn = 0;
            }
            else if (ctx->x < -EYEDUDE_WIDTH)
            {
                ctx->state = EYEDUDE_STATE_NONE;
            }
            ctx->inc = -EYEDUDE_WALK_SPEED;
            break;

        case EYEDUDE_DIR_RIGHT:
            if (ctx->x >= (play_w / 2) && ctx->turn)
            {
                ctx->direction = EYEDUDE_DIR_LEFT;
                ctx->turn = 0;
            }
            else if (ctx->x > (play_w + EYEDUDE_WC))
            {
                ctx->state = EYEDUDE_STATE_NONE;
            }
            ctx->inc = EYEDUDE_WALK_SPEED;
            break;

        case EYEDUDE_DIR_DEAD:
            break;
    }

    /* Move */
    ctx->x += ctx->inc;
}

static void do_die(eyedude_system_t *ctx)
{
    ctx->state = EYEDUDE_STATE_NONE;

    if (ctx->callbacks.on_message)
    {
        ctx->callbacks.on_message("- Eye Dude Bonus 10000 -", ctx->user_data);
    }

    if (ctx->callbacks.on_score)
    {
        ctx->callbacks.on_score(EYEDUDE_HIT_BONUS, ctx->user_data);
    }

    fire_sound(ctx, "supbons");
}

/* =========================================================================
 * Main update dispatch
 * ========================================================================= */

void eyedude_system_update(eyedude_system_t *ctx, int frame, int play_w)
{
    if (ctx == NULL)
    {
        return;
    }

    switch (ctx->state)
    {
        case EYEDUDE_STATE_RESET:
            do_reset(ctx, play_w);
            break;

        case EYEDUDE_STATE_WALK:
            do_walk(ctx, frame, play_w);
            break;

        case EYEDUDE_STATE_DIE:
            do_die(ctx);
            break;

        case EYEDUDE_STATE_NONE:
        case EYEDUDE_STATE_WAIT:
        case EYEDUDE_STATE_TURN:
            break;
    }
}

/* =========================================================================
 * Collision detection
 * ========================================================================= */

int eyedude_system_check_collision(const eyedude_system_t *ctx, int bx, int by, int ball_hw,
                                   int ball_hh)
{
    if (ctx == NULL || ctx->state != EYEDUDE_STATE_WALK)
    {
        return 0;
    }

    /* AABB overlap check matching legacy CheckBallEyeDudeCollision */
    if (((ctx->x + EYEDUDE_WC) >= (bx - ball_hw)) && ((ctx->x - EYEDUDE_WC) <= (bx + ball_hw)) &&
        ((ctx->y + EYEDUDE_HC) >= (by - ball_hh)) && ((ctx->y - EYEDUDE_HC) <= (by + ball_hh)))
    {
        return 1;
    }

    return 0;
}

/* =========================================================================
 * Queries
 * ========================================================================= */

void eyedude_system_get_position(const eyedude_system_t *ctx, int *out_x, int *out_y)
{
    if (ctx == NULL)
    {
        if (out_x)
        {
            *out_x = 0;
        }
        if (out_y)
        {
            *out_y = 0;
        }
        return;
    }
    if (out_x)
    {
        *out_x = ctx->oldx;
    }
    if (out_y)
    {
        *out_y = ctx->oldy;
    }
}

eyedude_render_info_t eyedude_system_get_render_info(const eyedude_system_t *ctx)
{
    eyedude_render_info_t info = {0, 0, 0, EYEDUDE_DIR_LEFT, 0};
    if (ctx == NULL)
    {
        return info;
    }

    info.x = ctx->x;
    info.y = ctx->y;
    info.frame_index = ctx->slide;
    info.dir = ctx->direction;
    info.visible = (ctx->state == EYEDUDE_STATE_WALK) ? 1 : 0;

    return info;
}
