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
                    /* 4-frame spin, descending 3->0 per BLOCK_BONUS_DELAY,
                     * matching original/blocks.c:1188-1218 (HandlePendingBonuses
                     * decrements bonusSlide). */
                    bp->bonus_slide = 3 - (frame / BLOCK_BONUS_DELAY) % 4;
                    break;

                case DEATH_BLK:
                {
                    /* Asymmetric wink (original/blocks.c:1313-1334): the eye
                     * closes for BLOCK_DEATH_DELAY2, then blinks through
                     * slides 1-3 at BLOCK_DEATH_DELAY1 each. The trigger
                     * that fires exactly at the DELAY2 boundary redraws
                     * slide 0 again first (bonusSlide is already 0 there,
                     * so that redraw is a visible no-op) before arming the
                     * DELAY1-spaced blink — so slide 0 is actually held for
                     * DELAY2 + DELAY1, one extra blink-tick longer than the
                     * raw DELAY2 hold. The 5th sprite (bonusSlide == 4) is
                     * drawn and instantly overwritten by the reset-to-0
                     * redraw within the same tick, so it is never visibly
                     * shown — hence 3 blink frames, not 4. */
                    const int hold0 = BLOCK_DEATH_DELAY2 + BLOCK_DEATH_DELAY1;
                    const int period = BLOCK_DEATH_DELAY2 + 4 * BLOCK_DEATH_DELAY1;
                    const int t = frame % period;
                    bp->bonus_slide = (t < hold0) ? 0 : 1 + (t - hold0) / BLOCK_DEATH_DELAY1;
                    break;
                }

                case EXTRABALL_BLK:
                    /* 2-frame flip */
                    bp->bonus_slide = (frame / BLOCK_EXTRABALL_DELAY) % 2;
                    break;

                case ROAMER_BLK:
                    /* Eye direction is driven by the rand()-scheduled eye
                     * timer in block_system_update_movement, not a
                     * deterministic cycle — see original/blocks.c:1364-1373. */
                    break;

                default:
                    break;
            }
        }
    }
}

/* =========================================================================
 * ROAMER_BLK / DROP_BLK grid movement
 * ========================================================================= */

/*
 * Port of GetRandomType(blankBlock=False) (original/blocks.c:1121-1165).
 * Returns a random ordinary block type for RANDOM_BLK's morph cycle.
 * Case 7 returns YELLOW_BLK rather than NONE_BLK because blankBlock is
 * False here — a morphing "?" block never turns into empty space.
 */
static int get_random_block_type(void)
{
    switch (rand() % 8)
    {
        case 0:
            return RED_BLK;
        case 1:
            return BLUE_BLK;
        case 2:
            return GREEN_BLK;
        case 3:
            return TAN_BLK;
        case 4:
            return YELLOW_BLK;
        case 5:
            return PURPLE_BLK;
        case 6:
            return BULLET_BLK;
        case 7:
        default:
            return YELLOW_BLK;
    }
}

/*
 * Port of CheckAdjacentBlocks() (original/blocks.c:1220-1256).  Returns
 * nonzero iff a ROAMER_BLK/DROP_BLK may move INTO (row, col): in bounds,
 * unoccupied, not exploding, at least two rows clear of the paddle, and
 * no active ball currently sits in that cell.
 */
static int check_adjacent(const block_system_t *ctx, int row, int col,
                          const block_system_ball_pos_t *balls, int nballs)
{
    if (row < 0 || row >= MAX_ROW)
    {
        return 0;
    }
    if (col < 0 || col >= MAX_COL)
    {
        return 0;
    }

    const block_entry_t *bp = &ctx->blocks[row][col];
    if (bp->occupied || bp->exploding)
    {
        return 0;
    }

    /* Rejects a destination row r when (row + 1) >= MAX_ROW - 2, i.e.
     * r >= MAX_ROW - 3 — the bottom three rows (15-17 when MAX_ROW=18) —
     * keeping moving blocks clear of the paddle
     * (original/blocks.c:1236-1237). */
    if ((row + 1) >= (MAX_ROW - 2))
    {
        return 0;
    }

    for (int i = 0; i < nballs; i++)
    {
        if (!balls[i].active)
        {
            continue;
        }

        /* original/blocks.c:179-180 X2COL/Y2ROW: col = x / colWidth,
         * row = y / rowHeight. */
        int ball_col = balls[i].x / ctx->col_width;
        int ball_row = balls[i].y / ctx->row_height;

        if (ball_row == row && ball_col == col)
        {
            return 0;
        }
    }

    return 1;
}

void block_system_update_movement(block_system_t *ctx, int frame,
                                  const block_system_ball_pos_t *balls, int nballs)
{
    if (ctx == NULL)
    {
        return;
    }
    if (balls == NULL)
    {
        nballs = 0;
    }

    for (int r = 0; r < MAX_ROW; r++)
    {
        for (int c = 0; c < MAX_COL; c++)
        {
            block_entry_t *bp = &ctx->blocks[r][c];
            if (!bp->occupied)
            {
                continue;
            }

            /* An exploding block is mid-finalize: skip movement/morph/drop
             * so clear_entry() can't decrement blocks_exploding out from
             * under the explosion path (see :83) or vacate the cell before
             * its scoring callback fires. Matches check_adjacent (:701) and
             * the placement guards (:1036, :1110). original/blocks.c:1834
             * keeps such a block occupied+exploding with its type intact. */
            if (bp->exploding)
            {
                continue;
            }

            /* ROAMER_BLK: eye timer + move timer (original/blocks.c:1364-1421).
             * The original checks `== frame`; this port checks `frame >=`
             * instead. Level-loaded blocks are added with a hardcoded
             * frame=0 (game_callbacks.c, game_init.c), so a timer scheduled
             * as next_frame = 0 + delay can land before the first update
             * tick actually runs under the modern fixed-timestep loop,
             * and an exact `==` match would then never fire. `>=` fires
             * once on the first tick the schedule is due and each handler
             * reschedules to a future frame before returning, so there is
             * no double-fire. Matches the ball timer convention in
             * ball_system.c (e.g. lines 405, 845, 890). */
            if (bp->block_type == ROAMER_BLK)
            {
                if (frame >= bp->next_frame)
                {
                    /* Eye timer fires: reroll gaze direction. */
                    bp->next_frame = frame + (rand() % BLOCK_ROAM_EYES_DELAY) + 50;
                    bp->bonus_slide = rand() % 5;
                }
                else if (frame >= bp->last_frame)
                {
                    /* Move timer fires: every firing attempts a real move
                     * (jck ruling, round 2 — original/blocks.c:1377 maps
                     * bonus_slide 1-4 to L/R/U/D via `d = bonus_slide + 1`
                     * and silently falls through to a stale r1/c1 from a
                     * prior block's move on the 0/5 cases, but it always
                     * *attempts* a move on every firing; a naive 0=neutral
                     * port would skip ~1/5 of firings and understate
                     * roamer wander frequency ~20% on roamer-dense
                     * levels).  This port maps all 5 rolled eye values to
                     * a direction so every firing attempts a move,
                     * without reproducing the stale-variable bug: 0=L,
                     * 1=R, 2=U, 3=D match the original's 0-3 exactly, and
                     * 4 wraps to L (deterministic substitute for the
                     * original's stale fallthrough on d==5). The eye
                     * sprite roll (bonus_slide) stays a plain rand() % 5
                     * above — eye/move alignment is cosmetic and not
                     * required to match. */
                    int dr = 0;
                    int dc = 0;
                    switch (bp->bonus_slide)
                    {
                        case 0:
                            dc = -1;
                            break;
                        case 1:
                            dc = 1;
                            break;
                        case 2:
                            dr = -1;
                            break;
                        case 3:
                            dr = 1;
                            break;
                        case 4:
                            dc = -1;
                            break;
                        default:
                            break;
                    }

                    if (check_adjacent(ctx, r + dr, c + dc, balls, nballs))
                    {
                        block_system_add(ctx, r + dr, c + dc, ROAMER_BLK, 0, frame);
                        clear_entry(bp, &ctx->blocks_exploding);
                    }
                    else
                    {
                        bp->last_frame = frame + (rand() % BLOCK_ROAM_DELAY) + 300;
                    }
                }
            }

            /* RANDOM_BLK morph: cycles the block's visible type on a
             * timer, independent of block_type (original/blocks.c:1427-
             * 1445).  The random flag is NEVER cleared here — it keeps
             * re-morphing forever until the block is destroyed, matching
             * the original which only ever touches blockType/bonusSlide/
             * nextFrame inside this branch. Checks `frame >=` rather than
             * the original's `==`: level-loaded blocks start at hardcoded
             * frame=0, so next_frame=1 can be skipped by an exact match
             * once the update loop's frame counter is already past 1 —
             * see the ROAMER_BLK comment above for the full rationale. */
            if (bp->random && frame >= bp->next_frame)
            {
                bp->block_type = get_random_block_type();
                bp->bonus_slide = 0;
                bp->next_frame = frame + (rand() % BLOCK_RANDOM_DELAY) + 300;
            }

            /* DROP_BLK: single drop timer (original/blocks.c:1447-1474).
             * `frame >=` rather than the original's `==` — same hardcoded
             * frame=0 level-load hazard as ROAMER_BLK/RANDOM_BLK above. */
            if (bp->drop && frame >= bp->next_frame)
            {
                if (check_adjacent(ctx, r + 1, c, balls, nballs))
                {
                    block_system_add(ctx, r + 1, c, DROP_BLK, 0, frame);
                    clear_entry(bp, &ctx->blocks_exploding);
                }
                else
                {
                    bp->next_frame = frame + BLOCK_DROP_DELAY;
                }
            }
        }
    }
}

/* =========================================================================
 * Collision detection — bbox-vs-triangle classifier
 *
 * Port of CheckRegions() in original/ball.c:1338-1457.  Tests the ball's
 * full BALL_WIDTH x BALL_HEIGHT bounding rectangle against each of the
 * block's four triangular face regions and returns a bitmask of overlapping
 * faces, with unconditional-on-neighbour-occupancy adjacency suppression.
 * ========================================================================= */

/*
 * Point-in-triangle test using the sign-of-cross-product (barycentric)
 * method.  Returns nonzero iff (px, py) is inside or on the boundary of
 * the triangle (v0, v1, v2).  Vertex winding is irrelevant; the test
 * accepts both orientations by allowing the signs to be all <= 0 OR all
 * >= 0.
 */
static int point_in_triangle(int px, int py, int v0x, int v0y, int v1x, int v1y, int v2x, int v2y)
{
    long d1 = (long)(px - v1x) * (v0y - v1y) - (long)(v0x - v1x) * (py - v1y);
    long d2 = (long)(px - v2x) * (v1y - v2y) - (long)(v1x - v2x) * (py - v2y);
    long d3 = (long)(px - v0x) * (v2y - v0y) - (long)(v2x - v0x) * (py - v0y);
    int has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    int has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(has_neg && has_pos);
}

/*
 * Segment (p1q1) intersects segment (p2q2) on the open or closed
 * interval.  Sufficient for ball-vs-triangle screen-pixel collision.
 */
static int seg_orient(int px, int py, int qx, int qy, int rx, int ry)
{
    long v = (long)(qy - py) * (rx - qx) - (long)(qx - px) * (ry - qy);
    if (v > 0)
        return 1;
    if (v < 0)
        return -1;
    return 0;
}

static int on_segment(int px, int py, int qx, int qy, int rx, int ry)
{
    int min_x = px < rx ? px : rx;
    int max_x = px > rx ? px : rx;
    int min_y = py < ry ? py : ry;
    int max_y = py > ry ? py : ry;
    return qx >= min_x && qx <= max_x && qy >= min_y && qy <= max_y;
}

static int segments_intersect(int p1x, int p1y, int q1x, int q1y, int p2x, int p2y, int q2x,
                              int q2y)
{
    int o1 = seg_orient(p1x, p1y, q1x, q1y, p2x, p2y);
    int o2 = seg_orient(p1x, p1y, q1x, q1y, q2x, q2y);
    int o3 = seg_orient(p2x, p2y, q2x, q2y, p1x, p1y);
    int o4 = seg_orient(p2x, p2y, q2x, q2y, q1x, q1y);

    if (o1 != o2 && o3 != o4)
        return 1;

    /* Collinear-overlap edge cases. */
    if (o1 == 0 && on_segment(p1x, p1y, p2x, p2y, q1x, q1y))
        return 1;
    if (o2 == 0 && on_segment(p1x, p1y, q2x, q2y, q1x, q1y))
        return 1;
    if (o3 == 0 && on_segment(p2x, p2y, p1x, p1y, q2x, q2y))
        return 1;
    if (o4 == 0 && on_segment(p2x, p2y, q1x, q1y, q2x, q2y))
        return 1;

    return 0;
}

/*
 * Does an axis-aligned rectangle overlap a triangle?
 *
 * The 3-condition test:
 *   (1) any triangle vertex is inside the rectangle, OR
 *   (2) any rectangle corner is inside the triangle, OR
 *   (3) any triangle edge crosses any rectangle edge.
 *
 * Equivalent to XRectInRegion(triangle_region, rect) != RectangleOut.
 *
 * Rectangle convention: half-open [rx, rx+rw) x [ry, ry+rh).  This
 * matches the half-open block geometry used elsewhere in this module
 * (e.g. ball_right > bp->x as the overlap predicate, with `right`
 * being one past the last covered pixel).  The inclusive variant
 * would over-report by one pixel on edge-only contact.
 *
 * For the corner-in-triangle and edge-vs-edge sub-tests we use the
 * inclusive corner coordinate (rx + rw - 1, ry + rh - 1) so a
 * geometrically-touching rectangle is treated as overlap iff at least
 * one of its interior pixels lies in the triangle.
 */
static int rect_overlaps_triangle(int rx, int ry, int rw, int rh, int v0x, int v0y, int v1x,
                                  int v1y, int v2x, int v2y)
{
    int rx2 = rx + rw - 1; /* inclusive right edge */
    int ry2 = ry + rh - 1; /* inclusive bottom edge */

    /* (1) Triangle vertices inside rect (half-open interpretation). */
    if ((v0x >= rx && v0x < rx + rw && v0y >= ry && v0y < ry + rh) ||
        (v1x >= rx && v1x < rx + rw && v1y >= ry && v1y < ry + rh) ||
        (v2x >= rx && v2x < rx + rw && v2y >= ry && v2y < ry + rh))
    {
        return 1;
    }

    /* (2) Rect corners (inclusive) inside triangle. */
    if (point_in_triangle(rx, ry, v0x, v0y, v1x, v1y, v2x, v2y) ||
        point_in_triangle(rx2, ry, v0x, v0y, v1x, v1y, v2x, v2y) ||
        point_in_triangle(rx, ry2, v0x, v0y, v1x, v1y, v2x, v2y) ||
        point_in_triangle(rx2, ry2, v0x, v0y, v1x, v1y, v2x, v2y))
    {
        return 1;
    }

    /* (3) Triangle edges vs rect edges (inclusive corner coords). */
    const int rect_edges[4][4] = {
        {rx, ry, rx2, ry},   /* top */
        {rx2, ry, rx2, ry2}, /* right */
        {rx2, ry2, rx, ry2}, /* bottom */
        {rx, ry2, rx, ry},   /* left */
    };
    const int tri_edges[3][4] = {
        {v0x, v0y, v1x, v1y},
        {v1x, v1y, v2x, v2y},
        {v2x, v2y, v0x, v0y},
    };
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            if (segments_intersect(tri_edges[i][0], tri_edges[i][1], tri_edges[i][2],
                                   tri_edges[i][3], rect_edges[j][0], rect_edges[j][1],
                                   rect_edges[j][2], rect_edges[j][3]))
            {
                return 1;
            }
        }
    }

    return 0;
}

int block_system_check_region_bbox(int row, int col, int bx, int by, int bdx, void *ud)
{
    (void)bdx;
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
    if (!bp->occupied || bp->exploding)
    {
        return BLOCK_REGION_NONE;
    }

    /* Ball bounding box.  Mirrors the (x - BALL_WC, y - BALL_HC, BALL_WIDTH,
     * BALL_HEIGHT) rect that original/ball.c:1387 passes to XRectInRegion. */
    int rx = bx - BALL_WC;
    int ry = by - BALL_HC;
    int rw = BALL_WIDTH;
    int rh = BALL_HEIGHT;

    /* Triangle vertices.  Each face's triangle is one quadrant of the block,
     * cut by the two diagonals.  Vertices match
     * original/blocks.c:2215-2265. */
    int bx0 = bp->x;
    int by0 = bp->y;
    int bx1 = bp->x + bp->width;
    int by1 = bp->y + bp->height;
    int cx = bp->x + bp->width / 2;
    int cy = bp->y + bp->height / 2;

    /* TOP triangle: (bx0, by0), (bx1, by0), (cx, cy) */
    int hit_top = rect_overlaps_triangle(rx, ry, rw, rh, bx0, by0, bx1, by0, cx, cy);
    /* BOTTOM triangle: (bx0, by1), (bx1, by1), (cx, cy) */
    int hit_bottom = rect_overlaps_triangle(rx, ry, rw, rh, bx0, by1, bx1, by1, cx, cy);
    /* LEFT triangle: (bx0, by0), (bx0, by1), (cx, cy) */
    int hit_left = rect_overlaps_triangle(rx, ry, rw, rh, bx0, by0, bx0, by1, cx, cy);
    /* RIGHT triangle: (bx1, by0), (bx1, by1), (cx, cy) */
    int hit_right = rect_overlaps_triangle(rx, ry, rw, rh, bx1, by0, bx1, by1, cx, cy);

    /* Adjacency suppression — unconditional on neighbour occupancy,
     * matching original/ball.c:1390-1452 (a region is set ONLY if the
     * neighbour in that direction is absent or empty).  Paired with the
     * bbox classifier's broader BOTTOM/TOP recognition, this gives the
     * seam case the right answer: RIGHT gets suppressed by the seam
     * neighbour AND BOTTOM still fires from the bbox dip into the
     * block's bottom triangle. */
    int region = BLOCK_REGION_NONE;
    if (hit_top && (row == 0 || !ctx->blocks[row - 1][col].occupied))
    {
        region |= BLOCK_REGION_TOP;
    }
    if (hit_bottom && (row == MAX_ROW - 1 || !ctx->blocks[row + 1][col].occupied))
    {
        region |= BLOCK_REGION_BOTTOM;
    }
    if (hit_left && (col == 0 || !ctx->blocks[row][col - 1].occupied))
    {
        region |= BLOCK_REGION_LEFT;
    }
    if (hit_right && (col == MAX_COL - 1 || !ctx->blocks[row][col + 1].occupied))
    {
        region |= BLOCK_REGION_RIGHT;
    }

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

    /* Regular block or multi-hit special exhausted: caller must arm
     * explosion via block_system_explode().  This function does NOT
     * clear or arm — the caller controls the lifecycle so the explosion
     * is driven from the gameplay tick (with the correct `frame` value)
     * and routes through game_callbacks_on_block_finalize. */
    return 0;
}

int block_system_ball_hit_counter(block_system_t *ctx, int row, int col)
{
    if (ctx == NULL || row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL)
        return -1;

    block_entry_t *bp = &ctx->blocks[row][col];
    if (!bp->occupied || bp->block_type != COUNTER_BLK)
        return -1;

    if (bp->counter_slide == 0)
        return 0;

    bp->counter_slide--;
    return 1;
}

int block_system_check_black_hit(block_system_t *ctx, int row, int col, int frame)
{
    if (ctx == NULL || row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL)
        return -1;

    block_entry_t *bp = &ctx->blocks[row][col];
    if (!bp->occupied || bp->block_type != BLACK_BLK)
        return -1;

    if (frame <= bp->next_frame)
        return 0;

    bp->next_frame = frame + 30;
    return 1;
}

int block_system_get_black_next_frame(const block_system_t *ctx, int row, int col)
{
    if (ctx == NULL || row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL)
        return 0;

    const block_entry_t *bp = &ctx->blocks[row][col];
    if (!bp->occupied || bp->block_type != BLACK_BLK)
        return 0;

    return bp->next_frame;
}

void block_system_set_black_next_frame(block_system_t *ctx, int row, int col, int next_frame)
{
    if (ctx == NULL || row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL)
        return;

    block_entry_t *bp = &ctx->blocks[row][col];
    if (!bp->occupied || bp->block_type != BLACK_BLK)
        return;

    bp->next_frame = next_frame;
}

int block_system_get_random(const block_system_t *ctx, int row, int col)
{
    if (ctx == NULL || row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL)
        return 0;

    const block_entry_t *bp = &ctx->blocks[row][col];
    if (!bp->occupied)
        return 0;

    return bp->random;
}

void block_system_set_random(block_system_t *ctx, int row, int col, int random)
{
    if (ctx == NULL || row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL)
        return;

    block_entry_t *bp = &ctx->blocks[row][col];
    if (!bp->occupied)
        return;

    bp->random = random ? 1 : 0;
}

int block_system_get_last_frame(const block_system_t *ctx, int row, int col)
{
    if (ctx == NULL || row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL)
        return 0;

    return ctx->blocks[row][col].last_frame;
}

void block_system_set_last_frame(block_system_t *ctx, int row, int col, int last_frame)
{
    if (ctx == NULL || row < 0 || row >= MAX_ROW || col < 0 || col >= MAX_COL)
        return;

    ctx->blocks[row][col].last_frame = last_frame;
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
        case BLOCK_SYS_ERR_INVALID_STATE:
            return "invalid state (cell unoccupied, already exploding, or HYPERSPACE_BLK)";
        default:
            return "unknown status";
    }
}
