/*
 * block_system.c — Pure C block grid system.
 *
 * Owns the 18x9 block grid, collision geometry, block info catalog,
 * and grid queries.  Replaces X11 Region objects (XPolygonRegion,
 * XRectInRegion) with diagonal cross-product math for collision detection.
 *
 * See ADR-016 in docs/DESIGN.md for design rationale.
 */

#include "block_system.h"
#include "ball_types.h"  /* BALL_WC, BALL_HC, BALL_WIDTH, BALL_HEIGHT */
#include "score_logic.h" /* score_block_hit_points() */

#include <stdlib.h>

/* =========================================================================
 * Internal block entry — replaces legacy struct aBlock
 *
 * No X11 Region pointers.  Collision geometry is computed on-the-fly
 * from (x, y, width, height) using diagonal cross-products.
 * ========================================================================= */

typedef struct
{
    /* General properties */
    int occupied;
    int block_type;
    int hit_points;

    /* Explosion state machine */
    int exploding;
    int explode_start_frame;
    int explode_next_frame;
    int explode_slide;

    /* Animation state */
    int current_frame;
    int next_frame;
    int last_frame;

    /* Pixel geometry (replaces X11 Region objects) */
    int offset_x, offset_y;
    int x, y;
    int width, height;

    /* Type-specific state */
    int counter_slide;
    int bonus_slide;
    int random;
    int drop;
    int special_popup;
    int explode_all;

    /* Ball tracking (for multiball split) */
    int ball_hit_index;
    int ball_dx, ball_dy;
} block_entry_t;

/* =========================================================================
 * Opaque context
 * ========================================================================= */

struct block_system
{
    block_entry_t blocks[MAX_ROW][MAX_COL];
    block_system_info_t info[MAX_BLOCKS];
    int blocks_exploding;
    int col_width;
    int row_height;
};

/* =========================================================================
 * Static helpers
 * ========================================================================= */

/*
 * Clear a single block entry to defaults.
 * Mirrors legacy ClearBlock() (blocks.c:2528-2598) minus the XDestroyRegion calls.
 */
static void clear_entry(block_entry_t *bp, int *blocks_exploding)
{
    if (bp->exploding && *blocks_exploding > 0)
    {
        (*blocks_exploding)--;
    }

    bp->occupied = 0;
    bp->exploding = 0;
    bp->x = 0;
    bp->y = 0;
    bp->width = 0;
    bp->height = 0;
    bp->hit_points = 0;
    bp->block_type = NONE_BLK;
    bp->explode_start_frame = 0;
    bp->explode_next_frame = 0;
    bp->explode_slide = 0;
    bp->counter_slide = 0;
    bp->bonus_slide = 0;
    bp->offset_y = 0;
    bp->offset_x = 0;
    bp->last_frame = BLOCK_INFINITE_DELAY;
    bp->next_frame = 0;
    bp->current_frame = 0;
    bp->random = 0;
    bp->drop = 0;
    bp->ball_hit_index = 0;
    bp->ball_dx = 0;
    bp->ball_dy = 0;
    bp->special_popup = 0;
    bp->explode_all = 0;
}

/*
 * Populate the block info catalog.
 * Mirrors legacy SetupBlockInfo() (blocks.c:607-760), including the
 * copy-paste bug where indices 1-8 write to BlockInfo[0].slide.
 * We preserve the bug for behavioral equivalence — the slide field
 * is always 0 so the bug is harmless.
 */
static void setup_info(block_system_info_t info[MAX_BLOCKS])
{
    /* Index 0 — RED_BLK */
    info[0].block_type = RED_BLK;
    info[0].width = 40;
    info[0].height = 20;
    info[0].slide = 0;

    /* Indices 1-8: legacy bug writes info[0].slide instead of info[i].slide */
    info[1].block_type = BLUE_BLK;
    info[1].width = 40;
    info[1].height = 20;
    info[0].slide = 0; /* BUG: should be info[1].slide — preserved from blocks.c:619 */

    info[2].block_type = GREEN_BLK;
    info[2].width = 40;
    info[2].height = 20;
    info[0].slide = 0; /* BUG: blocks.c:624 */

    info[3].block_type = TAN_BLK;
    info[3].width = 40;
    info[3].height = 20;
    info[0].slide = 0; /* BUG: blocks.c:629 */

    info[4].block_type = YELLOW_BLK;
    info[4].width = 40;
    info[4].height = 20;
    info[0].slide = 0; /* BUG: blocks.c:634 */

    info[5].block_type = PURPLE_BLK;
    info[5].width = 40;
    info[5].height = 20;
    info[0].slide = 0; /* BUG: blocks.c:639 */

    info[6].block_type = BULLET_BLK;
    info[6].width = 40;
    info[6].height = 20;
    info[0].slide = 0; /* BUG: blocks.c:644 */

    info[7].block_type = BLACK_BLK;
    info[7].width = 50;
    info[7].height = 30;
    info[0].slide = 0; /* BUG: blocks.c:649 */

    info[8].block_type = COUNTER_BLK;
    info[8].width = 40;
    info[8].height = 20;
    info[0].slide = 0; /* BUG: blocks.c:654 */

    /* Index 9+ — bug-free */
    info[9].block_type = BOMB_BLK;
    info[9].width = 30;
    info[9].height = 30;
    info[9].slide = 0;

    info[10].block_type = DEATH_BLK;
    info[10].width = 30;
    info[10].height = 30;
    info[10].slide = 0;

    info[11].block_type = REVERSE_BLK;
    info[11].width = 33;
    info[11].height = 16;
    info[11].slide = 0;

    info[12].block_type = HYPERSPACE_BLK;
    info[12].width = 31;
    info[12].height = 31;
    info[12].slide = 0;

    info[13].block_type = EXTRABALL_BLK;
    info[13].width = 30;
    info[13].height = 19;
    info[13].slide = 0;

    info[14].block_type = MGUN_BLK;
    info[14].width = 35;
    info[14].height = 15;
    info[14].slide = 0;

    info[15].block_type = WALLOFF_BLK;
    info[15].width = 27;
    info[15].height = 23;
    info[15].slide = 0;

    info[16].block_type = MULTIBALL_BLK;
    info[16].width = 40;
    info[16].height = 20;
    info[16].slide = 0;

    info[17].block_type = STICKY_BLK;
    info[17].width = 32;
    info[17].height = 27;
    info[17].slide = 0;

    info[18].block_type = PAD_SHRINK_BLK;
    info[18].width = 40;
    info[18].height = 15;
    info[18].slide = 0;

    info[19].block_type = PAD_EXPAND_BLK;
    info[19].width = 40;
    info[19].height = 15;
    info[19].slide = 0;

    info[20].block_type = DROP_BLK;
    info[20].width = 40;
    info[20].height = 20;
    info[20].slide = 0;

    info[21].block_type = MAXAMMO_BLK;
    info[21].width = 40;
    info[21].height = 20;
    info[21].slide = 0;

    info[22].block_type = ROAMER_BLK;
    info[22].width = 25;
    info[22].height = 27;
    info[22].slide = 0;

    info[23].block_type = TIMER_BLK;
    info[23].width = 21;
    info[23].height = 21;
    info[23].slide = 0;

    info[24].block_type = RANDOM_BLK;
    info[24].width = 40;
    info[24].height = 20;
    info[24].slide = 0;

    info[25].block_type = DYNAMITE_BLK;
    info[25].width = 40;
    info[25].height = 20;
    info[25].slide = 0;

    info[26].block_type = BONUSX2_BLK;
    info[26].width = 27;
    info[26].height = 27;
    info[26].slide = 0;

    info[27].block_type = BONUSX4_BLK;
    info[27].width = 27;
    info[27].height = 27;
    info[27].slide = 0;

    info[28].block_type = BONUS_BLK;
    info[28].width = 27;
    info[28].height = 27;
    info[28].slide = 0;

    info[29].block_type = BLACKHIT_BLK;
    info[29].width = 50;
    info[29].height = 30;
    info[29].slide = 0;
}

/*
 * Calculate pixel geometry for a block at (row, col).
 * Mirrors legacy CalculateBlockGeometry() (blocks.c:2075-2248) but
 * stores only (x, y, width, height) — no X11 Region creation.
 * The 4 triangular collision regions are computed on-the-fly from
 * these values during check_region.
 */
static void calculate_geometry(block_system_t *ctx, int row, int col)
{
    block_entry_t *bp = &ctx->blocks[row][col];

    switch (bp->block_type)
    {
        case COUNTER_BLK:
            bp->width = BLOCK_WIDTH;
            bp->height = BLOCK_HEIGHT;
            break;

        case TIMER_BLK:
            bp->width = 21;
            bp->height = 21;
            break;

        case ROAMER_BLK:
            bp->width = 25;
            bp->height = 27;
            break;

        case MGUN_BLK:
            bp->width = 35;
            bp->height = 15;
            break;

        case WALLOFF_BLK:
            bp->width = 27;
            bp->height = 23;
            break;

        case REVERSE_BLK:
            bp->width = 33;
            bp->height = 16;
            break;

        case EXTRABALL_BLK:
            bp->width = 30;
            bp->height = 19;
            break;

        case HYPERSPACE_BLK:
            bp->width = 31;
            bp->height = 31;
            break;

        case BOMB_BLK:
        case DEATH_BLK:
            bp->width = 30;
            bp->height = 30;
            break;

        case STICKY_BLK:
            bp->width = 32;
            bp->height = 27;
            break;

        case BLACK_BLK:
            bp->width = 50;
            bp->height = 30;
            break;

        case PAD_SHRINK_BLK:
        case PAD_EXPAND_BLK:
            bp->width = 40;
            bp->height = 15;
            break;

        case BONUS_BLK:
        case BONUSX4_BLK:
        case BONUSX2_BLK:
            bp->width = 27;
            bp->height = 27;
            break;

        default: /* All standard blocks */
            bp->width = BLOCK_WIDTH;
            bp->height = BLOCK_HEIGHT;
            break;
    }

    /* Center within the grid cell */
    bp->offset_x = (ctx->col_width - bp->width) / 2;
    bp->offset_y = (ctx->row_height - bp->height) / 2;

    /* Absolute pixel position */
    bp->x = (col * ctx->col_width) + bp->offset_x;
    bp->y = (row * ctx->row_height) + bp->offset_y;
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

block_system_t *block_system_create(int col_width, int row_height, block_system_status_t *status)
{
    block_system_t *ctx = calloc(1, sizeof(*ctx));

    if (ctx == NULL)
    {
        if (status != NULL)
        {
            *status = BLOCK_SYS_ERR_ALLOC_FAILED;
        }
        return NULL;
    }

    ctx->col_width = col_width;
    ctx->row_height = row_height;
    ctx->blocks_exploding = 0;

    /* Initialize all blocks to cleared state */
    for (int r = 0; r < MAX_ROW; r++)
    {
        for (int c = 0; c < MAX_COL; c++)
        {
            block_entry_t *bp = &ctx->blocks[r][c];
            bp->block_type = NONE_BLK;
            bp->last_frame = BLOCK_INFINITE_DELAY;
        }
    }

    /* Populate the block info catalog */
    setup_info(ctx->info);

    if (status != NULL)
    {
        *status = BLOCK_SYS_OK;
    }
    return ctx;
}

void block_system_destroy(block_system_t *ctx)
{
    free(ctx);
}

/* =========================================================================
 * Block management
 * ========================================================================= */

block_system_status_t block_system_add(block_system_t *ctx, int row, int col, int block_type,
                                       int counter_slide, int frame)
{
    if (ctx == NULL)
    {
        return BLOCK_SYS_ERR_NULL_ARG;
    }

    /*
     * Fixes legacy bounds check bug: blocks.c:2272-2275 uses > instead
     * of >=, allowing row==MAX_ROW and col==MAX_COL (one past the end).
     * We use >= to correctly reject these out-of-bounds values.
     */
    if (row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL)
    {
        return BLOCK_SYS_ERR_OUT_OF_BOUNDS;
    }

    /* Clear any existing block at this position */
    clear_entry(&ctx->blocks[row][col], &ctx->blocks_exploding);

    block_entry_t *bp = &ctx->blocks[row][col];

    bp->block_type = block_type;
    bp->occupied = 1;
    bp->counter_slide = counter_slide;
    bp->last_frame = frame + BLOCK_INFINITE_DELAY;

    /* Handle special block initialization — matches blocks.c:2289-2307 */
    if (block_type == RANDOM_BLK)
    {
        bp->random = 1;
        bp->block_type = RED_BLK;
        bp->next_frame = frame + 1;
    }
    else if (block_type == DROP_BLK)
    {
        bp->drop = 1;
        bp->next_frame = frame + (rand() % BLOCK_DROP_DELAY) + 200;
    }
    else if (block_type == ROAMER_BLK)
    {
        bp->next_frame = frame + (rand() % BLOCK_ROAM_EYES_DELAY) + 50;
        bp->last_frame = frame + (rand() % BLOCK_ROAM_DELAY) + 300;
    }

    /* Calculate pixel geometry */
    calculate_geometry(ctx, row, col);

    /* Assign hit points — matches blocks.c:2313-2384 */
    bp->hit_points = score_block_hit_points(block_type, row);

    /* Special animation timing — matches blocks.c:2360-2380 */
    if (block_type == EXTRABALL_BLK)
    {
        bp->next_frame = frame + BLOCK_EXTRABALL_DELAY;
    }
    else if (block_type == DEATH_BLK)
    {
        bp->next_frame = frame + BLOCK_DEATH_DELAY2;
    }

    return BLOCK_SYS_OK;
}

block_system_status_t block_system_clear(block_system_t *ctx, int row, int col)
{
    if (ctx == NULL)
    {
        return BLOCK_SYS_ERR_NULL_ARG;
    }
    if (row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL)
    {
        return BLOCK_SYS_ERR_OUT_OF_BOUNDS;
    }

    clear_entry(&ctx->blocks[row][col], &ctx->blocks_exploding);
    return BLOCK_SYS_OK;
}

block_system_status_t block_system_clear_all(block_system_t *ctx)
{
    if (ctx == NULL)
    {
        return BLOCK_SYS_ERR_NULL_ARG;
    }

    for (int r = 0; r < MAX_ROW; r++)
    {
        for (int c = 0; c < MAX_COL; c++)
        {
            clear_entry(&ctx->blocks[r][c], &ctx->blocks_exploding);
        }
    }
    return BLOCK_SYS_OK;
}

block_system_status_t block_system_explode(block_system_t *ctx, int row, int col, int frame)
{
    if (ctx == NULL)
    {
        return BLOCK_SYS_ERR_NULL_ARG;
    }
    if (row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL)
    {
        return BLOCK_SYS_ERR_OUT_OF_BOUNDS;
    }

    block_entry_t *bp = &ctx->blocks[row][col];

    /* HYPERSPACE_BLK is immune to explosion (original/blocks.c:1821-1822). */
    if (bp->block_type == HYPERSPACE_BLK)
    {
        return BLOCK_SYS_ERR_INVALID_STATE;
    }

    /* Re-entry guard: cell must be occupied and not already exploding
     * (original/blocks.c:1825). */
    if (!bp->occupied || bp->exploding)
    {
        return BLOCK_SYS_ERR_INVALID_STATE;
    }

    ctx->blocks_exploding++;
    bp->exploding = 1;
    bp->explode_start_frame = frame;
    bp->explode_next_frame = frame;
    bp->explode_slide = 1;

    return BLOCK_SYS_OK;
}

void block_system_update_explosions(block_system_t *ctx, int frame, block_system_finalize_cb_t cb,
                                    void *ud)
{
    if (ctx == NULL || ctx->blocks_exploding == 0)
    {
        return;
    }

    for (int r = 0; r < MAX_ROW; r++)
    {
        for (int c = 0; c < MAX_COL; c++)
        {
            block_entry_t *bp = &ctx->blocks[r][c];

            /* exploding flag is the canonical guard.  Match the equality
             * check at original/blocks.c:1502 (NOT >=). */
            if (!bp->exploding || bp->explode_next_frame != frame)
            {
                continue;
            }

            /* Stages 1..4 are rendered by the integration layer reading
             * exploding + explode_slide from render_info.  Stage 4 is the
             * clear-only frame; the render path skips drawing.  Always
             * advance slide and next_frame, then check for finalize. */
            bp->explode_slide++;
            bp->explode_next_frame += BLOCK_EXPLODE_DELAY;

            if (bp->explode_slide > 4)
            {
                /* Save before clear_entry zeroes the cell. */
                int saved_block_type = bp->block_type;
                int saved_hit_points = bp->hit_points;

                clear_entry(bp, &ctx->blocks_exploding);

                /* Callback fires AFTER clear_entry: cell is unoccupied. */
                if (cb != NULL)
                {
                    cb(r, c, saved_block_type, saved_hit_points, ud);
                }
            }
        }
    }
}

void block_system_advance_animations(block_system_t *ctx, int frame)
{
    if (ctx == NULL)
        return;

    for (int r = 0; r < MAX_ROW; r++)
    {
        for (int c = 0; c < MAX_COL; c++)
        {
            block_entry_t *bp = &ctx->blocks[r][c];
            if (!bp->occupied)
                continue;

            switch (bp->block_type)
            {
                case BONUSX2_BLK:
                case BONUSX4_BLK:
                case BONUS_BLK:
                    /* 4-frame spin cycle at BLOCK_BONUS_DELAY interval */
                    bp->bonus_slide = (frame / BLOCK_BONUS_DELAY) % 4;
                    break;

                case DEATH_BLK:
                    /* 5-frame winking pirate cycle */
                    bp->bonus_slide = (frame / BLOCK_DEATH_DELAY1) % 5;
                    break;

                case EXTRABALL_BLK:
                    /* 2-frame flip */
                    bp->bonus_slide = (frame / BLOCK_EXTRABALL_DELAY) % 2;
                    break;

                case ROAMER_BLK:
                    /* 5 directions: neutral, L, R, U, D */
                    bp->bonus_slide = (frame / BLOCK_ROAM_EYES_DELAY) % 5;
                    break;

                default:
                    break;
            }
        }
    }
}

/* =========================================================================
 * Collision detection
 * ========================================================================= */

int block_system_check_region(int row, int col, int bx, int by, int bdx, void *ud)
{
    block_system_t *ctx = (block_system_t *)ud;

    if (ctx == NULL)
    {
        return BLOCK_REGION_NONE;
    }
    if (row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL)
    {
        return BLOCK_REGION_NONE;
    }

    const block_entry_t *bp = &ctx->blocks[row][col];

    /* Only test occupied, non-exploding blocks */
    if (!bp->occupied || bp->exploding)
    {
        return BLOCK_REGION_NONE;
    }

    /*
     * Step 1: AABB overlap test.
     * Ball bounding box: (bx - BALL_WC, by - BALL_HC) to
     *                    (bx - BALL_WC + BALL_WIDTH, by - BALL_HC + BALL_HEIGHT)
     * Block bounding box: (bp->x, bp->y) to (bp->x + bp->width, bp->y + bp->height)
     */
    int ball_left = bx - BALL_WC;
    int ball_top = by - BALL_HC;
    int ball_right = ball_left + BALL_WIDTH;
    int ball_bottom = ball_top + BALL_HEIGHT;

    if (ball_right <= bp->x || ball_left >= bp->x + bp->width || ball_bottom <= bp->y ||
        ball_top >= bp->y + bp->height)
    {
        return BLOCK_REGION_NONE;
    }

    /*
     * Step 2: Determine which triangular quadrant the ball center falls in.
     *
     * The block rectangle is divided by its two diagonals into 4 triangles:
     *
     *   TL --------- TR        TL = (x, y)
     *    | \  TOP  / |         TR = (x+w, y)
     *    |   \ . /   |         BL = (x, y+h)
     *    | L  >X<  R |         BR = (x+w, y+h)
     *    |   / . \   |         X  = center
     *    | / BOTTOM\ |
     *   BL --------- BR
     *
     * Two diagonal cross-products determine the quadrant:
     *   d1 = w*(py - y) - h*(px - x)      (TL→BR diagonal)
     *   d2 = h*(x + w - px) - w*(py - y)  (TR→BL diagonal)
     *
     * d1 <= 0 && d2 >= 0  →  TOP
     * d1 >= 0 && d2 <= 0  →  BOTTOM
     * d1 >= 0 && d2 >= 0  →  LEFT
     * d1 <= 0 && d2 <= 0  →  RIGHT
     */
    int w = bp->width;
    int h = bp->height;
    int d1 = w * (by - bp->y) - h * (bx - bp->x);
    int d2 = h * (bp->x + w - bx) - w * (by - bp->y);

    int region;

    if (d1 <= 0 && d2 >= 0)
    {
        region = BLOCK_REGION_TOP;
    }
    else if (d1 >= 0 && d2 <= 0)
    {
        region = BLOCK_REGION_BOTTOM;
    }
    else if (d1 >= 0 && d2 >= 0)
    {
        region = BLOCK_REGION_LEFT;
    }
    else
    {
        region = BLOCK_REGION_RIGHT;
    }

    /*
     * Step 3: Adjacency filter.
     * Suppress the region hit if the neighboring cell in that direction
     * is occupied AND the ball is in the phantom case (ball center is
     * already inside this block on the axis being tested).
     *
     * The gap case (ball in the inter-block gap, genuinely approaching
     * the face) must NOT be suppressed.  The distinction:
     *
     *   TOP:    phantom when by >= bp->y  (ball center inside block top)
     *           gap case when by < bp->y  (ball above block top edge — real hit)
     *   BOTTOM: phantom when by <= bp->y + bp->height
     *   LEFT:   phantom when bx >= bp->x
     *   RIGHT:  phantom when bx <= bp->x + bp->width
     *
     * Fix for xboing-c-895: the original check suppressed on occupancy alone,
     * suppressing hits in both the phantom and gap cases and causing 2-row
     * tunneling.
     */
    switch (region)
    {
        case BLOCK_REGION_TOP:
            if (row > 0 && ctx->blocks[row - 1][col].occupied && by >= bp->y)
            {
                return BLOCK_REGION_NONE;
            }
            break;
        case BLOCK_REGION_BOTTOM:
            if (row < MAX_ROW - 1 && ctx->blocks[row + 1][col].occupied && by <= bp->y + bp->height)
            {
                return BLOCK_REGION_NONE;
            }
            break;
        case BLOCK_REGION_LEFT:
            if (col > 0 && ctx->blocks[row][col - 1].occupied && bx >= bp->x)
            {
                return BLOCK_REGION_NONE;
            }
            break;
        case BLOCK_REGION_RIGHT:
            if (col < MAX_COL - 1 && ctx->blocks[row][col + 1].occupied && bx <= bp->x + bp->width)
            {
                return BLOCK_REGION_NONE;
            }
            break;
        default:
            break;
    }

    (void)bdx; /* Accepted for callback signature compatibility */
    return region;
}

/* cppcheck-suppress constParameterPointer ; signature must match ball_system.h callback */
int block_system_cell_available(int row, int col, void *ud)
{
    const block_system_t *ctx = (const block_system_t *)ud;

    if (ctx == NULL)
    {
        return 0;
    }
    if (row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL)
    {
        return 0;
    }

    const block_entry_t *bp = &ctx->blocks[row][col];
    return !bp->occupied && !bp->exploding;
}

/* =========================================================================
 * Queries
 * ========================================================================= */

int block_system_is_occupied(const block_system_t *ctx, int row, int col)
{
    if (ctx == NULL || row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL)
    {
        return 0;
    }
    return ctx->blocks[row][col].occupied;
}

int block_system_get_type(const block_system_t *ctx, int row, int col)
{
    if (ctx == NULL || row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL)
    {
        return NONE_BLK;
    }
    return ctx->blocks[row][col].block_type;
}

int block_system_get_hit_points(const block_system_t *ctx, int row, int col)
{
    if (ctx == NULL || row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL)
    {
        return 0;
    }
    return ctx->blocks[row][col].hit_points;
}

int block_system_still_active(const block_system_t *ctx)
{
    /*
     * Returns nonzero if the level still has required blocks.
     * Matches legacy StillActiveBlocks() (blocks.c:2464-2526).
     *
     * Required blocks: color blocks (RED..PURPLE), COUNTER_BLK, DROP_BLK.
     * Non-required: BLACK, BULLET, ROAMER, BOMB, TIMER, HYPERSPACE,
     * STICKY, MULTIBALL, MAXAMMO, PAD_SHRINK, PAD_EXPAND, REVERSE,
     * MGUN, WALLOFF, EXTRABALL, DEATH, BONUSX2, BONUSX4, BONUS.
     */
    if (ctx == NULL)
    {
        return 0;
    }

    for (int r = 0; r < MAX_ROW; r++)
    {
        for (int c = 0; c < MAX_COL; c++)
        {
            const block_entry_t *bp = &ctx->blocks[r][c];

            if (!bp->occupied)
            {
                continue;
            }

            switch (bp->block_type)
            {
                /* Non-required blocks */
                case BLACK_BLK:
                case BULLET_BLK:
                case ROAMER_BLK:
                case BOMB_BLK:
                case TIMER_BLK:
                case HYPERSPACE_BLK:
                case STICKY_BLK:
                case MULTIBALL_BLK:
                case MAXAMMO_BLK:
                case PAD_SHRINK_BLK:
                case PAD_EXPAND_BLK:
                case REVERSE_BLK:
                case MGUN_BLK:
                case WALLOFF_BLK:
                case EXTRABALL_BLK:
                case DEATH_BLK:
                case BONUSX2_BLK:
                case BONUSX4_BLK:
                case BONUS_BLK:
                    break;

                default:
                    /* Required block found — level not complete */
                    return 1;
            }
        }
    }

    /* Explosions still pending — level not complete */
    if (ctx->blocks_exploding > 1)
    {
        return 1;
    }

    return 0;
}

int block_system_get_exploding_count(const block_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    return ctx->blocks_exploding;
}

block_system_status_t block_system_get_render_info(const block_system_t *ctx, int row, int col,
                                                   block_system_render_info_t *info)
{
    if (ctx == NULL || info == NULL)
    {
        return BLOCK_SYS_ERR_NULL_ARG;
    }
    if (row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL)
    {
        return BLOCK_SYS_ERR_OUT_OF_BOUNDS;
    }

    const block_entry_t *bp = &ctx->blocks[row][col];

    info->occupied = bp->occupied;
    info->block_type = bp->block_type;
    info->hit_points = bp->hit_points;
    info->x = bp->x;
    info->y = bp->y;
    info->width = bp->width;
    info->height = bp->height;
    info->exploding = bp->exploding;
    info->explode_slide = bp->explode_slide;
    info->counter_slide = bp->counter_slide;
    info->bonus_slide = bp->bonus_slide;
    info->random = bp->random;
    info->drop = bp->drop;
    info->special_popup = bp->special_popup;
    info->explode_all = bp->explode_all;

    return BLOCK_SYS_OK;
}

const block_system_info_t *block_system_get_info(const block_system_t *ctx, int block_type)
{
    if (ctx == NULL || block_type < 0 || block_type >= MAX_BLOCKS)
    {
        return NULL;
    }
    return &ctx->info[block_type];
}

/* =========================================================================
 * Gun hit handler
 * ========================================================================= */

/*
 * Multi-hit special block types — original/gun.c:325-340.
 * A bullet decrements counterSlide; the block dies when it reaches zero.
 */
static int is_multi_hit_special(int block_type)
{
    switch (block_type)
    {
        case REVERSE_BLK:
        case MGUN_BLK:
        case STICKY_BLK:
        case WALLOFF_BLK:
        case MULTIBALL_BLK:
        case PAD_EXPAND_BLK:
        case PAD_SHRINK_BLK:
        case DEATH_BLK:
        case COUNTER_BLK:
            return 1;
        default:
            return 0;
    }
}

int block_system_decrement_gun_hit(block_system_t *ctx, int row, int col)
{
    if (ctx == NULL || row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL)
        return 0;

    block_entry_t *bp = &ctx->blocks[row][col];
    if (!bp->occupied)
        return 0;

    int block_type = bp->block_type;

    /* HYPERSPACE_BLK and BLACK_BLK always absorb — original/gun.c:341-350. */
    if (block_type == HYPERSPACE_BLK || block_type == BLACK_BLK)
        return 1;

    /* Multi-hit specials: decrement counterSlide — original/gun.c:325-340. */
    if (is_multi_hit_special(block_type))
    {
        if (bp->counter_slide > 0)
            bp->counter_slide--;
        if (bp->counter_slide > 0)
            return 1; /* Still has hits remaining — bullet absorbed */
        /* counterSlide reached zero — fall through to clear */
    }

    /* Regular block or multi-hit special exhausted: clear the block. */
    block_system_clear(ctx, row, col);
    return 0;
}

/* =========================================================================
 * Utility
 * ========================================================================= */

const char *block_system_status_string(block_system_status_t status)
{
    switch (status)
    {
        case BLOCK_SYS_OK:
            return "OK";
        case BLOCK_SYS_ERR_NULL_ARG:
            return "NULL argument";
        case BLOCK_SYS_ERR_ALLOC_FAILED:
            return "allocation failed";
        case BLOCK_SYS_ERR_OUT_OF_BOUNDS:
            return "row/col out of bounds";
        default:
            return "unknown status";
    }
}
