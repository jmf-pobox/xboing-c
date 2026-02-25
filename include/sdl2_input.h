#ifndef SDL2_INPUT_H
#define SDL2_INPUT_H

/*
 * sdl2_input.h — SDL2 input handling with action mapping.
 *
 * Replaces the legacy XKeySym switch dispatch with a scancode-to-action
 * mapping layer.  Game logic queries actions (e.g., "is SHOOT pressed?")
 * instead of checking raw keycodes.  Bindings are rebindable at runtime.
 *
 * Tracks both level state (key held) and edge state (key just pressed).
 * Mouse position and button state are tracked for paddle control.
 *
 * Opaque context pattern: no globals, fully testable.
 * See ADR-011 in docs/DESIGN.md for design rationale.
 */

#include <stdbool.h>

#include <SDL2/SDL.h>

/* Maximum number of scancodes that can be bound to a single action. */
#define SDL2I_MAX_BINDINGS 2

/* =========================================================================
 * Game actions
 * ========================================================================= */

/*
 * Every distinct input action the game recognizes.
 * The enum covers gameplay, menu navigation, and global controls.
 * Editor-specific actions are reserved for the editor migration bead.
 */
typedef enum
{
    /* Paddle movement */
    SDL2I_LEFT = 0,
    SDL2I_RIGHT,

    /* Gameplay */
    SDL2I_SHOOT,
    SDL2I_PAUSE,
    SDL2I_TILT,
    SDL2I_KILL_BALL,
    SDL2I_SAVE,
    SDL2I_LOAD,
    SDL2I_ABORT, /* Escape — abort game with confirmation */

    /* Menu / navigation */
    SDL2I_START,        /* Space — start game or advance screen */
    SDL2I_CYCLE,        /* Cycle through intro screens */
    SDL2I_SCORES,       /* Show high scores */
    SDL2I_ENTER_EDITOR, /* Enter level editor */
    SDL2I_SET_LEVEL,    /* Set starting level */

    /* Global */
    SDL2I_QUIT,
    SDL2I_VOLUME_UP,
    SDL2I_VOLUME_DOWN,
    SDL2I_TOGGLE_AUDIO,
    SDL2I_TOGGLE_SFX,
    SDL2I_TOGGLE_CONTROL,
    SDL2I_ICONIFY,
    SDL2I_NEXT_LEVEL, /* Debug — skip to next level */

    /* Speed levels (1-9) */
    SDL2I_SPEED_1,
    SDL2I_SPEED_2,
    SDL2I_SPEED_3,
    SDL2I_SPEED_4,
    SDL2I_SPEED_5,
    SDL2I_SPEED_6,
    SDL2I_SPEED_7,
    SDL2I_SPEED_8,
    SDL2I_SPEED_9,

    SDL2I_ACTION_COUNT
} sdl2_input_action_t;

/* =========================================================================
 * Status codes
 * ========================================================================= */

typedef enum
{
    SDL2I_OK = 0,
    SDL2I_ERR_NULL_ARG,
    SDL2I_ERR_INVALID_ACTION,
    SDL2I_ERR_INVALID_SLOT,
    SDL2I_ERR_INVALID_SCANCODE,
    SDL2I_ERR_ALLOC_FAILED
} sdl2_input_status_t;

/* =========================================================================
 * Opaque context
 * ========================================================================= */

typedef struct sdl2_input sdl2_input_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/*
 * Create an input context with default key bindings matching the legacy
 * XBoing controls:
 *   Left/J = left,  Right/L = right,  K = shoot,  P = pause,
 *   T = tilt,  D = kill ball,  Z = save,  X = load,  Escape = abort,
 *   Space = start,  C = cycle,  H = scores,  E = editor,  W = set level,
 *   Q = quit,  =/KP+ = volume up,  -/KP- = volume down,
 *   A = audio,  S = sfx,  G = control toggle,
 *   I = iconify,  \ = next level (debug),  1-9 = speed levels.
 *
 * Note: Legacy used XK_plus (Shift+=) for volume up and XK_equal for the
 * debug next-level command.  Scancode-only mapping cannot distinguish these
 * since they share SDL_SCANCODE_EQUALS.  Volume up keeps the = key;
 * next-level was moved to backslash.
 *
 * Returns NULL on allocation failure.
 */
sdl2_input_t *sdl2_input_create(sdl2_input_status_t *status);

/* Destroy the input context.  Safe to call with NULL. */
void sdl2_input_destroy(sdl2_input_t *ctx);

/* =========================================================================
 * Per-frame processing
 * ========================================================================= */

/*
 * Call at the start of each frame to reset edge-trigger state.
 * After this call, just_pressed() returns false for all actions until
 * new key-down events are processed.
 */
void sdl2_input_begin_frame(sdl2_input_t *ctx);

/*
 * Process a single SDL event.  Updates action state for KeyDown/KeyUp
 * events and mouse state for MouseMotion/MouseButton events.
 * Non-input events are silently ignored.
 */
void sdl2_input_process_event(sdl2_input_t *ctx, const SDL_Event *event);

/* =========================================================================
 * Action queries
 * ========================================================================= */

/* True if the action's key is currently held down (level trigger). */
bool sdl2_input_pressed(const sdl2_input_t *ctx, sdl2_input_action_t action);

/* True if the action's key was pressed this frame (edge trigger). */
bool sdl2_input_just_pressed(const sdl2_input_t *ctx, sdl2_input_action_t action);

/* =========================================================================
 * Mouse queries
 * ========================================================================= */

/* Get current mouse position within the window. */
void sdl2_input_get_mouse(const sdl2_input_t *ctx, int *x, int *y);

/* True if the given mouse button is currently pressed (1=left, 2=mid, 3=right). */
bool sdl2_input_mouse_pressed(const sdl2_input_t *ctx, int button);

/* =========================================================================
 * Modifier queries
 * ========================================================================= */

/* True if either Shift key is currently held. */
bool sdl2_input_shift_held(const sdl2_input_t *ctx);

/* =========================================================================
 * Key binding management
 * ========================================================================= */

/*
 * Rebind an action's scancode at the given slot (0 or 1).
 * Use SDL_SCANCODE_UNKNOWN to clear a slot.
 */
sdl2_input_status_t sdl2_input_bind(sdl2_input_t *ctx, sdl2_input_action_t action, int slot,
                                    SDL_Scancode key);

/*
 * Get the scancode bound to an action at the given slot (0 or 1).
 * Returns SDL_SCANCODE_UNKNOWN if the slot is empty or args are invalid.
 */
SDL_Scancode sdl2_input_get_binding(const sdl2_input_t *ctx, sdl2_input_action_t action, int slot);

/* Reset all bindings to their defaults. */
void sdl2_input_reset_bindings(sdl2_input_t *ctx);

/* =========================================================================
 * Utility
 * ========================================================================= */

/* Return a human-readable name for an action (e.g., "left", "shoot"). */
const char *sdl2_input_action_name(sdl2_input_action_t action);

/* Return a human-readable string for a status code. */
const char *sdl2_input_status_string(sdl2_input_status_t status);

#endif /* SDL2_INPUT_H */
