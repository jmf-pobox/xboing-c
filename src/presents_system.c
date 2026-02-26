/*
 * presents_system.c — Pure C presents/splash screen sequencer.
 *
 * See presents_system.h for module overview.
 */

#include "presents_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Letter width table (legacy dists[])
 * ========================================================================= */

static const int letter_widths[PRESENTS_TITLE_LETTERS] = {
    PRESENTS_LETTER_X_W, PRESENTS_LETTER_B_W, PRESENTS_LETTER_O_W,
    PRESENTS_LETTER_I_W, PRESENTS_LETTER_N_W, PRESENTS_LETTER_G_W,
};

/* =========================================================================
 * Internal state
 * ========================================================================= */

struct presents_system
{
    presents_state_t state;
    int finished;

    /* Wait mechanism: state returns to wait_target after wait_frame. */
    presents_state_t wait_target;
    int wait_frame;

    /* Global frame at begin(). */
    int start_frame;
    int current_frame;
    int next_frame; /* throttle for multi-frame states */

    /* FLAG state positions. */
    presents_flag_info_t flag_info;

    /* LETTERS state. */
    int letter_index; /* 0..5 during stamping, 6 = "II" drawn */
    int letter_x;
    int letter_y;
    int ii_drawn; /* 1 after the "II" pair is stamped */

    /* SHINE (sparkle) state. */
    int sparkle_frame; /* 0..PRESENTS_SPARKLE_FRAMES-1 */
    int sparkle_x;
    int sparkle_y;
    int sparkle_started; /* 1 after first frame */
    int sparkle_done;    /* 1 when all frames shown */
    int sparkle_wait_frame;

    /* Typewriter state (3 lines). */
    char typewriter_text[PRESENTS_TYPEWRITER_LINES][128];
    int typewriter_y[PRESENTS_TYPEWRITER_LINES];
    int typewriter_len[PRESENTS_TYPEWRITER_LINES];
    int typewriter_chars[PRESENTS_TYPEWRITER_LINES]; /* chars revealed */
    int typewriter_active_line;                      /* 0, 1, or 2 */
    int typewriter_first_entry;

    /* CLEAR (wipe) state. */
    int wipe_top_y;
    int wipe_bottom_y;
    int wipe_first;

    /* Sound event for current frame. */
    presents_sound_t sound;

    /* Callbacks. */
    presents_system_callbacks_t callbacks;
    void *user_data;
};

/* =========================================================================
 * Wait mechanism
 * ========================================================================= */

static void set_wait(presents_system_t *ctx, presents_state_t target, int frame)
{
    ctx->wait_target = target;
    ctx->wait_frame = frame;
    ctx->state = PRESENTS_STATE_WAIT;
}

/* =========================================================================
 * FLAG state
 * ========================================================================= */

static void do_flag(presents_system_t *ctx)
{
    int center_x = PRESENTS_TOTAL_WIDTH / 2;

    ctx->flag_info.flag_x = center_x - 35;
    ctx->flag_info.flag_y = 15;
    ctx->flag_info.earth_x = center_x - 200;
    ctx->flag_info.earth_y = 93;
    ctx->flag_info.copyright_y = PRESENTS_TOTAL_HEIGHT - 20;

    ctx->sound.name = "intro";
    ctx->sound.volume = 40;

    set_wait(ctx, PRESENTS_STATE_TEXT1, ctx->current_frame + PRESENTS_FLAG_DELAY);
}

/* =========================================================================
 * TEXT1 — "Justin" bitmap
 * ========================================================================= */

static void do_text1(presents_system_t *ctx)
{
    /* Integration layer draws justin bitmap at computed position. */
    set_wait(ctx, PRESENTS_STATE_TEXT2, ctx->current_frame + PRESENTS_TEXT1_DELAY);
}

/* =========================================================================
 * TEXT2 — "Kibell" bitmap
 * ========================================================================= */

static void do_text2(presents_system_t *ctx)
{
    set_wait(ctx, PRESENTS_STATE_TEXT3, ctx->current_frame + PRESENTS_TEXT2_DELAY);
}

/* =========================================================================
 * TEXT3 — "Presents" bitmap (replaces justin/kibell after fade)
 * ========================================================================= */

static void do_text3(presents_system_t *ctx)
{
    set_wait(ctx, PRESENTS_STATE_TEXT_CLEAR, ctx->current_frame + PRESENTS_TEXT3_DELAY);
}

/* =========================================================================
 * TEXT_CLEAR — fade away the "presents" text
 * ========================================================================= */

static void do_text_clear(presents_system_t *ctx)
{
    /* Reset letter stamping state. */
    ctx->letter_index = 0;
    ctx->letter_x = 40;
    ctx->letter_y = 220;

    set_wait(ctx, PRESENTS_STATE_LETTERS, ctx->current_frame + PRESENTS_TEXT_CLEAR_DELAY);
}

/* =========================================================================
 * LETTERS — stamp X, B, O, I, N, G one at a time, then "II"
 * ========================================================================= */

static void do_letters(presents_system_t *ctx)
{
    if (ctx->letter_index < PRESENTS_TITLE_LETTERS)
    {
        ctx->sound.name = "stamp";
        ctx->sound.volume = 90;

        /* Advance x for next letter. */
        int w = letter_widths[ctx->letter_index];
        /* letter_x is the position for THIS letter; advance after. */
        ctx->letter_index++;
        if (ctx->letter_index < PRESENTS_TITLE_LETTERS)
        {
            ctx->letter_x += PRESENTS_GAP + w;
        }

        set_wait(ctx, PRESENTS_STATE_LETTERS, ctx->current_frame + PRESENTS_LETTER_DELAY);
    }
    else
    {
        /* Draw the "II" pair below the XBOING letters. */
        ctx->sound.name = "stamp";
        ctx->sound.volume = 90;
        ctx->ii_drawn = 1;

        /* Reset sparkle state for SHINE. */
        ctx->sparkle_frame = 0;
        ctx->sparkle_started = 0;
        ctx->sparkle_done = 0;
        ctx->sparkle_x = PRESENTS_TOTAL_WIDTH - 50;
        ctx->sparkle_y = 212;

        set_wait(ctx, PRESENTS_STATE_SHINE, ctx->current_frame + PRESENTS_AFTER_II_DELAY);
    }
}

/* =========================================================================
 * SHINE — sparkle animation (11 star frames)
 * ========================================================================= */

static void do_sparkle(presents_system_t *ctx)
{
    if (!ctx->sparkle_started)
    {
        ctx->sparkle_started = 1;
        ctx->sparkle_wait_frame = ctx->current_frame;
        ctx->sound.name = "ping";
        ctx->sound.volume = 70;
    }

    if (ctx->current_frame == ctx->sparkle_wait_frame)
    {
        ctx->sparkle_frame++;
        ctx->sparkle_wait_frame = ctx->current_frame + PRESENTS_SPARKLE_STEP;

        if (ctx->sparkle_frame > PRESENTS_SPARKLE_FRAMES)
        {
            ctx->sparkle_done = 1;
            /* Prepare typewriter state. */
            ctx->typewriter_active_line = 0;
            ctx->typewriter_first_entry = 1;
            set_wait(ctx, PRESENTS_STATE_SPECIAL_TEXT1,
                     ctx->current_frame + PRESENTS_AFTER_SPARKLE_DELAY);
        }
    }
}

/* =========================================================================
 * Typewriter helpers
 * ========================================================================= */

static void init_typewriter_texts(presents_system_t *ctx)
{
    const char *nick = NULL;
    const char *full = NULL;

    if (ctx->callbacks.get_nickname)
    {
        nick = ctx->callbacks.get_nickname(ctx->user_data);
    }
    if (nick == NULL && ctx->callbacks.get_fullname)
    {
        full = ctx->callbacks.get_fullname(ctx->user_data);
    }

    if (nick != NULL)
    {
        snprintf(ctx->typewriter_text[0], sizeof(ctx->typewriter_text[0]),
                 "Welcome %s, prepare for battle.", nick);
    }
    else if (full != NULL)
    {
        snprintf(ctx->typewriter_text[0], sizeof(ctx->typewriter_text[0]),
                 "Welcome %s, prepare for battle.", full);
    }
    else
    {
        snprintf(ctx->typewriter_text[0], sizeof(ctx->typewriter_text[0]),
                 "Welcome, prepare for battle.");
    }

    strncpy(ctx->typewriter_text[1], "The future of the planet Earth is in your hands!",
            sizeof(ctx->typewriter_text[1]) - 1);
    ctx->typewriter_text[1][sizeof(ctx->typewriter_text[1]) - 1] = '\0';

    strncpy(ctx->typewriter_text[2], "More instructions will follow within game zone - out.",
            sizeof(ctx->typewriter_text[2]) - 1);
    ctx->typewriter_text[2][sizeof(ctx->typewriter_text[2]) - 1] = '\0';

    for (int i = 0; i < PRESENTS_TYPEWRITER_LINES; i++)
    {
        ctx->typewriter_len[i] = (int)strlen(ctx->typewriter_text[i]);
        ctx->typewriter_chars[i] = 0;
        /* y positions: legacy uses 550, then 550 + line_height + 5, etc.
         * We store base offsets; integration layer computes actual y from
         * font metrics.  Use 550 as base y. */
        ctx->typewriter_y[i] = 550;
    }
}

static void do_typewriter(presents_system_t *ctx, int line, presents_state_t next_state,
                          int after_delay)
{
    if (ctx->typewriter_first_entry)
    {
        if (line == 0)
        {
            init_typewriter_texts(ctx);
        }
        ctx->typewriter_chars[line] = 0;
        ctx->next_frame = ctx->current_frame + 10;
        ctx->typewriter_first_entry = 0;
        ctx->typewriter_active_line = line;
    }

    if (ctx->current_frame >= ctx->next_frame)
    {
        ctx->sound.name = "key";
        ctx->sound.volume = 60;

        ctx->typewriter_chars[line]++;
        ctx->next_frame = ctx->current_frame + PRESENTS_TYPEWRITER_CHAR_DELAY;

        if (ctx->typewriter_chars[line] > ctx->typewriter_len[line])
        {
            ctx->typewriter_first_entry = 1;
            set_wait(ctx, next_state, ctx->current_frame + after_delay);
        }
    }
}

/* =========================================================================
 * SPECIAL_TEXT1/2/3 — typewriter welcome messages
 * ========================================================================= */

static void do_special_text1(presents_system_t *ctx)
{
    do_typewriter(ctx, 0, PRESENTS_STATE_SPECIAL_TEXT2, PRESENTS_AFTER_LINE_DELAY);
}

static void do_special_text2(presents_system_t *ctx)
{
    do_typewriter(ctx, 1, PRESENTS_STATE_SPECIAL_TEXT3, PRESENTS_AFTER_LINE_DELAY);
}

static void do_special_text3(presents_system_t *ctx)
{
    do_typewriter(ctx, 2, PRESENTS_STATE_CLEAR, PRESENTS_AFTER_LAST_LINE_DELAY);
}

/* =========================================================================
 * CLEAR — curtain wipe from top and bottom toward center
 * ========================================================================= */

static void do_clear(presents_system_t *ctx)
{
    if (ctx->wipe_first)
    {
        ctx->wipe_top_y = 0;
        ctx->wipe_bottom_y = PRESENTS_TOTAL_HEIGHT - PRESENTS_WIPE_STEP;
        ctx->wipe_first = 0;
        ctx->next_frame = ctx->current_frame;

        ctx->sound.name = "whoosh";
        ctx->sound.volume = 70;
    }

    if (ctx->current_frame >= ctx->next_frame)
    {
        ctx->wipe_top_y += PRESENTS_WIPE_STEP;
        ctx->wipe_bottom_y -= PRESENTS_WIPE_STEP;

        if (ctx->wipe_top_y > PRESENTS_TOTAL_HEIGHT / 2)
        {
            set_wait(ctx, PRESENTS_STATE_FINISH, ctx->current_frame + PRESENTS_WIPE_DELAY);
        }

        ctx->next_frame = ctx->current_frame + PRESENTS_WIPE_DELAY;
    }
}

/* =========================================================================
 * FINISH
 * ========================================================================= */

static void do_finish(presents_system_t *ctx)
{
    ctx->finished = 1;
    if (ctx->callbacks.on_finished)
    {
        ctx->callbacks.on_finished(ctx->user_data);
    }
}

/* =========================================================================
 * Public API
 * ========================================================================= */

presents_system_t *presents_system_create(const presents_system_callbacks_t *callbacks,
                                          void *user_data)
{
    presents_system_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
    {
        return NULL;
    }
    if (callbacks)
    {
        ctx->callbacks = *callbacks;
    }
    ctx->user_data = user_data;
    ctx->state = PRESENTS_STATE_NONE;
    return ctx;
}

void presents_system_destroy(presents_system_t *ctx)
{
    free(ctx);
}

void presents_system_begin(presents_system_t *ctx, int frame)
{
    if (!ctx)
    {
        return;
    }
    ctx->state = PRESENTS_STATE_FLAG;
    ctx->finished = 0;
    ctx->start_frame = frame;
    ctx->current_frame = frame;
    ctx->next_frame = frame;

    /* Reset all sub-state. */
    ctx->letter_index = 0;
    ctx->letter_x = 40;
    ctx->letter_y = 220;
    ctx->ii_drawn = 0;
    ctx->sparkle_frame = 0;
    ctx->sparkle_started = 0;
    ctx->sparkle_done = 0;
    ctx->typewriter_active_line = -1;
    ctx->typewriter_first_entry = 1;
    ctx->wipe_first = 1;
    ctx->wipe_top_y = 0;
    ctx->wipe_bottom_y = PRESENTS_TOTAL_HEIGHT - PRESENTS_WIPE_STEP;

    memset(&ctx->sound, 0, sizeof(ctx->sound));
}

int presents_system_update(presents_system_t *ctx, int frame)
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
        case PRESENTS_STATE_FLAG:
            do_flag(ctx);
            break;
        case PRESENTS_STATE_TEXT1:
            do_text1(ctx);
            break;
        case PRESENTS_STATE_TEXT2:
            do_text2(ctx);
            break;
        case PRESENTS_STATE_TEXT3:
            do_text3(ctx);
            break;
        case PRESENTS_STATE_TEXT_CLEAR:
            do_text_clear(ctx);
            break;
        case PRESENTS_STATE_LETTERS:
            do_letters(ctx);
            break;
        case PRESENTS_STATE_SHINE:
            do_sparkle(ctx);
            break;
        case PRESENTS_STATE_SPECIAL_TEXT1:
            do_special_text1(ctx);
            break;
        case PRESENTS_STATE_SPECIAL_TEXT2:
            do_special_text2(ctx);
            break;
        case PRESENTS_STATE_SPECIAL_TEXT3:
            do_special_text3(ctx);
            break;
        case PRESENTS_STATE_CLEAR:
            do_clear(ctx);
            break;
        case PRESENTS_STATE_FINISH:
            do_finish(ctx);
            break;
        case PRESENTS_STATE_WAIT:
            if (frame >= ctx->wait_frame)
            {
                ctx->state = ctx->wait_target;
            }
            break;
        case PRESENTS_STATE_NONE:
            break;
    }

    return ctx->finished ? 0 : 1;
}

void presents_system_skip(presents_system_t *ctx, int frame)
{
    if (!ctx || ctx->finished)
    {
        return;
    }
    ctx->current_frame = frame;
    set_wait(ctx, PRESENTS_STATE_FINISH, frame);
}

presents_state_t presents_system_get_state(const presents_system_t *ctx)
{
    if (!ctx)
    {
        return PRESENTS_STATE_NONE;
    }
    return ctx->state;
}

int presents_system_is_finished(const presents_system_t *ctx)
{
    if (!ctx)
    {
        return 1;
    }
    return ctx->finished;
}

void presents_system_get_flag_info(const presents_system_t *ctx, presents_flag_info_t *out)
{
    if (!ctx || !out)
    {
        return;
    }
    *out = ctx->flag_info;
}

int presents_system_get_letter_info(const presents_system_t *ctx, presents_letter_info_t *out)
{
    if (!ctx || !out)
    {
        return 0;
    }
    /* Return info about the most recently stamped letter.
     * letter_index tracks how many have been drawn (1..6 after stamping). */
    if (ctx->letter_index == 0)
    {
        return 0;
    }

    int idx = ctx->letter_index - 1;
    if (idx >= PRESENTS_TITLE_LETTERS)
    {
        return 0;
    }

    /* Reconstruct position by replaying x offsets. */
    int x = 40;
    for (int i = 0; i < idx; i++)
    {
        x += PRESENTS_GAP + letter_widths[i];
    }
    out->letter_index = idx;
    out->x = x;
    out->y = ctx->letter_y;
    out->width = letter_widths[idx];
    out->height = PRESENTS_LETTER_HEIGHT;
    return 1;
}

int presents_system_get_ii_info(const presents_system_t *ctx, presents_ii_info_t *out)
{
    if (!ctx || !out || !ctx->ii_drawn)
    {
        return 0;
    }
    int y = ctx->letter_y + 110;
    int center_x = PRESENTS_TOTAL_WIDTH / 2;
    out->i1_x = center_x - PRESENTS_LETTER_I_W;
    out->i2_x = center_x;
    out->y = y;
    out->width = PRESENTS_LETTER_I_W;
    out->height = PRESENTS_LETTER_HEIGHT;
    return 1;
}

void presents_system_get_sparkle_info(const presents_system_t *ctx, presents_sparkle_info_t *out)
{
    if (!ctx || !out)
    {
        return;
    }
    out->x = ctx->sparkle_x;
    out->y = ctx->sparkle_y;
    out->active = ctx->sparkle_started && !ctx->sparkle_done;
    out->frame_index = ctx->sparkle_frame > 0 ? ctx->sparkle_frame - 1 : 0;
    out->first_frame = (ctx->sparkle_frame == 1 && ctx->sparkle_started) ? 1 : 0;
    out->restore = ctx->sparkle_done ? 1 : 0;
}

int presents_system_get_typewriter_info(const presents_system_t *ctx, int line_index,
                                        presents_typewriter_info_t *out)
{
    if (!ctx || !out || line_index < 0 || line_index >= PRESENTS_TYPEWRITER_LINES)
    {
        return 0;
    }
    if (ctx->typewriter_chars[line_index] == 0)
    {
        return 0;
    }
    out->text = ctx->typewriter_text[line_index];
    out->y = ctx->typewriter_y[line_index];
    out->x_offset = 0; /* Integration layer computes from font metrics. */
    out->chars_visible = ctx->typewriter_chars[line_index];
    if (out->chars_visible > ctx->typewriter_len[line_index])
    {
        out->chars_visible = ctx->typewriter_len[line_index];
    }
    out->complete = (ctx->typewriter_chars[line_index] >= ctx->typewriter_len[line_index]) ? 1 : 0;
    return 1;
}

void presents_system_get_wipe_info(const presents_system_t *ctx, presents_wipe_info_t *out)
{
    if (!ctx || !out)
    {
        return;
    }
    out->top_y = ctx->wipe_top_y;
    out->bottom_y = ctx->wipe_bottom_y;
    out->total_width = PRESENTS_TOTAL_WIDTH;
    out->complete = (ctx->wipe_top_y > PRESENTS_TOTAL_HEIGHT / 2) ? 1 : 0;
}

presents_sound_t presents_system_get_sound(const presents_system_t *ctx)
{
    presents_sound_t none = {NULL, 0};
    if (!ctx)
    {
        return none;
    }
    return ctx->sound;
}

int presents_system_get_active_typewriter_line(const presents_system_t *ctx)
{
    if (!ctx)
    {
        return -1;
    }
    return ctx->typewriter_active_line;
}
