#ifndef SDL2_STATE_H
#define SDL2_STATE_H

/*
 * sdl2_state.h — Game state machine with function pointer dispatch.
 *
 * Replaces the legacy switch(mode) dispatch in main.c:handleGameStates()
 * with a function pointer table.  Each mode registers enter/update/exit
 * callbacks.  Transitions call exit on the old mode, then enter on the new.
 *
 * Dialogue mode uses push/pop semantics: entering dialogue saves the current
 * mode and restores it on exit — matching legacy oldMode save/restore.
 *
 * Frame counter increments on every update except pause and dialogue, matching
 * legacy behavior (main.c:1283).
 *
 * Opaque context pattern: no globals, fully testable.
 * See ADR-012 in docs/DESIGN.md for design rationale.
 */

#include <stdbool.h>

/* =========================================================================
 * Game modes — values match legacy MODE_* defines in include/main.h
 * ========================================================================= */

typedef enum
{
    SDL2ST_NONE = 0,
    SDL2ST_HIGHSCORE = 1,
    SDL2ST_INTRO = 2,
    SDL2ST_GAME = 3,
    SDL2ST_PAUSE = 4,
    SDL2ST_BALL_WAIT = 5, /* Legacy: defined but never assigned */
    SDL2ST_WAIT = 6,      /* Legacy: defined but never assigned */
    SDL2ST_BONUS = 7,
    SDL2ST_INSTRUCT = 8,
    SDL2ST_KEYS = 9,
    SDL2ST_PRESENTS = 10,
    SDL2ST_DEMO = 11,
    SDL2ST_PREVIEW = 12,
    SDL2ST_DIALOGUE = 13,
    SDL2ST_EDIT = 14,
    SDL2ST_KEYSEDIT = 15,
    SDL2ST_COUNT = 16
} sdl2_state_mode_t;

/* =========================================================================
 * Status codes
 * ========================================================================= */

typedef enum
{
    SDL2ST_OK = 0,
    SDL2ST_ERR_NULL_ARG,
    SDL2ST_ERR_INVALID_MODE,
    SDL2ST_ERR_ALLOC_FAILED,
    SDL2ST_ERR_ALREADY_IN_DIALOGUE,
    SDL2ST_ERR_NOT_IN_DIALOGUE
} sdl2_state_status_t;

/* =========================================================================
 * Callback types
 * ========================================================================= */

/*
 * Mode handler callback.  Called with the user_data pointer passed at
 * creation time.  The mode argument identifies which mode triggered
 * the callback (useful when sharing handlers across modes).
 */
typedef void (*sdl2_state_handler_fn)(sdl2_state_mode_t mode, void *user_data);

/*
 * Mode definition: the three callbacks for a mode.
 * Any callback may be NULL (no-op).
 */
typedef struct
{
    sdl2_state_handler_fn on_enter;  /* Called once when transitioning into this mode */
    sdl2_state_handler_fn on_update; /* Called every frame while in this mode */
    sdl2_state_handler_fn on_exit;   /* Called once when transitioning out of this mode */
} sdl2_state_mode_def_t;

/* =========================================================================
 * Opaque context
 * ========================================================================= */

typedef struct sdl2_state sdl2_state_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/*
 * Create a state machine context.  Starts in SDL2ST_NONE with frame 0.
 * user_data is passed to all handler callbacks.
 *
 * Returns NULL on allocation failure.
 */
sdl2_state_t *sdl2_state_create(void *user_data, sdl2_state_status_t *status);

/* Destroy the state machine.  Safe to call with NULL. */
void sdl2_state_destroy(sdl2_state_t *ctx);

/* =========================================================================
 * Mode registration
 * ========================================================================= */

/*
 * Register handlers for a mode.  Overwrites any previous registration.
 * The def struct is copied — caller need not keep it alive.
 * Pass NULL for def to clear all handlers for the mode.
 */
sdl2_state_status_t sdl2_state_register(sdl2_state_t *ctx, sdl2_state_mode_t mode,
                                        const sdl2_state_mode_def_t *def);

/* =========================================================================
 * Transitions
 * ========================================================================= */

/*
 * Transition to a new mode.  Calls on_exit for the old mode (if registered),
 * then on_enter for the new mode (if registered).
 *
 * Transitioning to the current mode is a no-op (returns SDL2ST_OK).
 * Cannot transition directly to/from SDL2ST_DIALOGUE — use push/pop.
 */
sdl2_state_status_t sdl2_state_transition(sdl2_state_t *ctx, sdl2_state_mode_t new_mode);

/*
 * Enter dialogue mode: saves the current mode and switches to DIALOGUE.
 * Calls on_exit for the current mode, then on_enter for DIALOGUE.
 * Returns SDL2ST_ERR_ALREADY_IN_DIALOGUE if already in dialogue.
 */
sdl2_state_status_t sdl2_state_push_dialogue(sdl2_state_t *ctx);

/*
 * Exit dialogue mode: restores the saved mode.
 * Calls on_exit for DIALOGUE, then on_enter for the restored mode.
 * Returns SDL2ST_ERR_NOT_IN_DIALOGUE if not currently in dialogue.
 */
sdl2_state_status_t sdl2_state_pop_dialogue(sdl2_state_t *ctx);

/* =========================================================================
 * Per-frame update
 * ========================================================================= */

/*
 * Dispatch the on_update handler for the current mode.
 * Increments the frame counter unless in SDL2ST_PAUSE or SDL2ST_DIALOGUE.
 */
void sdl2_state_update(sdl2_state_t *ctx);

/* =========================================================================
 * Queries
 * ========================================================================= */

/* Return the current mode. */
sdl2_state_mode_t sdl2_state_current(const sdl2_state_t *ctx);

/* Return the previous mode (before the last transition). */
sdl2_state_mode_t sdl2_state_previous(const sdl2_state_t *ctx);

/* Return the saved mode during dialogue, or SDL2ST_NONE if not in dialogue. */
sdl2_state_mode_t sdl2_state_saved_mode(const sdl2_state_t *ctx);

/* Return the frame counter. */
unsigned long sdl2_state_frame(const sdl2_state_t *ctx);

/* True if current mode is SDL2ST_PAUSE. */
bool sdl2_state_is_paused(const sdl2_state_t *ctx);

/* True if current mode is SDL2ST_DIALOGUE. */
bool sdl2_state_is_dialogue(const sdl2_state_t *ctx);

/*
 * True if the current mode is a gameplay mode (GAME, PAUSE, BALL_WAIT, WAIT).
 * Matches the legacy key dispatch routing that sends these modes to
 * handleGameKeys() vs handleIntroKeys().
 */
bool sdl2_state_is_gameplay(const sdl2_state_t *ctx);

/* =========================================================================
 * Utility
 * ========================================================================= */

/* Return a human-readable name for a mode (e.g., "game", "intro"). */
const char *sdl2_state_mode_name(sdl2_state_mode_t mode);

/* Return a human-readable string for a status code. */
const char *sdl2_state_status_string(sdl2_state_status_t status);

#endif /* SDL2_STATE_H */
