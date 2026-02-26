#ifndef SCORE_SYSTEM_H
#define SCORE_SYSTEM_H

/*
 * score_system.h — Pure C score management with callback-based side effects.
 *
 * Owns the game score value, multiplier application, extra life tracking,
 * and digit layout computation for rendering.  Communicates side effects
 * (extra life awarded, score changed) through an injected callback table.
 * Zero dependency on SDL2 or X11.
 *
 * Delegates pure arithmetic to score_logic.h functions.
 * Opaque context pattern: no globals, fully testable with CMocka stubs.
 * See ADR-019 in docs/DESIGN.md for design rationale.
 */

#include <sys/types.h> /* u_long */

/* =========================================================================
 * Status codes
 * ========================================================================= */

typedef enum
{
    SCORE_SYS_OK = 0,
    SCORE_SYS_ERR_NULL_ARG,
    SCORE_SYS_ERR_ALLOC_FAILED,
} score_system_status_t;

/* =========================================================================
 * Constants — match legacy score.c / stage.c values
 * ========================================================================= */

#define SCORE_DIGIT_WIDTH 30
#define SCORE_DIGIT_HEIGHT 40
#define SCORE_DIGIT_STRIDE 32  /* 30px digit + 2px gap */
#define SCORE_WINDOW_WIDTH 224 /* scoreWindow pixel width */
#define SCORE_WINDOW_HEIGHT 42 /* scoreWindow pixel height */
#define SCORE_MAX_DIGITS 7     /* Max displayable digits (9,999,999) */

/* =========================================================================
 * Callback table — injected at creation time
 * ========================================================================= */

typedef struct
{
    /* Notifies that an extra life was awarded at the given score. */
    void (*on_extra_life)(u_long score_value, void *ud);

    /* Notifies that the score value changed.  The integration layer
     * uses this to trigger a redraw of the score display. */
    void (*on_score_changed)(u_long new_score, void *ud);
} score_system_callbacks_t;

/* =========================================================================
 * Per-frame environment — replaces extern globals
 * ========================================================================= */

typedef struct
{
    int x2_active; /* x2Bonus flag from special.c */
    int x4_active; /* x4Bonus flag from special.c */
} score_system_env_t;

/* =========================================================================
 * Digit layout — read-only snapshot for the integration layer
 * ========================================================================= */

typedef struct
{
    int count;                         /* Number of digits (1 for score==0) */
    int digits[SCORE_MAX_DIGITS];      /* Digit values, most-significant first */
    int x_positions[SCORE_MAX_DIGITS]; /* X pixel offset for each digit */
    int y;                             /* Y pixel offset (always 0) */
} score_system_digit_layout_t;

/* =========================================================================
 * Opaque context
 * ========================================================================= */

typedef struct score_system score_system_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/*
 * Create a score system context.
 *
 * callbacks: callback table (copied, caller retains ownership).
 *            NULL callbacks are safe — those features are simply disabled.
 * user_data: opaque pointer passed to all callbacks.
 *
 * Initial state: score=0, no extra lives awarded.
 * Returns NULL on allocation failure (sets *status if non-NULL).
 */
score_system_t *score_system_create(const score_system_callbacks_t *callbacks, void *user_data,
                                    score_system_status_t *status);

/* Destroy the score system.  Safe to call with NULL. */
void score_system_destroy(score_system_t *ctx);

/* =========================================================================
 * Score operations
 * ========================================================================= */

/* Set the score to an exact value.  Resets extra life tracking. */
void score_system_set(score_system_t *ctx, u_long value);

/* Return the current score. */
u_long score_system_get(const score_system_t *ctx);

/*
 * Add points to the score with multiplier applied.
 *
 * Uses score_apply_multiplier() with the env's x2/x4 flags, then
 * adds the result to the current score.  Checks for extra life awards.
 * Fires on_score_changed and possibly on_extra_life callbacks.
 * Returns the new score value.
 */
u_long score_system_add(score_system_t *ctx, u_long increment, const score_system_env_t *env);

/*
 * Add raw points without multiplier.
 *
 * Used for bonus score commits where multiplier was already applied.
 * Checks for extra life awards.  Fires callbacks.
 * Returns the new score value.
 */
u_long score_system_add_raw(score_system_t *ctx, u_long increment);

/* =========================================================================
 * Digit layout computation
 * ========================================================================= */

/*
 * Compute the digit layout for rendering a score value.
 *
 * The layout matches the legacy DrawOutNumber right-aligned rendering:
 * rightmost digit at x = SCORE_WINDOW_WIDTH - SCORE_DIGIT_STRIDE,
 * each subsequent digit SCORE_DIGIT_STRIDE pixels to the left.
 *
 * For score==0, returns a single digit 0 at x=192 (matching legacy).
 * For any value, digits are ordered most-significant first.
 */
void score_system_get_digit_layout(u_long score_value, score_system_digit_layout_t *layout);

/* =========================================================================
 * Extra life tracking
 * ========================================================================= */

/* Return the current extra life threshold index. */
int score_system_get_life_threshold(const score_system_t *ctx);

/* =========================================================================
 * Utility
 * ========================================================================= */

/* Return a human-readable string for a status code. */
const char *score_system_status_string(score_system_status_t status);

#endif /* SCORE_SYSTEM_H */
