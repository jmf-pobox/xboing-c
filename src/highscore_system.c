/*
 * highscore_system.c — Pure C high score display screen sequencer.
 *
 * See highscore_system.h for module overview.
 */

#include "highscore_system.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal state
 * ========================================================================= */

struct highscore_system
{
    highscore_state_t state;
    highscore_type_t score_type;
    int finished;

    int current_frame;
    int end_frame;

    /* Score data. */
    highscore_table_t table;
    unsigned long current_score;

    /* Title sparkle (two stars flanking title). */
    int title_sparkle_index;
    int title_sparkle_delay;
    int title_sparkle_active;
    int title_sparkle_clear;

    /* Row sparkle (walks down score rows). */
    int row_sparkle_row;
    int row_sparkle_index;
    int row_sparkle_next_frame;
    int row_sparkle_save_bg;
    int row_sparkle_restore_bg;

    /* Wait state. */
    int wait_frame;
    highscore_state_t wait_target;

    /* Sound. */
    highscore_sound_t sound;

    /* Callbacks. */
    highscore_system_callbacks_t callbacks;
    void *user_data;
};

/* =========================================================================
 * Wait mechanism
 * ========================================================================= */

static void set_wait(highscore_system_t *ctx, highscore_state_t target, int frame)
{
    ctx->wait_target = target;
    ctx->wait_frame = frame;
    ctx->state = HIGHSCORE_STATE_WAIT;
}

/* =========================================================================
 * Title sparkle
 * ========================================================================= */

static void title_sparkle_update(highscore_system_t *ctx)
{
    ctx->title_sparkle_active = 0;
    ctx->title_sparkle_clear = 0;

    if ((ctx->current_frame % ctx->title_sparkle_delay) == 0)
    {
        if (ctx->title_sparkle_delay == HIGHSCORE_TITLE_SPARKLE_SLOW)
        {
            ctx->title_sparkle_delay = HIGHSCORE_TITLE_SPARKLE_FAST;
        }

        ctx->title_sparkle_active = 1;
        ctx->title_sparkle_index++;

        if (ctx->title_sparkle_index >= HIGHSCORE_TITLE_SPARKLE_FRAMES)
        {
            ctx->title_sparkle_clear = 1;
            ctx->title_sparkle_index = 0;

            if (ctx->title_sparkle_delay == HIGHSCORE_TITLE_SPARKLE_FAST)
            {
                ctx->title_sparkle_delay = HIGHSCORE_TITLE_SPARKLE_SLOW;
            }
        }
    }
}

/* =========================================================================
 * Row sparkle
 * ========================================================================= */

static void row_sparkle_update(highscore_system_t *ctx)
{
    ctx->row_sparkle_save_bg = 0;
    ctx->row_sparkle_restore_bg = 0;

    if (ctx->row_sparkle_index == 0)
    {
        ctx->row_sparkle_save_bg = 1;
    }

    if (ctx->current_frame == ctx->row_sparkle_next_frame)
    {
        ctx->row_sparkle_restore_bg = 1;
        ctx->row_sparkle_index++;
        ctx->row_sparkle_next_frame = ctx->current_frame + HIGHSCORE_ROW_SPARKLE_STEP;

        if (ctx->row_sparkle_index >= HIGHSCORE_ROW_SPARKLE_FRAMES)
        {
            ctx->row_sparkle_index = 0;
            ctx->row_sparkle_next_frame = ctx->current_frame + HIGHSCORE_ROW_SPARKLE_PAUSE;

            ctx->row_sparkle_row++;

            /* Skip empty rows, wrap around. */
            if (ctx->row_sparkle_row >= HIGHSCORE_NUM_ENTRIES ||
                ctx->table.entries[ctx->row_sparkle_row].score == 0)
            {
                ctx->row_sparkle_row = 0;
            }

            ctx->row_sparkle_save_bg = 1;
        }
    }
}

/* =========================================================================
 * State handlers
 * ========================================================================= */

static void do_title(highscore_system_t *ctx)
{
    set_wait(ctx, HIGHSCORE_STATE_SHOW, ctx->current_frame + 10);
}

static void do_show(highscore_system_t *ctx)
{
    /* Score data is already set via set_table().
     * Integration layer renders from get_table().
     * Transition to sparkle after a brief delay. */
    ctx->row_sparkle_row = 0;
    ctx->row_sparkle_index = 0;
    ctx->row_sparkle_next_frame = ctx->current_frame + HIGHSCORE_ROW_SPARKLE_STEP;
    ctx->title_sparkle_index = 0;
    ctx->title_sparkle_delay = HIGHSCORE_TITLE_SPARKLE_FAST;

    set_wait(ctx, HIGHSCORE_STATE_SPARKLE, ctx->current_frame + 2);
}

static void do_sparkle(highscore_system_t *ctx)
{
    if (ctx->current_frame >= ctx->end_frame)
    {
        set_wait(ctx, HIGHSCORE_STATE_FINISH, ctx->current_frame + 1);
        return;
    }

    title_sparkle_update(ctx);
    row_sparkle_update(ctx);
}

static void do_finish(highscore_system_t *ctx)
{
    ctx->finished = 1;
    ctx->sound.name = "gate";
    ctx->sound.volume = 50;

    if (ctx->callbacks.on_finished)
    {
        ctx->callbacks.on_finished(ctx->score_type, ctx->user_data);
    }
}

/* =========================================================================
 * Public API
 * ========================================================================= */

highscore_system_t *highscore_system_create(const highscore_system_callbacks_t *callbacks,
                                            void *user_data)
{
    highscore_system_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
    {
        return NULL;
    }
    if (callbacks)
    {
        ctx->callbacks = *callbacks;
    }
    ctx->user_data = user_data;
    ctx->state = HIGHSCORE_STATE_NONE;
    return ctx;
}

void highscore_system_destroy(highscore_system_t *ctx)
{
    free(ctx);
}

void highscore_system_set_table(highscore_system_t *ctx, const highscore_table_t *table)
{
    if (!ctx || !table)
    {
        return;
    }
    ctx->table = *table;
}

void highscore_system_set_current_score(highscore_system_t *ctx, unsigned long score)
{
    if (!ctx)
    {
        return;
    }
    ctx->current_score = score;
}

void highscore_system_begin(highscore_system_t *ctx, highscore_type_t type, int frame)
{
    if (!ctx)
    {
        return;
    }
    ctx->score_type = type;
    ctx->state = HIGHSCORE_STATE_TITLE;
    ctx->finished = 0;
    ctx->current_frame = frame;
    ctx->end_frame = frame + HIGHSCORE_END_FRAME_OFFSET;
    ctx->sound.name = NULL;
    ctx->sound.volume = 0;

    ctx->title_sparkle_index = 0;
    ctx->title_sparkle_delay = HIGHSCORE_TITLE_SPARKLE_FAST;
    ctx->title_sparkle_active = 0;
    ctx->title_sparkle_clear = 0;

    ctx->row_sparkle_row = 0;
    ctx->row_sparkle_index = 0;
    ctx->row_sparkle_next_frame = frame + HIGHSCORE_ROW_SPARKLE_STEP;
    ctx->row_sparkle_save_bg = 0;
    ctx->row_sparkle_restore_bg = 0;
}

int highscore_system_update(highscore_system_t *ctx, int frame)
{
    if (!ctx || ctx->finished)
    {
        return 0;
    }

    ctx->current_frame = frame;
    ctx->sound.name = NULL;
    ctx->sound.volume = 0;
    ctx->title_sparkle_active = 0;
    ctx->title_sparkle_clear = 0;

    switch (ctx->state)
    {
        case HIGHSCORE_STATE_TITLE:
            do_title(ctx);
            break;
        case HIGHSCORE_STATE_SHOW:
            do_show(ctx);
            break;
        case HIGHSCORE_STATE_SPARKLE:
            do_sparkle(ctx);
            break;
        case HIGHSCORE_STATE_WAIT:
            if (frame >= ctx->wait_frame)
            {
                ctx->state = ctx->wait_target;
            }
            break;
        case HIGHSCORE_STATE_FINISH:
            do_finish(ctx);
            break;
        case HIGHSCORE_STATE_NONE:
            break;
    }

    return ctx->finished ? 0 : 1;
}

highscore_state_t highscore_system_get_state(const highscore_system_t *ctx)
{
    if (!ctx)
    {
        return HIGHSCORE_STATE_NONE;
    }
    return ctx->state;
}

highscore_type_t highscore_system_get_type(const highscore_system_t *ctx)
{
    if (!ctx)
    {
        return HIGHSCORE_TYPE_GLOBAL;
    }
    return ctx->score_type;
}

int highscore_system_is_finished(const highscore_system_t *ctx)
{
    if (!ctx)
    {
        return 1;
    }
    return ctx->finished;
}

const highscore_table_t *highscore_system_get_table(const highscore_system_t *ctx)
{
    if (!ctx)
    {
        return NULL;
    }
    return &ctx->table;
}

unsigned long highscore_system_get_current_score(const highscore_system_t *ctx)
{
    if (!ctx)
    {
        return 0;
    }
    return ctx->current_score;
}

void highscore_system_get_title_sparkle(const highscore_system_t *ctx,
                                        highscore_title_sparkle_t *out)
{
    if (!ctx || !out)
    {
        return;
    }
    out->frame_index = ctx->title_sparkle_index;
    out->mirror_index = (HIGHSCORE_TITLE_SPARKLE_FRAMES - 1) - ctx->title_sparkle_index;
    out->active = ctx->title_sparkle_active;
    out->clear = ctx->title_sparkle_clear;
}

void highscore_system_get_row_sparkle(const highscore_system_t *ctx, highscore_row_sparkle_t *out)
{
    if (!ctx || !out)
    {
        return;
    }
    out->row = ctx->row_sparkle_row;
    out->frame_index = ctx->row_sparkle_index;
    out->active = (ctx->state == HIGHSCORE_STATE_SPARKLE && !ctx->finished) ? 1 : 0;
    out->save_bg = ctx->row_sparkle_save_bg;
    out->restore_bg = ctx->row_sparkle_restore_bg;
}

int highscore_system_should_draw_specials(const highscore_system_t *ctx)
{
    if (!ctx)
    {
        return 0;
    }
    if (ctx->state != HIGHSCORE_STATE_SPARKLE)
    {
        return 0;
    }
    return (ctx->current_frame % HIGHSCORE_FLASH) == 0 ? 1 : 0;
}

highscore_sound_t highscore_system_get_sound(const highscore_system_t *ctx)
{
    highscore_sound_t none = {NULL, 0};
    if (!ctx)
    {
        return none;
    }
    return ctx->sound;
}
