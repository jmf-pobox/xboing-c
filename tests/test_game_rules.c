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
#include "block_types.h"
#include "game_context.h"
#include "game_init.h"
#include "game_rules.h"
#include "message_system.h"
#include "paddle_system.h"
#include "sdl2_audio.h"
#include "sdl2_input.h"
#include "sdl2_state.h"
#include "sfx_system.h"

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
 * reach game-over exactly as before this guard was introduced.
 *
 * The game-over check runs BEFORE the decrement (game_rules_ball_died,
 * src/game_rules.c:291-319 comment) -- matching DeadBall's
 * `livesLeft <= 0 && GetAnActiveBall() == -1` (original/level.c:482).  With
 * 3 starting lives this yields 4 balls total: the death that FINDS
 * lives_left==1 respawns (decrementing to 0); only the NEXT death, which
 * finds lives_left already 0, triggers game-over. */
static void test_ball_died_real_game_lives_lost_game_over(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    ctx->play_test_active = false;
    ctx->lives_left = 1;
    ball_system_clear_all(ctx->ball);

    /* First death: lives_left (1) > 0 -> respawn branch, not game-over. */
    game_rules_ball_died(ctx);

    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_GAME);
    assert_int_equal(ctx->lives_left, 0);
    assert_int_equal(paddle_system_get_size_type(ctx->paddle), PADDLE_SIZE_HUGE);

    /* Second death: lives_left (0) <= 0 -> game-over. */
    ball_system_clear_all(ctx->ball);
    game_rules_ball_died(ctx);

    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_HIGHSCORE);
}

/* =========================================================================
 * 4-ball count + paddle re-expand regression (zpr)
 *
 * Pins two facts about game_rules_ball_died's respawn branch together:
 * (1) starting with 3 lives yields exactly 4 balls before game-over (the
 *     game-over check fires on the death that FINDS lives_left already 0,
 *     not the one that brings it there -- see the comment above and
 *     src/game_rules.c:291-306); (2) the paddle is forced back to
 *     PADDLE_SIZE_HUGE on every respawn regardless of its size going in
 *     (src/game_rules.c:339-345, ChangePaddleSize(PAD_EXPAND_BLK) x2 in
 *     original/level.c:496-497).  Each iteration sets the paddle to a
 *     non-HUGE size first so the HUGE assertion after game_rules_ball_died
 *     is non-vacuous.
 * ========================================================================= */

static void test_ball_died_four_balls_paddle_reexpands_each_respawn(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    ctx->play_test_active = false;
    ctx->lives_left = 3;

    const int expected_lives_after[3] = {2, 1, 0};

    for (int i = 0; i < 3; i++)
    {
        paddle_system_set_size(ctx->paddle, PADDLE_SIZE_SMALL);
        assert_int_equal(paddle_system_get_size_type(ctx->paddle), PADDLE_SIZE_SMALL);

        ball_system_clear_all(ctx->ball);
        game_rules_ball_died(ctx);

        assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_GAME);
        assert_int_equal(ctx->lives_left, expected_lives_after[i]);
        assert_int_equal(paddle_system_get_size_type(ctx->paddle), PADDLE_SIZE_HUGE);
    }

    /* 4th death: lives_left (0) <= 0 -> game-over. */
    ball_system_clear_all(ctx->ball);
    game_rules_ball_died(ctx);

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
 * Special-spawn throttle regression (m-2026-07-14-002 restore)
 *
 * Pins the one-at-a-time gate and finite lifetime of try_spawn_bonus
 * (static in src/game_rules.c, driven every SDL2ST_GAME tick via
 * game_rules_check -> mode_game_update, src/game_modes.c:345).  Before
 * the restore, the schedule-next-spawn branch did not check
 * ctx->bonus_block_active (`if (ctx->next_bonus_frame == 0)` with no
 * `!ctx->bonus_block_active` term), so a new spawn interval could
 * elapse -- and place a second special block -- while the previous one
 * was still on the grid, unhit.  These tests drive game_rules_check
 * indirectly through sdl2_state_update (mode_game_update calls it once
 * per tick) with a fixed rand() seed for reproducibility.
 *
 * ctx->play_test_active=true is used purely as a test seam here: it
 * suppresses game_rules_check's level-complete/bonus transition
 * (src/game_rules.c:471, `!block_system_still_active(...) &&
 * !ctx->play_test_active`) so an intentionally-emptied grid does not
 * end the run early -- try_spawn_bonus itself does not read this flag.
 *
 * Coverage is deliberately partitioned across the three restored
 * mechanisms in try_spawn_bonus (src/game_rules.c:67-266), not
 * merged into one test:
 *
 *   - test_spawn_throttle_one_at_a_time pins the historical root
 *     cause: unbounded accumulation of concurrent spawns.  It also
 *     re-asserts the no-reschedule-while-active invariant
 *     (next_bonus_frame == 0 whenever bonus_block_active) on every
 *     tick of its own 8000-tick run, so it independently fails
 *     against a narrow mutant that restores only the max-concurrency
 *     guard (line 108's `|| ctx->bonus_block_active`) but drops the
 *     re-arm guard at line 100 -- the concurrency check alone
 *     (max_concurrent <= 1) does not, because line 108 still blocks a
 *     second physical placement even when line 100's guard is
 *     mutated away.
 *   - test_spawn_throttle_expires_and_rearms pins the BONUS_LENGTH
 *     lifetime (expiry clears the cell) and confirms a later spawn
 *     still occurs after expiry (re-arm happens at all).
 *   - test_spawn_throttle_no_reschedule_while_active pins the
 *     `!bonus_block_active` guard at line 100 in isolation, over a
 *     tighter window scoped to exactly the active interval.
 *
 * Together the three (now with the added invariant folded into the
 * first) cover all three restored mechanisms; the sibling tests exist
 * independently of the folded invariant so a narrower failure still
 * localizes to a single, specifically-named test.
 * ========================================================================= */

/* src/game_rules.c BONUS_SEED -- max frames try_spawn_bonus waits before
 * attempting a spawn once armed. */
#define THROTTLE_BONUS_SEED 2000

static void throttle_tick(game_ctx_t *ctx)
{
    sdl2_input_begin_frame(ctx->input);
    sdl2_state_update(ctx->state);
}

/* Counts occupied cells in the whole grid.  Tests in this section start
 * from a fully-cleared grid and never launch a ball or fire the gun, so
 * every occupied cell observed afterward was placed by try_spawn_bonus. */
static int throttle_count_occupied(const block_system_t *block)
{
    int count = 0;
    for (int r = 0; r < MAX_ROW; r++)
    {
        for (int c = 0; c < MAX_COL; c++)
        {
            if (block_system_is_occupied(block, r, c))
                count++;
        }
    }
    return count;
}

static void test_spawn_throttle_one_at_a_time(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    srand(12345u);
    ctx->play_test_active = true;
    block_system_clear_all(ctx->block);
    assert_int_equal(throttle_count_occupied(ctx->block), 0);

    int max_concurrent = 0;
    int prev_count = 0;
    int spawn_events = 0;

    for (int i = 0; i < 8000; i++)
    {
        throttle_tick(ctx);

        /* play_test_active must keep us in GAME mode regardless of grid
         * state for the whole run -- a premature transition would mean
         * the harness itself broke, not the throttle under test. */
        assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_GAME);

        int count = throttle_count_occupied(ctx->block);
        if (count > max_concurrent)
            max_concurrent = count;
        if (count > prev_count)
            spawn_events++;
        prev_count = count;

        /* Re-arm guard, checked here too (not just in the dedicated
         * sibling test): while a spawn is active, next_bonus_frame must
         * stay at the sentinel 0 (src/game_rules.c:100). A mutant that
         * drops the `&& !ctx->bonus_block_active` term reschedules on
         * the very next tick after a placement -- deterministically,
         * regardless of the rand() stream -- so this catches it here
         * even though max_concurrent<=1 alone does not. */
        if (ctx->bonus_block_active)
            assert_int_equal(ctx->next_bonus_frame, 0);
    }

    /* Core anti-regression: before the throttle restore this could grow
     * past 1 (schedule-next-spawn ran without checking bonus_block_active).
     */
    assert_true(max_concurrent <= 1);
    /* Non-vacuous: prove spawns actually happened in this run, or the
     * assertion above proves nothing. BONUS_SEED=2000 max initial wait
     * guarantees at least one spawn attempt well within 8000 ticks. */
    assert_true(spawn_events >= 1);
}

static void test_spawn_throttle_expires_and_rearms(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    srand(777u);
    ctx->play_test_active = true;
    block_system_clear_all(ctx->block);

    /* Tick until the first spawn lands. THROTTLE_BONUS_SEED=2000 bounds
     * the wait; give generous headroom for the case-25/26 rolls that
     * consume a cycle without placing a block. */
    int found = 0;
    int spawn_row = -1;
    int spawn_col = -1;
    for (int i = 0; i < THROTTLE_BONUS_SEED * 4 && !found; i++)
    {
        throttle_tick(ctx);
        if (ctx->bonus_block_active)
        {
            found = 1;
            spawn_row = ctx->bonus_row;
            spawn_col = ctx->bonus_col;
        }
    }
    assert_true(found);
    assert_true(block_system_is_occupied(ctx->block, spawn_row, spawn_col));

    int expiry_frame = block_system_get_last_frame(ctx->block, spawn_row, spawn_col);

    /* Tick past the recorded expiry frame -- try_spawn_bonus clears the
     * cell and drops bonus_block_active on the first tick where
     * frame >= expiry_frame (src/game_rules.c:87-92). Bound the loop at
     * BLOCK_BONUS_LENGTH + margin: if a future regression restores
     * BLOCK_INFINITE_DELAY (9999999) as the spawned lifetime, this loop
     * must fail fast via the assertion below instead of spinning ~10M
     * iterations. */
    int frame = (int)sdl2_state_frame(ctx->state);
    int wait_needed = expiry_frame - frame + 2;
    int wait_bound = BLOCK_BONUS_LENGTH + 200;
    int wait_ticks = wait_needed < wait_bound ? wait_needed : wait_bound;

    int cleared = 0;
    for (int i = 0; i < wait_ticks; i++)
    {
        throttle_tick(ctx);
        if (!block_system_is_occupied(ctx->block, spawn_row, spawn_col) && !ctx->bonus_block_active)
        {
            cleared = 1;
            break;
        }
    }

    assert_true(cleared);
    assert_false(block_system_is_occupied(ctx->block, spawn_row, spawn_col));
    assert_false(ctx->bonus_block_active);

    /* Re-arm: a later spawn must still occur after expiry. */
    int rearmed = 0;
    for (int i = 0; i < THROTTLE_BONUS_SEED * 4 && !rearmed; i++)
    {
        throttle_tick(ctx);
        if (ctx->bonus_block_active)
            rearmed = 1;
    }
    assert_true(rearmed);
}

static void test_spawn_throttle_no_reschedule_while_active(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    srand(42u);
    ctx->play_test_active = true;
    block_system_clear_all(ctx->block);

    int found = 0;
    for (int i = 0; i < THROTTLE_BONUS_SEED * 4 && !found; i++)
    {
        throttle_tick(ctx);
        if (ctx->bonus_block_active)
            found = 1;
    }
    assert_true(found);

    /* Guard under test: `if (ctx->next_bonus_frame == 0 &&
     * !ctx->bonus_block_active)` (src/game_rules.c:100) -- while the
     * spawn is still active, next_bonus_frame must stay at 0 (the
     * sentinel try_spawn_bonus leaves it at after every cycle,
     * src/game_rules.c:265), never getting a premature reschedule. */
    for (int i = 0; i < 50 && ctx->bonus_block_active; i++)
    {
        assert_int_equal(ctx->next_bonus_frame, 0);
        throttle_tick(ctx);
    }
}

/* =========================================================================
 * game_rules_skip_level (mission m-2026-07-17-008,
 * docs/specs/2026-07-16-debug-skip-cheat-design.md)
 *
 * game_rules_skip_level is the thin orchestrator: it delegates the grid
 * sweep to block_system_explode_all_required() (unit-tested directly in
 * tests/test_block_system.c Group 17) and only sequences the
 * audio/sfx/state side effects around that call.  These tests exercise
 * the orchestration, not the sweep itself.
 * ========================================================================= */

/* TC-61: NULL ctx does not crash. */
static void test_skip_level_null_ctx_no_crash(void **state)
{
    (void)state;
    game_rules_skip_level(NULL, 0);
}

/* TC-62: Zero required blocks on the grid.  After the call: cheated is
 * set, the message is posted, sfx mode is SFX_MODE_SHAKE unconditionally
 * (armed regardless of cleared count -- src/game_rules.c's `if
 * (ctx->sfx)` block has no cleared>0 guard), and -- because cleared==0
 * -- the "touch" sound is never requested (that call is gated on
 * `cleared > 0`, src/game_rules.c). Verified via the sdl2_audio call-log
 * spy (sdl2_audio_log_snapshot), the same observability mechanism
 * tests/test_audio_name_validation.c uses; not a documentation-only
 * claim. */
static void test_skip_level_zero_required_shake_no_touch_audio(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    block_system_clear_all(ctx->block);
    assert_int_equal(block_system_still_active(ctx->block), 0);

    sdl2_audio_log_clear(ctx->audio);

    const int frame = 5000;
    game_rules_skip_level(ctx, frame);

    assert_true(ctx->cheated);
    assert_string_equal(message_system_get_text(ctx->message), "Cheating, skip level ...");
    assert_int_equal(sfx_system_get_mode(ctx->sfx), SFX_MODE_SHAKE);

    sdl2_audio_call_t entries[16];
    int n = sdl2_audio_log_snapshot(ctx->audio, entries, 16);
    for (int i = 0; i < n; i++)
    {
        assert_string_not_equal(entries[i].name, "touch");
    }
}

/* TC-63: cleared > 0 arms the shake for exactly 140 frames from the call
 * frame.  sfx_system has no end_frame getter (only
 * sfx_system_set_end_frame), so end_frame==frame+140 is pinned
 * indirectly: sfx_system_update() returns 1 (still running) at
 * frame+139 and 0 (expired, mode reset to SFX_MODE_NONE) at frame+140 --
 * matching update_shake's `if (frame >= ctx->end_frame)` expiry check
 * (src/sfx_system.c). This is the closest assertable proxy for a private
 * field with no accessor; noted per the mission's "closest asserting
 * alternative" instruction. */
static void test_skip_level_cleared_shake_end_frame_140(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    block_system_clear_all(ctx->block);
    block_system_add(ctx->block, 2, 3, RED_BLK, 0, 0);
    block_system_add(ctx->block, 2, 4, GREEN_BLK, 0, 0);

    const int frame = 8000;
    game_rules_skip_level(ctx, frame);

    assert_int_equal(sfx_system_get_mode(ctx->sfx), SFX_MODE_SHAKE);

    /* Still running one frame before expiry. */
    assert_int_equal(sfx_system_update(ctx->sfx, frame + 139), 1);
    assert_int_equal(sfx_system_get_mode(ctx->sfx), SFX_MODE_SHAKE);

    /* Expires exactly at frame + 140. */
    assert_int_equal(sfx_system_update(ctx->sfx, frame + 140), 0);
    assert_int_equal(sfx_system_get_mode(ctx->sfx), SFX_MODE_NONE);
}

/* TC-64: ctx->cheated flips to true regardless of what is on the board
 * (pins ADR-073) -- uses the level's default just-loaded board (many
 * required blocks still present) rather than an emptied grid, so the
 * assertion is independent of TC-62/TC-63's cleared-count scenarios. */
static void test_skip_level_sets_cheated_regardless_of_board(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    ctx->cheated = false;
    assert_int_equal(block_system_still_active(ctx->block), 1);

    game_rules_skip_level(ctx, 42);

    assert_true(ctx->cheated);
}

/* TC-65: The "Cheating, skip level ..." message replaces whatever was
 * posted before the call -- non-vacuous because the pre-condition text
 * is deliberately different from the expected post-call text. */
static void test_skip_level_sets_message(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    message_system_set(ctx->message, "- unrelated pre-existing message -", 0, 0);
    assert_string_equal(message_system_get_text(ctx->message), "- unrelated pre-existing message -");

    game_rules_skip_level(ctx, 99);

    assert_string_equal(message_system_get_text(ctx->message), "Cheating, skip level ...");
}

/* TC-66: Direct-API regression -- seed a small required-only grid, call
 * game_rules_skip_level, then drive block_system_update_explosions
 * across frames to finalize.  block_system_still_active must drain to 0,
 * proving the cheat drives the level to completion via the real
 * explosion lifecycle (not a shortcut that skips finalize). */
static void test_skip_level_drives_level_to_completion(void **vstate)
{
    fixture_t *f = (fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    block_system_clear_all(ctx->block);
    block_system_add(ctx->block, 4, 1, RED_BLK, 0, 0);
    block_system_add(ctx->block, 4, 2, BLUE_BLK, 0, 0);
    block_system_add(ctx->block, 4, 3, COUNTER_BLK, 3, 0);
    assert_int_equal(block_system_still_active(ctx->block), 1);

    const int frame = 100;
    game_rules_skip_level(ctx, frame);

    /* Drive the explosion state machine forward well past the 4-stage
     * animation (BLOCK_EXPLODE_DELAY=10 per stage) to finalize. */
    for (int f2 = frame; f2 < frame + 100; f2++)
    {
        block_system_update_explosions(ctx->block, f2, NULL, NULL);
    }

    assert_int_equal(block_system_still_active(ctx->block), 0);
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
        cmocka_unit_test_setup_teardown(test_ball_died_four_balls_paddle_reexpands_each_respawn,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(test_check_playtest_clears_board_no_bonus_transition,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(test_check_real_game_clears_board_reaches_bonus, setup,
                                        teardown),

        /* Special-spawn throttle regression (m-2026-07-14-002 restore) */
        cmocka_unit_test_setup_teardown(test_spawn_throttle_one_at_a_time, setup, teardown),
        cmocka_unit_test_setup_teardown(test_spawn_throttle_expires_and_rearms, setup, teardown),
        cmocka_unit_test_setup_teardown(test_spawn_throttle_no_reschedule_while_active, setup,
                                        teardown),

        /* game_rules_skip_level (mission m-2026-07-17-008) */
        cmocka_unit_test(test_skip_level_null_ctx_no_crash),
        cmocka_unit_test_setup_teardown(test_skip_level_zero_required_shake_no_touch_audio, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_skip_level_cleared_shake_end_frame_140, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_skip_level_sets_cheated_regardless_of_board, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_skip_level_sets_message, setup, teardown),
        cmocka_unit_test_setup_teardown(test_skip_level_drives_level_to_completion, setup,
                                        teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
