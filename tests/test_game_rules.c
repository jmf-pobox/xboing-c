/*
 * test_game_rules.c — Integration tests for game_rules module.
 *
 * Tests the two reverse-persistence bug fixes (xboing-c-qnk):
 *
 *   Fix 2a: game_rules_next_level clears reverse
 *     (matches original/file.c:122 — SetReverseOff() inside SetupStage)
 *   Fix 2b: game_rules_ball_died clears reverse in the still-have-lives branch
 *     (matches original/level.c:492 — SetReverseOff() inside DeadBall)
 *
 * Each test sets reverse_on=1 BEFORE the transition, then asserts it is 0
 * after.  Testing without the pre-set would be vacuous (default is already 0).
 *
 * Also tests the editor play-test fidelity guards
 * (docs/specs/2026-07-12-playtest-fidelity.md S3.2, xboing-hay):
 * ctx->play_test_active must suppress both the lives-decrement/game-over
 * transition in game_rules_ball_died and the level-complete/bonus
 * transition in game_rules_check, exactly as the original's
 * `mode != MODE_EDIT` guards did (original/level.c:349-350, 474-505;
 * original/main.c:1140-1141).  Each play-test case has a regression
 * sibling with play_test_active=false proving the real-game behavior is
 * unchanged by the guard.
 *
 * Requires: SDL_VIDEODRIVER=dummy, SDL_AUDIODRIVER=dummy
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>

#include <cmocka.h>

#include "ball_system.h"
#include "ball_types.h"
#include "block_system.h"
#include "game_context.h"
#include "game_init.h"
#include "game_rules.h"
#include "paddle_system.h"
#include "sdl2_state.h"

/* =========================================================================
 * Writable argv buffer
 * ========================================================================= */

static char s_arg_prog[] = "xboing_test";

/* =========================================================================
 * Fixture — creates game context in GAME mode
 * ========================================================================= */

typedef struct
{
    game_ctx_t *ctx;
} fixture_t;

static int setup(void **vstate)
{
    fixture_t *f = calloc(1, sizeof(*f));
    assert_non_null(f);

    char *argv[] = {s_arg_prog, NULL};
    f->ctx = game_create(1, argv);
    assert_non_null(f->ctx);

    /* Enter GAME mode — calls start_new_game, loads level 1, resets paddle */
    sdl2_state_transition(f->ctx->state, SDL2ST_GAME);

    *vstate = f;
    return 0;
}

static int teardown(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_destroy(f->ctx);
    free(f);
    return 0;
}

/* =========================================================================
 * Fix 2a: game_rules_next_level clears reverse
 *
 * Canonical reference: original/file.c:122 — SetReverseOff() inside
 * SetupStage, which is called on every level transition.
 * ========================================================================= */

static void test_next_level_clears_reverse(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* Pre-condition: set reverse on.  Without this the test passes vacuously. */
    paddle_system_set_reverse(ctx->paddle, 1);
    assert_int_equal(paddle_system_get_reverse(ctx->paddle), 1);

    /* Calling game_rules_next_level should clear reverse */
    game_rules_next_level(ctx);

    assert_int_equal(paddle_system_get_reverse(ctx->paddle), 0);
}

/* =========================================================================
 * Fix 2b: game_rules_ball_died clears reverse (still-have-lives branch)
 *
 * Canonical reference: original/level.c:492 — SetReverseOff() inside
 * DeadBall, fires only when GetAnActiveBall() == -1 && livesLeft > 0.
 * ========================================================================= */

static void test_ball_died_clears_reverse(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* Ensure we have lives left so we take the still-have-lives branch */
    ctx->lives_left = 3;

    /* Ensure no active balls (so game_rules_ball_died doesn't early-return
     * from the multiball guard) */
    ball_system_clear_all(ctx->ball);

    /* Pre-condition: set reverse on */
    paddle_system_set_reverse(ctx->paddle, 1);
    assert_int_equal(paddle_system_get_reverse(ctx->paddle), 1);

    /* game_rules_ball_died: lives_left > 0, no active balls → still-have-lives branch */
    game_rules_ball_died(ctx);

    assert_int_equal(paddle_system_get_reverse(ctx->paddle), 0);
    /* Lives should have been decremented */
    assert_int_equal(ctx->lives_left, 2);
}

/* =========================================================================
 * Play-test fidelity: game_rules_ball_died (xboing-hay)
 *
 * Canonical reference: docs/specs/2026-07-12-playtest-fidelity.md S3.2,
 * S5 case 1.  Original: DecExtraLife's `if (mode != MODE_EDIT) livesLeft--;`
 * (original/level.c:346-357) combined with `mode` staying MODE_EDIT for the
 * whole editor session (original/main.c:680, editor.c:386) means DeadBall's
 * `livesLeft <= 0` game-over check (original/level.c:474-505) can never trip
 * during play-test.
 * ========================================================================= */

static void test_ball_died_playtest_no_lives_lost_no_game_over(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    ctx->play_test_active = true;
    ctx->lives_left = 1;
    ball_system_clear_all(ctx->ball);

    game_rules_ball_died(ctx);

    /* Lives must NOT deplete during play-test. */
    assert_int_equal(ctx->lives_left, 1);
    /* Must NOT hijack the state machine into the real game-over screen. */
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_GAME);
    /* Still-have-lives tail runs unconditionally: ball is reset on the
     * paddle (BALL_WAIT -> BALL_CREATE sequence, matches
     * ball_system_reset_start, src/ball_system.c:283-310). Slot 0 is
     * picked because ball_system_clear_all left every slot inactive and
     * ball_system_add scans from index 0. */
    assert_int_equal(ball_system_get_state(ctx->ball, 0), BALL_WAIT);
}

/* Regression sibling: play_test_active=false must still deplete lives and
 * reach game-over exactly as before this guard was introduced. */
static void test_ball_died_real_game_lives_lost_game_over(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    ctx->play_test_active = false;
    ctx->lives_left = 1;
    ball_system_clear_all(ctx->ball);

    game_rules_ball_died(ctx);

    assert_int_equal(ctx->lives_left, 0);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_HIGHSCORE);
}

/* =========================================================================
 * Play-test fidelity: game_rules_check level-complete (xboing-hay addendum)
 *
 * Canonical reference: docs/specs/2026-07-12-playtest-fidelity.md S3.2
 * addendum, S5 case 2.  Original: CheckGameRules is only ever called under
 * `if (mode == MODE_GAME)` (original/main.c:1140-1141), so clearing the
 * board during play-test (mode == MODE_EDIT) never reaches the real bonus
 * sequence.
 * ========================================================================= */

static void test_check_playtest_clears_board_no_bonus_transition(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    ctx->play_test_active = true;
    block_system_clear_all(ctx->block);
    assert_false(block_system_still_active(ctx->block));

    game_rules_check(ctx);

    /* Must NOT hijack the state machine into the real bonus screen. */
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_GAME);
}

/* Regression sibling: play_test_active=false must still reach the real
 * level-complete -> bonus transition exactly as before this guard. */
static void test_check_real_game_clears_board_reaches_bonus(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    ctx->play_test_active = false;
    block_system_clear_all(ctx->block);
    assert_false(block_system_still_active(ctx->block));

    game_rules_check(ctx);

    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_BONUS);
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_next_level_clears_reverse, setup, teardown),
        cmocka_unit_test_setup_teardown(test_ball_died_clears_reverse, setup, teardown),

        /* Play-test fidelity guards (xboing-hay,
         * docs/specs/2026-07-12-playtest-fidelity.md) */
        cmocka_unit_test_setup_teardown(test_ball_died_playtest_no_lives_lost_no_game_over, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_ball_died_real_game_lives_lost_game_over, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_check_playtest_clears_board_no_bonus_transition,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(test_check_real_game_clears_board_reaches_bonus, setup,
                                        teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
