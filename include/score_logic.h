#ifndef SCORE_LOGIC_H
#define SCORE_LOGIC_H

/*
 * Pure scoring arithmetic — no X11 dependency.
 *
 * Extracted from score.c, bonus.c, blocks.c, and level.c for testability.
 * All functions take inputs as parameters and produce outputs via return
 * values. No global state is read or written.
 */

#include <sys/types.h>   /* u_long */

/*
 * Apply x2/x4 multiplier to a raw point increment.
 *
 * x2_active and x4_active map directly to the x2Bonus and x4Bonus globals.
 * If both are active, x2 wins (if/else if precedence in ComputeScore,
 * score.c:226). This is current behavior — do not change.
 */
u_long score_apply_multiplier(u_long inc, int x2_active, int x4_active);

/*
 * Return the threshold index for extra life awards.
 *
 * Replicates the ballInc logic in CheckAndAddExtraLife (level.c:504-514).
 * Returns score_value / NEW_LIVE_SCORE_INC (integer division).
 * Caller detects a life award when the return value > previous value.
 *
 * This function is stateless (unlike CheckAndAddExtraLife which uses a
 * static local). The caller manages the previous threshold index.
 */
int score_extra_life_threshold(long score_value);

/*
 * Compute bonus score from constituent inputs. Returns total points.
 *
 * Replicates ComputeAndAddBonusScore() (bonus.c:838-888). Pure calculation,
 * does NOT call AddToScore().
 *
 * Parameters map to the globals/getters the formula reads:
 *   time_bonus    <- GetLevelTimeBonus()
 *   num_bonus     <- GetNumberBonus() (numBonus static in bonus.c)
 *   max_bonus     <- MAX_BONUS (currently 8, from bonus.h)
 *   num_bullets   <- GetNumberBullets()
 *   level_adj     <- (int)level - GetStartingLevel() + 1
 */
u_long score_compute_bonus(int time_bonus, int num_bonus, int max_bonus,
                           int num_bullets, int level_adj);

/*
 * Return the hit point value for a block type.
 *
 * Replicates the switch in AddNewBlock() (blocks.c:2494-2566).
 * row is required for DROP_BLK (value depends on row position).
 * Returns 0 for block types with no score (DEATH_BLK, BLACK_BLK, etc.).
 */
int score_block_hit_points(int block_type, int row);

#endif /* SCORE_LOGIC_H */
