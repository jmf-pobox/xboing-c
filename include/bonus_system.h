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
/* Sub-frames between content-state transitions (e.g. SCORE→BONUS).
 * The original uses LINE_DELAY=100 game ticks at SLOW_SPEED=30 ms
 * (original/bonus.c:88, main.h:83), giving ~3 seconds of WAIT
 * between each content line — enough time for the player to read
 * the line before the next renders.  Modern at default speed
 * runs ATTRACT_FRAME_MULTIPLIER=6 sub-frames per 7.5 ms game
 * tick = ~1.25 ms/sub-frame, so 2400 sub-frames ≈ 3 seconds —
 * exact wall-clock match.
 *
 * Visual capture takes ~25 s per scenario at this value; that
 * tooling cost is fine for the once-per-release run, and the
 * gameplay readability matters more. */
#define BONUS_LINE_DELAY 2400

/* Sub-frames for the very first TEXT→SCORE snap.  Original
 * (bonus.c:257) used `frame + 5` at SLOW_SPEED=30 ms ≈ 150 ms —
 * a brief beat before "Congratulations" appears.  120 sub-frames
 * ≈ 150 ms at default modern speed. */
#define BONUS_INIT_DELAY 120

/* Per-step pacing for coin/bullet animations within
 * BONUS_STATE_BONUS / BONUS_STATE_BULLET.  The original drops one
 * item per main-loop iteration at SLOW_SPEED=30 ms/tick
 * (original/main.h:83 and the SetGameSpeed(SLOW_SPEED) call at
 * the head of each DoX function) — 30 ms per coin / per bullet.
 *
 * Modern equivalent at default speed (SDL2L_DEFAULT_SPEED=5):
 *   sdl2_loop tick interval = SDL2L_TICK_UNIT_US * (10 - speed)
 *                            = 1500 µs * 5 = 7.5 ms / game tick
 *   ATTRACT_FRAME_MULTIPLIER = 6 bonus_system_update calls per
 *   game tick → each sub-frame = 7.5 ms / 6 = 1.25 ms.
 *
 * Original 30 ms / 1.25 ms per sub-frame = 24 sub-frames per
 * step.  References: original/bonus.c:355-373 (DoBonuses coin
 * loop) and original/bonus.c:464-489 (DoBullets bullet loop). */
#define BONUS_STEP_DELAY 24

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

/*
 * Set the bonus coin count from an external source.  Called once
 * at MODE_BONUS entry to sync the gameplay-truth field
 * (game_ctx_t::bonus_count) into the renderer-side accumulator.
 * Must precede bonus_system_begin — see ADR-040.  Negative counts
 * and a NULL ctx are no-ops.
 */
void bonus_system_set_coins(bonus_system_t *ctx, int count);

/* Return the current bonus coin count */
int bonus_system_get_coins(const bonus_system_t *ctx);

/*
 * Return the initial coin count captured at bonus_system_begin.
 * Used by the renderer to compute how many coin sprites to draw during
 * the per-frame decrement animation: drawn = initial - live.
 */
int bonus_system_get_initial_coins(const bonus_system_t *ctx);

/* Return the current bullet count (decrements during BONUS_STATE_BULLET) */
int bonus_system_get_bullets(const bonus_system_t *ctx);

/*
 * Return the initial bullet count captured at bonus_system_begin.
 * Renderer counterpart to bonus_system_get_initial_coins for bullet rows.
 */
int bonus_system_get_initial_bullets(const bonus_system_t *ctx);

/*
 * Return the level time-bonus seconds captured at bonus_system_begin.
 * Renderer uses this to detect the "timer ran out" path: when
 * time_bonus_secs == 0 the state machine skips coin animation and
 * the renderer must show "Bonus coins void - Timer ran out!" instead.
 * Matches original/bonus.c:288-303.
 */
int bonus_system_get_time_bonus_secs(const bonus_system_t *ctx);

/* Reset the bonus coin count to 0 */
void bonus_system_reset_coins(bonus_system_t *ctx);

/* =========================================================================
 * Queries
 * ========================================================================= */

/* Return the current state machine state */
bonus_state_t bonus_system_get_state(const bonus_system_t *ctx);

/*
 * Return the highest content state the sequence has reached so far.
 * Always in TEXT..END_TEXT (WAIT and FINISH excluded).  Monotonic
 * within a single begin()..finish cycle; reset by begin().
 *
 * Useful where the raw state value misleads — e.g., during WAIT
 * (enum value 8) the renderer needs to know "we just finished
 * SCORE" rather than "we're at value 8 which is >= END_TEXT (7)".
 * Visual-capture uses the same getter to detect substate
 * transitions across the multi-update-per-tick attract loop.
 */
bonus_state_t bonus_system_get_highest_reached(const bonus_system_t *ctx);

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
