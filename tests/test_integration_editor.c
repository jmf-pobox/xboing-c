/*
 * test_integration_editor.c — Editor integration test.
 *
 * Enters EDIT mode through the full integration stack and exercises
 * editor operations: draw blocks, save, load, clear, and editor-to-game
 * play-test transitions.
 *
 * Tests verify:
 *   - Entering EDIT mode initializes the editor system
 *   - Mouse drawing places blocks in the block grid
 *   - Editor ticking doesn't crash
 *   - Play-test mode transition (EDIT → GAME → EDIT) works
 *   - Editor key commands dispatch correctly
 *
 * Requires: SDL_VIDEODRIVER=dummy, SDL_AUDIODRIVER=dummy
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <cmocka.h>

#include "block_system.h"
#include "editor_system.h"
#include "game_context.h"
#include "game_init.h"
#include "sdl2_input.h"
#include "sdl2_state.h"

/* =========================================================================
 * Writable argv buffer
 * ========================================================================= */

static char arg_prog[] = "xboing_test";

/* =========================================================================
 * Helper: tick N frames
 * ========================================================================= */

static void tick_frames(game_ctx_t *ctx, int n)
{
    for (int i = 0; i < n; i++)
    {
        sdl2_input_begin_frame(ctx->input);
        sdl2_state_update(ctx->state);
    }
}

/* =========================================================================
 * Fixture — creates game context and enters EDIT mode
 * ========================================================================= */

typedef struct
{
    game_ctx_t *ctx;
} test_fixture_t;

static int setup_edit_mode(void **vstate)
{
    test_fixture_t *f = calloc(1, sizeof(*f));
    assert_non_null(f);

    char *argv[] = {arg_prog, NULL};
    f->ctx = game_create(1, argv);
    assert_non_null(f->ctx);

    /* Enter EDIT mode */
    sdl2_state_transition(f->ctx->state, SDL2ST_EDIT);

    /* Tick a few frames to let editor initialize (LEVEL → NONE) */
    tick_frames(f->ctx, 5);

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
 * Tests
 * ========================================================================= */

static void test_edit_mode_enters(void **vstate)
{
    const test_fixture_t *f = (const test_fixture_t *)*vstate;

    /* Editor should be in NONE state (ready for input) after a few ticks */
    editor_state_t state = editor_system_get_state(f->ctx->editor);
    assert_int_equal(state, EDITOR_STATE_NONE);
}

static void test_editor_ticking_no_crash(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Tick 200 frames in editor mode — no crash */
    tick_frames(f->ctx, 200);

    sdl2_state_mode_t mode = sdl2_state_current(f->ctx->state);
    /* May still be in EDIT, or editor may have auto-transitioned */
    assert_true(mode >= SDL2ST_NONE && mode < SDL2ST_COUNT);
}

static void test_editor_draw_block(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Select first palette entry */
    editor_system_select_palette(f->ctx->editor, 0);

    /* Draw a block at grid position via mouse button.
     * Cell (2, 3) center: col=3 → x≈3*55+27=192, row=2 → y≈2*32+16=80 */
    editor_draw_action_t action =
        editor_system_mouse_button(f->ctx->editor, 192, 80, 1, 1);

    /* Should have drawn */
    assert_int_equal(action, EDITOR_ACTION_DRAW);

    /* Block should be in the block grid (integration wiring) */
    assert_true(block_system_is_occupied(f->ctx->block, 2, 3));
}

static void test_editor_clear_grid(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Draw a block first */
    editor_system_select_palette(f->ctx->editor, 0);
    editor_system_mouse_button(f->ctx->editor, 192, 80, 1, 1);
    assert_true(block_system_is_occupied(f->ctx->block, 2, 3));

    /* Clear the grid */
    editor_system_clear_grid(f->ctx->editor);

    /* Block should be gone */
    assert_false(block_system_is_occupied(f->ctx->block, 2, 3));
}

static void test_editor_playtest_transition(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Draw some blocks so play-test has something */
    editor_system_select_palette(f->ctx->editor, 0);
    editor_system_mouse_button(f->ctx->editor, 192, 80, 1, 1);
    editor_system_mouse_button(f->ctx->editor, 192, 80, 1, 0); /* release */

    /* Enter play-test mode */
    editor_system_key_input(f->ctx->editor, EDITOR_KEY_PLAYTEST);
    assert_int_equal(editor_system_get_state(f->ctx->editor), EDITOR_STATE_TEST);

    /* The game state should transition to GAME for play-testing */
    sdl2_state_mode_t mode = sdl2_state_current(f->ctx->state);
    assert_int_equal(mode, SDL2ST_GAME);

    /* Tick some frames in play-test */
    tick_frames(f->ctx, 50);

    /* Exit play-test */
    editor_system_key_input(f->ctx->editor, EDITOR_KEY_PLAYTEST);

    /* Editor resets to LEVEL state, needs ticks to reach NONE */
    tick_frames(f->ctx, 5);
    assert_int_equal(editor_system_get_state(f->ctx->editor), EDITOR_STATE_NONE);
}

static void test_editor_set_level_name(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Manually set the level name via the editor key */
    /* The dialogue stub returns current level number as string,
     * so we verify the name command doesn't crash */
    editor_system_key_input(f->ctx->editor, EDITOR_KEY_NAME);

    /* Just verify no crash — the dialogue integration returns a
     * default value that may or may not be a valid name length */
    tick_frames(f->ctx, 5);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_edit_mode_enters, setup_edit_mode, teardown),
        cmocka_unit_test_setup_teardown(test_editor_ticking_no_crash, setup_edit_mode, teardown),
        cmocka_unit_test_setup_teardown(test_editor_draw_block, setup_edit_mode, teardown),
        cmocka_unit_test_setup_teardown(test_editor_clear_grid, setup_edit_mode, teardown),
        cmocka_unit_test_setup_teardown(test_editor_playtest_transition, setup_edit_mode, teardown),
        cmocka_unit_test_setup_teardown(test_editor_set_level_name, setup_edit_mode, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
