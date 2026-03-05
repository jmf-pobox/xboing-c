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

#endif /* GAME_RULES_H */
