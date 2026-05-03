/*
 * bonus_system.c — Pure C bonus tally sequence state machine.
 *
 * Owns the 10-state bonus screen sequence, bonus coin counting,
 * score computation, and save trigger logic.  All rendering is
 * delegated to the integration layer via callbacks and queries.
 *
 * Opaque context pattern: no globals, fully testable with CMocka stubs.
 * See ADR-022 in docs/DESIGN.md for design rationale.
 */

#include "bonus_system.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal structure
 * ========================================================================= */

struct bonus_system
{
    /* State machine */
    bonus_state_t state;
    bonus_state_t wait_mode; /* Next state after BONUS_STATE_WAIT expires */
    int wait_frame;          /* Target frame for wait expiry */
    int finished;            /* Set to 1 after on_finished fires */

    /* Coin count (accumulated during gameplay) */
    int coin_count;

    /* Per-sequence state (set during begin, used during updates) */
    bonus_system_env_t env;
    unsigned long display_score; /* Running score counter for display */
    int first_time;              /* Sentinel for multi-call animation states */

    /* Initial counts captured at begin — used by the renderer to draw
     * coins/bullets at accumulating x positions during the per-frame
     * decrement animation (basket 5: original/bonus.c:280-389 DoBonuses,
     * 431-490 DoBullets).  Live counts decrement to 0; renderer draws
     * `initial - live` sprites. */
    int initial_coin_count;
    int initial_bullet_count;

    /* Callbacks */
    bonus_system_callbacks_t callbacks;
    void *user_data;
};

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static void set_bonus_wait(bonus_system_t *ctx, bonus_state_t next, int target_frame)
{
    ctx->wait_mode = next;
    ctx->wait_frame = target_frame;
    ctx->state = BONUS_STATE_WAIT;
}

static void fire_sound(const bonus_system_t *ctx, const char *name)
{
    if (ctx->callbacks.on_sound)
    {
        ctx->callbacks.on_sound(name, ctx->user_data);
    }
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

bonus_system_t *bonus_system_create(const bonus_system_callbacks_t *callbacks, void *user_data)
{
    bonus_system_t *ctx = calloc(1, sizeof(bonus_system_t));
    if (ctx == NULL)
    {
        return NULL;
    }

    ctx->user_data = user_data;

    if (callbacks)
    {
        ctx->callbacks = *callbacks;
    }

    /* calloc zeroes all fields: state=BONUS_STATE_TEXT, coin_count=0 */
    return ctx;
}

void bonus_system_destroy(bonus_system_t *ctx)
{
    free(ctx);
}

/* =========================================================================
 * Score computation (stateless utility)
 * ========================================================================= */

unsigned long bonus_system_compute_total(int coin_count, int level, int starting_level,
                                         int time_bonus_secs, int bullet_count)
{
    unsigned long total = 0;

    if (time_bonus_secs > 0)
    {
        /* Bonus coins: super bonus if > MAX, otherwise per-coin */
        if (coin_count > BONUS_MAX_COINS)
        {
            total += BONUS_SUPER_SCORE;
        }
        else
        {
            total += (unsigned long)coin_count * BONUS_COIN_SCORE;
        }

        /* Level bonus */
        int adjusted_level = level - starting_level + 1;
        if (adjusted_level > 0)
        {
            total += (unsigned long)adjusted_level * BONUS_LEVEL_SCORE;
        }
    }

    /* Bullet bonus (awarded regardless of timer) */
    if (bullet_count > 0)
    {
        total += (unsigned long)bullet_count * BONUS_BULLET_SCORE;
    }

    /* Time bonus */
    if (time_bonus_secs > 0)
    {
        total += (unsigned long)time_bonus_secs * BONUS_TIME_SCORE;
    }

    return total;
}

int bonus_system_should_save(int level, int starting_level)
{
    int levels_played = level - starting_level + 1;
    if (levels_played <= 0)
    {
        return 0;
    }
    return (levels_played % BONUS_SAVE_LEVEL) == 0 ? 1 : 0;
}

/* =========================================================================
 * Bonus sequence lifecycle
 * ========================================================================= */

void bonus_system_begin(bonus_system_t *ctx, const bonus_system_env_t *env, int frame)
{
    if (ctx == NULL || env == NULL)
    {
        return;
    }

    ctx->env = *env;
    ctx->state = BONUS_STATE_TEXT;
    ctx->display_score = env->score;
    ctx->first_time = 1;
    ctx->finished = 0;

    /* Capture initial counts so the renderer can draw accumulating
     * sprite rows during the per-frame decrement animation. */
    ctx->initial_coin_count = ctx->coin_count;
    ctx->initial_bullet_count = env->bullet_count;

    /* Compute and commit total bonus score immediately.
     * Legacy behavior: all bonus points are added to the real score
     * at the start of the bonus screen.  The animated tally is
     * purely cosmetic — it replays the computation visually. */
    unsigned long total = bonus_system_compute_total(
        ctx->coin_count, env->level, env->starting_level, env->time_bonus_secs, env->bullet_count);

    if (total > 0 && ctx->callbacks.on_score_add)
    {
        ctx->callbacks.on_score_add(total, ctx->user_data);
    }

    /* Check save trigger */
    if (bonus_system_should_save(env->level, env->starting_level))
    {
        if (ctx->callbacks.on_save_triggered)
        {
            ctx->callbacks.on_save_triggered(ctx->user_data);
        }
    }

    /* Arm timer: transition to BONUS_STATE_SCORE after 5 frames.
     * set_bonus_wait sets state to BONUS_STATE_WAIT — the first
     * update() call will poll the timer and transition when ready. */
    set_bonus_wait(ctx, BONUS_STATE_SCORE, frame + 5);
}

/* =========================================================================
 * Per-state update handlers
 * ========================================================================= */

static void do_bonus_wait(bonus_system_t *ctx, int frame)
{
    if (frame >= ctx->wait_frame)
    {
        ctx->state = ctx->wait_mode;
        ctx->first_time = 1;
    }
}

static void do_score(bonus_system_t *ctx, int frame)
{
    /* "Congratulations" — transition to bonus coin tally */
    set_bonus_wait(ctx, BONUS_STATE_BONUS, frame + BONUS_LINE_DELAY);
}

static void do_bonuses(bonus_system_t *ctx, int frame)
{
    if (ctx->env.time_bonus_secs == 0)
    {
        /* Timer ran out — no coins count */
        fire_sound(ctx, "wzzz");
        set_bonus_wait(ctx, BONUS_STATE_LEVEL, frame + BONUS_LINE_DELAY);
        ctx->first_time = 1;
        return;
    }

    if (ctx->coin_count == 0 && ctx->first_time)
    {
        /* No coins collected */
        fire_sound(ctx, "wzzz");
        set_bonus_wait(ctx, BONUS_STATE_LEVEL, frame + BONUS_LINE_DELAY);
        ctx->first_time = 1;
        return;
    }

    if (ctx->coin_count > BONUS_MAX_COINS && ctx->first_time)
    {
        /* Super bonus — one-shot display */
        fire_sound(ctx, "supbons");
        ctx->display_score += BONUS_SUPER_SCORE;
        ctx->coin_count = 0;
        set_bonus_wait(ctx, BONUS_STATE_LEVEL, frame + BONUS_LINE_DELAY);
        ctx->first_time = 1;
        return;
    }

    /* Animate one coin per frame */
    if (ctx->coin_count > 0)
    {
        ctx->coin_count--;
        ctx->display_score += BONUS_COIN_SCORE;
        fire_sound(ctx, "bonus");

        if (ctx->coin_count == 0)
        {
            set_bonus_wait(ctx, BONUS_STATE_LEVEL, frame + BONUS_LINE_DELAY);
            ctx->first_time = 1;
        }
    }
}

static void do_level(bonus_system_t *ctx, int frame)
{
    if (ctx->env.time_bonus_secs > 0)
    {
        int adjusted = ctx->env.level - ctx->env.starting_level + 1;
        if (adjusted > 0)
        {
            ctx->display_score += (unsigned long)adjusted * BONUS_LEVEL_SCORE;
        }
    }
    set_bonus_wait(ctx, BONUS_STATE_BULLET, frame + BONUS_LINE_DELAY);
}

static void do_bullets(bonus_system_t *ctx, int frame)
{
    if (ctx->first_time)
    {
        ctx->first_time = 0;

        if (ctx->env.bullet_count == 0)
        {
            /* No bullets — skip animation */
            fire_sound(ctx, "wzzz");
            set_bonus_wait(ctx, BONUS_STATE_TIME, frame + BONUS_LINE_DELAY);
            ctx->first_time = 1;
            return;
        }
    }

    /* Animate one bullet per frame */
    if (ctx->env.bullet_count > 0)
    {
        ctx->env.bullet_count--;
        ctx->display_score += BONUS_BULLET_SCORE;

        if (ctx->callbacks.on_bullet_consumed)
        {
            ctx->callbacks.on_bullet_consumed(ctx->user_data);
        }

        if (ctx->env.bullet_count == 0)
        {
            set_bonus_wait(ctx, BONUS_STATE_TIME, frame + BONUS_LINE_DELAY);
            ctx->first_time = 1;
        }
    }
}

static void do_time_bonus(bonus_system_t *ctx, int frame)
{
    if (ctx->env.time_bonus_secs > 0)
    {
        ctx->display_score += (unsigned long)ctx->env.time_bonus_secs * BONUS_TIME_SCORE;
    }
    set_bonus_wait(ctx, BONUS_STATE_HSCORE, frame + BONUS_LINE_DELAY);
}

static void do_highscore(bonus_system_t *ctx, int frame)
{
    set_bonus_wait(ctx, BONUS_STATE_END_TEXT, frame + BONUS_LINE_DELAY);
}

static void do_end_text(bonus_system_t *ctx, int frame)
{
    fire_sound(ctx, "applause");
    /* Double delay before finish */
    set_bonus_wait(ctx, BONUS_STATE_FINISH, frame + BONUS_LINE_DELAY * 2);
}

static void do_finish(bonus_system_t *ctx)
{
    int next_level = ctx->env.level + 1;

    if (ctx->callbacks.on_finished)
    {
        ctx->callbacks.on_finished(next_level, ctx->user_data);
    }

    /* Mark sequence complete.  State stays at FINISH so callers
     * can observe is_finished().  begin() resets everything. */
    ctx->finished = 1;
}

/* =========================================================================
 * Main update dispatch
 * ========================================================================= */

bonus_state_t bonus_system_update(bonus_system_t *ctx, int frame)
{
    if (ctx == NULL)
    {
        return BONUS_STATE_TEXT;
    }

    switch (ctx->state)
    {
        case BONUS_STATE_TEXT:
            /* Initial text is shown by begin(); wait for timer */
            break;

        case BONUS_STATE_WAIT:
            do_bonus_wait(ctx, frame);
            break;

        case BONUS_STATE_SCORE:
            do_score(ctx, frame);
            break;

        case BONUS_STATE_BONUS:
            do_bonuses(ctx, frame);
            break;

        case BONUS_STATE_LEVEL:
            do_level(ctx, frame);
            break;

        case BONUS_STATE_BULLET:
            do_bullets(ctx, frame);
            break;

        case BONUS_STATE_TIME:
            do_time_bonus(ctx, frame);
            break;

        case BONUS_STATE_HSCORE:
            do_highscore(ctx, frame);
            break;

        case BONUS_STATE_END_TEXT:
            do_end_text(ctx, frame);
            break;

        case BONUS_STATE_FINISH:
            do_finish(ctx);
            break;
    }

    return ctx->state;
}

void bonus_system_skip(bonus_system_t *ctx, int frame)
{
    if (ctx == NULL)
    {
        return;
    }

    /* Jump to finish on next update, matching legacy space-bar behavior */
    set_bonus_wait(ctx, BONUS_STATE_FINISH, frame);
}

/* =========================================================================
 * Coin counting
 * ========================================================================= */

void bonus_system_inc_coins(bonus_system_t *ctx)
{
    if (ctx)
    {
        ctx->coin_count++;
    }
}

void bonus_system_dec_coins(bonus_system_t *ctx)
{
    if (ctx && ctx->coin_count > 0)
    {
        ctx->coin_count--;
    }
}

int bonus_system_get_coins(const bonus_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    return ctx->coin_count;
}

int bonus_system_get_initial_coins(const bonus_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    return ctx->initial_coin_count;
}

int bonus_system_get_bullets(const bonus_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    return ctx->env.bullet_count;
}

int bonus_system_get_initial_bullets(const bonus_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    return ctx->initial_bullet_count;
}

void bonus_system_reset_coins(bonus_system_t *ctx)
{
    if (ctx)
    {
        ctx->coin_count = 0;
    }
}

/* =========================================================================
 * Queries
 * ========================================================================= */

bonus_state_t bonus_system_get_state(const bonus_system_t *ctx)
{
    if (ctx == NULL)
    {
        return BONUS_STATE_TEXT;
    }
    return ctx->state;
}

int bonus_system_is_finished(const bonus_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    return ctx->finished;
}

unsigned long bonus_system_get_display_score(const bonus_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    return ctx->display_score;
}
