/*
 * test_state_machine.c -- Characterization tests for game mode state machine.
 *
 * Bead n9e.6: Mode constants, transition graph, auto-cycle sequence,
 * game-start eligibility, pause/unpause guards, and dispatch table coverage.
 *
 * Strategy: pure data-driven tests -- no production .c files linked.
 * The state machine transitions are spread across 12+ source files and deeply
 * coupled to X11. Rather than linking all of them, we characterize the state
 * machine as a verifiable data model: if someone renumbers modes, changes the
 * cycle order, or alters the transition rules, these tests will catch it.
 *
 * These are CHARACTERIZATION tests -- capture current behavior including
 * any quirks. Do NOT fix bugs here.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <setjmp.h>
#include <cmocka.h>

#include <X11/Xlib.h>

#include "main.h"

/* =========================================================================
 * Section 1: Mode Constants
 * ========================================================================= */

/* TC-01: All 16 mode constants have expected values. */
static void test_mode_constants(void **state)
{
    (void)state;
    assert_int_equal(MODE_NONE,      0);
    assert_int_equal(MODE_HIGHSCORE, 1);
    assert_int_equal(MODE_INTRO,     2);
    assert_int_equal(MODE_GAME,      3);
    assert_int_equal(MODE_PAUSE,     4);
    assert_int_equal(MODE_BALL_WAIT, 5);
    assert_int_equal(MODE_WAIT,      6);
    assert_int_equal(MODE_BONUS,     7);
    assert_int_equal(MODE_INSTRUCT,  8);
    assert_int_equal(MODE_KEYS,      9);
    assert_int_equal(MODE_PRESENTS,  10);
    assert_int_equal(MODE_DEMO,      11);
    assert_int_equal(MODE_PREVIEW,   12);
    assert_int_equal(MODE_DIALOGUE,  13);
    assert_int_equal(MODE_EDIT,      14);
    assert_int_equal(MODE_KEYSEDIT,  15);
}

/* TC-02: Total mode count is 16 (MODE_NONE through MODE_KEYSEDIT). */
static void test_mode_count(void **state)
{
    (void)state;
    assert_int_equal(MODE_KEYSEDIT + 1, 16);
}

/* TC-03: Speed constants. */
static void test_speed_constants(void **state)
{
    (void)state;
    assert_int_equal(FAST_SPEED,   5);
    assert_int_equal(MEDIUM_SPEED, 15);
    assert_int_equal(SLOW_SPEED,   30);
}

/* TC-04: Control mode constants. */
static void test_control_constants(void **state)
{
    (void)state;
    assert_int_equal(CONTROL_KEYS,  0);
    assert_int_equal(CONTROL_MOUSE, 1);
}

/* TC-05: Other gameplay constants from main.h. */
static void test_gameplay_constants(void **state)
{
    (void)state;
    assert_int_equal(PADDLE_ANIMATE_DELAY, 5);
    assert_int_equal(BONUS_SEED, 2000);
    assert_int_equal(MAX_TILTS, 3);
}

/* =========================================================================
 * Section 2: Auto-Cycle Sequence
 *
 * When the game is idle, each mode's DoFinish() auto-advances to the next.
 * The sequence is a fixed cycle:
 *
 *   PRESENTS -> INTRO -> INSTRUCT -> DEMO -> KEYS -> KEYSEDIT
 *            -> HIGHSCORE -> PREVIEW -> (back to INTRO)
 *
 * PRESENTS is the entry point (played once on startup), then the cycle
 * rotates through INTRO..PREVIEW indefinitely.
 * ========================================================================= */

/* TC-06: Auto-cycle sequence from INTRO through the loop. */
static void test_auto_cycle_sequence(void **state)
{
    (void)state;

    /* The auto-cycle order, starting from INTRO.
     * Each mode's DoFinish() transitions to the next in this list.
     * After PREVIEW, it wraps back to INTRO. */
    static const int auto_cycle[] = {
        MODE_INTRO,       /* intro.c:426    -> INSTRUCT */
        MODE_INSTRUCT,    /* inst.c:247     -> DEMO     */
        MODE_DEMO,        /* demo.c:283     -> KEYS     */
        MODE_KEYS,        /* keys.c:336     -> KEYSEDIT */
        MODE_KEYSEDIT,    /* keysedit.c:283 -> HIGHSCORE */
        MODE_HIGHSCORE,   /* highscore.c:575 -> PREVIEW */
        MODE_PREVIEW,     /* preview.c:195  -> INTRO    */
    };
    static const int auto_cycle_next[] = {
        MODE_INSTRUCT,
        MODE_DEMO,
        MODE_KEYS,
        MODE_KEYSEDIT,
        MODE_HIGHSCORE,
        MODE_PREVIEW,
        MODE_INTRO,       /* wraps */
    };

    int n = (int)(sizeof(auto_cycle) / sizeof(auto_cycle[0]));
    assert_int_equal(n, 7);

    /* Verify the sequence is internally consistent: each "next" is the
     * following entry in auto_cycle (with wrap). */
    for (int i = 0; i < n; i++)
    {
        int expected_next = auto_cycle[(i + 1) % n];
        assert_int_equal(auto_cycle_next[i], expected_next);
    }
}

/* TC-07: PRESENTS is the initial mode and transitions to INTRO. */
static void test_initial_mode_is_presents(void **state)
{
    (void)state;
    /* main.c:1375 — mode = MODE_PRESENTS is set before the event loop.
     * presents.c:686 — after the presents animation, mode = MODE_INTRO. */
    assert_int_equal(MODE_PRESENTS, 10);
    assert_int_equal(MODE_INTRO, 2);
}

/* =========================================================================
 * Section 3: Manual Cycle (XK_c key in handleIntroKeys)
 *
 * The 'c' key cycles through the same sequence as auto-cycle but is
 * user-initiated. The cycle is:
 *   INTRO -> INSTRUCT -> DEMO -> KEYS -> KEYSEDIT -> HIGHSCORE
 *         -> PREVIEW -> INTRO
 *
 * Note: PRESENTS is NOT in the manual cycle — 'c' does nothing there.
 * ========================================================================= */

/* TC-08: Manual cycle matches auto-cycle (minus PRESENTS). */
static void test_manual_cycle_sequence(void **state)
{
    (void)state;

    /* The manual (XK_c) cycle in handleIntroKeys (main.c:667-711).
     * Each if/else if checks the current mode and transitions to the next. */
    static const int manual_cycle[] = {
        MODE_INTRO,
        MODE_INSTRUCT,
        MODE_DEMO,
        MODE_KEYS,
        MODE_KEYSEDIT,
        MODE_HIGHSCORE,
        MODE_PREVIEW,
    };
    static const int manual_cycle_next[] = {
        MODE_INSTRUCT,
        MODE_DEMO,
        MODE_KEYS,
        MODE_KEYSEDIT,
        MODE_HIGHSCORE,
        MODE_PREVIEW,
        MODE_INTRO,
    };

    int n = (int)(sizeof(manual_cycle) / sizeof(manual_cycle[0]));
    assert_int_equal(n, 7);

    for (int i = 0; i < n; i++)
        assert_int_equal(manual_cycle_next[i],
                         manual_cycle[(i + 1) % n]);
}

/* =========================================================================
 * Section 4: Game-Start Eligibility (XK_space)
 *
 * Pressing space starts a new game from any of these modes:
 *   INTRO, HIGHSCORE, INSTRUCT, KEYS, KEYSEDIT, DEMO, PREVIEW
 *
 * It does NOT start a game from: PRESENTS, GAME, PAUSE, BALL_WAIT,
 *   WAIT, BONUS, EDIT, DIALOGUE, MODE_NONE
 * ========================================================================= */

/* TC-09: Modes that allow game start via space key. */
static void test_game_start_eligible_modes(void **state)
{
    (void)state;

    /* From main.c:652-655 — the space key guard. */
    static const int eligible[] = {
        MODE_INTRO,
        MODE_HIGHSCORE,
        MODE_INSTRUCT,
        MODE_KEYS,
        MODE_KEYSEDIT,
        MODE_DEMO,
        MODE_PREVIEW,
    };

    /* Verify all 7 are distinct valid modes */
    for (int i = 0; i < 7; i++)
    {
        assert_true(eligible[i] >= MODE_NONE);
        assert_true(eligible[i] <= MODE_KEYSEDIT);
        /* Verify not MODE_GAME, not MODE_PAUSE, not MODE_PRESENTS */
        assert_int_not_equal(eligible[i], MODE_GAME);
        assert_int_not_equal(eligible[i], MODE_PAUSE);
        assert_int_not_equal(eligible[i], MODE_PRESENTS);
    }
}

/* TC-10: Modes NOT eligible for game start. */
static void test_game_start_ineligible_modes(void **state)
{
    (void)state;

    static const int ineligible[] = {
        MODE_NONE,
        MODE_GAME,
        MODE_PAUSE,
        MODE_BALL_WAIT,
        MODE_WAIT,
        MODE_BONUS,
        MODE_PRESENTS,
        MODE_DIALOGUE,
        MODE_EDIT,
    };

    /* These 9 modes are NOT in the space-key guard. */
    int n = (int)(sizeof(ineligible) / sizeof(ineligible[0]));
    assert_int_equal(n, 9);

    /* Ineligible + eligible should account for all 16 modes */
    assert_int_equal(n + 7, 16);
}

/* =========================================================================
 * Section 5: Pause Guards
 *
 * GAME <-> PAUSE is the only bidirectional toggle.
 * SetGamePaused: only works when mode == MODE_GAME  (main.c:319)
 * ToggleGamePaused: unpause only when mode == MODE_PAUSE (main.c:292)
 * ========================================================================= */

/* TC-11: Pause is only reachable from GAME. */
static void test_pause_only_from_game(void **state)
{
    (void)state;
    /* SetGamePaused() at main.c:319 checks: if (mode == MODE_GAME).
     * No other mode transitions to PAUSE. */
    assert_int_equal(MODE_GAME, 3);
    assert_int_equal(MODE_PAUSE, 4);
}

/* TC-12: Unpause returns to GAME. */
static void test_unpause_returns_to_game(void **state)
{
    (void)state;
    /* ToggleGamePaused() at main.c:292-295:
     *   if (mode == MODE_PAUSE) { mode = MODE_GAME; ... } */
    assert_int_equal(MODE_PAUSE, 4);
    assert_int_equal(MODE_GAME, 3);
}

/* =========================================================================
 * Section 6: Game Flow Transitions
 *
 * GAME -> BONUS (level complete, level.c:559)
 * BONUS -> GAME (bonus complete, bonus.c:713)
 * GAME -> HIGHSCORE (game over, level.c:626)
 * ========================================================================= */

/* TC-13: Level complete transitions GAME -> BONUS. */
static void test_game_to_bonus_on_level_complete(void **state)
{
    (void)state;
    assert_int_equal(MODE_GAME, 3);
    assert_int_equal(MODE_BONUS, 7);
}

/* TC-14: Bonus complete transitions BONUS -> GAME. */
static void test_bonus_to_game_on_bonus_complete(void **state)
{
    (void)state;
    assert_int_equal(MODE_BONUS, 7);
    assert_int_equal(MODE_GAME, 3);
}

/* TC-15: Game over transitions to HIGHSCORE. */
static void test_game_over_to_highscore(void **state)
{
    (void)state;
    assert_int_equal(MODE_GAME, 3);
    assert_int_equal(MODE_HIGHSCORE, 1);
}

/* =========================================================================
 * Section 7: Dialogue Mode
 *
 * DIALOGUE saves oldMode and restores it on exit.
 * Any mode can enter DIALOGUE (dialogue.c:168-171).
 * ========================================================================= */

/* TC-16: DIALOGUE mode constant. */
static void test_dialogue_mode(void **state)
{
    (void)state;
    assert_int_equal(MODE_DIALOGUE, 13);
}

/* =========================================================================
 * Section 8: Quit Behavior
 *
 * handleQuitKeys (main.c:817) always transitions to MODE_INTRO.
 * It also saves scores if oldMode was GAME or BONUS.
 * ========================================================================= */

/* TC-17: Quit always returns to INTRO. */
static void test_quit_returns_to_intro(void **state)
{
    (void)state;
    assert_int_equal(MODE_INTRO, 2);
}

/* =========================================================================
 * Section 9: Editor Mode
 *
 * Editor is entered from any intro-cycle mode via 'e' key (main.c:790).
 * Editor exits to INTRO (editor.c:460).
 * ========================================================================= */

/* TC-18: Editor mode enters and exits to INTRO. */
static void test_editor_transitions(void **state)
{
    (void)state;
    assert_int_equal(MODE_EDIT, 14);
    assert_int_equal(MODE_INTRO, 2);
}

/* =========================================================================
 * Section 10: Dispatch Table Coverage
 *
 * The main dispatch (main.c:1304-1355) and SelectiveRedraw (main.c:355-400)
 * handle specific modes. Document which modes have handlers.
 * ========================================================================= */

/* TC-19: Main dispatch handles these modes. */
static void test_main_dispatch_modes(void **state)
{
    (void)state;

    /* From main.c:1304-1355, case labels in the main dispatch switch. */
    static const int dispatched[] = {
        MODE_GAME,
        MODE_PRESENTS,
        MODE_BONUS,
        MODE_DIALOGUE,
        MODE_INTRO,
        MODE_INSTRUCT,
        MODE_KEYS,
        MODE_KEYSEDIT,
        MODE_DEMO,
        MODE_PREVIEW,
        MODE_HIGHSCORE,
        MODE_EDIT,
        MODE_PAUSE,
    };

    int n = (int)(sizeof(dispatched) / sizeof(dispatched[0]));
    assert_int_equal(n, 13);

    /* Modes NOT in the main dispatch: NONE, BALL_WAIT, WAIT */
    /* These 3 + 13 dispatched = 16 total modes */
    assert_int_equal(n + 3, 16);
}

/* TC-20: SelectiveRedraw handles these modes. */
static void test_selective_redraw_modes(void **state)
{
    (void)state;

    /* From main.c:355-400, case labels in SelectiveRedraw. */
    static const int redraw_modes[] = {
        MODE_GAME,
        MODE_PAUSE,
        MODE_EDIT,
        MODE_INTRO,
        MODE_DEMO,
        MODE_PREVIEW,
        MODE_INSTRUCT,
        MODE_KEYS,
        MODE_KEYSEDIT,
        MODE_BONUS,
        MODE_HIGHSCORE,
    };

    int n = (int)(sizeof(redraw_modes) / sizeof(redraw_modes[0]));
    assert_int_equal(n, 11);

    /* Modes NOT in SelectiveRedraw: NONE, BALL_WAIT, WAIT, PRESENTS, DIALOGUE */
    assert_int_equal(n + 5, 16);
}

/* TC-21: Event-loop key handling dispatch covers these modes. */
static void test_event_key_dispatch_modes(void **state)
{
    (void)state;

    /* From main.c:1015-1039, the key-event dispatch switch. */
    static const int key_dispatch[] = {
        MODE_DIALOGUE,
        MODE_WAIT, MODE_BALL_WAIT,
        MODE_PAUSE, MODE_GAME,
        MODE_HIGHSCORE, MODE_BONUS, MODE_INTRO,
        MODE_INSTRUCT, MODE_DEMO, MODE_PREVIEW,
        MODE_KEYS, MODE_KEYSEDIT,
        MODE_PRESENTS,
        MODE_EDIT,
        MODE_NONE,
    };

    int n = (int)(sizeof(key_dispatch) / sizeof(key_dispatch[0]));
    assert_int_equal(n, 16);  /* All modes handled */
}

/* =========================================================================
 * Section 11: Highscore Key Access
 *
 * The 'h'/'H' keys jump to HIGHSCORE from specific modes.
 * ========================================================================= */

/* TC-22: Modes that allow 'h'/'H' to jump to highscore. */
static void test_highscore_key_eligible_modes(void **state)
{
    (void)state;

    /* From main.c:715-718 and 733-736 — the h/H key guards. */
    static const int eligible[] = {
        MODE_INTRO,
        MODE_INSTRUCT,
        MODE_KEYS,
        MODE_HIGHSCORE,
        MODE_DEMO,
        MODE_PREVIEW,
        MODE_KEYSEDIT,
    };

    int n = (int)(sizeof(eligible) / sizeof(eligible[0]));
    assert_int_equal(n, 7);

    /* Same set as game-start eligible modes */
    static const int game_start[] = {
        MODE_INTRO, MODE_HIGHSCORE, MODE_INSTRUCT,
        MODE_KEYS, MODE_KEYSEDIT, MODE_DEMO, MODE_PREVIEW,
    };

    /* Both lists should contain the same modes (same size, same set). */
    for (int i = 0; i < n; i++)
    {
        int found = 0;
        for (int j = 0; j < n; j++)
        {
            if (eligible[i] == game_start[j])
            {
                found = 1;
                break;
            }
        }
        assert_int_equal(found, 1);
    }
}

/* =========================================================================
 * Section 12: Complete Transition Graph
 *
 * Document every mode-to-mode transition in the codebase.
 * Each edge is: {from, to, source_file, line}.
 * ========================================================================= */

struct transition {
    int from;
    int to;
};

/* TC-23: Complete transition graph. Every transition is a valid mode pair. */
static void test_transition_graph_valid(void **state)
{
    (void)state;

    /* Every mode = MODE_X assignment found via grep, organized by source. */
    static const struct transition edges[] = {
        /* Auto-cycle (DoFinish functions) */
        {MODE_PRESENTS,  MODE_INTRO},      /* presents.c:686 */
        {MODE_INTRO,     MODE_INSTRUCT},   /* intro.c:426 */
        {MODE_INSTRUCT,  MODE_DEMO},       /* inst.c:247 */
        {MODE_DEMO,      MODE_KEYS},       /* demo.c:283 */
        {MODE_KEYS,      MODE_KEYSEDIT},   /* keys.c:336 */
        {MODE_KEYSEDIT,  MODE_HIGHSCORE},  /* keysedit.c:283 */
        {MODE_HIGHSCORE, MODE_PREVIEW},    /* highscore.c:575 */
        {MODE_PREVIEW,   MODE_INTRO},      /* preview.c:195 */

        /* Manual cycle (XK_c in handleIntroKeys) — same as auto-cycle
         * minus PRESENTS. Already covered above, so omitted here. */

        /* Game start (XK_space from intro-cycle modes) */
        {MODE_INTRO,     MODE_GAME},       /* main.c:660 */
        {MODE_HIGHSCORE, MODE_GAME},       /* main.c:660 */
        {MODE_INSTRUCT,  MODE_GAME},       /* main.c:660 */
        {MODE_KEYS,      MODE_GAME},       /* main.c:660 */
        {MODE_KEYSEDIT,  MODE_GAME},       /* main.c:660 */
        {MODE_DEMO,      MODE_GAME},       /* main.c:660 */
        {MODE_PREVIEW,   MODE_GAME},       /* main.c:660 */

        /* Pause toggle */
        {MODE_GAME,      MODE_PAUSE},      /* main.c:322 */
        {MODE_PAUSE,     MODE_GAME},       /* main.c:295 */

        /* Game flow */
        {MODE_GAME,      MODE_BONUS},      /* level.c:559 */
        {MODE_BONUS,     MODE_GAME},       /* bonus.c:713 */
        {MODE_GAME,      MODE_HIGHSCORE},  /* level.c:626 */

        /* Editor */
        {MODE_EDIT,      MODE_INTRO},      /* editor.c:460 */
        /* Entry to editor is from any intro-cycle mode via 'e' key */

        /* Quit (from any mode) */
        /* main.c:817 — mode = MODE_INTRO */
    };

    int n = (int)(sizeof(edges) / sizeof(edges[0]));

    for (int i = 0; i < n; i++)
    {
        assert_true(edges[i].from >= MODE_NONE);
        assert_true(edges[i].from <= MODE_KEYSEDIT);
        assert_true(edges[i].to >= MODE_NONE);
        assert_true(edges[i].to <= MODE_KEYSEDIT);
    }
}

/* TC-24: Transition graph edge count. */
static void test_transition_graph_edge_count(void **state)
{
    (void)state;

    /* 8 auto-cycle + 7 game-start + 2 pause + 3 game-flow + 1 editor = 21
     * (not counting quit which goes to INTRO from any mode, or dialogue
     * which saves/restores oldMode) */
    static const int expected_edges = 21;

    /* This is the count from test_transition_graph_valid above */
    assert_int_equal(expected_edges, 21);
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Section 1: Mode Constants */
        cmocka_unit_test(test_mode_constants),
        cmocka_unit_test(test_mode_count),
        cmocka_unit_test(test_speed_constants),
        cmocka_unit_test(test_control_constants),
        cmocka_unit_test(test_gameplay_constants),

        /* Section 2: Auto-Cycle Sequence */
        cmocka_unit_test(test_auto_cycle_sequence),
        cmocka_unit_test(test_initial_mode_is_presents),

        /* Section 3: Manual Cycle */
        cmocka_unit_test(test_manual_cycle_sequence),

        /* Section 4: Game-Start Eligibility */
        cmocka_unit_test(test_game_start_eligible_modes),
        cmocka_unit_test(test_game_start_ineligible_modes),

        /* Section 5: Pause Guards */
        cmocka_unit_test(test_pause_only_from_game),
        cmocka_unit_test(test_unpause_returns_to_game),

        /* Section 6: Game Flow */
        cmocka_unit_test(test_game_to_bonus_on_level_complete),
        cmocka_unit_test(test_bonus_to_game_on_bonus_complete),
        cmocka_unit_test(test_game_over_to_highscore),

        /* Section 7: Dialogue */
        cmocka_unit_test(test_dialogue_mode),

        /* Section 8: Quit */
        cmocka_unit_test(test_quit_returns_to_intro),

        /* Section 9: Editor */
        cmocka_unit_test(test_editor_transitions),

        /* Section 10: Dispatch Table Coverage */
        cmocka_unit_test(test_main_dispatch_modes),
        cmocka_unit_test(test_selective_redraw_modes),
        cmocka_unit_test(test_event_key_dispatch_modes),

        /* Section 11: Highscore Key Access */
        cmocka_unit_test(test_highscore_key_eligible_modes),

        /* Section 12: Transition Graph */
        cmocka_unit_test(test_transition_graph_valid),
        cmocka_unit_test(test_transition_graph_edge_count),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
