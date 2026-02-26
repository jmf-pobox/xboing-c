#ifndef EYEDUDE_SYSTEM_H
#define EYEDUDE_SYSTEM_H

/*
 * eyedude_system.h — Pure C EyeDude animated character system.
 *
 * Owns the EyeDude state machine: walk, turn, die, collision detection.
 * The character walks across the top row of the play area with 6 walk
 * frames per direction.  Hitting it awards EYEDUDE_HIT_BONUS points.
 *
 * All rendering is delegated to the integration layer via callbacks
 * and query functions.
 *
 * Opaque context pattern: no globals, fully testable with CMocka stubs.
 * See ADR-024 in docs/DESIGN.md for design rationale.
 */

/* =========================================================================
 * Constants — match legacy eyedude.h values
 * ========================================================================= */

#define EYEDUDE_WIDTH 32
#define EYEDUDE_HEIGHT 32
#define EYEDUDE_WC (EYEDUDE_WIDTH / 2)
#define EYEDUDE_HC (EYEDUDE_HEIGHT / 2)

#define EYEDUDE_FRAME_RATE 30 /* Frames between animation steps */
#define EYEDUDE_WALK_SPEED 5  /* Pixels per step */
#define EYEDUDE_HIT_BONUS 10000
#define EYEDUDE_WALK_FRAMES 6 /* Animation frames per direction */

#define EYEDUDE_TURN_CHANCE 30 /* Percent chance to turn at midpoint */

/* =========================================================================
 * State enum — match legacy eyeDudeStates
 * ========================================================================= */

typedef enum
{
    EYEDUDE_STATE_NONE = 0,
    EYEDUDE_STATE_RESET,
    EYEDUDE_STATE_WAIT,
    EYEDUDE_STATE_WALK,
    EYEDUDE_STATE_TURN,
    EYEDUDE_STATE_DIE,
} eyedude_state_t;

/* =========================================================================
 * Direction enum — match legacy WALK_LEFT/RIGHT/DEAD
 * ========================================================================= */

typedef enum
{
    EYEDUDE_DIR_LEFT = 1,
    EYEDUDE_DIR_RIGHT = 2,
    EYEDUDE_DIR_DEAD = 3,
} eyedude_dir_t;

/* =========================================================================
 * Render info — current frame, position, direction for drawing
 * ========================================================================= */

typedef struct
{
    int x;             /* Center X position */
    int y;             /* Center Y position */
    int frame_index;   /* Animation frame (0..5) */
    eyedude_dir_t dir; /* Walk direction (or DEAD) */
    int visible;       /* 1 if character should be drawn */
} eyedude_render_info_t;

/* =========================================================================
 * Callback table — injected at creation time
 * ========================================================================= */

typedef struct
{
    /* Query: is the top row clear of blocks? Return 1=clear, 0=blocked. */
    int (*is_path_clear)(void *ud);

    /* Score bonus awarded */
    void (*on_score)(unsigned long points, void *ud);

    /* Sound effect requested */
    void (*on_sound)(const char *name, void *ud);

    /* Message display */
    void (*on_message)(const char *msg, void *ud);
} eyedude_system_callbacks_t;

/* =========================================================================
 * Random function type — injectable for deterministic testing
 * ========================================================================= */

typedef int (*eyedude_rand_fn)(void);

/* =========================================================================
 * Opaque context
 * ========================================================================= */

typedef struct eyedude_system eyedude_system_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

eyedude_system_t *eyedude_system_create(const eyedude_system_callbacks_t *callbacks,
                                        void *user_data, eyedude_rand_fn rand_fn);

void eyedude_system_destroy(eyedude_system_t *ctx);

/* =========================================================================
 * State management
 * ========================================================================= */

/* Set the EyeDude mode (used by external triggers) */
void eyedude_system_set_state(eyedude_system_t *ctx, eyedude_state_t state);

/* Get the current state */
eyedude_state_t eyedude_system_get_state(const eyedude_system_t *ctx);

/* =========================================================================
 * Per-frame update
 * ========================================================================= */

/*
 * Advance the EyeDude by one frame.
 *
 * frame:  current frame counter.
 * play_w: play area width (for positioning and bounds).
 */
void eyedude_system_update(eyedude_system_t *ctx, int frame, int play_w);

/* =========================================================================
 * Collision detection
 * ========================================================================= */

/*
 * Check if a ball at (bx, by) collides with the EyeDude.
 *
 * ball_hw: ball half-width (BALL_WC).
 * ball_hh: ball half-height (BALL_HC).
 *
 * Returns 1 if collision detected, 0 otherwise.
 * Only returns 1 when state is WALK (character is active on screen).
 */
int eyedude_system_check_collision(const eyedude_system_t *ctx, int bx, int by, int ball_hw,
                                   int ball_hh);

/* =========================================================================
 * Queries
 * ========================================================================= */

/* Get current position (center coordinates) */
void eyedude_system_get_position(const eyedude_system_t *ctx, int *out_x, int *out_y);

/* Get render info for the integration layer */
eyedude_render_info_t eyedude_system_get_render_info(const eyedude_system_t *ctx);

#endif /* EYEDUDE_SYSTEM_H */
