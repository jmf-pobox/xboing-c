/*
 * test_integration_modes.c — Mode handler crash test.
 *
 * Force-transitions to each of the registered game modes and ticks
 * N frames to verify no crashes, memory errors, or undefined behavior.
 * ASan catches any issues during the ticking.
 *
 * This is primarily a robustness (no-crash) suite — most tests only
 * verify that a mode's enter/update don't crash, not that they do the
 * right thing.  A small set of focused correctness regressions is mixed
 * in (e.g. the bonus-interstitial rank tests at the end), each clearly
 * marked.
 *
 * Modes tested:
 *   PRESENTS, INTRO, INSTRUCT, DEMO, PREVIEW, KEYS, KEYSEDIT,
 *   HIGHSCORE, BONUS, GAME, PAUSE, EDIT
 *
 * Modes NOT directly tested:
 *   NONE (no handler), BALL_WAIT / WAIT (legacy, never assigned),
 *   DIALOGUE (requires push/pop semantics)
 *
 * Requires: SDL_VIDEODRIVER=dummy, SDL_AUDIODRIVER=dummy
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cmocka.h>

#include "bonus_system.h"
#include "game_context.h"
#include "game_init.h"
#include "gun_system.h"
#include "highscore_io.h"
#include "highscore_system.h"
#include "paths.h"
#include "score_system.h"
#include "sdl2_input.h"
#include "sdl2_renderer.h"
#include "sdl2_state.h"

/* =========================================================================
 * Writable argv buffer
 * ========================================================================= */

static char arg_prog[] = "xboing_test";

/* =========================================================================
 * Fixture — creates full game context, starts in PRESENTS
 * ========================================================================= */

typedef struct
{
    game_ctx_t *ctx;
} test_fixture_t;

static int setup(void **vstate)
{
    test_fixture_t *f = calloc(1, sizeof(*f));
    assert_non_null(f);

    char *argv[] = {arg_prog, NULL};
    f->ctx = game_create(1, argv);
    assert_non_null(f->ctx);

    /* Start in PRESENTS like game_main.c */
    sdl2_state_transition(f->ctx->state, SDL2ST_PRESENTS);

    *vstate = f;
    return 0;
}

static int teardown(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_destroy(f->ctx);
    free(f);
    return 0;
}

/* =========================================================================
 * Helper: transition to mode and tick N frames
 * ========================================================================= */

#define TICK_COUNT 100

static void enter_and_tick(game_ctx_t *ctx, sdl2_state_mode_t mode)
{
    sdl2_state_status_t status = sdl2_state_transition(ctx->state, mode);
    assert_int_equal(status, SDL2ST_OK);
    assert_int_equal(sdl2_state_current(ctx->state), mode);

    for (int i = 0; i < TICK_COUNT; i++)
    {
        sdl2_input_begin_frame(ctx->input);
        sdl2_state_update(ctx->state);
    }

    /* Mode may have auto-transitioned — that's fine, just verify no crash */
}

/* =========================================================================
 * Tests — one per mode
 * ========================================================================= */

static void test_mode_presents(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_PRESENTS);
}

static void test_mode_intro(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_INTRO);
}

static void test_mode_instruct(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_INSTRUCT);
}

static void test_mode_demo(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_DEMO);
}

static void test_mode_preview(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_PREVIEW);
}

static void test_mode_keys(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_KEYS);
}

static void test_mode_keysedit(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_KEYSEDIT);
}

static void test_mode_highscore(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_HIGHSCORE);
}

static void test_mode_bonus(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_BONUS);
}

static void test_mode_game(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_GAME);
}

/* =========================================================================
 * Bonus interstitial rank — correctness (not just no-crash)
 *
 * The test process is never setgid and sets no XBOING_SCORE_FILE, so the
 * global board is inactive and the interstitial must rank against the
 * PERSONAL board — the one the player is actually placed on at game over.
 * ========================================================================= */

/* Replace the personal board with a single top entry of the given score. */
static void seed_personal_top(game_ctx_t *ctx, unsigned long top_score)
{
    highscore_io_init_table(&ctx->hs_personal);
    ctx->hs_personal.entries[0].score = top_score;
    snprintf(ctx->hs_personal.entries[0].name, HIGHSCORE_NAME_LEN, "Prior Best");
}

/* Set the running score and zero every bonus input so the projected
 * (post-bonus) score the interstitial ranks equals the running score —
 * bonus_system_compute_total returns 0 when the timer and bullet count are
 * both zero.  Keeps these table-selection assertions independent of the
 * bonus arithmetic. */
static void set_score_no_bonus(game_ctx_t *ctx, unsigned long score)
{
    ctx->time_remaining = 0;
    ctx->bonus_count = 0;
    gun_system_set_ammo(ctx->gun, 0);
    score_system_set(ctx->score, score);
}

/* Regression for "interstitial said #1, game over said #2": with a higher
 * prior personal score, the bonus screen must report rank 2, not the
 * spurious 1 that ranking against an empty global board produced. */
static void test_bonus_rank_uses_personal_board(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    seed_personal_top(ctx, 100000);
    ctx->level_number = 3;
    set_score_no_bonus(ctx, 5000);

    sdl2_state_status_t st = sdl2_state_transition(ctx->state, SDL2ST_BONUS);
    assert_int_equal(st, SDL2ST_OK);
    assert_int_equal(bonus_system_get_highscore_rank(ctx->bonus), 2);
}

/* A score tied with the board's top entry predicts placement BEHIND it
 * (insert semantics), so the interstitial never over-promises "1st" for a
 * tie that will land the player 2nd. */
static void test_bonus_rank_tie_lands_behind(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    seed_personal_top(ctx, 5000);
    ctx->level_number = 2;
    set_score_no_bonus(ctx, 5000);

    sdl2_state_status_t st = sdl2_state_transition(ctx->state, SDL2ST_BONUS);
    assert_int_equal(st, SDL2ST_OK);
    assert_int_equal(bonus_system_get_highscore_rank(ctx->bonus), 2);
}

/* =========================================================================
 * SafeHighscore invariant — attract-path board selection
 *
 * On the attract auto-cycle (game_active == false) the high-score screen
 * defaults to the GLOBAL board (game_init.c:340).  On an unprivileged
 * install the global Hall of Fame is empty, so displaying it shows a blank
 * leaderboard even though the player has personal scores.  The screen must
 * fall back to the populated personal board.
 *
 * Formal proof of the reachable violation:
 * docs/specs/2026-07-04-screen-state-machine.tex, invariant SafeHighscore
 * (probcli goal-found trace to mHighscore / bGlobal / globalHasData=false /
 * personalHasData=true).  The test process is never setgid and sets no
 * XBOING_SCORE_FILE, so hs_global is inactive/empty — the exact scenario.
 * ========================================================================= */
static void test_highscore_attract_falls_back_to_personal(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* Unprivileged install: empty global board, populated personal. */
    highscore_io_init_table(&ctx->hs_global);
    seed_personal_top(ctx, 50000);
    ctx->highscore_request_type = HIGHSCORE_TYPE_GLOBAL; /* startup default */
    ctx->game_active = false;                            /* attract path */

    sdl2_state_status_t st = sdl2_state_transition(ctx->state, SDL2ST_HIGHSCORE);
    assert_int_equal(st, SDL2ST_OK);

    /* highscore_system copies the bound table by value, so assert on
     * content: the displayed board must be non-empty and show the
     * player's personal top score, not the blank global board. */
    const highscore_table_t *shown = highscore_system_get_table(ctx->highscore_display);
    assert_int_not_equal(highscore_io_count(shown), 0);
    assert_int_equal(shown->entries[0].score, 50000);
}

/* Negative case: when the global board HAS data the attract screen must
 * keep showing it (no spurious personal fallback), so the fallback guard
 * fires only in the empty-global scenario it was written for. */
static void test_highscore_attract_keeps_populated_global(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    highscore_io_init_table(&ctx->hs_global);
    ctx->hs_global.entries[0].score = 70000;
    snprintf(ctx->hs_global.entries[0].name, HIGHSCORE_NAME_LEN, "Global Champ");
    seed_personal_top(ctx, 50000);
    ctx->highscore_request_type = HIGHSCORE_TYPE_GLOBAL;
    ctx->game_active = false;

    sdl2_state_status_t st = sdl2_state_transition(ctx->state, SDL2ST_HIGHSCORE);
    assert_int_equal(st, SDL2ST_OK);

    const highscore_table_t *shown = highscore_system_get_table(ctx->highscore_display);
    assert_int_equal(shown->entries[0].score, 70000);
}

/* Guard's other branch: when there is nothing to fall back to (both boards
 * empty) the attract screen stays on GLOBAL — the fallback fires only when
 * the personal board actually has scores, so dropping the ">0" check would
 * fail this. */
static void test_highscore_attract_both_empty_stays_global(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    highscore_io_init_table(&ctx->hs_global);
    highscore_io_init_table(&ctx->hs_personal);
    ctx->highscore_request_type = HIGHSCORE_TYPE_GLOBAL;
    ctx->game_active = false;

    sdl2_state_status_t st = sdl2_state_transition(ctx->state, SDL2ST_HIGHSCORE);
    assert_int_equal(st, SDL2ST_OK);

    assert_int_equal(ctx->highscore_request_type, HIGHSCORE_TYPE_GLOBAL);
    const highscore_table_t *shown = highscore_system_get_table(ctx->highscore_display);
    assert_int_equal(highscore_io_count(shown), 0);
}

/* =========================================================================
 * SafeAttract invariant — game_active must not leak into attract screens
 *
 * After a game over the Highscore screen is shown with game_active still
 * true (it gates score submission).  When the finish-timer auto-advances
 * to the next attract screen (Intro), game_active must be cleared.  The
 * single clear site is mode_intro_enter, which every attract exit from
 * Highscore lands on (timer, C key, Space return; ADR-055).  If it leaks,
 * the next Highscore's Space returns to Intro instead of starting a game.
 *
 * Formal proof: docs/specs/2026-07-04-screen-state-machine.tex, invariant
 * SafeAttract (probcli goal "mode = mIntro & gameActive = ztrue" reachable
 * via GameOver ; AttractAdvance).
 * ========================================================================= */
static void test_highscore_autoadvance_clears_game_active(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* Simulate the game-over Highscore display; score already submitted so
     * the enter handler does not attempt a real submission. */
    ctx->game_active = true;
    ctx->score_submitted = true;
    sdl2_state_status_t st = sdl2_state_transition(ctx->state, SDL2ST_HIGHSCORE);
    assert_int_equal(st, SDL2ST_OK);

    /* Tick until the finish-timer auto-advances out of Highscore.  The
     * display reaches HIGHSCORE_END_FRAME_OFFSET (4000) at
     * ATTRACT_FRAME_MULTIPLIER (6) sub-frames per tick, i.e. ~670 ticks;
     * the 5000 cap is ~7x that headroom and only bounds a hang. */
    int guard = 0;
    while (sdl2_state_current(ctx->state) == SDL2ST_HIGHSCORE && guard < 5000)
    {
        sdl2_input_begin_frame(ctx->input);
        sdl2_state_update(ctx->state);
        guard++;
    }
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_INTRO);
    assert_false(ctx->game_active);
}

/* =========================================================================
 * ADR-073 — the '=' debug skip-level cheat forfeits BOTH highscore boards
 *
 * submit_score (game_modes.c:1029) early-returns `if (ctx->cheated)` before
 * either highscore_io_insert (personal) or highscore_io_insert_global_atomic
 * (global) runs.  A cheated session that would otherwise post a qualifying
 * score must not land on the personal board.
 *
 * These are the only tests in this file that drive a REAL submit_score
 * insert+write (every other HIGHSCORE test either never sets
 * game_active=true, or sets score_submitted=true to short-circuit the
 * submission — see test_highscore_autoadvance_clears_game_active above).
 * A real submission writes the personal table to
 * $XDG_DATA_HOME/xboing/score-personal.json, so these two tests use their
 * own fixture — mirroring tests/test_savegame_system.c's redirection
 * pattern — to keep that write inside a per-test tmp dir instead of the
 * developer's real XDG_DATA_HOME.  The shared `setup`/`teardown` above is
 * left untouched for every other test in the file.
 * ========================================================================= */

typedef struct
{
    game_ctx_t *ctx;
    char tmp_xdg[256]; /* per-test XDG_DATA_HOME so the personal-board write
                        * lands in a tmp dir, never the real user dir */
    char *prev_xdg;    /* heap-owned strdup of caller's prior XDG (or NULL) */
} cheat_fixture_t;

static int setup_cheat(void **vstate)
{
    cheat_fixture_t *f = calloc(1, sizeof(*f));
    assert_non_null(f);

    /* Preserve the caller's XDG_DATA_HOME so teardown can restore it
     * exactly — never leak an environment change across tests in the
     * same process. */
    const char *existing = getenv("XDG_DATA_HOME");
    f->prev_xdg = existing ? strdup(existing) : NULL;

    /* Paths are resolved inside game_create, so XDG_DATA_HOME must be
     * redirected BEFORE that call, not after. */
    snprintf(f->tmp_xdg, sizeof(f->tmp_xdg), ".tmp/test_integration_modes_xdg_%d",
             (int)getpid());
    (void)mkdir(".tmp", 0700);
    (void)mkdir(f->tmp_xdg, 0700);
    setenv("XDG_DATA_HOME", f->tmp_xdg, 1);

    char *argv[] = {arg_prog, NULL};
    f->ctx = game_create(1, argv);
    assert_non_null(f->ctx);
    sdl2_state_transition(f->ctx->state, SDL2ST_PRESENTS);

    *vstate = f;
    return 0;
}

static int teardown_cheat(void **vstate)
{
    cheat_fixture_t *f = (cheat_fixture_t *)*vstate;

    /* Delete any personal score file this test may have written before
     * destroying the context (path-config still valid). */
    char score_path[PATHS_MAX_PATH];
    if (paths_score_file_personal(&f->ctx->paths, score_path, sizeof(score_path)) == PATHS_OK)
        (void)unlink(score_path);

    game_destroy(f->ctx);

    /* Score files live at $XDG_DATA_HOME/xboing/..., so the intermediate
     * xboing/ subdir must be removed before the parent tmp dir. */
    char xboing_dir[300];
    snprintf(xboing_dir, sizeof(xboing_dir), "%s/xboing", f->tmp_xdg);
    (void)rmdir(xboing_dir);
    (void)rmdir(f->tmp_xdg);

    if (f->prev_xdg)
    {
        setenv("XDG_DATA_HOME", f->prev_xdg, 1);
        free(f->prev_xdg);
    }
    else
    {
        unsetenv("XDG_DATA_HOME");
    }
    free(f);
    return 0;
}

#define CHEAT_TEST_PRIOR_SCORE 1000UL
#define CHEAT_TEST_QUALIFYING_SCORE 42000UL

/* Drive the exact mode_highscore_enter seam
 * test_highscore_autoadvance_clears_game_active uses to reach submit_score:
 * game_active=true + score_submitted=false + a positive score triggers a
 * real (non-deferred) submission on the SDL2ST_HIGHSCORE transition —
 * sys_priv_global_board_active is false here (no XBOING_SCORE_FILE, not
 * setgid), so the would-be-global-master dialogue branch is never taken
 * and submit_score runs synchronously inline. */
static void test_cheated_session_forfeits_highscore(void **vstate)
{
    cheat_fixture_t *f = (cheat_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    seed_personal_top(ctx, CHEAT_TEST_PRIOR_SCORE);
    int prior_count = highscore_io_count(&ctx->hs_personal);

    ctx->level_number = 1;
    ctx->game_active = true;
    ctx->score_submitted = false;
    ctx->savegame_restored_session = false;
    ctx->game_start = 0;
    ctx->cheated = true;
    score_system_set(ctx->score, CHEAT_TEST_QUALIFYING_SCORE);

    sdl2_state_status_t st = sdl2_state_transition(ctx->state, SDL2ST_HIGHSCORE);
    assert_int_equal(st, SDL2ST_OK);

    /* Cheated: submit_score returned before either insert ran.  The board
     * is byte-for-byte the seeded state — no new entry, top entry
     * untouched.  Scan every slot directly rather than using
     * highscore_io_get_ranking: that function computes the rank a score
     * WOULD hold even when it was never inserted (it doesn't require
     * presence), so it cannot tell "not on the board" from "hypothetically
     * ranks ahead of everything on the board" — exactly the false pass
     * this regression test must not have. */
    assert_int_equal(highscore_io_count(&ctx->hs_personal), prior_count);
    assert_int_equal(ctx->hs_personal.entries[0].score, CHEAT_TEST_PRIOR_SCORE);
    for (int i = 0; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        assert_int_not_equal(ctx->hs_personal.entries[i].score, CHEAT_TEST_QUALIFYING_SCORE);
    }
}

/* Positive control: identical setup with ctx->cheated cleared.  Proves the
 * test above actually discriminates on the cheated flag rather than passing
 * for an unrelated reason (e.g. submit_score never being reached at all). */
static void test_uncheated_session_records_highscore(void **vstate)
{
    cheat_fixture_t *f = (cheat_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    seed_personal_top(ctx, CHEAT_TEST_PRIOR_SCORE);
    int prior_count = highscore_io_count(&ctx->hs_personal);

    ctx->level_number = 1;
    ctx->game_active = true;
    ctx->score_submitted = false;
    ctx->savegame_restored_session = false;
    ctx->game_start = 0;
    ctx->cheated = false;
    score_system_set(ctx->score, CHEAT_TEST_QUALIFYING_SCORE);

    sdl2_state_status_t st = sdl2_state_transition(ctx->state, SDL2ST_HIGHSCORE);
    assert_int_equal(st, SDL2ST_OK);

    /* Uncheated: the qualifying score is really inserted into the
     * personal board, ranked ahead of the seeded prior top (42000 > 1000)
     * — asserted on the entry directly, not just the rank helper, so this
     * test fails the same way test_cheated_session_forfeits_highscore's
     * would if the insert didn't actually happen. */
    assert_int_equal(highscore_io_count(&ctx->hs_personal), prior_count + 1);
    assert_int_equal(ctx->hs_personal.entries[0].score, CHEAT_TEST_QUALIFYING_SCORE);
    assert_int_equal(highscore_io_get_ranking(&ctx->hs_personal, CHEAT_TEST_QUALIFYING_SCORE), 1);
}

/* =========================================================================
 * -grab wiring — game_create applies the CLI flag to the window
 * ========================================================================= */

static char arg_grab[] = "-grab";

/* With -grab, the window's mouse is grabbed after game_create. */
static void test_grab_flag_applied(void **vstate)
{
    (void)vstate;
    char *argv[] = {arg_prog, arg_grab, NULL};
    game_ctx_t *ctx = game_create(2, argv);
    assert_non_null(ctx);
    /* Capture, destroy, then assert — so a failing assert still frees the
     * context (cmocka longjmps out of the test on failure). */
    bool grabbed = sdl2_renderer_is_mouse_grabbed(ctx->renderer);
    game_destroy(ctx);
    assert_true(grabbed);
}

/* Without -grab, the mouse is not grabbed (default). */
static void test_grab_flag_absent_default(void **vstate)
{
    (void)vstate;
    char *argv[] = {arg_prog, NULL};
    game_ctx_t *ctx = game_create(1, argv);
    assert_non_null(ctx);
    bool grabbed = sdl2_renderer_is_mouse_grabbed(ctx->renderer);
    game_destroy(ctx);
    assert_false(grabbed);
}

/* =========================================================================
 * SafeGame invariant — a failed level load must not enter GAME
 *
 * start_new_game must never leave the machine in GAME with an unloaded
 * (empty) grid: the unconditional game_rules_check (game_rules.c:333) would
 * read the empty grid as "level complete" and drop to BONUS on the first
 * tick.  Faithful to original/file.c:142-146 (SetupStage ShutDown on a
 * ReadNextLevel failure), modernised to refuse the game and return to the
 * attract cycle instead of exiting.  See ADR-056.
 *
 * Formal proof: docs/specs/2026-07-04-screen-state-machine.tex, invariant
 * SafeGame (probcli goal "mode = mGame & blocksLoaded = zfalse").
 *
 * A corrupt level file in XBOING_LEVELS_DIR forces a deterministic parse
 * failure: resolve_asset's legacy-override branch (src/paths.c:240-248)
 * short-circuits on file_exists, so the load hits this file rather than the
 * real levels/ tree the test's working directory would otherwise provide.
 * ========================================================================= */
static void test_game_aborts_on_level_load_failure(void **vstate)
{
    (void)vstate;

    /* EEXIST is the only tolerable mkdir failure; capture the verdict now
     * (errno is only valid immediately after the call) and assert at the end. */
    bool tmp_ok = (mkdir(".tmp", 0755) == 0 || errno == EEXIST);
    char dir[] = ".tmp/xboing_badlevel_XXXXXX";
    assert_non_null(mkdtemp(dir));

    char lvl_path[128];
    snprintf(lvl_path, sizeof(lvl_path), "%s/level01.data", dir);
    FILE *fp = fopen(lvl_path, "w");
    if (fp == NULL)
        rmdir(dir); /* clean up before the assert longjmps out */
    assert_non_null(fp);
    /* One line only: the parser needs a second line (time bonus) and returns
     * LEVEL_SYS_ERR_PARSE_FAILED on EOF, so the load fails deterministically. */
    fputs("not a valid xboing level file\n", fp);
    fclose(fp);

    /* strdup so a long pre-existing value round-trips exactly on restore
     * (a fixed buffer would truncate it and corrupt the env for later tests). */
    const char *prev = getenv("XBOING_LEVELS_DIR");
    char *saved = prev ? strdup(prev) : NULL;
    int set_rc = setenv("XBOING_LEVELS_DIR", dir, 1);

    char *argv[] = {arg_prog, NULL};
    game_ctx_t *ctx = game_create(1, argv);
    /* Paths are resolved at create; restore the environment immediately.
     * Capture the return code and assert at the end so a restore failure
     * doesn't leak the temp dir via longjmp. */
    int restore_rc = saved ? setenv("XBOING_LEVELS_DIR", saved, 1) : unsetenv("XBOING_LEVELS_DIR");
    free(saved);
    if (ctx == NULL)
    {
        unlink(lvl_path); /* clean up before the assert longjmps out */
        rmdir(dir);
    }
    assert_non_null(ctx);

    ctx->start_level = 1; /* pin to level01.data regardless of on-disk config */
    ctx->level_number = 1;

    sdl2_state_transition(ctx->state, SDL2ST_INTRO);
    sdl2_state_transition(ctx->state, SDL2ST_GAME);
    /* Post-fix, mode_game_enter redirects to INTRO synchronously inside the
     * GAME transition above (nested transition), so we are already on INTRO
     * here.  This tick only matters pre-fix: it runs mode_game_update ->
     * game_rules_check on the empty grid, which is what dropped to BONUS. */
    sdl2_state_update(ctx->state);

    /* Capture before destroy so a failing assert still frees the context. */
    sdl2_state_mode_t mode = sdl2_state_current(ctx->state);
    bool active = ctx->game_active;
    game_destroy(ctx);
    unlink(lvl_path);
    rmdir(dir);

    /* Env/mkdir bookkeeping asserted here (post-cleanup) so a failure can't
     * silently make the test use the wrong levels dir or leak the temp dir. */
    assert_true(tmp_ok);
    assert_int_equal(set_rc, 0);
    assert_int_equal(restore_rc, 0);

    /* Must not corrupt into an empty-grid BONUS; refuse the game and return
     * to the title with no active session. */
    assert_int_not_equal(mode, SDL2ST_BONUS);
    assert_int_equal(mode, SDL2ST_INTRO);
    assert_false(active);
}

static void test_mode_pause(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    /* Enter GAME first, then PAUSE (pause needs game to be active) */
    sdl2_state_status_t status = sdl2_state_transition(f->ctx->state, SDL2ST_GAME);
    assert_int_equal(status, SDL2ST_OK);
    enter_and_tick(f->ctx, SDL2ST_PAUSE);
}

static void test_mode_edit(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    enter_and_tick(f->ctx, SDL2ST_EDIT);
}

static void test_mode_dialogue_push_pop(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* Dialogue uses push/pop, not direct transition */
    sdl2_state_transition(ctx->state, SDL2ST_INTRO);

    sdl2_state_status_t status = sdl2_state_push_dialogue(ctx->state);
    assert_int_equal(status, SDL2ST_OK);
    assert_true(sdl2_state_is_dialogue(ctx->state));

    /* Tick in dialogue mode */
    for (int i = 0; i < TICK_COUNT; i++)
    {
        sdl2_input_begin_frame(ctx->input);
        sdl2_state_update(ctx->state);
    }

    /* Pop back to INTRO */
    status = sdl2_state_pop_dialogue(ctx->state);
    assert_int_equal(status, SDL2ST_OK);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_INTRO);
}

static void test_rapid_transitions(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* Rapidly cycle through all modes — stress test transition logic */
    sdl2_state_mode_t modes[] = {
        SDL2ST_PRESENTS, SDL2ST_INTRO,     SDL2ST_INSTRUCT, SDL2ST_DEMO,
        SDL2ST_PREVIEW,  SDL2ST_KEYS,      SDL2ST_KEYSEDIT, SDL2ST_HIGHSCORE,
        SDL2ST_BONUS,    SDL2ST_GAME,      SDL2ST_EDIT,     SDL2ST_PAUSE,
    };
    int mode_count = (int)(sizeof(modes) / sizeof(modes[0]));

    for (int cycle = 0; cycle < 3; cycle++)
    {
        for (int i = 0; i < mode_count; i++)
        {
            sdl2_state_transition(ctx->state, modes[i]);
            sdl2_input_begin_frame(ctx->input);
            sdl2_state_update(ctx->state);
        }
    }

    /* Just verify we're still alive */
    sdl2_state_mode_t mode = sdl2_state_current(ctx->state);
    assert_true(mode >= SDL2ST_NONE && mode < SDL2ST_COUNT);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_mode_presents, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_intro, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_instruct, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_demo, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_preview, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_keys, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_keysedit, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_highscore, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_bonus, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_game, setup, teardown),
        cmocka_unit_test_setup_teardown(test_bonus_rank_uses_personal_board, setup, teardown),
        cmocka_unit_test_setup_teardown(test_bonus_rank_tie_lands_behind, setup, teardown),
        cmocka_unit_test_setup_teardown(test_highscore_attract_falls_back_to_personal, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_highscore_attract_keeps_populated_global, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_highscore_attract_both_empty_stays_global, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_highscore_autoadvance_clears_game_active, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_cheated_session_forfeits_highscore, setup_cheat,
                                        teardown_cheat),
        cmocka_unit_test_setup_teardown(test_uncheated_session_records_highscore, setup_cheat,
                                        teardown_cheat),
        cmocka_unit_test(test_grab_flag_applied),
        cmocka_unit_test(test_grab_flag_absent_default),
        cmocka_unit_test(test_game_aborts_on_level_load_failure),
        cmocka_unit_test_setup_teardown(test_mode_pause, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_edit, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mode_dialogue_push_pop, setup, teardown),
        cmocka_unit_test_setup_teardown(test_rapid_transitions, setup, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
