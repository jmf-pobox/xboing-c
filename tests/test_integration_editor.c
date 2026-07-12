/*
 * test_integration_editor.c — Editor integration test.
 *
 * Enters EDIT mode through the full integration stack and exercises
 * editor operations: draw blocks, clear, and editor-to-game
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
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL2/SDL.h>
#include <cmocka.h>

#include "block_system.h"
#include "editor_system.h"
#include "game_context.h"
#include "game_init.h"
#include "level_system.h"
#include "sdl2_cursor.h"
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

/* Run one frame with a synthetic mouse-button event (down or up) at the
 * given window coordinates, then tick the state machine. */
static void tick_with_mouse_button(game_ctx_t *ctx, Uint8 button, bool pressed, int x, int y)
{
    sdl2_input_begin_frame(ctx->input);
    SDL_Event e = {0};
    e.type = pressed ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    e.button.button = button;
    e.button.x = x;
    e.button.y = y;
    sdl2_input_process_event(ctx->input, &e);
    sdl2_state_update(ctx->state);
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

/* Erasing (middle button held over the board) shows the skull cursor;
 * releasing reverts to the crosshair.  Parity with original editor.c:535/573.
 * The decision is observed via sdl2_cursor_current() (a cached enum, so it
 * runs headless under the dummy driver). */
static void test_editor_erase_shows_skull_cursor(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_NONE);

    /* Board cell (2,3) is play-area (192,80); window coords add
     * PLAY_AREA_X=35 / PLAY_AREA_Y=60 (game_modes.c). */
    const int wx = 192 + 35, wy = 80 + 60;

    tick_with_mouse_button(ctx, SDL_BUTTON_MIDDLE, true, wx, wy);
    assert_int_equal(editor_system_get_draw_action(ctx->editor), EDITOR_ACTION_ERASE);
    assert_int_equal(sdl2_cursor_current(ctx->cursor), SDL2CUR_SKULL);

    tick_with_mouse_button(ctx, SDL_BUTTON_MIDDLE, false, wx, wy);
    assert_int_equal(sdl2_cursor_current(ctx->cursor), SDL2CUR_PLUS);
}

/* Cursor state doesn't leak between modes: the editor sets the plus, and
 * entering the instructions screen resets it to the arrow rather than
 * inheriting the leftover editor cursor. */
static void test_cursor_no_leak_editor_to_instruct(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    assert_int_equal(sdl2_cursor_current(ctx->cursor), SDL2CUR_PLUS);

    sdl2_state_transition(ctx->state, SDL2ST_INSTRUCT);
    assert_int_equal(sdl2_cursor_current(ctx->cursor), SDL2CUR_ARROW);
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
 * Editor save path — real time bonus (bead xboing-di8)
 *
 * Drives the REAL editor_cb_save_level (static in src/game_callbacks.c)
 * through the production callback wiring, not a local copy:
 *
 *   EDITOR_KEY_SAVE -> editor_system.c do_save()
 *     -> ctx->cb.on_input_dialogue (stub, defaults to level 80)
 *     -> ctx->cb.on_save_level == editor_cb_save_level (game_callbacks.c)
 *
 * XBOING_LEVELS_DIR is redirected to a per-test tmp directory before
 * game_create() so both the editor's readable and writable levels dirs
 * point there (paths_levels_dir_readable/writable resolve the same env
 * override — src/paths.c).  do_save() writes to
 * <tmp>/level80.data; reading that file back proves what
 * editor_cb_save_level actually wrote.
 * ========================================================================= */

typedef struct
{
    game_ctx_t *ctx;
    char tmp_levels_dir[300];
    char *prev_levels_dir; /* heap-owned strdup of caller's prior value, or NULL */
} save_fixture_t;

static int setup_edit_save(void **vstate)
{
    save_fixture_t *f = calloc(1, sizeof(*f));
    assert_non_null(f);

    /* Preserve the caller's XBOING_LEVELS_DIR so we don't leak
     * environment changes across tests in this process. */
    const char *existing = getenv("XBOING_LEVELS_DIR");
    f->prev_levels_dir = existing ? strdup(existing) : NULL;

    snprintf(f->tmp_levels_dir, sizeof(f->tmp_levels_dir),
             ".tmp/test_integration_editor_levels_%d", (int)getpid());
    (void)mkdir(".tmp", 0700);
    (void)mkdir(f->tmp_levels_dir, 0700);
    setenv("XBOING_LEVELS_DIR", f->tmp_levels_dir, 1);

    char *argv[] = {arg_prog, NULL};
    f->ctx = game_create(1, argv);
    assert_non_null(f->ctx);

    /* Enter EDIT mode; the LEVEL->NONE auto-load looks for
     * <tmp>/editor.data, which doesn't exist -- on_load_level fails
     * silently (on_error is unset in game_callbacks_editor()) and
     * ctx->level is untouched, ready for this test to seed it below. */
    sdl2_state_transition(f->ctx->state, SDL2ST_EDIT);
    tick_frames(f->ctx, 5);

    *vstate = f;
    return 0;
}

static int teardown_edit_save(void **vstate)
{
    save_fixture_t *f = (save_fixture_t *)*vstate;

    char saved_path[512];
    snprintf(saved_path, sizeof(saved_path), "%s/level80.data", f->tmp_levels_dir);
    (void)unlink(saved_path);

    game_destroy(f->ctx);
    (void)rmdir(f->tmp_levels_dir);

    if (f->prev_levels_dir)
    {
        setenv("XBOING_LEVELS_DIR", f->prev_levels_dir, 1);
        free(f->prev_levels_dir);
    }
    else
    {
        unsetenv("XBOING_LEVELS_DIR");
    }
    free(f);
    return 0;
}

static void test_editor_save_writes_real_time_bonus(void **vstate)
{
    save_fixture_t *f = (save_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* Seed ctx->level with a real level file whose bonus is NOT 120
     * (levels/level02.data == 150).  WORKING_DIRECTORY is
     * CMAKE_SOURCE_DIR for integration tests (tests/CMakeLists.txt),
     * so this relative path resolves under ctest. */
    level_system_status_t status = level_system_load_file(ctx->level, "levels/level02.data");
    if (status == LEVEL_SYS_ERR_FILE_NOT_FOUND)
    {
        skip();
        return;
    }
    assert_int_equal(status, LEVEL_SYS_OK);
    assert_int_equal(level_system_get_time_bonus(ctx->level), 150);

    /* Drive the real save path.  do_save() runs synchronously: reads
     * the dialogue stub (defaults to level 80 since level_number == 0),
     * builds <tmp>/level80.data, and calls editor_cb_save_level(). */
    editor_system_key_input(ctx->editor, EDITOR_KEY_SAVE);

    char saved_path[512];
    snprintf(saved_path, sizeof(saved_path), "%s/level80.data", f->tmp_levels_dir);

    FILE *fp = fopen(saved_path, "r");
    assert_non_null(fp);

    char line1[256];
    char line2[256];
    assert_non_null(fgets(line1, sizeof(line1), fp));
    assert_non_null(fgets(line2, sizeof(line2), fp));
    fclose(fp);

    line2[strcspn(line2, "\n")] = '\0';
    int written_bonus = (int)strtol(line2, NULL, 10);

    assert_int_equal(written_bonus, 150);
    assert_int_not_equal(written_bonus, 120);
}

/* =========================================================================
 * Editor save path — counter_slide + random preserved (bead xboing-di8,
 * design stage 2, docs/specs/2026-07-11-editor-parity.md S2.3/S2.4)
 *
 * Reuses the setup_edit_save/teardown_edit_save fixture from the
 * time-bonus test above.  Seeds two grid cells directly via
 * block_system_add() (the same entry point editor_cb_add_block() uses),
 * confirms the seed took by reading render_info back, then drives the
 * REAL save path (EDITOR_KEY_SAVE -> do_save() -> editor_cb_save_level())
 * and reads the written grid rows back off disk.
 *
 * original/file.c:418-440 — counter digit encoding ('0' no counter,
 * '1'-'5' slide levels).
 * original/file.c:562-609 — '?' written whenever blockP->random is set,
 * regardless of the resolved concrete color.
 * ========================================================================= */

static void test_editor_save_preserves_counter_and_random(void **vstate)
{
    save_fixture_t *f = (save_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    const int counter_row = 5, counter_col = 2, counter_slide = 3;
    const int random_row = 7, random_col = 4;

    /* Seed via the same block_system_add() entry point the editor's
     * on_add_block callback uses -- block_system_add(..., COUNTER_BLK,
     * counter_slide, ...) stores counter_slide unconditionally
     * (src/block_system.c:451); block_system_add(..., RANDOM_BLK, ...)
     * sets random=1 and resolves block_type to RED_BLK internally
     * (src/block_system.c:455-459). */
    assert_int_equal(
        block_system_add(ctx->block, counter_row, counter_col, COUNTER_BLK, counter_slide, 0),
        BLOCK_SYS_OK);
    assert_int_equal(block_system_add(ctx->block, random_row, random_col, RANDOM_BLK, 0, 0),
                     BLOCK_SYS_OK);

    /* Confirm the seed actually set counter_slide/random before saving --
     * otherwise a passing test would prove nothing about the save path. */
    block_system_render_info_t info;
    assert_int_equal(
        block_system_get_render_info(ctx->block, counter_row, counter_col, &info), BLOCK_SYS_OK);
    assert_true(info.occupied);
    assert_int_equal(info.block_type, COUNTER_BLK);
    assert_int_equal(info.counter_slide, counter_slide);

    assert_int_equal(block_system_get_render_info(ctx->block, random_row, random_col, &info),
                     BLOCK_SYS_OK);
    assert_true(info.occupied);
    assert_true(info.random);

    /* Drive the real save path: EDITOR_KEY_SAVE -> do_save() (defaults
     * to level 80 via the dialogue stub) -> editor_cb_save_level(). */
    editor_system_key_input(ctx->editor, EDITOR_KEY_SAVE);

    char saved_path[512];
    snprintf(saved_path, sizeof(saved_path), "%s/level80.data", f->tmp_levels_dir);

    FILE *fp = fopen(saved_path, "r");
    assert_non_null(fp);

    /* Lines 1-2 are title + time bonus; grid rows start at line 3 and
     * map 1:1 to block grid rows 0..14. */
    char line[256];
    assert_non_null(fgets(line, sizeof(line), fp)); /* title */
    assert_non_null(fgets(line, sizeof(line), fp)); /* time bonus */

    char grid_lines[15][256];
    for (int row = 0; row < 15; row++)
    {
        assert_non_null(fgets(grid_lines[row], sizeof(grid_lines[row]), fp));
    }
    fclose(fp);

    assert_int_equal(grid_lines[counter_row][counter_col], '0' + counter_slide);
    assert_int_equal(grid_lines[random_row][random_col], '?');
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_edit_mode_enters, setup_edit_mode, teardown),
        cmocka_unit_test_setup_teardown(test_editor_erase_shows_skull_cursor, setup_edit_mode,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_cursor_no_leak_editor_to_instruct, setup_edit_mode,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_editor_ticking_no_crash, setup_edit_mode, teardown),
        cmocka_unit_test_setup_teardown(test_editor_draw_block, setup_edit_mode, teardown),
        cmocka_unit_test_setup_teardown(test_editor_clear_grid, setup_edit_mode, teardown),
        cmocka_unit_test_setup_teardown(test_editor_playtest_transition, setup_edit_mode, teardown),
        cmocka_unit_test_setup_teardown(test_editor_set_level_name, setup_edit_mode, teardown),
        cmocka_unit_test_setup_teardown(test_editor_save_writes_real_time_bonus, setup_edit_save,
                                        teardown_edit_save),
        cmocka_unit_test_setup_teardown(test_editor_save_preserves_counter_and_random,
                                        setup_edit_save, teardown_edit_save),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
