#ifndef BALL_SYSTEM_H
#define BALL_SYSTEM_H

/*
 * ball_system.h — Pure C ball physics system with callback-based side effects.
 *
 * Owns the BALL array, state machine dispatch, physics, and queries.
 * Communicates side effects (sound, score, block hits, rendering) through
 * an injected callback table.  Zero dependency on SDL2 or X11.
 *
 * Opaque context pattern: no globals, fully testable with CMocka stubs.
 * See ADR-015 in docs/DESIGN.md for design rationale.
 */

#include "ball_types.h"

/* =========================================================================
 * Status codes
 * ========================================================================= */

typedef enum
{
    BALL_SYS_OK = 0,
    BALL_SYS_ERR_NULL_ARG,
    BALL_SYS_ERR_ALLOC_FAILED,
    BALL_SYS_ERR_FULL,         /* All MAX_BALLS slots occupied */
    BALL_SYS_ERR_INVALID_INDEX /* Ball index out of range */
} ball_system_status_t;

/* =========================================================================
 * Ball lifecycle event types — reported via on_event callback
 * ========================================================================= */

typedef enum
{
    BALL_EVT_DIED = 0,    /* Ball finished pop animation, slot freed */
    BALL_EVT_RESET_START, /* New ball created on paddle */
    BALL_EVT_SPLIT,       /* Multiball: new ball spawned */
    BALL_EVT_TILT,        /* Auto-tilt activated */
    BALL_EVT_ACTIVATED,   /* Ball left paddle (READY -> ACTIVE) */
    BALL_EVT_PADDLE_HIT   /* Ball bounced off paddle */
} ball_system_event_t;

/* =========================================================================
 * Region check results — returned by check_region callback
 * Matches legacy XRectInRegion() usage in ball.c CheckRegions()
 * ========================================================================= */

#define BALL_REGION_NONE 0
#define BALL_REGION_TOP 1
#define BALL_REGION_BOTTOM 2
#define BALL_REGION_LEFT 3
#define BALL_REGION_RIGHT 4

/* =========================================================================
 * Environment struct — replaces extern globals, passed per-frame
 * ========================================================================= */

typedef struct
{
    int frame;       /* extern int frame */
    int speed_level; /* extern int speedLevel */
    int paddle_pos;  /* extern int paddlePos */
    int paddle_dx;   /* extern int paddleDx */
    int paddle_size; /* GetPaddleSize() */
    int play_width;  /* PLAY_WIDTH (495) */
    int play_height; /* PLAY_HEIGHT (580) */
    int no_walls;    /* extern int noWalls */
    int killer;      /* extern int Killer */
    int sticky_bat;  /* extern int stickyBat */
    int col_width;   /* colWidth (PLAY_WIDTH / MAX_COL) */
    int row_height;  /* rowHeight (PLAY_HEIGHT / MAX_ROW) */
} ball_system_env_t;

/* =========================================================================
 * Callback table — side effects injected by the integration layer
 * ========================================================================= */

typedef struct
{
    /*
     * Block collision: returns BALL_REGION_NONE/TOP/BOTTOM/LEFT/RIGHT.
     * Replaces XRectInRegion() in legacy CheckRegions().
     */
    int (*check_region)(int row, int col, int bx, int by, int bdx, void *ud);

    /*
     * Block hit: called when a ball strikes a block.
     * Returns nonzero if ball should NOT bounce (killer block, teleport, etc.).
     */
    int (*on_block_hit)(int row, int col, int ball_index, void *ud);

    /*
     * Cell availability query for teleport.
     * Returns nonzero if the cell at (row, col) is available for placement.
     */
    int (*cell_available)(int row, int col, void *ud);

    /* Audio playback: play the named sound effect. */
    void (*on_sound)(const char *name, void *ud);

    /* Score addition: add points to the player's score. */
    void (*on_score)(unsigned long points, void *ud);

    /* Message display: show a message string to the player. */
    void (*on_message)(const char *msg, void *ud);

    /*
     * Ball lifecycle events (DIED, RESET_START, SPLIT, TILT, etc.).
     * Integration layer handles side effects (e.g., DeadBall, AddABullet).
     */
    void (*on_event)(ball_system_event_t event, int ball_index, void *ud);
} ball_system_callbacks_t;

/* =========================================================================
 * Render info — read-only snapshot for the integration layer to draw
 * ========================================================================= */

typedef struct
{
    int active;            /* Nonzero if this slot is in use */
    int x;                 /* Ball center x (current tick) */
    int y;                 /* Ball center y (current tick) */
    int from_x;            /* Ball center x before last movement */
    int from_y;            /* Ball center y before last movement */
    int ticks_since_move;  /* Ticks elapsed since last update_a_ball */
    int slide;             /* Animation frame index */
    enum BallStates state; /* Current ball state */
} ball_system_render_info_t;

typedef struct
{
    int pos; /* Guide position 0-10 */
    int inc; /* Guide animation direction: +1 or -1 */
} ball_system_guide_info_t;

/* =========================================================================
 * Opaque context
 * ========================================================================= */

typedef struct ball_system ball_system_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/*
 * Create a ball system context.  All ball slots start cleared (inactive).
 * Initializes machine_eps for swept-circle collision detection.
 *
 * callbacks: side-effect function pointers (any may be NULL for stubs).
 *            The struct is copied — caller need not keep it alive.
 * user_data: opaque pointer passed to all callbacks.
 *
 * Returns NULL on allocation failure (sets *status if non-NULL).
 */
ball_system_t *ball_system_create(const ball_system_callbacks_t *callbacks, void *user_data,
                                  ball_system_status_t *status);

/* Destroy the ball system.  Safe to call with NULL. */
void ball_system_destroy(ball_system_t *ctx);

/* =========================================================================
 * Ball management
 * ========================================================================= */

/*
 * Add a new ball at (x, y) with velocity (dx, dy).
 * The ball starts in BALL_CREATE state with a random mass.
 * Sets nextFrame = env->frame + BIRTH_FRAME_RATE.
 *
 * Returns the slot index (0..MAX_BALLS-1) on success, or -1 if all slots
 * are full.  Sets *status if non-NULL.
 */
int ball_system_add(ball_system_t *ctx, const ball_system_env_t *env, int x, int y, int dx, int dy,
                    ball_system_status_t *status);

/* Clear a single ball slot to defaults (inactive, BALL_CREATE, radius=BALL_WC). */
ball_system_status_t ball_system_clear(ball_system_t *ctx, int index);

/* Clear all ball slots. */
ball_system_status_t ball_system_clear_all(ball_system_t *ctx);

/* =========================================================================
 * Ball state control
 * ========================================================================= */

/*
 * Change ball state (e.g., BALL_POP to kill a ball).
 * When entering BALL_POP, sets up the pop animation countdown.
 */
ball_system_status_t ball_system_change_mode(ball_system_t *ctx, const ball_system_env_t *env,
                                             int index, enum BallStates mode);

/*
 * Activate the first READY ball — applies guide direction and fires it.
 * Returns the slot index of the activated ball, or -1 if none waiting.
 */
int ball_system_activate_waiting(ball_system_t *ctx, const ball_system_env_t *env);

/*
 * Create a new ball on the paddle (reset after death).
 * Adds a ball at the paddle position in BALL_WAIT → BALL_CREATE sequence.
 * Emits BALL_EVT_RESET_START via callback.
 * Returns the slot index, or -1 if all slots full.
 */
int ball_system_reset_start(ball_system_t *ctx, const ball_system_env_t *env);

/*
 * Auto-tilt: randomize ball velocity to break infinite loops.
 * Only affects balls in BALL_ACTIVE state.
 * Emits BALL_EVT_TILT and "Auto Tilt Activated" message via callbacks.
 */
void ball_system_do_tilt(ball_system_t *ctx, const ball_system_env_t *env, int index);

/*
 * Split a ball in two (multiball bonus).
 * Adds a new ball in BALL_ACTIVE state at a random empty cell.
 * Emits BALL_EVT_SPLIT via callback.
 * Returns the new ball's slot index, or -1 if all slots full.
 */
int ball_system_split(ball_system_t *ctx, const ball_system_env_t *env);

/* =========================================================================
 * Per-frame update
 * ========================================================================= */

/*
 * Main update: iterate all active balls, dispatch state machine.
 * This is the equivalent of legacy HandleBallMode().
 *
 * Handles: BALL_CREATE animation, BALL_READY (follow paddle + auto-activate),
 * BALL_ACTIVE (physics + wall/paddle collision), BALL_DIE (move until off-screen),
 * BALL_POP (countdown animation), BALL_WAIT (frame delay).
 *
 * Block collision via check_region/on_block_hit callbacks.
 * Ball-to-ball collision via ball_math_will_collide/ball_math_collide.
 */
void ball_system_update(ball_system_t *ctx, const ball_system_env_t *env);

/* =========================================================================
 * Queries
 * ========================================================================= */

/* Return the number of balls in BALL_ACTIVE state. */
int ball_system_get_active_count(const ball_system_t *ctx);

/* Return the index of the first BALL_ACTIVE ball, or -1 if none. */
int ball_system_get_active_index(const ball_system_t *ctx);

/* Return nonzero if any ball is in BALL_READY state. */
int ball_system_is_ball_waiting(const ball_system_t *ctx);

/* Get the state of ball at index.  Returns BALL_NONE on invalid index. */
enum BallStates ball_system_get_state(const ball_system_t *ctx, int index);

/* Get ball position.  Returns BALL_SYS_ERR_INVALID_INDEX on bad index. */
ball_system_status_t ball_system_get_position(const ball_system_t *ctx, int index, int *x, int *y);

/* Fill render info for ball at index.  Returns error on bad index. */
ball_system_status_t ball_system_get_render_info(const ball_system_t *ctx, int index,
                                                 ball_system_render_info_t *info);

/* Get guide direction indicator state. */
ball_system_guide_info_t ball_system_get_guide_info(const ball_system_t *ctx);

/* =========================================================================
 * Utility
 * ========================================================================= */

/* Return a human-readable string for a status code. */
const char *ball_system_status_string(ball_system_status_t status);

/* Return a human-readable string for a ball state enum. */
const char *ball_system_state_name(enum BallStates state);

#endif /* BALL_SYSTEM_H */
