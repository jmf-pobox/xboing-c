/*
 * sdl2_input.c — SDL2 input handling with action mapping.
 *
 * See include/sdl2_input.h for API documentation.
 * See ADR-011 in docs/DESIGN.md for design rationale.
 */

#include "sdl2_input.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal data structures
 * ========================================================================= */

struct sdl2_input_binding
{
    SDL_Scancode keys[SDL2I_MAX_BINDINGS];
};

struct sdl2_input
{
    /* Forward map: action -> scancodes (for display and rebinding). */
    struct sdl2_input_binding bindings[SDL2I_ACTION_COUNT];

    /* Reverse map: scancode -> action (for O(1) event dispatch).
     * SDL2I_ACTION_COUNT means "no action bound to this scancode". */
    sdl2_input_action_t scancode_map[SDL_NUM_SCANCODES];

    /* Per-scancode pressed state — used to derive per-action state correctly
     * when an action has multiple scancodes bound (dual binding).  Releasing
     * one bound key should not clear the action if the other is still held. */
    bool scancode_pressed[SDL_NUM_SCANCODES];

    /* Per-action derived state. */
    bool pressed[SDL2I_ACTION_COUNT];      /* currently held */
    bool just_pressed[SDL2I_ACTION_COUNT]; /* pressed this frame */

    /* Mouse state. */
    int mouse_x;
    int mouse_y;
    Uint32 mouse_buttons;

    /* Modifier state. */
    SDL_Keymod modifiers;
};

/* =========================================================================
 * Default bindings — matches legacy XBoing key assignments
 * ========================================================================= */

static const struct sdl2_input_binding default_bindings[SDL2I_ACTION_COUNT] = {
    [SDL2I_LEFT] = {{SDL_SCANCODE_LEFT, SDL_SCANCODE_J}},
    [SDL2I_RIGHT] = {{SDL_SCANCODE_RIGHT, SDL_SCANCODE_L}},
    [SDL2I_SHOOT] = {{SDL_SCANCODE_K, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_PAUSE] = {{SDL_SCANCODE_P, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_TILT] = {{SDL_SCANCODE_T, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_KILL_BALL] = {{SDL_SCANCODE_D, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_SAVE] = {{SDL_SCANCODE_Z, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_LOAD] = {{SDL_SCANCODE_X, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_ABORT] = {{SDL_SCANCODE_ESCAPE, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_START] = {{SDL_SCANCODE_SPACE, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_CYCLE] = {{SDL_SCANCODE_C, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_SCORES] = {{SDL_SCANCODE_H, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_ENTER_EDITOR] = {{SDL_SCANCODE_E, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_SET_LEVEL] = {{SDL_SCANCODE_W, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_QUIT] = {{SDL_SCANCODE_Q, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_VOLUME_UP] = {{SDL_SCANCODE_EQUALS, SDL_SCANCODE_KP_PLUS}},
    [SDL2I_VOLUME_DOWN] = {{SDL_SCANCODE_MINUS, SDL_SCANCODE_KP_MINUS}},
    [SDL2I_TOGGLE_AUDIO] = {{SDL_SCANCODE_A, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_TOGGLE_SFX] = {{SDL_SCANCODE_S, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_TOGGLE_CONTROL] = {{SDL_SCANCODE_G, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_ICONIFY] = {{SDL_SCANCODE_I, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_NEXT_LEVEL] = {{SDL_SCANCODE_BACKSLASH, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_SPEED_1] = {{SDL_SCANCODE_1, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_SPEED_2] = {{SDL_SCANCODE_2, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_SPEED_3] = {{SDL_SCANCODE_3, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_SPEED_4] = {{SDL_SCANCODE_4, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_SPEED_5] = {{SDL_SCANCODE_5, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_SPEED_6] = {{SDL_SCANCODE_6, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_SPEED_7] = {{SDL_SCANCODE_7, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_SPEED_8] = {{SDL_SCANCODE_8, SDL_SCANCODE_UNKNOWN}},
    [SDL2I_SPEED_9] = {{SDL_SCANCODE_9, SDL_SCANCODE_UNKNOWN}},
};

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static bool is_valid_action(sdl2_input_action_t action)
{
    return action >= 0 && action < SDL2I_ACTION_COUNT;
}

static bool is_valid_slot(int slot)
{
    return slot >= 0 && slot < SDL2I_MAX_BINDINGS;
}

static bool is_valid_scancode(SDL_Scancode sc)
{
    return sc == SDL_SCANCODE_UNKNOWN || (sc > SDL_SCANCODE_UNKNOWN && sc < SDL_NUM_SCANCODES);
}

/*
 * Recompute the pressed state for a single action based on which of its
 * bound scancodes are currently held.  This correctly handles dual bindings:
 * the action stays pressed as long as at least one bound key is held.
 */
static void recompute_action_pressed(sdl2_input_t *ctx, sdl2_input_action_t action)
{
    bool held = false;
    for (int s = 0; s < SDL2I_MAX_BINDINGS; s++)
    {
        SDL_Scancode sc = ctx->bindings[action].keys[s];
        if (sc != SDL_SCANCODE_UNKNOWN && sc < SDL_NUM_SCANCODES && ctx->scancode_pressed[sc])
        {
            held = true;
            break;
        }
    }
    ctx->pressed[action] = held;
}

/*
 * Rebuild the reverse lookup table from the current bindings.
 * Must be called after any change to the bindings array.
 */
static void rebuild_scancode_map(sdl2_input_t *ctx)
{
    /* Clear: all scancodes map to "no action". */
    for (int i = 0; i < SDL_NUM_SCANCODES; i++)
    {
        ctx->scancode_map[i] = SDL2I_ACTION_COUNT;
    }

    /* Populate from bindings.  Later actions overwrite earlier ones
     * if the same scancode is bound to multiple actions. */
    for (int a = 0; a < SDL2I_ACTION_COUNT; a++)
    {
        for (int s = 0; s < SDL2I_MAX_BINDINGS; s++)
        {
            SDL_Scancode sc = ctx->bindings[a].keys[s];
            if (sc != SDL_SCANCODE_UNKNOWN && sc < SDL_NUM_SCANCODES)
            {
                ctx->scancode_map[sc] = (sdl2_input_action_t)a;
            }
        }
    }
}

/* =========================================================================
 * Public API — Lifecycle
 * ========================================================================= */

sdl2_input_t *sdl2_input_create(sdl2_input_status_t *status)
{
    sdl2_input_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
    {
        if (status != NULL)
        {
            *status = SDL2I_ERR_ALLOC_FAILED;
        }
        return NULL;
    }

    /* Install default bindings and build reverse map. */
    memcpy(ctx->bindings, default_bindings, sizeof(default_bindings));
    rebuild_scancode_map(ctx);

    if (status != NULL)
    {
        *status = SDL2I_OK;
    }
    return ctx;
}

void sdl2_input_destroy(sdl2_input_t *ctx)
{
    free(ctx);
}

/* =========================================================================
 * Public API — Per-frame processing
 * ========================================================================= */

void sdl2_input_begin_frame(sdl2_input_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    memset(ctx->just_pressed, 0, sizeof(ctx->just_pressed));
}

void sdl2_input_process_event(sdl2_input_t *ctx, const SDL_Event *event)
{
    if (ctx == NULL || event == NULL)
    {
        return;
    }

    switch (event->type)
    {
        case SDL_KEYDOWN:
        {
            ctx->modifiers = event->key.keysym.mod;
            SDL_Scancode sc = event->key.keysym.scancode;
            if (sc > SDL_SCANCODE_UNKNOWN && sc < SDL_NUM_SCANCODES)
            {
                ctx->scancode_pressed[sc] = true;
                sdl2_input_action_t action = ctx->scancode_map[sc];
                if (action < SDL2I_ACTION_COUNT)
                {
                    if (!event->key.repeat)
                    {
                        ctx->just_pressed[action] = true;
                    }
                    ctx->pressed[action] = true;
                }
            }
            break;
        }

        case SDL_KEYUP:
        {
            ctx->modifiers = event->key.keysym.mod;
            SDL_Scancode sc = event->key.keysym.scancode;
            if (sc > SDL_SCANCODE_UNKNOWN && sc < SDL_NUM_SCANCODES)
            {
                ctx->scancode_pressed[sc] = false;
                sdl2_input_action_t action = ctx->scancode_map[sc];
                if (action < SDL2I_ACTION_COUNT)
                {
                    /* Recompute from all bound scancodes — releasing one key
                     * should not clear the action if the other is still held. */
                    recompute_action_pressed(ctx, action);
                }
            }
            break;
        }

        case SDL_MOUSEMOTION:
            ctx->mouse_x = event->motion.x;
            ctx->mouse_y = event->motion.y;
            break;

        case SDL_MOUSEBUTTONDOWN:
        {
            Uint8 button = event->button.button;
            if (button >= SDL_BUTTON_LEFT && button <= SDL_BUTTON_X2)
            {
                ctx->mouse_buttons |= (Uint32)SDL_BUTTON(button);
            }
            ctx->mouse_x = event->button.x;
            ctx->mouse_y = event->button.y;
            break;
        }

        case SDL_MOUSEBUTTONUP:
        {
            Uint8 button = event->button.button;
            if (button >= SDL_BUTTON_LEFT && button <= SDL_BUTTON_X2)
            {
                ctx->mouse_buttons &= (Uint32)~SDL_BUTTON(button);
            }
            ctx->mouse_x = event->button.x;
            ctx->mouse_y = event->button.y;
            break;
        }

        default:
            break;
    }
}

/* =========================================================================
 * Public API — Action queries
 * ========================================================================= */

bool sdl2_input_pressed(const sdl2_input_t *ctx, sdl2_input_action_t action)
{
    if (ctx == NULL || !is_valid_action(action))
    {
        return false;
    }
    return ctx->pressed[action];
}

bool sdl2_input_just_pressed(const sdl2_input_t *ctx, sdl2_input_action_t action)
{
    if (ctx == NULL || !is_valid_action(action))
    {
        return false;
    }
    return ctx->just_pressed[action];
}

/* =========================================================================
 * Public API — Mouse queries
 * ========================================================================= */

void sdl2_input_get_mouse(const sdl2_input_t *ctx, int *x, int *y)
{
    if (ctx == NULL)
    {
        if (x != NULL)
        {
            *x = 0;
        }
        if (y != NULL)
        {
            *y = 0;
        }
        return;
    }

    if (x != NULL)
    {
        *x = ctx->mouse_x;
    }
    if (y != NULL)
    {
        *y = ctx->mouse_y;
    }
}

bool sdl2_input_mouse_pressed(const sdl2_input_t *ctx, int button)
{
    if (ctx == NULL || button < 1 || button > 5)
    {
        return false;
    }
    return (ctx->mouse_buttons & SDL_BUTTON((unsigned)button)) != 0;
}

/* =========================================================================
 * Public API — Modifier queries
 * ========================================================================= */

bool sdl2_input_shift_held(const sdl2_input_t *ctx)
{
    if (ctx == NULL)
    {
        return false;
    }
    return (ctx->modifiers & KMOD_SHIFT) != 0;
}

/* =========================================================================
 * Public API — Key binding management
 * ========================================================================= */

sdl2_input_status_t sdl2_input_bind(sdl2_input_t *ctx, sdl2_input_action_t action, int slot,
                                    SDL_Scancode key)
{
    if (ctx == NULL)
    {
        return SDL2I_ERR_NULL_ARG;
    }
    if (!is_valid_action(action))
    {
        return SDL2I_ERR_INVALID_ACTION;
    }
    if (!is_valid_slot(slot))
    {
        return SDL2I_ERR_INVALID_SLOT;
    }
    if (!is_valid_scancode(key))
    {
        return SDL2I_ERR_INVALID_SCANCODE;
    }

    ctx->bindings[action].keys[slot] = key;
    rebuild_scancode_map(ctx);

    /* Clear pressed state for the affected action — bindings changed, so
     * old key-up events may never arrive for the previous binding. */
    ctx->pressed[action] = false;
    ctx->just_pressed[action] = false;

    return SDL2I_OK;
}

SDL_Scancode sdl2_input_get_binding(const sdl2_input_t *ctx, sdl2_input_action_t action, int slot)
{
    if (ctx == NULL || !is_valid_action(action) || !is_valid_slot(slot))
    {
        return SDL_SCANCODE_UNKNOWN;
    }
    return ctx->bindings[action].keys[slot];
}

void sdl2_input_reset_bindings(sdl2_input_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    memcpy(ctx->bindings, default_bindings, sizeof(default_bindings));
    rebuild_scancode_map(ctx);

    /* Clear all pressed state — bindings changed globally. */
    memset(ctx->pressed, 0, sizeof(ctx->pressed));
    memset(ctx->just_pressed, 0, sizeof(ctx->just_pressed));
    memset(ctx->scancode_pressed, 0, sizeof(ctx->scancode_pressed));
}

/* =========================================================================
 * Public API — Utility
 * ========================================================================= */

const char *sdl2_input_action_name(sdl2_input_action_t action)
{
    switch (action)
    {
        case SDL2I_LEFT:
            return "left";
        case SDL2I_RIGHT:
            return "right";
        case SDL2I_SHOOT:
            return "shoot";
        case SDL2I_PAUSE:
            return "pause";
        case SDL2I_TILT:
            return "tilt";
        case SDL2I_KILL_BALL:
            return "kill_ball";
        case SDL2I_SAVE:
            return "save";
        case SDL2I_LOAD:
            return "load";
        case SDL2I_ABORT:
            return "abort";
        case SDL2I_START:
            return "start";
        case SDL2I_CYCLE:
            return "cycle";
        case SDL2I_SCORES:
            return "scores";
        case SDL2I_ENTER_EDITOR:
            return "enter_editor";
        case SDL2I_SET_LEVEL:
            return "set_level";
        case SDL2I_QUIT:
            return "quit";
        case SDL2I_VOLUME_UP:
            return "volume_up";
        case SDL2I_VOLUME_DOWN:
            return "volume_down";
        case SDL2I_TOGGLE_AUDIO:
            return "toggle_audio";
        case SDL2I_TOGGLE_SFX:
            return "toggle_sfx";
        case SDL2I_TOGGLE_CONTROL:
            return "toggle_control";
        case SDL2I_ICONIFY:
            return "iconify";
        case SDL2I_NEXT_LEVEL:
            return "next_level";
        case SDL2I_SPEED_1:
            return "speed_1";
        case SDL2I_SPEED_2:
            return "speed_2";
        case SDL2I_SPEED_3:
            return "speed_3";
        case SDL2I_SPEED_4:
            return "speed_4";
        case SDL2I_SPEED_5:
            return "speed_5";
        case SDL2I_SPEED_6:
            return "speed_6";
        case SDL2I_SPEED_7:
            return "speed_7";
        case SDL2I_SPEED_8:
            return "speed_8";
        case SDL2I_SPEED_9:
            return "speed_9";
        case SDL2I_ACTION_COUNT:
            break;
    }
    return "unknown";
}

const char *sdl2_input_status_string(sdl2_input_status_t status)
{
    switch (status)
    {
        case SDL2I_OK:
            return "OK";
        case SDL2I_ERR_NULL_ARG:
            return "NULL argument";
        case SDL2I_ERR_INVALID_ACTION:
            return "invalid action ID";
        case SDL2I_ERR_INVALID_SLOT:
            return "invalid binding slot";
        case SDL2I_ERR_INVALID_SCANCODE:
            return "invalid scancode";
        case SDL2I_ERR_ALLOC_FAILED:
            return "allocation failed";
    }
    return "unknown status";
}
