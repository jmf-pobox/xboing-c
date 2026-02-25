/*
 * test_sdl2_regions.c — CMocka tests for SDL2 logical render regions.
 *
 * Pure data tests — no video driver needed.
 * Verifies region coordinates match the legacy sub-window layout in stage.c.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "sdl2_regions.h"

/* Legacy constants from stage.h for cross-reference. */
#define PLAY_WIDTH 495
#define PLAY_HEIGHT 580
#define MAIN_WIDTH 70
#define MAIN_HEIGHT 130
#define MESS_HEIGHT 30

/* =========================================================================
 * Group 1: Play area region
 * ========================================================================= */

/* TC-01: Play area matches legacy playWindow coordinates. */
static void test_play_region(void **state)
{
    (void)state;
    SDL_Rect r = sdl2_region_get(SDL2RGN_PLAY);
    assert_int_equal(r.x, MAIN_WIDTH / 2); /* offsetX = 35 */
    assert_int_equal(r.y, 60);
    assert_int_equal(r.w, PLAY_WIDTH);  /* 495 */
    assert_int_equal(r.h, PLAY_HEIGHT); /* 580 */
}

/* =========================================================================
 * Group 2: Score and level regions
 * ========================================================================= */

/* TC-02: Score area matches legacy scoreWindow. */
static void test_score_region(void **state)
{
    (void)state;
    SDL_Rect r = sdl2_region_get(SDL2RGN_SCORE);
    assert_int_equal(r.x, 35);
    assert_int_equal(r.y, 10);
    assert_int_equal(r.w, 224);
    assert_int_equal(r.h, 42);
}

/* TC-03: Level area matches legacy levelWindow. */
static void test_level_region(void **state)
{
    (void)state;
    SDL_Rect r = sdl2_region_get(SDL2RGN_LEVEL);
    assert_int_equal(r.x, 284); /* 224 + 35 + 25 */
    assert_int_equal(r.y, 5);
    assert_int_equal(r.w, 286); /* 495 + 35 - 20 - 224 */
    assert_int_equal(r.h, 52);
}

/* TC-04: Score and level regions don't overlap. */
static void test_score_level_no_overlap(void **state)
{
    (void)state;
    SDL_Rect score = sdl2_region_get(SDL2RGN_SCORE);
    SDL_Rect level = sdl2_region_get(SDL2RGN_LEVEL);
    assert_false(SDL_HasIntersection(&score, &level));
}

/* =========================================================================
 * Group 3: Bottom bar regions (message, special, timer)
 * ========================================================================= */

/* TC-05: Message area matches legacy messWindow. */
static void test_message_region(void **state)
{
    (void)state;
    SDL_Rect r = sdl2_region_get(SDL2RGN_MESSAGE);
    assert_int_equal(r.x, 35);
    assert_int_equal(r.y, 655);
    assert_int_equal(r.w, PLAY_WIDTH / 2); /* 247 */
    assert_int_equal(r.h, MESS_HEIGHT);    /* 30 */
}

/* TC-06: Special area matches legacy specialWindow. */
static void test_special_region(void **state)
{
    (void)state;
    SDL_Rect r = sdl2_region_get(SDL2RGN_SPECIAL);
    assert_int_equal(r.x, 292); /* 35 + 247 + 10 */
    assert_int_equal(r.y, 655);
    assert_int_equal(r.w, 180);
    assert_int_equal(r.h, MESS_HEIGHT + 5); /* 35 */
}

/* TC-07: Timer area matches legacy timeWindow. */
static void test_timer_region(void **state)
{
    (void)state;
    SDL_Rect r = sdl2_region_get(SDL2RGN_TIMER);
    assert_int_equal(r.x, 477); /* 35 + 247 + 10 + 180 + 5 */
    assert_int_equal(r.y, 655);
    assert_int_equal(r.w, PLAY_WIDTH / 8);  /* 61 */
    assert_int_equal(r.h, MESS_HEIGHT + 5); /* 35 */
}

/* TC-08: Bottom bar regions don't overlap each other. */
static void test_bottom_bar_no_overlap(void **state)
{
    (void)state;
    SDL_Rect msg = sdl2_region_get(SDL2RGN_MESSAGE);
    SDL_Rect spc = sdl2_region_get(SDL2RGN_SPECIAL);
    SDL_Rect tmr = sdl2_region_get(SDL2RGN_TIMER);
    assert_false(SDL_HasIntersection(&msg, &spc));
    assert_false(SDL_HasIntersection(&msg, &tmr));
    assert_false(SDL_HasIntersection(&spc, &tmr));
}

/* TC-09: All bottom bar regions share the same y origin. */
static void test_bottom_bar_same_y(void **state)
{
    (void)state;
    SDL_Rect msg = sdl2_region_get(SDL2RGN_MESSAGE);
    SDL_Rect spc = sdl2_region_get(SDL2RGN_SPECIAL);
    SDL_Rect tmr = sdl2_region_get(SDL2RGN_TIMER);
    assert_int_equal(msg.y, spc.y);
    assert_int_equal(msg.y, tmr.y);
}

/* =========================================================================
 * Group 4: Editor regions
 * ========================================================================= */

/* TC-10: Editor palette matches legacy blockWindow. */
static void test_editor_region(void **state)
{
    (void)state;
    SDL_Rect r = sdl2_region_get(SDL2RGN_EDITOR);
    assert_int_equal(r.x, 545); /* 35 + 495 + 15 */
    assert_int_equal(r.y, 60);
    assert_int_equal(r.w, 120);
    assert_int_equal(r.h, PLAY_HEIGHT); /* 580 */
}

/* TC-11: Editor type matches legacy typeWindow. */
static void test_editor_type_region(void **state)
{
    (void)state;
    SDL_Rect r = sdl2_region_get(SDL2RGN_EDITOR_TYPE);
    assert_int_equal(r.x, 545);
    assert_int_equal(r.y, 650); /* 65 + 580 + 5 */
    assert_int_equal(r.w, 120);
    assert_int_equal(r.h, 35);
}

/* TC-12: Editor palette is to the right of the play area. */
static void test_editor_right_of_play(void **state)
{
    (void)state;
    SDL_Rect play = sdl2_region_get(SDL2RGN_PLAY);
    SDL_Rect edit = sdl2_region_get(SDL2RGN_EDITOR);
    assert_true(edit.x > play.x + play.w);
}

/* =========================================================================
 * Group 5: Dialogue region
 * ========================================================================= */

/* TC-13: Dialogue is centered within main window (575x720). */
static void test_dialogue_centered(void **state)
{
    (void)state;
    SDL_Rect r = sdl2_region_get(SDL2RGN_DIALOGUE);
    int main_w = PLAY_WIDTH + MAIN_WIDTH + 10;   /* 575 */
    int main_h = PLAY_HEIGHT + MAIN_HEIGHT + 10; /* 720 */
    /* Check center is within 1 pixel of main center (integer rounding). */
    int cx = r.x + r.w / 2;
    int cy = r.y + r.h / 2;
    assert_in_range(cx, main_w / 2 - 1, main_w / 2 + 1);
    assert_in_range(cy, main_h / 2 - 1, main_h / 2 + 1);
}

/* TC-14: Dialogue overlaps the play area (it's a modal overlay). */
static void test_dialogue_overlaps_play(void **state)
{
    (void)state;
    SDL_Rect play = sdl2_region_get(SDL2RGN_PLAY);
    SDL_Rect dlg = sdl2_region_get(SDL2RGN_DIALOGUE);
    assert_true(SDL_HasIntersection(&play, &dlg));
}

/* =========================================================================
 * Group 6: Out-of-range and name strings
 * ========================================================================= */

/* TC-15: Out-of-range ID returns zero rect. */
static void test_get_out_of_range(void **state)
{
    (void)state;
    SDL_Rect r = sdl2_region_get(SDL2RGN_COUNT);
    assert_int_equal(r.x, 0);
    assert_int_equal(r.y, 0);
    assert_int_equal(r.w, 0);
    assert_int_equal(r.h, 0);

    SDL_Rect r2 = sdl2_region_get((sdl2_region_id_t)-1);
    assert_int_equal(r2.w, 0);
}

/* TC-16: All regions have non-NULL names. */
static void test_all_names(void **state)
{
    (void)state;
    for (int i = 0; i < SDL2RGN_COUNT; i++)
    {
        const char *name = sdl2_region_name((sdl2_region_id_t)i);
        assert_non_null(name);
        assert_true(name[0] != '\0');
    }
}

/* TC-17: Out-of-range name returns "unknown". */
static void test_name_out_of_range(void **state)
{
    (void)state;
    assert_string_equal(sdl2_region_name(SDL2RGN_COUNT), "unknown");
}

/* TC-18: Specific name values. */
static void test_name_values(void **state)
{
    (void)state;
    assert_string_equal(sdl2_region_name(SDL2RGN_PLAY), "play");
    assert_string_equal(sdl2_region_name(SDL2RGN_SCORE), "score");
    assert_string_equal(sdl2_region_name(SDL2RGN_TIMER), "timer");
    assert_string_equal(sdl2_region_name(SDL2RGN_DIALOGUE), "dialogue");
}

/* =========================================================================
 * Group 7: Hit testing
 * ========================================================================= */

/* TC-19: Hit test finds correct regions. */
static void test_hit_test_basic(void **state)
{
    (void)state;
    /* Center of play area. */
    SDL_Rect play = sdl2_region_get(SDL2RGN_PLAY);
    assert_int_equal(sdl2_region_hit_test(play.x + play.w / 2, play.y + play.h / 2), SDL2RGN_PLAY);

    /* Center of score area. */
    SDL_Rect score = sdl2_region_get(SDL2RGN_SCORE);
    assert_int_equal(sdl2_region_hit_test(score.x + score.w / 2, score.y + score.h / 2),
                     SDL2RGN_SCORE);

    /* Center of timer area. */
    SDL_Rect timer = sdl2_region_get(SDL2RGN_TIMER);
    assert_int_equal(sdl2_region_hit_test(timer.x + timer.w / 2, timer.y + timer.h / 2),
                     SDL2RGN_TIMER);
}

/* TC-20: Hit test returns COUNT for empty space. */
static void test_hit_test_miss(void **state)
{
    (void)state;
    /* Point (0, 0) is in the top-left margin, not in any region. */
    assert_int_equal(sdl2_region_hit_test(0, 0), SDL2RGN_COUNT);
    /* Negative coordinates. */
    assert_int_equal(sdl2_region_hit_test(-10, -10), SDL2RGN_COUNT);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest play_tests[] = {
        cmocka_unit_test(test_play_region),
    };

    const struct CMUnitTest header_tests[] = {
        cmocka_unit_test(test_score_region),
        cmocka_unit_test(test_level_region),
        cmocka_unit_test(test_score_level_no_overlap),
    };

    const struct CMUnitTest bottom_tests[] = {
        cmocka_unit_test(test_message_region),    cmocka_unit_test(test_special_region),
        cmocka_unit_test(test_timer_region),      cmocka_unit_test(test_bottom_bar_no_overlap),
        cmocka_unit_test(test_bottom_bar_same_y),
    };

    const struct CMUnitTest editor_tests[] = {
        cmocka_unit_test(test_editor_region),
        cmocka_unit_test(test_editor_type_region),
        cmocka_unit_test(test_editor_right_of_play),
    };

    const struct CMUnitTest dialogue_tests[] = {
        cmocka_unit_test(test_dialogue_centered),
        cmocka_unit_test(test_dialogue_overlaps_play),
    };

    const struct CMUnitTest name_tests[] = {
        cmocka_unit_test(test_get_out_of_range),
        cmocka_unit_test(test_all_names),
        cmocka_unit_test(test_name_out_of_range),
        cmocka_unit_test(test_name_values),
    };

    const struct CMUnitTest hit_tests[] = {
        cmocka_unit_test(test_hit_test_basic),
        cmocka_unit_test(test_hit_test_miss),
    };

    int fail = 0;
    fail += cmocka_run_group_tests_name("play area", play_tests, NULL, NULL);
    fail += cmocka_run_group_tests_name("header regions", header_tests, NULL, NULL);
    fail += cmocka_run_group_tests_name("bottom bar", bottom_tests, NULL, NULL);
    fail += cmocka_run_group_tests_name("editor regions", editor_tests, NULL, NULL);
    fail += cmocka_run_group_tests_name("dialogue", dialogue_tests, NULL, NULL);
    fail += cmocka_run_group_tests_name("names and bounds", name_tests, NULL, NULL);
    fail += cmocka_run_group_tests_name("hit testing", hit_tests, NULL, NULL);
    return fail;
}
