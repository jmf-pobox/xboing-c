#ifndef GUN_SYSTEM_H
#define GUN_SYSTEM_H

/*
 * gun_system.h — Pure C gun/bullet system with callback-based side effects.
 *
 * Owns bullet and tink arrays, ammo state, movement physics, and collision
 * dispatch.  Communicates side effects (sound, block damage, ball kill,
 * eyedude hit) through an injected callback table.  Zero dependency on
 * SDL2 or X11.
 *
 * Opaque context pattern: no globals, fully testable with CMocka stubs.
 * See ADR-018 in docs/DESIGN.md for design rationale.
 */

/* =========================================================================
 * Status codes
 * ========================================================================= */

typedef enum
{
    GUN_SYS_OK = 0,
    GUN_SYS_ERR_NULL_ARG,
    GUN_SYS_ERR_ALLOC_FAILED,
    GUN_SYS_ERR_OUT_OF_BOUNDS,
} gun_system_status_t;

/* =========================================================================
 * Constants — match legacy gun.c / gun.h values
 * ========================================================================= */

/* Ammo */
#define GUN_MAX_AMMO 20      /* Hard cap on ammo count */
#define GUN_AMMO_PER_LEVEL 4 /* Ammo given at level start */

/* Bullet physics */
#define GUN_BULLET_DY (-7) /* Pixels per update (negative = upward) */
#define GUN_BULLET_WIDTH 7
#define GUN_BULLET_HEIGHT 16
#define GUN_BULLET_WC (GUN_BULLET_WIDTH / 2)  /* = 3 */
#define GUN_BULLET_HC (GUN_BULLET_HEIGHT / 2) /* = 8 */
#define GUN_BULLET_FRAME_RATE 3               /* Bullets update every Nth frame */
#define GUN_MAX_BULLETS 40                    /* Bullet slots in flight */

/* Tink impact effects */
#define GUN_TINK_WIDTH 10
#define GUN_TINK_HEIGHT 5
#define GUN_TINK_WC (GUN_TINK_WIDTH / 2)  /* = 5 */
#define GUN_TINK_HC (GUN_TINK_HEIGHT / 2) /* = 2 */
#define GUN_TINK_DELAY 100                /* Frames a tink stays visible */
#define GUN_MAX_TINKS 40                  /* Tink slots */

/* Bullet spawn Y position offset from play area bottom */
#define GUN_BULLET_START_OFFSET 40 /* BULLET_START_Y = play_height - 40 */

/* =========================================================================
 * Callback table — injected at creation time
 * ========================================================================= */

typedef struct
{
    /*
     * Check if bullet at center (bx, by) hits a block.
     * Returns nonzero on hit, sets *out_row, *out_col to the hit block's
     * grid position.  The integration layer uses block_system for the
     * actual AABB overlap check against the block grid.
     */
    int (*check_block_hit)(int bx, int by, int *out_row, int *out_col, void *ud);

    /*
     * Report bullet hit on block at (row, col).
     * The integration layer handles type-specific behavior:
     *   - COUNTER_BLK: decrement counter, kill at 0
     *   - HYPERSPACE/BLACK: absorb (block survives)
     *   - Special blocks: decrement counter (SHOTS_TO_KILL=3), kill at 0
     *   - Everything else: kill immediately
     */
    void (*on_block_hit)(int row, int col, void *ud);

    /*
     * Check if bullet at center (bx, by) hits any active ball.
     * Returns ball index (0..MAX_BALLS-1) on hit, or -1 for miss.
     */
    int (*check_ball_hit)(int bx, int by, void *ud);

    /* Report bullet killed ball at given index. */
    void (*on_ball_hit)(int ball_index, void *ud);

    /*
     * Check if bullet at center (bx, by) hits the eyedude.
     * Called whenever a bullet is active; implementation must return 0 when
     * the eyedude is not hittable (e.g., not in walk mode).  Returns
     * nonzero on hit.
     */
    int (*check_eyedude_hit)(int bx, int by, void *ud);

    /* Report bullet killed the eyedude. */
    void (*on_eyedude_hit)(void *ud);

    /* Sound playback. */
    void (*on_sound)(const char *name, void *ud);

    /*
     * Ball-waiting query: returns nonzero if ball is waiting on paddle.
     * Shooting is blocked when ball is waiting.
     */
    int (*is_ball_waiting)(void *ud);
} gun_system_callbacks_t;

/* =========================================================================
 * Per-frame environment — replaces extern globals
 * ========================================================================= */

typedef struct
{
    int frame;       /* Current game frame counter */
    int paddle_pos;  /* Paddle center X position */
    int paddle_size; /* Paddle pixel width (40/50/70) */
    int fast_gun;    /* Fast gun mode flag (nonzero = machine gun) */
} gun_system_env_t;

/* =========================================================================
 * Render info — read-only snapshots for the integration layer
 * ========================================================================= */

typedef struct
{
    int active;           /* Nonzero if this slot is in use */
    int x;                /* Center X position */
    int y;                /* Center Y position */
    int from_y;           /* Y before last movement (for interpolation) */
    int ticks_since_move; /* Ticks since last position change or spawn */
} gun_system_bullet_info_t;

typedef struct
{
    int active; /* Nonzero if this slot is in use */
    int x;      /* Center X position */
} gun_system_tink_info_t;

/* =========================================================================
 * Opaque context
 * ========================================================================= */

typedef struct gun_system gun_system_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/*
 * Create a gun system context.
 *
 * play_height: playfield height (580) — used for bullet spawn Y.
 * callbacks:   callback table (copied into context, caller retains ownership).
 *              NULL callbacks are safe — those features are simply disabled.
 * user_data:   opaque pointer passed to all callbacks.
 *
 * Initial state: 0 ammo, all bullets and tinks cleared, unlimited off.
 * Returns NULL on allocation failure (sets *status if non-NULL).
 */
gun_system_t *gun_system_create(int play_height, const gun_system_callbacks_t *callbacks,
                                void *user_data, gun_system_status_t *status);

/* Destroy the gun system.  Safe to call with NULL. */
void gun_system_destroy(gun_system_t *ctx);

/* =========================================================================
 * Per-frame update
 * ========================================================================= */

/*
 * Update the gun system for one frame.
 *
 * Every BULLET_FRAME_RATE (3) frames: moves bullets, checks collisions.
 * Every frame: expires old tinks.
 *
 * Collision priority (matching legacy): ball > eyedude > block.
 * A bullet that hits a ball is consumed before checking blocks.
 */
void gun_system_update(gun_system_t *ctx, const gun_system_env_t *env);

/* =========================================================================
 * Player actions
 * ========================================================================= */

/*
 * Attempt to fire a bullet.
 *
 * Guards: requires ammo > 0 AND ball not waiting (via callback).
 * Normal mode: spawns 1 bullet at paddle center.
 * Fast gun mode: spawns 2 bullets at ±(paddle_size/3) from center.
 *
 * Returns nonzero if at least one bullet was spawned.
 * Plays "shotgun" on success, "click" on no-ammo.
 */
int gun_system_shoot(gun_system_t *ctx, const gun_system_env_t *env);

/* =========================================================================
 * Ammo management
 * ========================================================================= */

/* Set ammo count directly (0..GUN_MAX_AMMO, or above for unlimited). */
void gun_system_set_ammo(gun_system_t *ctx, int count);

/* Add one ammo, clamped at GUN_MAX_AMMO. */
void gun_system_add_ammo(gun_system_t *ctx);

/*
 * Consume one ammo.  No-op if unlimited mode is on.
 * Returns the new ammo count.
 */
int gun_system_use_ammo(gun_system_t *ctx);

/* Return current ammo count. */
int gun_system_get_ammo(const gun_system_t *ctx);

/* Set unlimited ammo mode (0 = off, nonzero = on). */
void gun_system_set_unlimited(gun_system_t *ctx, int on);

/* Return nonzero if unlimited ammo is active. */
int gun_system_get_unlimited(const gun_system_t *ctx);

/* =========================================================================
 * Reset
 * ========================================================================= */

/* Clear all bullets and tinks (level start).  Ammo is NOT reset. */
void gun_system_clear(gun_system_t *ctx);

/* =========================================================================
 * Render queries
 * ========================================================================= */

/*
 * Get bullet info for slot index (0..GUN_MAX_BULLETS-1).
 * info->active is 0 if the slot is empty.
 */
gun_system_status_t gun_system_get_bullet_info(const gun_system_t *ctx, int index,
                                               gun_system_bullet_info_t *info);

/*
 * Get tink info for slot index (0..GUN_MAX_TINKS-1).
 * info->active is 0 if the slot is empty.
 */
gun_system_status_t gun_system_get_tink_info(const gun_system_t *ctx, int index,
                                             gun_system_tink_info_t *info);

/* Return number of active (in-flight) bullets. */
int gun_system_get_active_bullet_count(const gun_system_t *ctx);

/* Return number of active tinks. */
int gun_system_get_active_tink_count(const gun_system_t *ctx);

/* =========================================================================
 * Utility
 * ========================================================================= */

/* Return a human-readable string for a status code. */
const char *gun_system_status_string(gun_system_status_t status);

#endif /* GUN_SYSTEM_H */
