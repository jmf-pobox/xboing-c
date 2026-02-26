#ifndef BONUS_SYSTEM_H
#define BONUS_SYSTEM_H

/*
 * bonus_system.h — Pure C bonus tally sequence state machine.
 *
 * Owns the 10-state bonus screen sequence (text, score tallies, high score
 * rank, end), bonus coin counting during gameplay, score computation, and
 * save trigger logic (every SAVE_LEVEL levels).
 *
 * All rendering is delegated to the integration layer.  The module fires
 * callbacks for side effects (score addition, sound, save trigger) and
 * provides query functions for rendering state.
 *
 * Opaque context pattern: no globals, fully testable with CMocka stubs.
 * See ADR-022 in docs/DESIGN.md for design rationale.
 */

/* =========================================================================
 * Constants — match legacy bonus.c / bonus.h values
 * ========================================================================= */

#define BONUS_COIN_SCORE 3000   /* Points per bonus coin */
#define BONUS_SUPER_SCORE 50000 /* Replaces per-coin when count > MAX_BONUS */
#define BONUS_BULLET_SCORE 500  /* Points per unused bullet */
#define BONUS_LEVEL_SCORE 100   /* Multiplied by adjusted level number */
#define BONUS_TIME_SCORE 100    /* Points per second remaining */
#define BONUS_MAX_COINS 8       /* Threshold: > 8 triggers super bonus */
#define BONUS_SAVE_LEVEL 5      /* Save enabled every N levels */
#define BONUS_LINE_DELAY 100    /* Frames between state transitions */

/* =========================================================================
 * State machine states — match legacy enum BonusStates
 * ========================================================================= */

typedef enum
{
    BONUS_STATE_TEXT = 0, /* Title header text */
    BONUS_STATE_SCORE,    /* "Congratulations" line */
    BONUS_STATE_BONUS,    /* Coin tally (animated or one-shot) */
    BONUS_STATE_LEVEL,    /* Level score */
    BONUS_STATE_BULLET,   /* Bullet tally (animated) */
    BONUS_STATE_TIME,     /* Time bonus */
    BONUS_STATE_HSCORE,   /* High score ranking */
    BONUS_STATE_END_TEXT, /* "Prepare for level N+1" */
    BONUS_STATE_WAIT,     /* Timer wait state */
    BONUS_STATE_FINISH,   /* Terminal: sequence complete */
} bonus_state_t;

/* =========================================================================
 * Environment struct — replaces extern globals
 * ========================================================================= */

typedef struct
{
    unsigned long score; /* Current total score (post-bonus) */
    int level;           /* Current level number (1-based) */
    int starting_level;  /* Level player started at */
    int time_bonus_secs; /* Seconds remaining on timer (0 = expired) */
    int bullet_count;    /* Bullets remaining */
    int highscore_rank;  /* High score rank (0=unranked, 1-10=ranked) */
} bonus_system_env_t;

/* =========================================================================
 * Callback table — injected at creation time
 * ========================================================================= */

typedef struct
{
    /* Score addition — called during begin() for the total bonus */
    void (*on_score_add)(unsigned long points, void *ud);

    /* A bullet was consumed during the bullet tally animation */
    void (*on_bullet_consumed)(void *ud);

    /* Save should be enabled (every SAVE_LEVEL levels) */
    void (*on_save_triggered)(void *ud);

    /* Sound effect requested */
    void (*on_sound)(const char *name, void *ud);

    /* Bonus sequence is complete; next_level is the level to start */
    void (*on_finished)(int next_level, void *ud);
} bonus_system_callbacks_t;

/* =========================================================================
 * Opaque context
 * ========================================================================= */

typedef struct bonus_system bonus_system_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/*
 * Create a bonus system context.
 *
 * callbacks: callback table (copied).  NULL is safe.
 * user_data: opaque pointer passed to all callbacks.
 *
 * Initial state: no bonus sequence active, coin count = 0.
 * Returns NULL on allocation failure.
 */
bonus_system_t *bonus_system_create(const bonus_system_callbacks_t *callbacks, void *user_data);

/* Destroy the bonus system.  Safe to call with NULL. */
void bonus_system_destroy(bonus_system_t *ctx);

/* =========================================================================
 * Bonus sequence lifecycle
 * ========================================================================= */

/*
 * Begin the bonus sequence.
 *
 * Computes and commits the total bonus score (fires on_score_add),
 * resets the state machine to BONUS_STATE_TEXT, and stores the
 * environment for subsequent update calls.
 *
 * env: current game state snapshot.  Copied internally.
 * frame: current frame counter.
 */
void bonus_system_begin(bonus_system_t *ctx, const bonus_system_env_t *env, int frame);

/*
 * Advance the bonus sequence by one frame.
 *
 * Drives the state machine: processes timers, fires callbacks for
 * coin/bullet animations, and transitions between states.
 *
 * frame: current frame counter.
 *
 * Returns the current state after the update.
 */
bonus_state_t bonus_system_update(bonus_system_t *ctx, int frame);

/*
 * Skip to the end of the bonus sequence (space bar).
 *
 * Sets the wait target to the current frame so BONUS_STATE_FINISH
 * triggers on the next update.
 *
 * frame: current frame counter.
 */
void bonus_system_skip(bonus_system_t *ctx, int frame);

/* =========================================================================
 * Coin counting (during gameplay)
 * ========================================================================= */

/* Increment the bonus coin count (called when player collects a BONUS_BLK) */
void bonus_system_inc_coins(bonus_system_t *ctx);

/* Decrement the bonus coin count (internal animation use) */
void bonus_system_dec_coins(bonus_system_t *ctx);

/* Return the current bonus coin count */
int bonus_system_get_coins(const bonus_system_t *ctx);

/* Reset the bonus coin count to 0 */
void bonus_system_reset_coins(bonus_system_t *ctx);

/* =========================================================================
 * Queries
 * ========================================================================= */

/* Return the current state machine state */
bonus_state_t bonus_system_get_state(const bonus_system_t *ctx);

/* Return true if the bonus sequence is complete */
int bonus_system_is_finished(const bonus_system_t *ctx);

/* Return the display score (for rendering the score counter) */
unsigned long bonus_system_get_display_score(const bonus_system_t *ctx);

/* =========================================================================
 * Score computation (stateless utility)
 * ========================================================================= */

/*
 * Compute the total bonus points for a completed level.
 *
 * Returns the total bonus.  Does not modify any state.
 * This is the pure computation that begin() commits via on_score_add.
 */
unsigned long bonus_system_compute_total(int coin_count, int level, int starting_level,
                                         int time_bonus_secs, int bullet_count);

/*
 * Check if save should be triggered for the given level.
 *
 * Returns 1 if (level - starting_level + 1) is a multiple of SAVE_LEVEL.
 */
int bonus_system_should_save(int level, int starting_level);

#endif /* BONUS_SYSTEM_H */
