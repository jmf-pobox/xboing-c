/*
 * test_replay.c — Lightweight input replay for integration tests.
 *
 * See test_replay.h for usage and design.
 */

#include "test_replay.h"

#include <stdio.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "sdl2_input.h"
#include "sdl2_state.h"

/* =========================================================================
 * Action → scancode mapping (mirrors default_bindings in sdl2_input.c)
 *
 * SYNC NOTE: This table must match the default_bindings array in
 * sdl2_input.c.  If bindings change, update this table.  Using a
 * hardcoded table avoids requiring a live sdl2_input_t context for
 * the mapping, keeping the replay module lightweight and usable
 * before game_create().
 * ========================================================================= */

static const SDL_Scancode action_scancodes[SDL2I_ACTION_COUNT] = {
    [SDL2I_LEFT] = SDL_SCANCODE_LEFT,
    [SDL2I_RIGHT] = SDL_SCANCODE_RIGHT,
    [SDL2I_SHOOT] = SDL_SCANCODE_K,
    [SDL2I_PAUSE] = SDL_SCANCODE_P,
    [SDL2I_TILT] = SDL_SCANCODE_T,
    [SDL2I_KILL_BALL] = SDL_SCANCODE_D,
    [SDL2I_SAVE] = SDL_SCANCODE_Z,
    [SDL2I_LOAD] = SDL_SCANCODE_X,
    [SDL2I_ABORT] = SDL_SCANCODE_ESCAPE,
    [SDL2I_START] = SDL_SCANCODE_SPACE,
    [SDL2I_CYCLE] = SDL_SCANCODE_C,
    [SDL2I_SCORES] = SDL_SCANCODE_H,
    [SDL2I_ENTER_EDITOR] = SDL_SCANCODE_E,
    [SDL2I_SET_LEVEL] = SDL_SCANCODE_W,
    [SDL2I_QUIT] = SDL_SCANCODE_Q,
    [SDL2I_VOLUME_UP] = SDL_SCANCODE_EQUALS,
    [SDL2I_VOLUME_DOWN] = SDL_SCANCODE_MINUS,
    [SDL2I_TOGGLE_AUDIO] = SDL_SCANCODE_A,
    [SDL2I_TOGGLE_SFX] = SDL_SCANCODE_S,
    [SDL2I_TOGGLE_CONTROL] = SDL_SCANCODE_G,
    [SDL2I_ICONIFY] = SDL_SCANCODE_I,
    [SDL2I_NEXT_LEVEL] = SDL_SCANCODE_BACKSLASH,
    [SDL2I_SPEED_1] = SDL_SCANCODE_1,
    [SDL2I_SPEED_2] = SDL_SCANCODE_2,
    [SDL2I_SPEED_3] = SDL_SCANCODE_3,
    [SDL2I_SPEED_4] = SDL_SCANCODE_4,
    [SDL2I_SPEED_5] = SDL_SCANCODE_5,
    [SDL2I_SPEED_6] = SDL_SCANCODE_6,
    [SDL2I_SPEED_7] = SDL_SCANCODE_7,
    [SDL2I_SPEED_8] = SDL_SCANCODE_8,
    [SDL2I_SPEED_9] = SDL_SCANCODE_9,
};

SDL_Scancode replay_action_to_scancode(sdl2_input_action_t action)
{
    if (action < 0 || action >= SDL2I_ACTION_COUNT)
        return SDL_SCANCODE_UNKNOWN;
    return action_scancodes[action];
}

/* =========================================================================
 * Synthesize an SDL keyboard event
 * ========================================================================= */

static void inject_key_event(sdl2_input_t *input, SDL_Scancode sc, int pressed)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
    event.key.repeat = 0;
    event.key.keysym.scancode = sc;
    event.key.keysym.sym = SDL_GetKeyFromScancode(sc);
    event.key.keysym.mod = KMOD_NONE;
    sdl2_input_process_event(input, &event);
}

/* =========================================================================
 * API implementation
 * ========================================================================= */

void replay_init(replay_ctx_t *rctx, game_ctx_t *ctx, const replay_event_t *script)
{
    rctx->ctx = ctx;
    rctx->script = script;
    rctx->current_frame = 0;
    rctx->script_idx = 0;

    /* Count script length and validate non-decreasing frame order */
    rctx->script_len = 0;
    while (script[rctx->script_len].frame >= 0)
    {
        if (rctx->script_len > 0 &&
            script[rctx->script_len].frame < script[rctx->script_len - 1].frame)
        {
            fprintf(stderr, "replay_init: script events must be in non-decreasing "
                            "frame order (event %d: frame %d < frame %d)\n",
                    rctx->script_len, script[rctx->script_len].frame,
                    script[rctx->script_len - 1].frame);
        }
        rctx->script_len++;
    }
}

int replay_tick(replay_ctx_t *rctx)
{
    int frame = rctx->current_frame;

    /* Begin frame (resets edge triggers) */
    sdl2_input_begin_frame(rctx->ctx->input);

    /* Inject all events scheduled for this frame */
    while (rctx->script_idx < rctx->script_len &&
           rctx->script[rctx->script_idx].frame == frame)
    {
        const replay_event_t *ev = &rctx->script[rctx->script_idx];
        SDL_Scancode sc = replay_action_to_scancode(ev->action);
        if (sc != SDL_SCANCODE_UNKNOWN)
            inject_key_event(rctx->ctx->input, sc, ev->pressed);
        rctx->script_idx++;
    }

    /* Tick the state machine */
    sdl2_state_update(rctx->ctx->state);

    rctx->current_frame++;
    return frame;
}

int replay_tick_until(replay_ctx_t *rctx, int target_frame)
{
    int ticked = 0;
    while (rctx->current_frame < target_frame)
    {
        replay_tick(rctx);
        ticked++;
    }
    return ticked;
}
