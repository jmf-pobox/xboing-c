/*
 * test_replay.h — Lightweight input replay for integration tests.
 *
 * Provides frame-indexed action scripts that can be played back against
 * a live game context.  Each script entry specifies a frame number, an
 * input action, and whether the key is pressed or released.
 *
 * On each tick, the replay system:
 *   1. Calls sdl2_input_begin_frame()
 *   2. Synthesizes SDL_KeyboardEvent for any events at the current frame
 *   3. Feeds them to sdl2_input_process_event()
 *   4. Calls sdl2_state_update()
 *
 * Scripts are defined inline in test code as arrays of replay_event_t.
 * No file I/O — deterministic and fast.
 *
 * Usage:
 *   replay_event_t script[] = {
 *       {  0, SDL2I_START, 1},   // frame 0: press Space
 *       {  1, SDL2I_START, 0},   // frame 1: release Space
 *       {100, SDL2I_LEFT,  1},   // frame 100: press Left
 *       {150, SDL2I_LEFT,  0},   // frame 150: release Left
 *       REPLAY_END
 *   };
 *   replay_ctx_t rctx;
 *   replay_init(&rctx, ctx, script);
 *   replay_tick_until(&rctx, 200);  // tick frames 0..199
 */

#ifndef TEST_REPLAY_H
#define TEST_REPLAY_H

#include "game_context.h"
#include "sdl2_input.h"

/* =========================================================================
 * Script event
 * ========================================================================= */

typedef struct
{
    int frame;                 /* Frame number at which to inject (0-based) */
    sdl2_input_action_t action; /* Which game action */
    int pressed;               /* 1 = key down, 0 = key up */
} replay_event_t;

/* Sentinel: marks end of script array */
#define REPLAY_END {-1, (sdl2_input_action_t)0, 0}

/* =========================================================================
 * Replay context
 * ========================================================================= */

typedef struct
{
    game_ctx_t *ctx;
    const replay_event_t *script;
    int script_len;
    int current_frame;
    int script_idx; /* Next event to process */
} replay_ctx_t;

/* =========================================================================
 * API
 * ========================================================================= */

/*
 * Initialize a replay context.
 * script: array of events terminated by REPLAY_END.
 * The game should already be in the desired starting mode.
 */
void replay_init(replay_ctx_t *rctx, game_ctx_t *ctx, const replay_event_t *script);

/*
 * Tick one frame: begin_frame, inject events, state_update.
 * Returns the frame number that was ticked.
 */
int replay_tick(replay_ctx_t *rctx);

/*
 * Tick from current frame until target_frame (exclusive).
 * Returns the number of frames ticked.
 */
int replay_tick_until(replay_ctx_t *rctx, int target_frame);

/*
 * Look up the default SDL_Scancode for a game action.
 * Returns SDL_SCANCODE_UNKNOWN if the action has no default binding.
 */
SDL_Scancode replay_action_to_scancode(sdl2_input_action_t action);

#endif /* TEST_REPLAY_H */
