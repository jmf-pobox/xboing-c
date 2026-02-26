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
    BLOCK_SYS_ERR_OUT_OF_BOUNDS /* Row/col out of grid range */
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
 * Preserves legacy AddNewBlock() bounds check bug: row > MAX_ROW and
 * col > MAX_COL are accepted (should be >= MAX_ROW/MAX_COL).
 */
block_system_status_t block_system_add(block_system_t *ctx, int row, int col, int block_type,
                                       int counter_slide, int frame);

/* Clear a single block to defaults (unoccupied, NONE_BLK). */
block_system_status_t block_system_clear(block_system_t *ctx, int row, int col);

/* Clear all blocks in the grid. */
block_system_status_t block_system_clear_all(block_system_t *ctx);

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
