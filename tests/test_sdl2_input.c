/*
 * test_sdl2_input.c — CMocka tests for SDL2 input action mapping module.
 *
 * Pure data tests — no video or audio driver needed.
 * Tests synthesize SDL_Event structs directly.
 *
 * Bead: xboing-cks.3
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "sdl2_input.h"

/* =========================================================================
 * Helpers — synthesize SDL events
 * ========================================================================= */

static SDL_Event make_key_event(Uint32 type, SDL_Scancode scancode, SDL_Keymod mod)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.key.keysym.scancode = scancode;
    ev.key.keysym.mod = (Uint16)mod;
    ev.key.repeat = 0;
    return ev;
}

static SDL_Event make_key_repeat(SDL_Scancode scancode)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.scancode = scancode;
    ev.key.keysym.mod = 0;
    ev.key.repeat = 1;
    return ev;
}

static SDL_Event make_mouse_motion(int x, int y)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_MOUSEMOTION;
    ev.motion.x = x;
    ev.motion.y = y;
    return ev;
}

static SDL_Event make_mouse_button(Uint32 type, Uint8 button, int x, int y)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.button.button = button;
    ev.button.x = x;
    ev.button.y = y;
    return ev;
}

/* =========================================================================
 * Per-test fixtures
 * ========================================================================= */

static int setup_input(void **state)
{
    sdl2_input_status_t st;
    sdl2_input_t *ctx = sdl2_input_create(&st);
    if (ctx == NULL)
    {
        fprintf(stderr, "sdl2_input_create failed: %s\n", sdl2_input_status_string(st));
        return -1;
    }
    *state = ctx;
    return 0;
}

static int teardown_input(void **state)
{
    sdl2_input_destroy((sdl2_input_t *)*state);
    *state = NULL;
    return 0;
}

/* =========================================================================
 * Group 1: Lifecycle and defaults
 * ========================================================================= */

static void test_create_destroy(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;
    assert_non_null(ctx);
}

static void test_create_status_ok(void **state)
{
    (void)state;
    sdl2_input_status_t st;
    sdl2_input_t *ctx = sdl2_input_create(&st);
    assert_int_equal(st, SDL2I_OK);
    sdl2_input_destroy(ctx);
}

static void test_destroy_null(void **state)
{
    (void)state;
    sdl2_input_destroy(NULL); /* must not crash */
}

static void test_create_null_status_ptr(void **state)
{
    (void)state;
    sdl2_input_t *ctx = sdl2_input_create(NULL);
    assert_non_null(ctx);
    sdl2_input_destroy(ctx);
}

/* =========================================================================
 * Group 2: Default bindings
 * ========================================================================= */

static void test_default_left_binding(void **state)
{
    const sdl2_input_t *ctx = (const sdl2_input_t *)*state;
    assert_int_equal(sdl2_input_get_binding(ctx, SDL2I_LEFT, 0), SDL_SCANCODE_LEFT);
    assert_int_equal(sdl2_input_get_binding(ctx, SDL2I_LEFT, 1), SDL_SCANCODE_J);
}

static void test_default_right_binding(void **state)
{
    const sdl2_input_t *ctx = (const sdl2_input_t *)*state;
    assert_int_equal(sdl2_input_get_binding(ctx, SDL2I_RIGHT, 0), SDL_SCANCODE_RIGHT);
    assert_int_equal(sdl2_input_get_binding(ctx, SDL2I_RIGHT, 1), SDL_SCANCODE_L);
}

static void test_default_shoot_binding(void **state)
{
    const sdl2_input_t *ctx = (const sdl2_input_t *)*state;
    assert_int_equal(sdl2_input_get_binding(ctx, SDL2I_SHOOT, 0), SDL_SCANCODE_K);
}

static void test_default_pause_binding(void **state)
{
    const sdl2_input_t *ctx = (const sdl2_input_t *)*state;
    assert_int_equal(sdl2_input_get_binding(ctx, SDL2I_PAUSE, 0), SDL_SCANCODE_P);
}

static void test_default_quit_binding(void **state)
{
    const sdl2_input_t *ctx = (const sdl2_input_t *)*state;
    assert_int_equal(sdl2_input_get_binding(ctx, SDL2I_QUIT, 0), SDL_SCANCODE_Q);
}

static void test_default_speed_bindings(void **state)
{
    const sdl2_input_t *ctx = (const sdl2_input_t *)*state;
    assert_int_equal(sdl2_input_get_binding(ctx, SDL2I_SPEED_1, 0), SDL_SCANCODE_1);
    assert_int_equal(sdl2_input_get_binding(ctx, SDL2I_SPEED_5, 0), SDL_SCANCODE_5);
    assert_int_equal(sdl2_input_get_binding(ctx, SDL2I_SPEED_9, 0), SDL_SCANCODE_9);
}

/* =========================================================================
 * Group 3: Key press / release
 * ========================================================================= */

static void test_key_press_sets_pressed(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;
    assert_false(sdl2_input_pressed(ctx, SDL2I_SHOOT));
    SDL_Event ev = make_key_event(SDL_KEYDOWN, SDL_SCANCODE_K, 0);
    sdl2_input_process_event(ctx, &ev);
    assert_true(sdl2_input_pressed(ctx, SDL2I_SHOOT));
}

static void test_key_release_clears_pressed(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;
    SDL_Event down = make_key_event(SDL_KEYDOWN, SDL_SCANCODE_K, 0);
    SDL_Event up = make_key_event(SDL_KEYUP, SDL_SCANCODE_K, 0);
    sdl2_input_process_event(ctx, &down);
    assert_true(sdl2_input_pressed(ctx, SDL2I_SHOOT));
    sdl2_input_process_event(ctx, &up);
    assert_false(sdl2_input_pressed(ctx, SDL2I_SHOOT));
}

static void test_just_pressed_edge_trigger(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;
    sdl2_input_begin_frame(ctx);
    SDL_Event ev = make_key_event(SDL_KEYDOWN, SDL_SCANCODE_P, 0);
    sdl2_input_process_event(ctx, &ev);
    assert_true(sdl2_input_just_pressed(ctx, SDL2I_PAUSE));
    /* After begin_frame, edge trigger resets */
    sdl2_input_begin_frame(ctx);
    assert_false(sdl2_input_just_pressed(ctx, SDL2I_PAUSE));
    /* But level trigger persists */
    assert_true(sdl2_input_pressed(ctx, SDL2I_PAUSE));
}

static void test_key_repeat_no_edge_trigger(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;
    sdl2_input_begin_frame(ctx);
    /* First press triggers edge */
    SDL_Event ev = make_key_event(SDL_KEYDOWN, SDL_SCANCODE_K, 0);
    sdl2_input_process_event(ctx, &ev);
    assert_true(sdl2_input_just_pressed(ctx, SDL2I_SHOOT));
    /* Reset frame, then send repeat — no new edge trigger */
    sdl2_input_begin_frame(ctx);
    SDL_Event rep = make_key_repeat(SDL_SCANCODE_K);
    sdl2_input_process_event(ctx, &rep);
    assert_false(sdl2_input_just_pressed(ctx, SDL2I_SHOOT));
    assert_true(sdl2_input_pressed(ctx, SDL2I_SHOOT));
}

static void test_secondary_binding(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;
    /* J is secondary binding for LEFT */
    SDL_Event ev = make_key_event(SDL_KEYDOWN, SDL_SCANCODE_J, 0);
    sdl2_input_process_event(ctx, &ev);
    assert_true(sdl2_input_pressed(ctx, SDL2I_LEFT));
}

static void test_unbound_scancode_ignored(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;
    /* F12 is not bound to any action by default */
    SDL_Event ev = make_key_event(SDL_KEYDOWN, SDL_SCANCODE_F12, 0);
    sdl2_input_process_event(ctx, &ev);
    /* No action should be triggered */
    for (int i = 0; i < SDL2I_ACTION_COUNT; i++)
    {
        assert_false(sdl2_input_pressed(ctx, (sdl2_input_action_t)i));
    }
}

static void test_dual_binding_release_one(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;

    /* Press primary (Left arrow) for LEFT. */
    SDL_Event primary_down = make_key_event(SDL_KEYDOWN, SDL_SCANCODE_LEFT, 0);
    sdl2_input_process_event(ctx, &primary_down);
    assert_true(sdl2_input_pressed(ctx, SDL2I_LEFT));

    /* Also press secondary (J) for LEFT. */
    SDL_Event secondary_down = make_key_event(SDL_KEYDOWN, SDL_SCANCODE_J, 0);
    sdl2_input_process_event(ctx, &secondary_down);
    assert_true(sdl2_input_pressed(ctx, SDL2I_LEFT));

    /* Release primary — secondary still held, action should persist. */
    SDL_Event primary_up = make_key_event(SDL_KEYUP, SDL_SCANCODE_LEFT, 0);
    sdl2_input_process_event(ctx, &primary_up);
    assert_true(sdl2_input_pressed(ctx, SDL2I_LEFT));

    /* Release secondary — both released, action should clear. */
    SDL_Event secondary_up = make_key_event(SDL_KEYUP, SDL_SCANCODE_J, 0);
    sdl2_input_process_event(ctx, &secondary_up);
    assert_false(sdl2_input_pressed(ctx, SDL2I_LEFT));
}

/* =========================================================================
 * Group 4: Mouse
 * ========================================================================= */

static void test_mouse_motion(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;
    SDL_Event ev = make_mouse_motion(150, 300);
    sdl2_input_process_event(ctx, &ev);
    int x = 0, y = 0;
    sdl2_input_get_mouse(ctx, &x, &y);
    assert_int_equal(x, 150);
    assert_int_equal(y, 300);
}

static void test_mouse_button_press_release(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;
    assert_false(sdl2_input_mouse_pressed(ctx, 1));
    SDL_Event down = make_mouse_button(SDL_MOUSEBUTTONDOWN, 1, 100, 200);
    sdl2_input_process_event(ctx, &down);
    assert_true(sdl2_input_mouse_pressed(ctx, 1));
    SDL_Event up = make_mouse_button(SDL_MOUSEBUTTONUP, 1, 100, 200);
    sdl2_input_process_event(ctx, &up);
    assert_false(sdl2_input_mouse_pressed(ctx, 1));
}

static void test_mouse_multiple_buttons(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;
    SDL_Event b1 = make_mouse_button(SDL_MOUSEBUTTONDOWN, 1, 0, 0);
    SDL_Event b3 = make_mouse_button(SDL_MOUSEBUTTONDOWN, 3, 0, 0);
    sdl2_input_process_event(ctx, &b1);
    sdl2_input_process_event(ctx, &b3);
    assert_true(sdl2_input_mouse_pressed(ctx, 1));
    assert_false(sdl2_input_mouse_pressed(ctx, 2));
    assert_true(sdl2_input_mouse_pressed(ctx, 3));
}

static void test_mouse_null_ctx(void **state)
{
    (void)state;
    int x = -1, y = -1;
    sdl2_input_get_mouse(NULL, &x, &y);
    assert_int_equal(x, 0);
    assert_int_equal(y, 0);
    assert_false(sdl2_input_mouse_pressed(NULL, 1));
}

static void test_mouse_invalid_button(void **state)
{
    const sdl2_input_t *ctx = (const sdl2_input_t *)*state;
    assert_false(sdl2_input_mouse_pressed(ctx, 0));
    assert_false(sdl2_input_mouse_pressed(ctx, 6));
}

static void test_mouse_invalid_button_event(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;

    /* Feed an invalid button=0 event — should be ignored (no bit set). */
    SDL_Event down = make_mouse_button(SDL_MOUSEBUTTONDOWN, 0, 100, 200);
    sdl2_input_process_event(ctx, &down);

    /* No buttons should be registered as pressed. */
    assert_false(sdl2_input_mouse_pressed(ctx, 1));
    assert_false(sdl2_input_mouse_pressed(ctx, 2));
    assert_false(sdl2_input_mouse_pressed(ctx, 3));

    /* Position still updates (button value is invalid, not the position). */
    int x = 0, y = 0;
    sdl2_input_get_mouse(ctx, &x, &y);
    assert_int_equal(x, 100);
    assert_int_equal(y, 200);
}

/* =========================================================================
 * Group 5: Modifier queries
 * ========================================================================= */

static void test_shift_not_held(void **state)
{
    const sdl2_input_t *ctx = (const sdl2_input_t *)*state;
    assert_false(sdl2_input_shift_held(ctx));
}

static void test_shift_held(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;
    SDL_Event ev = make_key_event(SDL_KEYDOWN, SDL_SCANCODE_H, KMOD_LSHIFT);
    sdl2_input_process_event(ctx, &ev);
    assert_true(sdl2_input_shift_held(ctx));
}

/* =========================================================================
 * Group 6: Rebinding
 * ========================================================================= */

static void test_rebind_action(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;
    /* Rebind SHOOT from K to F5 (unused by any default binding) */
    sdl2_input_status_t st = sdl2_input_bind(ctx, SDL2I_SHOOT, 0, SDL_SCANCODE_F5);
    assert_int_equal(st, SDL2I_OK);
    assert_int_equal(sdl2_input_get_binding(ctx, SDL2I_SHOOT, 0), SDL_SCANCODE_F5);
    /* Old key (K) no longer triggers SHOOT */
    SDL_Event old_key = make_key_event(SDL_KEYDOWN, SDL_SCANCODE_K, 0);
    sdl2_input_process_event(ctx, &old_key);
    assert_false(sdl2_input_pressed(ctx, SDL2I_SHOOT));
    /* New key (F5) triggers SHOOT */
    SDL_Event new_key = make_key_event(SDL_KEYDOWN, SDL_SCANCODE_F5, 0);
    sdl2_input_process_event(ctx, &new_key);
    assert_true(sdl2_input_pressed(ctx, SDL2I_SHOOT));
}

static void test_rebind_invalid_action(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;
    sdl2_input_status_t st = sdl2_input_bind(ctx, SDL2I_ACTION_COUNT, 0, SDL_SCANCODE_A);
    assert_int_equal(st, SDL2I_ERR_INVALID_ACTION);
}

static void test_rebind_invalid_slot(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;
    sdl2_input_status_t st = sdl2_input_bind(ctx, SDL2I_SHOOT, 2, SDL_SCANCODE_A);
    assert_int_equal(st, SDL2I_ERR_INVALID_SLOT);
}

static void test_rebind_null_ctx(void **state)
{
    (void)state;
    sdl2_input_status_t st = sdl2_input_bind(NULL, SDL2I_SHOOT, 0, SDL_SCANCODE_A);
    assert_int_equal(st, SDL2I_ERR_NULL_ARG);
}

static void test_reset_bindings(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;
    /* Change a binding */
    sdl2_input_bind(ctx, SDL2I_SHOOT, 0, SDL_SCANCODE_SPACE);
    assert_int_equal(sdl2_input_get_binding(ctx, SDL2I_SHOOT, 0), SDL_SCANCODE_SPACE);
    /* Reset to defaults */
    sdl2_input_reset_bindings(ctx);
    assert_int_equal(sdl2_input_get_binding(ctx, SDL2I_SHOOT, 0), SDL_SCANCODE_K);
}

static void test_clear_binding(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;
    /* Clear secondary binding for LEFT */
    sdl2_input_bind(ctx, SDL2I_LEFT, 1, SDL_SCANCODE_UNKNOWN);
    assert_int_equal(sdl2_input_get_binding(ctx, SDL2I_LEFT, 1), SDL_SCANCODE_UNKNOWN);
    /* J no longer triggers LEFT */
    SDL_Event ev = make_key_event(SDL_KEYDOWN, SDL_SCANCODE_J, 0);
    sdl2_input_process_event(ctx, &ev);
    assert_false(sdl2_input_pressed(ctx, SDL2I_LEFT));
}

/* =========================================================================
 * Group 7: Error handling and edge cases
 * ========================================================================= */

static void test_pressed_null_ctx(void **state)
{
    (void)state;
    assert_false(sdl2_input_pressed(NULL, SDL2I_SHOOT));
    assert_false(sdl2_input_just_pressed(NULL, SDL2I_SHOOT));
}

static void test_pressed_invalid_action(void **state)
{
    const sdl2_input_t *ctx = (const sdl2_input_t *)*state;
    assert_false(sdl2_input_pressed(ctx, SDL2I_ACTION_COUNT));
    assert_false(sdl2_input_pressed(ctx, (sdl2_input_action_t)-1));
}

static void test_process_event_null(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;
    sdl2_input_process_event(ctx, NULL);  /* must not crash */
    sdl2_input_process_event(NULL, NULL); /* must not crash */
}

static void test_begin_frame_null(void **state)
{
    (void)state;
    sdl2_input_begin_frame(NULL); /* must not crash */
}

static void test_get_binding_null(void **state)
{
    (void)state;
    assert_int_equal(sdl2_input_get_binding(NULL, SDL2I_SHOOT, 0), SDL_SCANCODE_UNKNOWN);
}

/* =========================================================================
 * Group 8 (new): sdl2_input_mouse_just_pressed edge-trigger API
 *
 * Gap 12 — original/main.c:357-366 fires once per ButtonPress event.
 * sdl2_input_mouse_just_pressed must behave like sdl2_input_just_pressed:
 * fires only on the first frame of a button-down, cleared by begin_frame.
 * ========================================================================= */

/* Mouse just-pressed: set on button-down, cleared by begin_frame */
static void test_mouse_just_pressed_edge_trigger(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;

    sdl2_input_begin_frame(ctx);
    assert_false(sdl2_input_mouse_just_pressed(ctx, SDL_BUTTON_LEFT));

    SDL_Event down = make_mouse_button(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, 100, 200);
    sdl2_input_process_event(ctx, &down);
    assert_true(sdl2_input_mouse_just_pressed(ctx, SDL_BUTTON_LEFT));
    /* Level trigger also set */
    assert_true(sdl2_input_mouse_pressed(ctx, SDL_BUTTON_LEFT));

    /* begin_frame clears edge, but level trigger persists */
    sdl2_input_begin_frame(ctx);
    assert_false(sdl2_input_mouse_just_pressed(ctx, SDL_BUTTON_LEFT));
    assert_true(sdl2_input_mouse_pressed(ctx, SDL_BUTTON_LEFT));
}

/* Mouse just-pressed: not re-triggered while button held across frames */
static void test_mouse_just_pressed_not_repeated(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;

    sdl2_input_begin_frame(ctx);
    SDL_Event down = make_mouse_button(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, 0, 0);
    sdl2_input_process_event(ctx, &down);
    assert_true(sdl2_input_mouse_just_pressed(ctx, SDL_BUTTON_LEFT));

    /* Second frame — no new button-down event — edge cleared */
    sdl2_input_begin_frame(ctx);
    assert_false(sdl2_input_mouse_just_pressed(ctx, SDL_BUTTON_LEFT));
    /* Third frame — still no new down event */
    sdl2_input_begin_frame(ctx);
    assert_false(sdl2_input_mouse_just_pressed(ctx, SDL_BUTTON_LEFT));
}

/* Mouse just-pressed: independent per button */
static void test_mouse_just_pressed_independent_buttons(void **state)
{
    sdl2_input_t *ctx = (sdl2_input_t *)*state;

    sdl2_input_begin_frame(ctx);
    SDL_Event right_down = make_mouse_button(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT, 0, 0);
    sdl2_input_process_event(ctx, &right_down);

    assert_false(sdl2_input_mouse_just_pressed(ctx, SDL_BUTTON_LEFT));
    assert_false(sdl2_input_mouse_just_pressed(ctx, SDL_BUTTON_MIDDLE));
    assert_true(sdl2_input_mouse_just_pressed(ctx, SDL_BUTTON_RIGHT));
}

/* Mouse just-pressed: NULL ctx returns false safely */
static void test_mouse_just_pressed_null_ctx(void **state)
{
    (void)state;
    assert_false(sdl2_input_mouse_just_pressed(NULL, SDL_BUTTON_LEFT));
}

/* Mouse just-pressed: invalid button returns false */
static void test_mouse_just_pressed_invalid_button(void **state)
{
    const sdl2_input_t *ctx = (const sdl2_input_t *)*state;
    assert_false(sdl2_input_mouse_just_pressed(ctx, 0));
    assert_false(sdl2_input_mouse_just_pressed(ctx, 6));
}

/* =========================================================================
 * Group 9: Action names and status strings
 * ========================================================================= */


static void test_all_actions_have_names(void **state)
{
    (void)state;
    for (int i = 0; i < SDL2I_ACTION_COUNT; i++)
    {
        const char *name = sdl2_input_action_name((sdl2_input_action_t)i);
        assert_non_null(name);
        assert_string_not_equal(name, "unknown");
    }
}

static void test_unknown_action_name(void **state)
{
    (void)state;
    const char *name = sdl2_input_action_name(SDL2I_ACTION_COUNT);
    assert_string_equal(name, "unknown");
}

static void test_all_statuses_have_strings(void **state)
{
    (void)state;
    sdl2_input_status_t codes[] = {
        SDL2I_OK,
        SDL2I_ERR_NULL_ARG,
        SDL2I_ERR_INVALID_ACTION,
        SDL2I_ERR_INVALID_SLOT,
        SDL2I_ERR_INVALID_SCANCODE,
        SDL2I_ERR_ALLOC_FAILED,
    };
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
    {
        const char *s = sdl2_input_status_string(codes[i]);
        assert_non_null(s);
        assert_true(strlen(s) > 0);
    }
}

static void test_unknown_status_string(void **state)
{
    (void)state;
    const char *s = sdl2_input_status_string((sdl2_input_status_t)999);
    assert_string_equal(s, "unknown status");
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest lifecycle_tests[] = {
        cmocka_unit_test_setup_teardown(test_create_destroy, setup_input, teardown_input),
        cmocka_unit_test(test_create_status_ok),
        cmocka_unit_test(test_destroy_null),
        cmocka_unit_test(test_create_null_status_ptr),
    };

    const struct CMUnitTest binding_tests[] = {
        cmocka_unit_test_setup_teardown(test_default_left_binding, setup_input, teardown_input),
        cmocka_unit_test_setup_teardown(test_default_right_binding, setup_input, teardown_input),
        cmocka_unit_test_setup_teardown(test_default_shoot_binding, setup_input, teardown_input),
        cmocka_unit_test_setup_teardown(test_default_pause_binding, setup_input, teardown_input),
        cmocka_unit_test_setup_teardown(test_default_quit_binding, setup_input, teardown_input),
        cmocka_unit_test_setup_teardown(test_default_speed_bindings, setup_input, teardown_input),
    };

    const struct CMUnitTest keypress_tests[] = {
        cmocka_unit_test_setup_teardown(test_key_press_sets_pressed, setup_input, teardown_input),
        cmocka_unit_test_setup_teardown(test_key_release_clears_pressed, setup_input,
                                        teardown_input),
        cmocka_unit_test_setup_teardown(test_just_pressed_edge_trigger, setup_input,
                                        teardown_input),
        cmocka_unit_test_setup_teardown(test_key_repeat_no_edge_trigger, setup_input,
                                        teardown_input),
        cmocka_unit_test_setup_teardown(test_secondary_binding, setup_input, teardown_input),
        cmocka_unit_test_setup_teardown(test_unbound_scancode_ignored, setup_input, teardown_input),
        cmocka_unit_test_setup_teardown(test_dual_binding_release_one, setup_input, teardown_input),
    };

    const struct CMUnitTest mouse_tests[] = {
        cmocka_unit_test_setup_teardown(test_mouse_motion, setup_input, teardown_input),
        cmocka_unit_test_setup_teardown(test_mouse_button_press_release, setup_input,
                                        teardown_input),
        cmocka_unit_test_setup_teardown(test_mouse_multiple_buttons, setup_input, teardown_input),
        cmocka_unit_test(test_mouse_null_ctx),
        cmocka_unit_test_setup_teardown(test_mouse_invalid_button, setup_input, teardown_input),
        cmocka_unit_test_setup_teardown(test_mouse_invalid_button_event, setup_input,
                                        teardown_input),
    };

    const struct CMUnitTest modifier_tests[] = {
        cmocka_unit_test_setup_teardown(test_shift_not_held, setup_input, teardown_input),
        cmocka_unit_test_setup_teardown(test_shift_held, setup_input, teardown_input),
    };

    const struct CMUnitTest rebind_tests[] = {
        cmocka_unit_test_setup_teardown(test_rebind_action, setup_input, teardown_input),
        cmocka_unit_test_setup_teardown(test_rebind_invalid_action, setup_input, teardown_input),
        cmocka_unit_test_setup_teardown(test_rebind_invalid_slot, setup_input, teardown_input),
        cmocka_unit_test(test_rebind_null_ctx),
        cmocka_unit_test_setup_teardown(test_reset_bindings, setup_input, teardown_input),
        cmocka_unit_test_setup_teardown(test_clear_binding, setup_input, teardown_input),
    };

    const struct CMUnitTest error_tests[] = {
        cmocka_unit_test(test_pressed_null_ctx),
        cmocka_unit_test_setup_teardown(test_pressed_invalid_action, setup_input, teardown_input),
        cmocka_unit_test_setup_teardown(test_process_event_null, setup_input, teardown_input),
        cmocka_unit_test(test_begin_frame_null),
        cmocka_unit_test(test_get_binding_null),
    };

    const struct CMUnitTest name_tests[] = {
        cmocka_unit_test(test_all_actions_have_names),
        cmocka_unit_test(test_unknown_action_name),
        cmocka_unit_test(test_all_statuses_have_strings),
        cmocka_unit_test(test_unknown_status_string),
    };

    const struct CMUnitTest mouse_just_pressed_tests[] = {
        cmocka_unit_test_setup_teardown(test_mouse_just_pressed_edge_trigger, setup_input,
                                        teardown_input),
        cmocka_unit_test_setup_teardown(test_mouse_just_pressed_not_repeated, setup_input,
                                        teardown_input),
        cmocka_unit_test_setup_teardown(test_mouse_just_pressed_independent_buttons, setup_input,
                                        teardown_input),
        cmocka_unit_test(test_mouse_just_pressed_null_ctx),
        cmocka_unit_test_setup_teardown(test_mouse_just_pressed_invalid_button, setup_input,
                                        teardown_input),
    };

    int failed = 0;
    failed += cmocka_run_group_tests_name("lifecycle", lifecycle_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("default bindings", binding_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("key press/release", keypress_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("mouse input", mouse_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("modifiers", modifier_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("rebinding", rebind_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("error handling", error_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("names and strings", name_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("mouse just-pressed", mouse_just_pressed_tests, NULL,
                                          NULL);
    return failed;
}
