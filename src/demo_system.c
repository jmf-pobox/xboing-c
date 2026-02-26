/*
 * demo_system.c — Pure C demo and preview screen sequencer.
 *
 * See demo_system.h for module overview.
 */

#include "demo_system.h"

#include <stdlib.h>

/* =========================================================================
 * Static data tables
 * ========================================================================= */

/* Ball animation trail from legacy DoBlocks().
 * Starting position: x = PLAY_WIDTH - PLAY_WIDTH/3, y = PLAY_HEIGHT - PLAY_HEIGHT/3
 * = 330, 387.  Then steps of -18, +18 for 6 balls, then -25/-18 direction change. */
static const demo_ball_pos_t ball_trail[DEMO_BALL_TRAIL_COUNT] = {
    {330, 387, 0}, {312, 405, 1}, {294, 423, 2}, {276, 441, 3}, {258, 459, 0},
    {240, 477, 1}, {215, 459, 0}, {197, 441, 1}, {179, 423, 2}, {161, 405, 3},
};

/* Descriptive text from legacy DoBlocks(). */
static const demo_text_line_t demo_text[DEMO_TEXT_LINES] = {
    {"Ball hits the paddle", 300, DEMO_PLAY_HEIGHT - 140},
    {"and bounces back.", 300, DEMO_PLAY_HEIGHT - 120},
    {"Ball hits block", 30, DEMO_PLAY_HEIGHT - 170},
    {"and rebounds.", 30, DEMO_PLAY_HEIGHT - 150},
    {"Paddle moves left to intercept ball.", 160, DEMO_PLAY_HEIGHT - 60},
};

/* Maximum number of levels for random selection. */
#define MAX_NUM_LEVELS 80

/* FLASH interval for specials redraw. */
#define DEMO_FLASH 30

/* =========================================================================
 * Internal state
 * ========================================================================= */

struct demo_system
{
    demo_screen_mode_t screen_mode;
    demo_state_t state;
    int finished;

    int current_frame;
    int end_frame;

    /* Preview level number. */
    int preview_level;

    /* Sparkle state. */
    int sparkle_x;
    int sparkle_y;
    int sparkle_frame;
    int sparkle_started;
    int sparkle_next_frame;
    int sparkle_bg_saved;
    int sparkle_restore;

    /* Wait state. */
    int wait_frame;
    demo_state_t wait_target;

    /* Sound. */
    demo_sound_t sound;

    /* Callbacks. */
    demo_system_callbacks_t callbacks;
    void *user_data;
    demo_rand_fn rand_fn;
};

static int get_rand(demo_system_t *ctx)
{
    if (ctx->rand_fn)
    {
        return ctx->rand_fn(ctx->user_data);
    }
    return rand();
}

/* =========================================================================
 * Sparkle
 * ========================================================================= */

static void sparkle_init(demo_system_t *ctx, int delay)
{
    ctx->sparkle_x = 100;
    ctx->sparkle_y = 20;
    ctx->sparkle_frame = 0;
    ctx->sparkle_started = 0;
    ctx->sparkle_next_frame = ctx->current_frame + delay;
    ctx->sparkle_bg_saved = 0;
    ctx->sparkle_restore = 0;
}

static void sparkle_update(demo_system_t *ctx)
{
    ctx->sparkle_bg_saved = 0;
    ctx->sparkle_restore = 0;

    if (!ctx->sparkle_started)
    {
        ctx->sparkle_started = 1;
        ctx->sparkle_bg_saved = 1;
    }

    if (ctx->current_frame == ctx->sparkle_next_frame)
    {
        ctx->sparkle_restore = 1;
        ctx->sparkle_frame++;
        ctx->sparkle_next_frame = ctx->current_frame + DEMO_SPARKLE_STEP;

        if (ctx->sparkle_frame >= DEMO_SPARKLE_FRAMES)
        {
            ctx->sparkle_frame = 0;
            ctx->sparkle_next_frame = ctx->current_frame + DEMO_SPARKLE_PAUSE;
            ctx->sparkle_x = (get_rand(ctx) % 474) + 5;
            ctx->sparkle_y = (get_rand(ctx) % 74) + 5;
            ctx->sparkle_bg_saved = 1;
        }
    }
}

/* =========================================================================
 * Wait mechanism
 * ========================================================================= */

static void set_wait(demo_system_t *ctx, demo_state_t target, int frame)
{
    ctx->wait_target = target;
    ctx->wait_frame = frame;
    ctx->state = DEMO_STATE_WAIT;
}

/* =========================================================================
 * State handlers
 * ========================================================================= */

static void do_title(demo_system_t *ctx)
{
    if (ctx->screen_mode == DEMO_MODE_DEMO)
    {
        ctx->state = DEMO_STATE_BLOCKS;
    }
    else
    {
        /* Preview: load a random level. */
        int level = (get_rand(ctx) % (MAX_NUM_LEVELS - 1)) + 1;
        ctx->preview_level = level;
        if (ctx->callbacks.on_load_level)
        {
            ctx->callbacks.on_load_level(level, ctx->user_data);
        }
        ctx->state = DEMO_STATE_TEXT;
    }
}

static void do_blocks(demo_system_t *ctx)
{
    /* Block content + ball trail drawn by integration layer
     * using get_ball_trail() and get_demo_text(). */
    ctx->state = DEMO_STATE_TEXT;
}

static void do_text(demo_system_t *ctx)
{
    if (ctx->screen_mode == DEMO_MODE_DEMO)
    {
        /* Demo: transition to sparkle loop with endFrame. */
        sparkle_init(ctx, 10);
        ctx->end_frame = ctx->current_frame + DEMO_END_FRAME_OFFSET;
        ctx->state = DEMO_STATE_SPARKLE;
    }
    else
    {
        /* Preview: 33% chance of "looksbad" sound. */
        if ((get_rand(ctx) % 3) == 0)
        {
            ctx->sound.name = "looksbad";
            ctx->sound.volume = 80;
        }
        set_wait(ctx, DEMO_STATE_FINISH, ctx->current_frame + PREVIEW_WAIT_FRAMES);
    }
}

static void do_sparkle(demo_system_t *ctx)
{
    if (ctx->current_frame >= ctx->end_frame)
    {
        ctx->state = DEMO_STATE_FINISH;
        return;
    }
    sparkle_update(ctx);
}

static void do_finish(demo_system_t *ctx)
{
    ctx->finished = 1;

    if (ctx->screen_mode == DEMO_MODE_DEMO)
    {
        ctx->sound.name = "whizzo";
        ctx->sound.volume = 50;
    }
    else
    {
        ctx->sound.name = "whizzo";
        ctx->sound.volume = 50;
    }

    if (ctx->callbacks.on_finished)
    {
        ctx->callbacks.on_finished(ctx->screen_mode, ctx->user_data);
    }
}

/* =========================================================================
 * Public API
 * ========================================================================= */

demo_system_t *demo_system_create(const demo_system_callbacks_t *callbacks, void *user_data,
                                  demo_rand_fn rand_fn)
{
    demo_system_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
    {
        return NULL;
    }
    if (callbacks)
    {
        ctx->callbacks = *callbacks;
    }
    ctx->user_data = user_data;
    ctx->rand_fn = rand_fn;
    ctx->state = DEMO_STATE_NONE;
    return ctx;
}

void demo_system_destroy(demo_system_t *ctx)
{
    free(ctx);
}

void demo_system_begin(demo_system_t *ctx, demo_screen_mode_t mode, int frame)
{
    if (!ctx)
    {
        return;
    }
    ctx->screen_mode = mode;
    ctx->state = DEMO_STATE_TITLE;
    ctx->finished = 0;
    ctx->current_frame = frame;
    ctx->end_frame = 0;
    ctx->preview_level = 0;
    ctx->sound.name = NULL;
    ctx->sound.volume = 0;
    sparkle_init(ctx, 10);
}

int demo_system_update(demo_system_t *ctx, int frame)
{
    if (!ctx || ctx->finished)
    {
        return 0;
    }

    ctx->current_frame = frame;
    ctx->sound.name = NULL;
    ctx->sound.volume = 0;

    switch (ctx->state)
    {
        case DEMO_STATE_TITLE:
            do_title(ctx);
            break;
        case DEMO_STATE_BLOCKS:
            do_blocks(ctx);
            break;
        case DEMO_STATE_TEXT:
            do_text(ctx);
            break;
        case DEMO_STATE_SPARKLE:
            do_sparkle(ctx);
            break;
        case DEMO_STATE_WAIT:
            if (frame >= ctx->wait_frame)
            {
                ctx->state = ctx->wait_target;
            }
            break;
        case DEMO_STATE_FINISH:
            do_finish(ctx);
            break;
        case DEMO_STATE_NONE:
            break;
    }

    return ctx->finished ? 0 : 1;
}

demo_state_t demo_system_get_state(const demo_system_t *ctx)
{
    if (!ctx)
    {
        return DEMO_STATE_NONE;
    }
    return ctx->state;
}

demo_screen_mode_t demo_system_get_mode(const demo_system_t *ctx)
{
    if (!ctx)
    {
        return DEMO_MODE_DEMO;
    }
    return ctx->screen_mode;
}

int demo_system_is_finished(const demo_system_t *ctx)
{
    if (!ctx)
    {
        return 1;
    }
    return ctx->finished;
}

int demo_system_get_ball_trail(const demo_system_t *ctx, const demo_ball_pos_t **out)
{
    if (!ctx || !out)
    {
        return 0;
    }
    *out = ball_trail;
    return DEMO_BALL_TRAIL_COUNT;
}

int demo_system_get_demo_text(const demo_system_t *ctx, const demo_text_line_t **out)
{
    if (!ctx || !out)
    {
        return 0;
    }
    *out = demo_text;
    return DEMO_TEXT_LINES;
}

int demo_system_get_preview_level(const demo_system_t *ctx)
{
    if (!ctx)
    {
        return 0;
    }
    return ctx->preview_level;
}

void demo_system_get_sparkle_info(const demo_system_t *ctx, demo_sparkle_info_t *out)
{
    if (!ctx || !out)
    {
        return;
    }
    out->x = ctx->sparkle_x;
    out->y = ctx->sparkle_y;
    out->frame_index = ctx->sparkle_frame;
    out->active = (ctx->state == DEMO_STATE_SPARKLE && !ctx->finished) ? 1 : 0;
    out->save_bg = ctx->sparkle_bg_saved;
    out->restore_bg = ctx->sparkle_restore;
}

demo_sound_t demo_system_get_sound(const demo_system_t *ctx)
{
    demo_sound_t none = {NULL, 0};
    if (!ctx)
    {
        return none;
    }
    return ctx->sound;
}

int demo_system_should_draw_specials(const demo_system_t *ctx)
{
    if (!ctx)
    {
        return 0;
    }
    int in_loop = (ctx->state == DEMO_STATE_SPARKLE) ||
                  (ctx->state == DEMO_STATE_WAIT && ctx->screen_mode == DEMO_MODE_PREVIEW);
    if (!in_loop)
    {
        return 0;
    }
    return (ctx->current_frame % DEMO_FLASH) == 0 ? 1 : 0;
}
