/*
 * intro_system.c — Pure C intro and instructions screen sequencer.
 *
 * See intro_system.h for module overview.
 */

#include "intro_system.h"

#include <stdlib.h>

/* =========================================================================
 * Static block description table (matches legacy DoBlocks)
 * ========================================================================= */

static const intro_block_entry_t block_table[INTRO_BLOCK_TOTAL] = {
    /* Left column (x=40) */
    {INTRO_BLK_RED, 40, 120, 0, 0, "- Normal block"},
    {INTRO_BLK_ROAMER, 40, 160, 6, 0, "- Roamer dude"},
    {INTRO_BLK_PAD_EXPAND, 40, 200, 0, 0, "- Paddle sizer"},
    {INTRO_BLK_BONUSX2, 40, 240, 5, 0, "- x2 scores"},
    {INTRO_BLK_MAXAMMO, 40, 280, 0, 0, "- Full Ammo"},
    {INTRO_BLK_DROP, 40, 320, 0, 0, "- Dropper"},
    {INTRO_BLK_BULLET, 40, 360, 0, 0, "- Ammunition"},
    {INTRO_BLK_HYPERSPACE, 40, 400, 5, -5, "- Teleport"},
    {INTRO_BLK_REVERSE, 40, 440, 5, 5, "- Reverse Control"},
    {INTRO_BLK_MGUN, 40, 480, 5, 5, "- Machine Gun"},
    {INTRO_BLK_MULTIBALL, 40, 520, 0, 0, "- Multi Balls"},
    /* Right column (x=260) */
    {INTRO_BLK_BONUS, 260, 120, 5, 0, "- 3000 points"},
    {INTRO_BLK_COUNTER, 260, 160, 0, 0, "- 200 points"},
    {INTRO_BLK_TIMER, 260, 200, 10, 0, "- Extra Time"},
    {INTRO_BLK_BLACK, 260, 240, 0, 0, "- Solid wall"},
    {INTRO_BLK_BOMB, 260, 280, 9, 0, "- Bomb!"},
    {INTRO_BLK_PADDLE, 260, 320, 20, 0, "- The Paddle"},
    {INTRO_BLK_BULLET_ITEM, 260, 360, 20, 5, "- Bullet"},
    {INTRO_BLK_DEATH, 260, 400, 8, 0, "- Instant Death!"},
    {INTRO_BLK_EXTRABALL, 260, 440, 10, 0, "- Extra Ball"},
    {INTRO_BLK_WALLOFF, 260, 480, 10, 0, "- Walls Off"},
    {INTRO_BLK_STICKY, 260, 520, 10, -5, "- Sticky Ball"},
};

/* =========================================================================
 * Static instruction text (matches legacy instructionText[])
 * ========================================================================= */

static const intro_instruct_line_t instruct_lines[INSTRUCT_TEXT_LINES] = {
    {"XBoing is a blockout game where you use a paddle to bounce", 0, 0},
    {"a proton ball around an arena full of nasties while keeping", 0, 1},
    {"the ball from leaving the arena via the bottom rebound wall.", 0, 0},
    {NULL, 1, 0},
    {"Each block has a point value associated with it. Some blocks", 0, 1},
    {"may award you more points for hitting them a number of times.", 0, 0},
    {"Some blocks may toggle extra time/points or even special effects", 0, 1},
    {"such as no walls, machine gun, sticky paddle, reverse controls, etc.", 0, 0},
    {NULL, 1, 0},
    {"Your paddle is equipped with special bullets that can disintegrate", 0, 1},
    {"a block. You only have a limited supply of bullets so use them wisely.", 0, 0},
    {NULL, 1, 0},
    {"The multiple ball block will give you an extra ball to play with in", 0, 1},
    {"the arena. This ball will act like any other ball except that when", 0, 0},
    {"it dies it will not force a new ball to start. You can shoot your", 0, 1},
    {"own ball so watch out. The death symbol is not too healthy either.", 0, 0},
    {NULL, 1, 0},
    {"Sometimes a special block may appear or be added to another block", 0, 1},
    {"that will effect the gameplay if hit. They also disappear randomly.", 0, 0},
    {NULL, 1, 0},
};

/* =========================================================================
 * Internal state
 * ========================================================================= */

/* FLASH constant from legacy main.h — specials redraw interval. */
#define INTRO_FLASH 30

struct intro_system
{
    intro_screen_mode_t screen_mode;
    intro_state_t state;
    int finished;

    int start_frame;
    int current_frame;
    int end_frame;

    /* Sparkle state. */
    int sparkle_x;
    int sparkle_y;
    int sparkle_frame;
    int sparkle_started;
    int sparkle_next_frame;
    int sparkle_bg_saved;
    int sparkle_restore;

    /* Blink state (intro only). */
    int next_blink;
    int blink_active;

    /* Sound for current frame. */
    intro_sound_t sound;

    /* Callbacks. */
    intro_system_callbacks_t callbacks;
    void *user_data;
    intro_rand_fn rand_fn;
};

/* =========================================================================
 * Sparkle
 * ========================================================================= */

static int get_rand(intro_system_t *ctx)
{
    if (ctx->rand_fn)
    {
        return ctx->rand_fn(ctx->user_data);
    }
    return rand();
}

static void sparkle_init(intro_system_t *ctx, int initial_delay)
{
    ctx->sparkle_x = 100;
    ctx->sparkle_y = 20;
    ctx->sparkle_frame = 0;
    ctx->sparkle_started = 0;
    ctx->sparkle_next_frame = ctx->current_frame + initial_delay;
    ctx->sparkle_bg_saved = 0;
    ctx->sparkle_restore = 0;
}

static void sparkle_update(intro_system_t *ctx)
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
        ctx->sparkle_next_frame = ctx->current_frame + INTRO_SPARKLE_STEP;

        if (ctx->sparkle_frame >= INTRO_SPARKLE_FRAMES)
        {
            ctx->sparkle_frame = 0;
            ctx->sparkle_next_frame = ctx->current_frame + INTRO_SPARKLE_PAUSE;
            ctx->sparkle_x = (get_rand(ctx) % INTRO_TITLE_WIDTH) + 5;
            ctx->sparkle_y = (get_rand(ctx) % INTRO_TITLE_HEIGHT) + 5;
            ctx->sparkle_bg_saved = 1;
        }
    }
}

/* =========================================================================
 * Blink
 * ========================================================================= */

static void blink_update(intro_system_t *ctx)
{
    ctx->blink_active = 0;
    if (ctx->current_frame >= ctx->next_blink)
    {
        ctx->blink_active = 1;
        /* Advance to next blink cycle.  Legacy alternates between
         * BLINK_RATE (during animation) and BLINK_GAP (between blinks).
         * We emit one pulse per gap; integration layer drives the
         * multi-frame blink animation. */
        ctx->next_blink = ctx->current_frame + INTRO_BLINK_GAP;
    }
}

/* =========================================================================
 * State handlers
 * ========================================================================= */

static void do_title(intro_system_t *ctx)
{
    /* Title is drawn once.  Advance to next state immediately. */
    if (ctx->screen_mode == INTRO_MODE_INTRO)
    {
        ctx->state = INTRO_STATE_BLOCKS;
    }
    else
    {
        ctx->state = INTRO_STATE_TEXT;
    }
}

static void do_blocks(intro_system_t *ctx)
{
    /* Block descriptions drawn once.  Advance to text. */
    ctx->state = INTRO_STATE_TEXT;
}

static void do_text(intro_system_t *ctx)
{
    /* Text drawn once.  Advance to sparkle loop.
     * Sparkle state was initialized in begin(), matching legacy where
     * Reset*() sets up the frame counters before the state machine runs. */
    ctx->state = INTRO_STATE_SPARKLE;
}

static void do_sparkle(intro_system_t *ctx)
{
    /* Check end of sequence. */
    if (ctx->current_frame >= ctx->end_frame)
    {
        ctx->state = INTRO_STATE_FINISH;
        return;
    }

    sparkle_update(ctx);

    /* Blink only in intro mode. */
    if (ctx->screen_mode == INTRO_MODE_INTRO)
    {
        blink_update(ctx);
    }
}

static void do_finish(intro_system_t *ctx)
{
    ctx->finished = 1;

    if (ctx->screen_mode == INTRO_MODE_INSTRUCT)
    {
        ctx->sound.name = "shark";
        ctx->sound.volume = 50;
    }
    else
    {
        ctx->sound.name = "whoosh";
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

intro_system_t *intro_system_create(const intro_system_callbacks_t *callbacks, void *user_data,
                                    intro_rand_fn rand_fn)
{
    intro_system_t *ctx = calloc(1, sizeof(*ctx));
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
    ctx->state = INTRO_STATE_NONE;
    return ctx;
}

void intro_system_destroy(intro_system_t *ctx)
{
    free(ctx);
}

void intro_system_begin(intro_system_t *ctx, intro_screen_mode_t mode, int frame)
{
    if (!ctx)
    {
        return;
    }
    ctx->screen_mode = mode;
    ctx->state = INTRO_STATE_TITLE;
    ctx->finished = 0;
    ctx->start_frame = frame;
    ctx->current_frame = frame;

    if (mode == INTRO_MODE_INTRO)
    {
        ctx->end_frame = frame + INTRO_END_FRAME_OFFSET;
    }
    else
    {
        ctx->end_frame = frame + INSTRUCT_END_FRAME_OFFSET;
    }

    ctx->next_blink = frame + 10;
    ctx->blink_active = 0;
    ctx->sound.name = NULL;
    ctx->sound.volume = 0;

    /* Legacy intro uses startFrame = frame+10; instructions uses
     * nextFrame = frame+100. */
    int sparkle_delay = (mode == INTRO_MODE_INTRO) ? 10 : 100;
    sparkle_init(ctx, sparkle_delay);
}

int intro_system_update(intro_system_t *ctx, int frame)
{
    if (!ctx || ctx->finished)
    {
        return 0;
    }

    ctx->current_frame = frame;
    ctx->sound.name = NULL;
    ctx->sound.volume = 0;
    ctx->blink_active = 0;

    switch (ctx->state)
    {
        case INTRO_STATE_TITLE:
            do_title(ctx);
            break;
        case INTRO_STATE_BLOCKS:
            do_blocks(ctx);
            break;
        case INTRO_STATE_TEXT:
            do_text(ctx);
            break;
        case INTRO_STATE_SPARKLE:
            do_sparkle(ctx);
            break;
        case INTRO_STATE_FINISH:
            do_finish(ctx);
            break;
        case INTRO_STATE_WAIT:
        case INTRO_STATE_NONE:
            break;
    }

    return ctx->finished ? 0 : 1;
}

intro_state_t intro_system_get_state(const intro_system_t *ctx)
{
    if (!ctx)
    {
        return INTRO_STATE_NONE;
    }
    return ctx->state;
}

intro_screen_mode_t intro_system_get_mode(const intro_system_t *ctx)
{
    if (!ctx)
    {
        return INTRO_MODE_INTRO;
    }
    return ctx->screen_mode;
}

int intro_system_is_finished(const intro_system_t *ctx)
{
    if (!ctx)
    {
        return 1;
    }
    return ctx->finished;
}

int intro_system_get_block_table(const intro_system_t *ctx, const intro_block_entry_t **out)
{
    if (!ctx || !out)
    {
        return 0;
    }
    *out = block_table;
    return INTRO_BLOCK_TOTAL;
}

int intro_system_get_instruct_text(const intro_system_t *ctx, const intro_instruct_line_t **out)
{
    if (!ctx || !out)
    {
        return 0;
    }
    *out = instruct_lines;
    return INSTRUCT_TEXT_LINES;
}

void intro_system_get_sparkle_info(const intro_system_t *ctx, intro_sparkle_info_t *out)
{
    if (!ctx || !out)
    {
        return;
    }
    out->x = ctx->sparkle_x;
    out->y = ctx->sparkle_y;
    out->frame_index = ctx->sparkle_frame;
    out->active = (ctx->state == INTRO_STATE_SPARKLE && !ctx->finished) ? 1 : 0;
    out->save_bg = ctx->sparkle_bg_saved;
    out->restore_bg = ctx->sparkle_restore;
}

intro_sound_t intro_system_get_sound(const intro_system_t *ctx)
{
    intro_sound_t none = {NULL, 0};
    if (!ctx)
    {
        return none;
    }
    return ctx->sound;
}

int intro_system_should_blink(const intro_system_t *ctx)
{
    if (!ctx)
    {
        return 0;
    }
    return ctx->blink_active;
}

int intro_system_should_draw_specials(const intro_system_t *ctx)
{
    if (!ctx || ctx->state != INTRO_STATE_SPARKLE)
    {
        return 0;
    }
    return (ctx->current_frame % INTRO_FLASH) == 0 ? 1 : 0;
}
