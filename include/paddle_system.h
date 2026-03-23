#ifndef PADDLE_SYSTEM_H
#define PADDLE_SYSTEM_H

/*
 * paddle_system.h — Pure C paddle system with no X11/SDL2 dependency.
 *
 * Owns paddle position, size, control flags (reverse, sticky), and
 * movement logic.  Communicates render state through a snapshot struct.
 * Zero dependency on SDL2 or X11.
 *
 * Opaque context pattern: no globals, fully testable with CMocka.
 * See ADR-017 in docs/DESIGN.md for design rationale.
 */

/* =========================================================================
 * Status codes
 * ========================================================================= */

typedef enum
{
    PADDLE_SYS_OK = 0,
    PADDLE_SYS_ERR_NULL_ARG,
    PADDLE_SYS_ERR_ALLOC_FAILED,
} paddle_system_status_t;

/* =========================================================================
 * Constants — match legacy paddle.h values
 * ========================================================================= */

/* Direction constants (input to update) */
#define PADDLE_DIR_NONE 0
#define PADDLE_DIR_LEFT 1
#define PADDLE_DIR_SHOOT 2
#define PADDLE_DIR_RIGHT 3

/* Size type constants */
#define PADDLE_SIZE_SMALL 4
#define PADDLE_SIZE_MEDIUM 5
#define PADDLE_SIZE_HUGE 6

/* Pixel dimensions per size */
#define PADDLE_WIDTH_SMALL 40
#define PADDLE_WIDTH_MEDIUM 50
#define PADDLE_WIDTH_HUGE 70

/* Movement velocity for keyboard control (pixels per update) */
#define PADDLE_VELOCITY 10

/* Paddle geometry */
#define PADDLE_RENDER_HEIGHT 15   /* Pixmap render height */
#define PADDLE_COLLISION_HEIGHT 9 /* PADDLE_HEIGHT from legacy */
#define PADDLE_HALF_HEIGHT 4      /* PADDLE_HC from legacy */

/* Distance from bottom of play area to paddle top */
#define PADDLE_DIST_BASE 30

/* =========================================================================
 * Render info — read-only snapshot for the integration layer to draw
 * ========================================================================= */

typedef struct
{
    int pos;       /* Center X position in play-area coordinates */
    int prev_pos;  /* Previous tick position (for render interpolation) */
    int y;         /* Top Y position (play_height - PADDLE_DIST_BASE) */
    int width;     /* Pixel width (40 / 50 / 70) */
    int height;    /* Render height (15) */
    int size_type; /* PADDLE_SIZE_SMALL / MEDIUM / HUGE */
} paddle_system_render_info_t;

/* =========================================================================
 * Opaque context
 * ========================================================================= */

typedef struct paddle_system paddle_system_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/*
 * Create a paddle system context.
 *
 * play_width:  playfield width in pixels (495)
 * play_height: playfield height in pixels (580)
 * main_width:  info panel width in pixels (70) — used for mouse position
 *              offset formula matching legacy MovePaddle()
 *
 * Initial state: centered, PADDLE_SIZE_HUGE, reverse off, sticky off.
 * Returns NULL on allocation failure (sets *status if non-NULL).
 */
paddle_system_t *paddle_system_create(int play_width, int play_height, int main_width,
                                      paddle_system_status_t *status);

/* Destroy the paddle system.  Safe to call with NULL. */
void paddle_system_destroy(paddle_system_t *ctx);

/* =========================================================================
 * Update — call once per frame
 * ========================================================================= */

/*
 * Update paddle position from input.
 *
 * direction: PADDLE_DIR_LEFT, PADDLE_DIR_RIGHT (keyboard),
 *            or PADDLE_DIR_NONE (mouse mode / no input).
 * mouse_x:   mouse X position in play-window coordinates.
 *            Used only when direction == PADDLE_DIR_NONE and mouse_x > 0.
 *            Pass 0 for keyboard mode.
 *
 * The legacy formula is preserved exactly:
 *   paddlePos = mouse_x - (main_width / 2) + half_width
 *
 * Reverse controls: keyboard LEFT/RIGHT swap; mouse X is mirrored
 * (play_width - mouse_x) before the formula.
 *
 * Position is clamped to [half_width, play_width - half_width].
 */
void paddle_system_update(paddle_system_t *ctx, int direction, int mouse_x);

/* =========================================================================
 * State changes
 * ========================================================================= */

/*
 * Reset paddle to center position and clear motion state.
 * Size and flags are NOT changed (matches legacy ResetPaddleStart).
 */
void paddle_system_reset(paddle_system_t *ctx);

/*
 * Change paddle size by one step.
 *
 * shrink: nonzero = shrink one step (HUGE→MEDIUM→SMALL),
 *         zero = expand one step (SMALL→MEDIUM→HUGE).
 * Clamps at extremes (shrinking SMALL stays SMALL, expanding HUGE stays HUGE).
 */
void paddle_system_change_size(paddle_system_t *ctx, int shrink);

/* Set the paddle size type directly. */
void paddle_system_set_size(paddle_system_t *ctx, int size_type);

/* Set reverse controls flag (0 = off, nonzero = on). */
void paddle_system_set_reverse(paddle_system_t *ctx, int on);

/* Toggle reverse controls flag. */
void paddle_system_toggle_reverse(paddle_system_t *ctx);

/* Set sticky bat flag (0 = off, nonzero = on). */
void paddle_system_set_sticky(paddle_system_t *ctx, int on);

/* =========================================================================
 * Queries — for ball_system_env_t and other consumers
 * ========================================================================= */

/* Return paddle center X position. */
int paddle_system_get_pos(const paddle_system_t *ctx);

/*
 * Return paddle movement delta since last update.
 *
 * Mouse mode: mouse_x - previous_mouse_x (from last update call).
 * Keyboard mode: always 0 (matches legacy — paddleDx not set for keys).
 */
int paddle_system_get_dx(const paddle_system_t *ctx);

/* Return paddle pixel width (40 / 50 / 70). */
int paddle_system_get_size(const paddle_system_t *ctx);

/* Return paddle size type (PADDLE_SIZE_SMALL / MEDIUM / HUGE). */
int paddle_system_get_size_type(const paddle_system_t *ctx);

/* Return nonzero if the paddle moved during the last update. */
int paddle_system_is_moving(const paddle_system_t *ctx);

/* Return nonzero if reverse controls are active. */
int paddle_system_get_reverse(const paddle_system_t *ctx);

/* Return nonzero if sticky bat is active. */
int paddle_system_get_sticky(const paddle_system_t *ctx);

/* Fill render info snapshot.  Returns error on NULL arg. */
paddle_system_status_t paddle_system_get_render_info(const paddle_system_t *ctx,
                                                     paddle_system_render_info_t *info);

/* =========================================================================
 * Utility
 * ========================================================================= */

/* Return a human-readable string for a status code. */
const char *paddle_system_status_string(paddle_system_status_t status);

#endif /* PADDLE_SYSTEM_H */
