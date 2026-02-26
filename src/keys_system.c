/*
 * keys_system.c — Pure C keys and level editor controls screen sequencer.
 *
 * See keys_system.h for module overview.
 */

#include "keys_system.h"

#include <stdlib.h>

/* =========================================================================
 * Static data tables
 * ========================================================================= */

/* Game key bindings from legacy keys.c DoText().
 * Left column (x=30), right column (x=280). */
static const keys_binding_entry_t game_bindings[KEYS_GAME_BINDINGS_COUNT] = {
    /* Left column */
    {"<s> = Sfx On/Off", 0},
    {"<P> = Pause/Resume", 0},
    {"<I> = Iconify Quickly", 0},
    {"<h> = Roll of Honour", 0},
    {"<H> = Personal scores", 0},
    {"<d> = Kill Ball", 0},
    {"<q> = Quit XBoing", 0},
    {"<+/-> = Inc/Dec Volume", 0},
    {"<z/x> = Save/Load game", 0},
    {"<w> = Set Starting level", 0},
    /* Right column */
    {"<j> = Paddle left", 1},
    {"<k> = Shoot", 1},
    {"<l> = Paddle right", 1},
    {"<a> = Audio On/Off", 1},
    {"<c> = Cycle intros", 1},
    {"<g> = Toggle control", 1},
    {"<1-9> = Game speed", 1},
    {"<t> = Tilt board", 1},
    {"<e> = Level Editor", 1},
    {"<?> = spare", 1},
};

/* Editor info text from legacy keysedit.c infoText[]. */
static const keys_info_line_t editor_info[KEYS_EDITOR_INFO_COUNT] = {
    {"The level editor will allow you to edit any of the levels and"},
    {"create your own ones. You use the left button for drawing blocks"},
    {"and the middle button for erasing. Below lists the keys that can"},
    {"be used while in the level editor."},
    {"The bottom four rows are reserved so that your paddle and ball"},
    {"has room to start. Design your levels so that it is possible"},
    {"to finish. Remember that random specials will appear."},
};

/* Editor key bindings from legacy keysedit.c DoText().
 * Left column (x=30), right column (x=270). */
static const keys_binding_entry_t editor_bindings[KEYS_EDITOR_BINDINGS_COUNT] = {
    /* Left column */
    {"<r> = Redraw level", 0},
    {"<c> = Clear Level", 0},
    {"<q> = Quit editor", 0},
    {"<h/v> = Flip horz/vert", 0},
    {"<H/V> = Scroll horz/vert", 0},
    /* Right column */
    {"<s> = Save level", 1},
    {"<l> = load level", 1},
    {"<t> = Set time limit", 1},
    {"<n> = Change level name", 1},
    {"<p> = Play test", 1},
};

/* =========================================================================
 * Internal state
 * ========================================================================= */

struct keys_system
{
    keys_screen_mode_t screen_mode;
    keys_state_t state;
    int finished;

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

    /* Blink state (game mode only). */
    int next_blink;
    int blink_active;

    /* Wait state. */
    int wait_frame;
    keys_state_t wait_target;

    /* Sound. */
    keys_sound_t sound;

    /* Callbacks. */
    keys_system_callbacks_t callbacks;
    void *user_data;
    keys_rand_fn rand_fn;
};

static int get_rand(keys_system_t *ctx)
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

static void sparkle_init(keys_system_t *ctx, int delay)
{
    ctx->sparkle_x = 100;
    ctx->sparkle_y = 20;
    ctx->sparkle_frame = 0;
    ctx->sparkle_started = 0;
    ctx->sparkle_next_frame = ctx->current_frame + delay;
    ctx->sparkle_bg_saved = 0;
    ctx->sparkle_restore = 0;
}

static void sparkle_update(keys_system_t *ctx)
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
        ctx->sparkle_next_frame = ctx->current_frame + KEYS_SPARKLE_STEP;

        if (ctx->sparkle_frame >= KEYS_SPARKLE_FRAMES)
        {
            ctx->sparkle_frame = 0;
            ctx->sparkle_next_frame = ctx->current_frame + KEYS_SPARKLE_PAUSE;
            ctx->sparkle_x = (get_rand(ctx) % 474) + 5;
            ctx->sparkle_y = (get_rand(ctx) % 74) + 5;
            ctx->sparkle_bg_saved = 1;
        }
    }
}

/* =========================================================================
 * Blink (game mode only)
 * ========================================================================= */

static void blink_update(keys_system_t *ctx)
{
    ctx->blink_active = 0;
    if (ctx->current_frame >= ctx->next_blink)
    {
        ctx->blink_active = 1;
        ctx->next_blink = ctx->current_frame + KEYS_BLINK_GAP;
    }
}

/* =========================================================================
 * State handlers
 * ========================================================================= */

static void do_title(keys_system_t *ctx)
{
    ctx->state = KEYS_STATE_TEXT;
}

static void do_text(keys_system_t *ctx)
{
    sparkle_init(ctx, KEYS_SPARKLE_DELAY);
    ctx->end_frame = ctx->current_frame + KEYS_END_FRAME_OFFSET;
    ctx->state = KEYS_STATE_SPARKLE;
}

static void do_sparkle(keys_system_t *ctx)
{
    if (ctx->current_frame >= ctx->end_frame)
    {
        ctx->state = KEYS_STATE_FINISH;
        return;
    }
    sparkle_update(ctx);
    if (ctx->screen_mode == KEYS_MODE_GAME)
    {
        blink_update(ctx);
    }
}

static void do_finish(keys_system_t *ctx)
{
    ctx->finished = 1;

    if (ctx->screen_mode == KEYS_MODE_GAME)
    {
        ctx->sound.name = "boing";
        ctx->sound.volume = 50;
    }
    else
    {
        ctx->sound.name = "warp";
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

keys_system_t *keys_system_create(const keys_system_callbacks_t *callbacks, void *user_data,
                                  keys_rand_fn rand_fn)
{
    keys_system_t *ctx = calloc(1, sizeof(*ctx));
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
    ctx->state = KEYS_STATE_NONE;
    return ctx;
}

void keys_system_destroy(keys_system_t *ctx)
{
    free(ctx);
}

void keys_system_begin(keys_system_t *ctx, keys_screen_mode_t mode, int frame)
{
    if (!ctx)
    {
        return;
    }
    ctx->screen_mode = mode;
    ctx->state = KEYS_STATE_TITLE;
    ctx->finished = 0;
    ctx->current_frame = frame;
    ctx->end_frame = 0;
    ctx->sound.name = NULL;
    ctx->sound.volume = 0;
    ctx->blink_active = 0;
    ctx->next_blink = frame + KEYS_BLINK_GAP;
    sparkle_init(ctx, KEYS_SPARKLE_DELAY);
}

int keys_system_update(keys_system_t *ctx, int frame)
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
        case KEYS_STATE_TITLE:
            do_title(ctx);
            break;
        case KEYS_STATE_TEXT:
            do_text(ctx);
            break;
        case KEYS_STATE_SPARKLE:
            do_sparkle(ctx);
            break;
        case KEYS_STATE_WAIT:
            if (frame >= ctx->wait_frame)
            {
                ctx->state = ctx->wait_target;
            }
            break;
        case KEYS_STATE_FINISH:
            do_finish(ctx);
            break;
        case KEYS_STATE_NONE:
            break;
    }

    return ctx->finished ? 0 : 1;
}

keys_state_t keys_system_get_state(const keys_system_t *ctx)
{
    if (!ctx)
    {
        return KEYS_STATE_NONE;
    }
    return ctx->state;
}

keys_screen_mode_t keys_system_get_mode(const keys_system_t *ctx)
{
    if (!ctx)
    {
        return KEYS_MODE_GAME;
    }
    return ctx->screen_mode;
}

int keys_system_is_finished(const keys_system_t *ctx)
{
    if (!ctx)
    {
        return 1;
    }
    return ctx->finished;
}

int keys_system_get_game_bindings(const keys_system_t *ctx, const keys_binding_entry_t **out)
{
    if (!ctx || !out)
    {
        return 0;
    }
    *out = game_bindings;
    return KEYS_GAME_BINDINGS_COUNT;
}

int keys_system_get_editor_info(const keys_system_t *ctx, const keys_info_line_t **out)
{
    if (!ctx || !out)
    {
        return 0;
    }
    *out = editor_info;
    return KEYS_EDITOR_INFO_COUNT;
}

int keys_system_get_editor_bindings(const keys_system_t *ctx, const keys_binding_entry_t **out)
{
    if (!ctx || !out)
    {
        return 0;
    }
    *out = editor_bindings;
    return KEYS_EDITOR_BINDINGS_COUNT;
}

void keys_system_get_sparkle_info(const keys_system_t *ctx, keys_sparkle_info_t *out)
{
    if (!ctx || !out)
    {
        return;
    }
    out->x = ctx->sparkle_x;
    out->y = ctx->sparkle_y;
    out->frame_index = ctx->sparkle_frame;
    out->active = (ctx->state == KEYS_STATE_SPARKLE && !ctx->finished) ? 1 : 0;
    out->save_bg = ctx->sparkle_bg_saved;
    out->restore_bg = ctx->sparkle_restore;
}

int keys_system_should_blink(const keys_system_t *ctx)
{
    if (!ctx)
    {
        return 0;
    }
    if (ctx->screen_mode != KEYS_MODE_GAME)
    {
        return 0;
    }
    return ctx->blink_active;
}

int keys_system_should_draw_specials(const keys_system_t *ctx)
{
    if (!ctx)
    {
        return 0;
    }
    if (ctx->state != KEYS_STATE_SPARKLE)
    {
        return 0;
    }
    return (ctx->current_frame % KEYS_FLASH) == 0 ? 1 : 0;
}

keys_sound_t keys_system_get_sound(const keys_system_t *ctx)
{
    keys_sound_t none = {NULL, 0};
    if (!ctx)
    {
        return none;
    }
    return ctx->sound;
}
