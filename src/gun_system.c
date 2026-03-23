/*
 * gun_system.c — Pure C gun/bullet system with callback-based side effects.
 *
 * Owns bullet and tink arrays, ammo state, movement physics, and collision
 * dispatch.  Communicates side effects (sound, block damage, ball kill,
 * eyedude hit) through an injected callback table.  Zero dependency on
 * SDL2 or X11.
 *
 * Opaque context pattern: no globals, fully testable with CMocka stubs.
 * See ADR-018 in docs/DESIGN.md for design rationale.
 */

#include "gun_system.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal data structures
 * ========================================================================= */

typedef struct
{
    int xpos;            /* Center X position (-1 = inactive) */
    int ypos;            /* Center Y position */
    int oldypos;         /* Previous Y position (for erase) */
    int dy;              /* Velocity (negative = upward) */
    int render_from_y;   /* Y before last movement (for interpolation) */
    int last_move_frame; /* Frame of last movement */
} bullet_slot_t;

typedef struct
{
    int xpos;       /* Center X position (-1 = inactive) */
    int clearFrame; /* Frame at which tink expires */
} tink_slot_t;

struct gun_system
{
    bullet_slot_t bullets[GUN_MAX_BULLETS];
    tink_slot_t tinks[GUN_MAX_TINKS];
    int ammo;
    int unlimited;
    int bullet_start_y;    /* play_height - GUN_BULLET_START_OFFSET */
    int last_update_frame; /* Most recent env->frame from gun_system_update */
    gun_system_callbacks_t callbacks;
    void *user_data;
};

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static void clear_bullet(gun_system_t *ctx, int i)
{
    ctx->bullets[i].xpos = -1;
    ctx->bullets[i].ypos = ctx->bullet_start_y;
    ctx->bullets[i].oldypos = ctx->bullet_start_y;
    ctx->bullets[i].dy = GUN_BULLET_DY;
    ctx->bullets[i].render_from_y = ctx->bullet_start_y;
    ctx->bullets[i].last_move_frame = 0;
}

static void clear_all_bullets(gun_system_t *ctx)
{
    for (int i = 0; i < GUN_MAX_BULLETS; i++)
    {
        clear_bullet(ctx, i);
    }
}

static void clear_all_tinks(gun_system_t *ctx)
{
    for (int i = 0; i < GUN_MAX_TINKS; i++)
    {
        ctx->tinks[i].xpos = -1;
        ctx->tinks[i].clearFrame = 0;
    }
}

static void add_tink(gun_system_t *ctx, int xpos, int frame)
{
    for (int i = 0; i < GUN_MAX_TINKS; i++)
    {
        if (ctx->tinks[i].xpos == -1)
        {
            ctx->tinks[i].xpos = xpos;
            ctx->tinks[i].clearFrame = frame + GUN_TINK_DELAY;

            if (ctx->callbacks.on_sound)
            {
                ctx->callbacks.on_sound("shoot", ctx->user_data);
            }
            return;
        }
    }
    /* Full tink array — silently ignore (matches legacy WarningMessage) */
}

static void check_tinks(gun_system_t *ctx, int frame)
{
    for (int i = 0; i < GUN_MAX_TINKS; i++)
    {
        if (ctx->tinks[i].xpos != -1)
        {
            if (frame >= ctx->tinks[i].clearFrame)
            {
                ctx->tinks[i].xpos = -1;
                ctx->tinks[i].clearFrame = 0;
            }
        }
    }
}

/*
 * Try to start a bullet at the given X position.
 * In normal mode, only one bullet slot is checked (returns False if occupied).
 * In fast gun mode, searches all slots.
 * Returns nonzero on success.
 */
static int start_a_bullet(gun_system_t *ctx, int xpos, int frame, int fast_gun)
{
    for (int i = 0; i < GUN_MAX_BULLETS; i++)
    {
        if (ctx->bullets[i].xpos == -1)
        {
            ctx->bullets[i].xpos = xpos;
            ctx->bullets[i].last_move_frame = frame;
            return 1;
        }

        /* Break out as the machine gun is not active */
        if (!fast_gun)
        {
            return 0;
        }
    }

    /* Full bullet array */
    return 0;
}

static void update_bullets(gun_system_t *ctx, const gun_system_env_t *env)
{
    for (int i = 0; i < GUN_MAX_BULLETS; i++)
    {
        if (ctx->bullets[i].xpos == -1)
        {
            continue;
        }

        /* Snapshot for render interpolation */
        ctx->bullets[i].render_from_y = ctx->bullets[i].ypos;

        /* Update position */
        ctx->bullets[i].ypos = ctx->bullets[i].oldypos + ctx->bullets[i].dy;

        /* Has the bullet gone off the top edge? */
        if (ctx->bullets[i].ypos < -GUN_BULLET_HC)
        {
            add_tink(ctx, ctx->bullets[i].xpos, env->frame);
            clear_bullet(ctx, i);
            continue;
        }

        /* Check ball collision first (highest priority) */
        if (ctx->callbacks.check_ball_hit)
        {
            int ball_index = ctx->callbacks.check_ball_hit(ctx->bullets[i].xpos,
                                                           ctx->bullets[i].ypos, ctx->user_data);
            if (ball_index >= 0)
            {
                clear_bullet(ctx, i);

                if (ctx->callbacks.on_ball_hit)
                {
                    ctx->callbacks.on_ball_hit(ball_index, ctx->user_data);
                }

                if (ctx->callbacks.on_sound)
                {
                    ctx->callbacks.on_sound("ballshot", ctx->user_data);
                }
                continue;
            }
        }

        /* Check eyedude collision (second priority) */
        if (ctx->callbacks.check_eyedude_hit)
        {
            if (ctx->callbacks.check_eyedude_hit(ctx->bullets[i].xpos, ctx->bullets[i].ypos,
                                                 ctx->user_data))
            {
                clear_bullet(ctx, i);

                if (ctx->callbacks.on_eyedude_hit)
                {
                    ctx->callbacks.on_eyedude_hit(ctx->user_data);
                }
                continue;
            }
        }

        /* Check block collision (lowest priority) */
        if (ctx->callbacks.check_block_hit)
        {
            int row = 0;
            int col = 0;
            if (ctx->callbacks.check_block_hit(ctx->bullets[i].xpos, ctx->bullets[i].ypos, &row,
                                               &col, ctx->user_data))
            {
                clear_bullet(ctx, i);

                if (ctx->callbacks.on_block_hit)
                {
                    ctx->callbacks.on_block_hit(row, col, ctx->user_data);
                }
                continue;
            }
        }

        /* Keep track of old position */
        ctx->bullets[i].oldypos = ctx->bullets[i].ypos;
        ctx->bullets[i].last_move_frame = env->frame;
    }
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

gun_system_t *gun_system_create(int play_height, const gun_system_callbacks_t *callbacks,
                                void *user_data, gun_system_status_t *status)
{
    gun_system_t *ctx = calloc(1, sizeof(gun_system_t));
    if (ctx == NULL)
    {
        if (status)
        {
            *status = GUN_SYS_ERR_ALLOC_FAILED;
        }
        return NULL;
    }

    ctx->bullet_start_y = play_height - GUN_BULLET_START_OFFSET;
    ctx->user_data = user_data;

    if (callbacks)
    {
        ctx->callbacks = *callbacks;
    }

    clear_all_bullets(ctx);
    clear_all_tinks(ctx);

    ctx->ammo = 0;
    ctx->unlimited = 0;

    if (status)
    {
        *status = GUN_SYS_OK;
    }
    return ctx;
}

void gun_system_destroy(gun_system_t *ctx)
{
    free(ctx);
}

/* =========================================================================
 * Per-frame update
 * ========================================================================= */

void gun_system_update(gun_system_t *ctx, const gun_system_env_t *env)
{
    if (ctx == NULL || env == NULL)
    {
        return;
    }

    ctx->last_update_frame = env->frame;

    /* Move bullets and check collisions every BULLET_FRAME_RATE frames */
    if ((env->frame % GUN_BULLET_FRAME_RATE) == 0)
    {
        update_bullets(ctx, env);
    }

    /* Expire old tinks every frame */
    check_tinks(ctx, env->frame);
}

/* =========================================================================
 * Player actions
 * ========================================================================= */

int gun_system_shoot(gun_system_t *ctx, const gun_system_env_t *env)
{
    if (ctx == NULL || env == NULL)
    {
        return 0;
    }

    /* Check if ball is waiting — shooting blocked */
    if (ctx->callbacks.is_ball_waiting)
    {
        if (ctx->callbacks.is_ball_waiting(ctx->user_data))
        {
            return 0;
        }
    }

    if (gun_system_get_ammo(ctx) > 0)
    {
        int status = 0;

        if (env->fast_gun)
        {
            /* Dual fire: two bullets at ±(paddle_size/3) from center */
            int s1 = start_a_bullet(ctx, env->paddle_pos - (env->paddle_size / 3), env->frame, 1);
            int s2 = start_a_bullet(ctx, env->paddle_pos + (env->paddle_size / 3), env->frame, 1);
            status = s1 || s2;
        }
        else
        {
            status = start_a_bullet(ctx, env->paddle_pos, env->frame, 0);
        }

        if (status)
        {
            /* Consume one ammo */
            gun_system_use_ammo(ctx);

            if (ctx->callbacks.on_sound)
            {
                ctx->callbacks.on_sound("shotgun", ctx->user_data);
            }
            return 1;
        }
    }
    else
    {
        /* No ammo — click sound */
        if (ctx->callbacks.on_sound)
        {
            ctx->callbacks.on_sound("click", ctx->user_data);
        }
    }

    return 0;
}

/* =========================================================================
 * Ammo management
 * ========================================================================= */

void gun_system_set_ammo(gun_system_t *ctx, int count)
{
    if (ctx == NULL)
    {
        return;
    }
    ctx->ammo = count;
}

void gun_system_add_ammo(gun_system_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }
    ctx->ammo++;
    if (ctx->ammo > GUN_MAX_AMMO)
    {
        ctx->ammo = GUN_MAX_AMMO;
    }
}

int gun_system_use_ammo(gun_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }

    /* Unlimited mode: don't decrement */
    if (ctx->unlimited)
    {
        return ctx->ammo;
    }

    ctx->ammo--;
    if (ctx->ammo < 0)
    {
        ctx->ammo = 0;
    }
    return ctx->ammo;
}

int gun_system_get_ammo(const gun_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    return ctx->ammo;
}

void gun_system_set_unlimited(gun_system_t *ctx, int on)
{
    if (ctx == NULL)
    {
        return;
    }
    ctx->unlimited = on ? 1 : 0;
}

int gun_system_get_unlimited(const gun_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    return ctx->unlimited;
}

/* =========================================================================
 * Reset
 * ========================================================================= */

void gun_system_clear(gun_system_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }
    clear_all_bullets(ctx);
    clear_all_tinks(ctx);
}

/* =========================================================================
 * Render queries
 * ========================================================================= */

gun_system_status_t gun_system_get_bullet_info(const gun_system_t *ctx, int index,
                                               gun_system_bullet_info_t *info)
{
    if (ctx == NULL || info == NULL)
    {
        return GUN_SYS_ERR_NULL_ARG;
    }
    if (index < 0 || index >= GUN_MAX_BULLETS)
    {
        return GUN_SYS_ERR_OUT_OF_BOUNDS;
    }

    if (ctx->bullets[index].xpos == -1)
    {
        info->active = 0;
        info->x = 0;
        info->y = 0;
        info->from_y = 0;
        info->ticks_since_move = 0;
    }
    else
    {
        info->active = 1;
        info->x = ctx->bullets[index].xpos;
        info->y = ctx->bullets[index].ypos;
        info->from_y = ctx->bullets[index].render_from_y;
        info->ticks_since_move = ctx->last_update_frame - ctx->bullets[index].last_move_frame;
    }
    return GUN_SYS_OK;
}

gun_system_status_t gun_system_get_tink_info(const gun_system_t *ctx, int index,
                                             gun_system_tink_info_t *info)
{
    if (ctx == NULL || info == NULL)
    {
        return GUN_SYS_ERR_NULL_ARG;
    }
    if (index < 0 || index >= GUN_MAX_TINKS)
    {
        return GUN_SYS_ERR_OUT_OF_BOUNDS;
    }

    if (ctx->tinks[index].xpos == -1)
    {
        info->active = 0;
        info->x = 0;
    }
    else
    {
        info->active = 1;
        info->x = ctx->tinks[index].xpos;
    }
    return GUN_SYS_OK;
}

int gun_system_get_active_bullet_count(const gun_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    int count = 0;
    for (int i = 0; i < GUN_MAX_BULLETS; i++)
    {
        if (ctx->bullets[i].xpos != -1)
        {
            count++;
        }
    }
    return count;
}

int gun_system_get_active_tink_count(const gun_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    int count = 0;
    for (int i = 0; i < GUN_MAX_TINKS; i++)
    {
        if (ctx->tinks[i].xpos != -1)
        {
            count++;
        }
    }
    return count;
}

/* =========================================================================
 * Utility
 * ========================================================================= */

const char *gun_system_status_string(gun_system_status_t status)
{
    switch (status)
    {
        case GUN_SYS_OK:
            return "GUN_SYS_OK";
        case GUN_SYS_ERR_NULL_ARG:
            return "GUN_SYS_ERR_NULL_ARG";
        case GUN_SYS_ERR_ALLOC_FAILED:
            return "GUN_SYS_ERR_ALLOC_FAILED";
        case GUN_SYS_ERR_OUT_OF_BOUNDS:
            return "GUN_SYS_ERR_OUT_OF_BOUNDS";
        default:
            return "GUN_SYS_UNKNOWN";
    }
}
