/*
 * score_logic.c — Pure scoring arithmetic, no X11 dependency.
 *
 * Extracted from score.c, bonus.c, blocks.c, and level.c for testability.
 * All functions are pure: inputs come from parameters, outputs go to
 * return values.
 *
 * CHARACTERIZATION EXTRACTION: This code replicates the exact behavior
 * of the original modules, including any quirks. Do not fix bugs here.
 */

#include <sys/types.h>
#include "score_logic.h"
#include "block_types.h"

/* From level.c:90 */
#define NEW_LIVE_SCORE_INC  100000L

/* From bonus.c:91-95 */
#define BONUS_COIN_SCORE    3000
#define SUPER_BONUS_SCORE   50000
#define BULLET_SCORE        500
#define LEVEL_SCORE         100
#define TIME_BONUS_POINTS   100

u_long score_apply_multiplier(u_long inc, int x2_active, int x4_active)
{
    /* Replicates ComputeScore() (score.c:226-229).
     * x2 is checked first — if both active, x2 wins. */
    if (x2_active)
        inc *= 2;
    else if (x4_active)
        inc *= 4;

    return inc;
}

int score_extra_life_threshold(long score_value)
{
    /* Replicates CheckAndAddExtraLife() (level.c:504-514).
     * Returns score_value / NEW_LIVE_SCORE_INC. */
    return (int)(score_value / NEW_LIVE_SCORE_INC);
}

u_long score_compute_bonus(int time_bonus, int num_bonus, int max_bonus,
                           int num_bullets, int level_adj)
{
    /* Replicates ComputeAndAddBonusScore() (bonus.c:838-888). */
    u_long total = 0;

    if (time_bonus > 0)
    {
        /* Bonus coin calculation */
        if (num_bonus > max_bonus)
            total += (u_long)SUPER_BONUS_SCORE;
        else
            total += (u_long)(num_bonus * BONUS_COIN_SCORE);

        /* Level bonus */
        total += (u_long)(LEVEL_SCORE * level_adj);
    }

    /* Bullet bonus (unconditional — not gated by time_bonus) */
    if (num_bullets != 0)
        total += (u_long)(num_bullets * BULLET_SCORE);

    /* Time bonus */
    if (time_bonus > 0)
        total += (u_long)(TIME_BONUS_POINTS * time_bonus);

    return total;
}

int score_block_hit_points(int block_type, int row)
{
    /* Replicates the switch in AddNewBlock() (blocks.c:2494-2566). */
    switch (block_type)
    {
        case BULLET_BLK:
        case MAXAMMO_BLK:
            return 50;

        case RED_BLK:
            return 100;

        case GREEN_BLK:
            return 120;

        case BLUE_BLK:
            return 110;

        case TAN_BLK:
            return 130;

        case YELLOW_BLK:
            return 140;

        case PURPLE_BLK:
            return 150;

        case BOMB_BLK:
            return 50;

        case ROAMER_BLK:
            return 400;

        case DROP_BLK:
            return (MAX_ROW - row) * 100;

        case COUNTER_BLK:
            return 200;

        case EXTRABALL_BLK:
        case TIMER_BLK:
        case HYPERSPACE_BLK:
        case MGUN_BLK:
        case WALLOFF_BLK:
        case REVERSE_BLK:
        case MULTIBALL_BLK:
        case STICKY_BLK:
        case PAD_SHRINK_BLK:
        case PAD_EXPAND_BLK:
            return 100;

        case DEATH_BLK:
            return 0;

        default:
            /* DYNAMITE_BLK, BONUSX2_BLK, BONUSX4_BLK, BONUS_BLK,
             * BLACKHIT_BLK, BLACK_BLK — hitPoints is implicitly 0
             * (struct is zeroed before the switch in AddNewBlock). */
            return 0;
    }
}
