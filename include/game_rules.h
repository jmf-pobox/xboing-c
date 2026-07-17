/*
 * game_rules.h -- Game rule logic for the SDL2 game.
 *
 * Checks for level completion, ball death, extra lives, and game over.
 * Called once per frame during MODE_GAME.
 */

#ifndef GAME_RULES_H
#define GAME_RULES_H

#include "game_context.h"

/*
 * Check game rules for the current frame.
 *
 * - Level completion: all required blocks cleared → advance level
 * - Ball death event: decrement lives, reset ball or game over
 * - Extra life: score threshold check
 */
void game_rules_check(game_ctx_t *ctx);

/*
 * Handle a ball death event (called from ball on_event callback).
 * Decrements lives, resets ball on paddle, or transitions to game over.
 */
void game_rules_ball_died(game_ctx_t *ctx);

/*
 * Advance to the next level: clear blocks, load next level file,
 * reset ball, cycle background.
 */
void game_rules_next_level(game_ctx_t *ctx);

/*
 * Check if any active ball overlaps the eyedude.
 * On hit: set eyedude to DIE state. Score, sound, and message fire
 * via eyedude_system_update's own callbacks on the next tick.
 * Matches original/ball.c:1339-1347 + original/eyedude.c:372-383.
 */
void game_rules_check_ball_eyedude(game_ctx_t *ctx);

/*
 * Debug skip-level cheat: orchestrates the cross-subsystem effects of
 * clearing every required block on the grid.  Delegates the grid sweep
 * to block_system_explode_all_required() (this function holds no grid
 * knowledge itself), then plays one "touch" sound if anything
 * exploded, arms a 140-frame screen shake, marks the session as
 * cheated (ADR-073), and posts the "Cheating, skip level ..." message.
 * Behavior citation: original/blocks.c:2409-2462 (SkipToNextLevel),
 * 1543-1548 (score at explosion finalize), 2460-2461 (shake).
 *
 * Caller must already have confirmed ctx->debug_mode and
 * SDL2ST_GAME -- this function does not re-check either.
 */
void game_rules_skip_level(game_ctx_t *ctx, int frame);

#endif /* GAME_RULES_H */
