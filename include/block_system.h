#ifndef BLOCK_SYSTEM_H
#define BLOCK_SYSTEM_H

/*
 * block_system.h — Pure C block grid system with callback-based side effects.
 *
 * Owns the 18x9 block grid, collision geometry (pure C triangles replacing
 * X11 Region objects), block info catalog, and grid queries.  Communicates
 * side effects through an injected callback table.  Zero dependency on
 * SDL2 or X11.
 *
 * Collision detection uses diagonal cross-products to determine which
 * triangular quadrant of a block the ball center falls in — replacing
 * XPolygonRegion/XRectInRegion with ~10 lines of integer math.
 *
 * Opaque context pattern: no globals, fully testable with CMocka stubs.
 * See ADR-016 in docs/DESIGN.md for design rationale.
 */

#include "block_types.h"

/* =========================================================================
 * Status codes
 * ========================================================================= */

typedef enum
{
    BLOCK_SYS_OK = 0,
    BLOCK_SYS_ERR_NULL_ARG,
    BLOCK_SYS_ERR_ALLOC_FAILED,
    BLOCK_SYS_ERR_OUT_OF_BOUNDS, /* Row/col out of grid range */
    BLOCK_SYS_ERR_INVALID_STATE  /* Pre-condition failed (e.g. unoccupied, already exploding) */
} block_system_status_t;

/* =========================================================================
 * Collision region results
 *
 * Sequential values matching BALL_REGION_* in ball_system.h.
 * block_system_check_region() can be used directly as the check_region
 * callback for ball_system_t.
 * ========================================================================= */

#define BLOCK_REGION_NONE 0
#define BLOCK_REGION_TOP 1
#define BLOCK_REGION_BOTTOM 2
#define BLOCK_REGION_LEFT 3
#define BLOCK_REGION_RIGHT 4

/* =========================================================================
 * Constants — match legacy blocks.h values
 * ========================================================================= */

#define BLOCK_WIDTH 40
#define BLOCK_HEIGHT 20
#define BLOCK_SPACE 7

#define BLOCK_EXPLODE_DELAY 10
#define BLOCK_BONUS_DELAY 150
#define BLOCK_BONUS_LENGTH 1500
#define BLOCK_DEATH_DELAY1 100
#define BLOCK_DEATH_DELAY2 700
#define BLOCK_EXTRABALL_DELAY 300
#define BLOCK_RANDOM_DELAY 500
#define BLOCK_DROP_DELAY 1000
#define BLOCK_INFINITE_DELAY 9999999
#define BLOCK_ROAM_EYES_DELAY 300
#define BLOCK_ROAM_DELAY 1000
#define BLOCK_EXTRA_TIME 20
#define BLOCK_SHOTS_TO_KILL_SPECIAL 3
#define BLOCK_NUMBER_OF_BULLETS_NEW_LEVEL 4

/* =========================================================================
 * Block info catalog entry — per-type metadata
 * ========================================================================= */

typedef struct
{
    int block_type; /* Block type ID (index into catalog) */
    int width;      /* Pixmap width in pixels */
    int height;     /* Pixmap height in pixels */
    int slide;      /* Animation frame count */
} block_system_info_t;

/* =========================================================================
 * Render info — read-only snapshot for the integration layer to draw
 * ========================================================================= */

typedef struct
{
    int occupied;
    int block_type;
    int hit_points;
    int x, y;          /* Pixel position */
    int width, height; /* Pixel size */
    int exploding;     /* Nonzero if exploding */
    int explode_slide; /* Explosion animation frame */
    int counter_slide; /* COUNTER_BLK hit counter */
    int bonus_slide;   /* Bonus/animation frame index */
    int random;        /* RANDOM_BLK flag */
    int drop;          /* DROP_BLK flag */
    int special_popup; /* Dynamically spawned block flag */
    int explode_all;   /* Dynamite overlay flag */
} block_system_render_info_t;

/* =========================================================================
 * Opaque context
 * ========================================================================= */

typedef struct block_system block_system_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/*
 * Create a block system context.  All grid cells start cleared (unoccupied).
 * Initializes the block info catalog (type metadata).
 *
 * col_width:  pixel width per grid column  (typically PLAY_WIDTH / MAX_COL = 55)
 * row_height: pixel height per grid row    (typically PLAY_HEIGHT / MAX_ROW = 32)
 *
 * Returns NULL on allocation failure (sets *status if non-NULL).
 */
block_system_t *block_system_create(int col_width, int row_height, block_system_status_t *status);

/* Destroy the block system.  Safe to call with NULL. */
void block_system_destroy(block_system_t *ctx);

/* =========================================================================
 * Block management
 * ========================================================================= */

/*
 * Add a block at grid position (row, col).
 *
 * Clears any existing block at that position, sets the block type and
 * counter_slide, calculates pixel geometry (position, size, centered
 * within the cell), and assigns hit points.
 *
 * frame: current game frame (used for animation timing on RANDOM, DROP,
 *        ROAMER, EXTRABALL, DEATH block types).
 *
 * Fixes legacy AddNewBlock() bounds check bug: row >= MAX_ROW and
 * col >= MAX_COL are now correctly rejected (legacy used > instead of >=).
 */
block_system_status_t block_system_add(block_system_t *ctx, int row, int col, int block_type,
                                       int counter_slide, int frame);

/* Clear a single block to defaults (unoccupied, NONE_BLK). */
block_system_status_t block_system_clear(block_system_t *ctx, int row, int col);

/* Clear all blocks in the grid. */
block_system_status_t block_system_clear_all(block_system_t *ctx);

/*
 * Callback invoked at explosion finalize (one tick after stage 4).
 * Receives the block's saved type and hit_points so the integration layer
 * can apply score + per-type finalize-only side effects (BOMB chain,
 * BULLET +4 ammo, BONUS counter, X2/X4 toggles, etc.).
 *
 * Matches original/blocks.c:1547 (AddToScore) and the per-type switch at
 * original/blocks.c:1550-1637 — all of which fire at finalize, not hit time.
 *
 * Ordering invariant: clear_entry runs BEFORE the callback, so the cell
 * is unoccupied AND reusable at callback time
 * (block_system_is_occupied returns 0; block_system_cell_available returns 1).
 * Saved block_type and hit_points are passed by parameter — the cell no
 * longer holds them.
 */
typedef void (*block_system_finalize_cb_t)(int row, int col, int block_type, int hit_points,
                                           void *ud);

/*
 * Trigger an explosion at (row, col).  Sets exploding=1,
 * explode_start_frame=frame, explode_next_frame=frame, explode_slide=1.
 * Increments the global blocks-exploding counter.
 *
 * Pre-conditions enforced:
 *   - Cell occupied AND not already exploding (original/blocks.c:1825).
 *   - block_type != HYPERSPACE_BLK (original/blocks.c:1821-1822 immunity).
 *
 * Returns BLOCK_SYS_OK if armed, BLOCK_SYS_ERR_OUT_OF_BOUNDS for bad
 * coordinates, BLOCK_SYS_ERR_NULL_ARG for ctx==NULL, or
 * BLOCK_SYS_ERR_INVALID_STATE if pre-conditions failed.
 *
 * Callers in chain-reaction contexts (e.g. BOMB_BLK) intentionally
 * discard the return value — overlapping chains may try to re-arm an
 * already-exploding neighbor and silently skip, matching the original
 * outer-if no-op at original/blocks.c:1825.
 *
 * The cell remains "occupied" through the animation.  Do NOT call
 * block_system_clear on a cell already exploding — occupancy is cleared
 * at finalize.
 */
block_system_status_t block_system_explode(block_system_t *ctx, int row, int col, int frame);

/*
 * Per-tick driver for the explosion state machine.  Advances every
 * exploding block one stage if its explode_next_frame matches `frame`.
 * Calls `cb` (if non-NULL) for every block reaching finalize this tick.
 * Matches original/blocks.c:1480-1646 ExplodeBlocksPending.
 *
 * Stage cadence (slide values 1..4, with BLOCK_EXPLODE_DELAY=10 ticks
 * between):
 *   slide=1: render path draws explosion sprite frame 0 (slide-1 offset)
 *   slide=2: render path draws explosion sprite frame 1
 *   slide=3: render path draws explosion sprite frame 2
 *   slide=4: render path SKIPS sprite draw (clear-only frame)
 *   slide>4: finalize — clear cell, decrement blocks_exploding, fire cb
 *
 * Same-tick stage-1 behavior: trigger at frame F sets
 * explode_next_frame=F.  If block_system_update_explosions is called at
 * frame=F immediately after arming, stage 1 fires AND slide increments
 * to 2, next_frame to F+10.  Matches original/blocks.c:1502 equality
 * comparison and post-switch increment at :1537-1538.
 *
 * Call once per game tick from the gameplay update loop.
 */
void block_system_update_explosions(block_system_t *ctx, int frame, block_system_finalize_cb_t cb,
                                    void *ud);

/*
 * Advance per-block animation slides for animated block types based on the
 * current frame counter.  Updates `bonus_slide` for BONUSX2/X4/BONUS_BLK
 * (4-frame cycle, BLOCK_BONUS_DELAY interval), DEATH_BLK (5-frame cycle,
 * BLOCK_DEATH_DELAY1 interval), EXTRABALL_BLK (2-frame cycle,
 * BLOCK_EXTRABALL_DELAY interval), and ROAMER_BLK (5-direction cycle,
 * BLOCK_ROAM_EYES_DELAY interval).
 *
 * Call once per game tick (typically from game_modes after gun_system_update).
 * Without this, sprite_block_animated_key sees a static bonus_slide=0 and
 * all animated blocks render as frame 1 forever.
 */
void block_system_advance_animations(block_system_t *ctx, int frame);

/*
 * Handle a bullet hit on the block at (row, col).
 *
 * Encapsulates the per-block-type hit logic from original/gun.c:318-350.
 *
 * Returns 1 if the bullet was absorbed (block still occupied):
 *   - HYPERSPACE_BLK and BLACK_BLK always absorb (original/gun.c:341-350).
 *   - Multi-hit specials (REVERSE_BLK, MGUN_BLK, STICKY_BLK, WALLOFF_BLK,
 *     MULTIBALL_BLK, PAD_EXPAND_BLK, PAD_SHRINK_BLK, DEATH_BLK, COUNTER_BLK)
 *     decrement counterSlide; absorb while counterSlide > 0 after decrement
 *     (original/gun.c:325-340).
 *
 * Returns 0 in any of the following cases:
 *   - Block was destroyed (all other types, or multi-hit specials whose
 *     counterSlide reached 0).  Caller must arm the explosion lifecycle
 *     by calling block_system_explode(ctx, row, col, frame) — this
 *     function no longer clears the block, so finalize-time effects
 *     (BULLET +4 ammo, MAXAMMO unlimited, etc.) fire via the
 *     block-finalize callback rather than at hit time.
 *   - ctx is NULL.
 *   - (row, col) is out of bounds.
 *   - The cell is already unoccupied.
 *
 * The four 0-return cases are not distinguishable from the function's
 * return value alone. Callers needing to distinguish should check
 * block_system_is_occupied before invoking.
 */
int block_system_decrement_gun_hit(block_system_t *ctx, int row, int col);

/* =========================================================================
 * Collision detection — designed as ball_system callbacks
 *
 * These functions match the callback signatures in ball_system.h:
 *   check_region: int (*)(int row, int col, int bx, int by, int bdx, void *ud)
 *   cell_available: int (*)(int row, int col, void *ud)
 *
 * Pass block_system_t* as the user_data (ud) parameter.
 * ========================================================================= */

/*
 * Check if a ball at center (bx, by) collides with the block at (row, col).
 *
 * Returns BLOCK_REGION_NONE if no collision, or BLOCK_REGION_TOP/BOTTOM/
 * LEFT/RIGHT indicating which face of the block was hit.
 *
 * Algorithm:
 *   1. Bounds check and occupancy/exploding guard.
 *   2. AABB overlap test (ball bounding box vs block bounding box).
 *   3. Diagonal cross-product test: the block rectangle is divided into
 *      4 triangles by its diagonals.  The ball center determines which
 *      triangle it falls in → which face was hit.
 *   4. Adjacency filter: if the neighboring cell in the hit direction is
 *      occupied, suppress the hit (prevents phantom bounces at junctions).
 *
 * ud must be a block_system_t* (cast from void*).
 * bdx is accepted for callback compatibility but not used.
 */
int block_system_check_region(int row, int col, int bx, int by, int bdx, void *ud);

/*
 * Return nonzero if cell (row, col) is available for placement.
 * A cell is available if it is within bounds, unoccupied, and not exploding.
 *
 * ud must be a block_system_t* (cast from void*).
 */
int block_system_cell_available(int row, int col, void *ud);

/* =========================================================================
 * Queries
 * ========================================================================= */

/* Return nonzero if the block at (row, col) is occupied. */
int block_system_is_occupied(const block_system_t *ctx, int row, int col);

/* Return the block type at (row, col), or NONE_BLK if empty/out-of-bounds. */
int block_system_get_type(const block_system_t *ctx, int row, int col);

/* Return the hit points of the block at (row, col), or 0. */
int block_system_get_hit_points(const block_system_t *ctx, int row, int col);

/*
 * Return nonzero if the level still has required blocks.
 *
 * Required blocks: color blocks (RED..PURPLE), COUNTER_BLK, DROP_BLK.
 * Also returns nonzero if blocks_exploding > 1 (explosions still pending).
 * Matches legacy StillActiveBlocks().
 */
int block_system_still_active(const block_system_t *ctx);

/* Return the count of blocks currently in explosion animation. */
int block_system_get_exploding_count(const block_system_t *ctx);

/* Fill render info for block at (row, col).  Returns error on bad coords. */
block_system_status_t block_system_get_render_info(const block_system_t *ctx, int row, int col,
                                                   block_system_render_info_t *info);

/* Get the block info catalog entry for a block type (0..MAX_BLOCKS-1). */
const block_system_info_t *block_system_get_info(const block_system_t *ctx, int block_type);

/* =========================================================================
 * Utility
 * ========================================================================= */

/* Return a human-readable string for a status code. */
const char *block_system_status_string(block_system_status_t status);

#endif /* BLOCK_SYSTEM_H */
