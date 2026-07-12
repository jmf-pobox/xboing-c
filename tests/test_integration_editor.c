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
#include "dialogue_system.h"
#include "editor_system.h"
#include "game_context.h"
#include "game_init.h"
#include "game_render.h"
#include "level_system.h"
#include "sdl2_cursor.h"
#include "sdl2_input.h"
#include "sdl2_renderer.h"
#include "sdl2_state.h"
#include "special_system.h"

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

/* Set (or clear) the input layer's Shift-held state, then run one frame
 * with a synthetic mouse-button event -- mirrors a real Shift+click, where
 * SDL delivers the modifier keydown before the button event within the
 * same poll loop.  Drives sdl2_input_process_event for both events so
 * ctx->input's modifiers field (read by sdl2_input_shift_held) is set the
 * same way a real LSHIFT keydown/keyup would set it -- no test-local
 * mirror of the modifier tracking. */
static void tick_with_mouse_button_shift(game_ctx_t *ctx, Uint8 button, bool pressed, int x, int y,
                                         bool shift)
{
    sdl2_input_begin_frame(ctx->input);

    SDL_Event key = {0};
    key.type = shift ? SDL_KEYDOWN : SDL_KEYUP;
    key.key.keysym.scancode = SDL_SCANCODE_LSHIFT;
    key.key.keysym.mod = shift ? KMOD_LSHIFT : KMOD_NONE;
    sdl2_input_process_event(ctx->input, &key);

    SDL_Event e = {0};
    e.type = pressed ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    e.button.button = button;
    e.button.x = x;
    e.button.y = y;
    sdl2_input_process_event(ctx->input, &e);

    sdl2_state_update(ctx->state);
}

/* Run one frame with a synthetic mouse-motion event (drag), same button
 * state carried over from the prior tick_with_mouse_button(_shift) call --
 * a real drag is a stream of SDL_MOUSEMOTION events with the button state
 * untouched, not repeated button-down events. */
static void tick_with_mouse_motion(game_ctx_t *ctx, int x, int y)
{
    sdl2_input_begin_frame(ctx->input);
    SDL_Event e = {0};
    e.type = SDL_MOUSEMOTION;
    e.motion.x = x;
    e.motion.y = y;
    sdl2_input_process_event(ctx->input, &e);
    sdl2_state_update(ctx->state);
}

/* Run one frame with a synthetic keydown event for the given scancode,
 * then tick the state machine -- drives mode_edit_update's SDL2I_QUIT/
 * SDL2I_ABORT branch the same way a real keypress would (mode_edit_update
 * uses sdl2_input_just_pressed(), not raw SDL_GetKeyboardState(), for
 * those two). */
static void tick_with_key(game_ctx_t *ctx, SDL_Scancode sc)
{
    sdl2_input_begin_frame(ctx->input);
    SDL_Event e = {0};
    e.type = SDL_KEYDOWN;
    e.key.keysym.scancode = sc;
    e.key.repeat = 0;
    sdl2_input_process_event(ctx->input, &e);
    sdl2_state_update(ctx->state);
}

/* =========================================================================
 * Async dialogue resolution helpers (bead xboing-di8, stage 3)
 *
 * Drive the REAL production pipeline end to end: dialogue_system_update
 * (via sdl2_state_update -> mode_dialogue_update), dialogue_system_key_input
 * (the same function game_main.c's SDL event loop calls), sdl2_state's
 * pop_dialogue, game_modes.c's mode_dialogue_exit, and finally
 * editor_system_dialogue_result.  No test-local mirror of any of these —
 * every step here is the same code path a real player's keystrokes drive.
 * ========================================================================= */

/* Types one character at a time then presses Return (or just Return for
 * an empty/no-op submit), then ticks the dialogue closed. */
static void resolve_dialogue_submit(game_ctx_t *ctx, const char *text)
{
    /* MAP -> TEXT, so dialogue_system_key_input's state guard accepts input. */
    tick_frames(ctx, 1);
    for (const char *p = text; p != NULL && *p != '\0'; p++)
        dialogue_system_key_input(ctx->dialogue, DIALOGUE_KEY_CHAR, *p);
    dialogue_system_key_input(ctx->dialogue, DIALOGUE_KEY_RETURN, '\0');
    /* UNMAP -> FINISHED, then mode_dialogue_update pops it, which fires
     * mode_dialogue_exit -> editor_system_dialogue_result. */
    tick_frames(ctx, 1);
}

/* Presses Escape (cancel) instead of submitting. */
static void resolve_dialogue_cancel(game_ctx_t *ctx)
{
    tick_frames(ctx, 1);
    dialogue_system_key_input(ctx->dialogue, DIALOGUE_KEY_ESCAPE, '\0');
    tick_frames(ctx, 1);
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

/* Creates the game context WITHOUT transitioning to EDIT yet, so a test
 * can seed pre-editor state (e.g. an active special from an abandoned
 * game) before driving the real SDL2ST_EDIT entry. */
static int setup_pre_edit(void **vstate)
{
    test_fixture_t *f = calloc(1, sizeof(*f));
    assert_non_null(f);

    char *argv[] = {arg_prog, NULL};
    f->ctx = game_create(1, argv);
    assert_non_null(f->ctx);

    *vstate = f;
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

/* S3.3 point 3 (docs/specs/2026-07-11-editor-parity.md): entering the
 * editor must clear any specials left active from an abandoned game --
 * E is reachable mid-game (src/game_input.c).  Matches DoLoadLevel's
 * TurnSpecialsOff(display) (original/editor.c:200).  Drives the REAL
 * mode_edit_enter via sdl2_state_transition, not a direct call to
 * special_system_turn_off(). */
static void test_editor_entry_clears_active_specials(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    special_system_set(ctx->special, SPECIAL_KILLER, 1);
    assert_true(special_system_is_active(ctx->special, SPECIAL_KILLER));

    sdl2_state_transition(ctx->state, SDL2ST_EDIT);
    tick_frames(ctx, 5);

    assert_false(special_system_is_active(ctx->special, SPECIAL_KILLER));
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

    /* N opens a real async input dialogue (bead xboing-di8 stage 3) —
     * verify it opens and ticking while it's open doesn't crash. */
    editor_system_key_input(f->ctx->editor, EDITOR_KEY_NAME);
    assert_int_equal(editor_system_get_state(f->ctx->editor), EDITOR_STATE_DIALOGUE);

    tick_frames(f->ctx, 5);
}

/* =========================================================================
 * Editor save path — real time bonus (bead xboing-di8)
 *
 * Drives the REAL editor_cb_save_level (static in src/game_callbacks.c)
 * through the production callback wiring, not a local copy:
 *
 *   EDITOR_KEY_SAVE -> editor_system.c begin_save()
 *     -> ctx->cb.on_request_input_dialogue == editor_cb_request_input
 *          (game_callbacks.c) -> pushes SDL2ST_DIALOGUE
 *     -> resolve_dialogue_submit() types the level number and presses
 *        Return, driving the real dialogue_system + sdl2_state pop +
 *        mode_dialogue_exit -> editor_system_dialogue_result chain
 *     -> finish_save() -> ctx->cb.on_save_level == editor_cb_save_level
 *
 * XBOING_LEVELS_DIR is redirected to a per-test tmp directory before
 * game_create() so both the editor's readable and writable levels dirs
 * point there (paths_levels_dir_readable/writable resolve the same env
 * override — src/paths.c).  The save writes to <tmp>/level80.data;
 * reading that file back proves what editor_cb_save_level actually wrote.
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

    /* Drive the real async save path: SAVE opens a level-number input
     * dialogue (begin_save -> on_request_input_dialogue). */
    editor_system_key_input(ctx->editor, EDITOR_KEY_SAVE);
    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_DIALOGUE);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_DIALOGUE);

    /* Type "80" and submit -- resolves through the real production
     * dialogue_system/sdl2_state/mode_dialogue_exit chain into
     * editor_system_dialogue_result(), which calls finish_save() ->
     * editor_cb_save_level() -> <tmp>/level80.data. */
    resolve_dialogue_submit(ctx, "80");
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_EDIT);

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

    /* Drive the real async save path: EDITOR_KEY_SAVE -> begin_save()
     * opens a level-number input dialogue; typing "80" and submitting
     * resolves through the real dialogue_system/sdl2_state/
     * mode_dialogue_exit chain into editor_system_dialogue_result() ->
     * finish_save() -> editor_cb_save_level(). */
    editor_system_key_input(ctx->editor, EDITOR_KEY_SAVE);
    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_DIALOGUE);
    resolve_dialogue_submit(ctx, "80");

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
 * Async dialogue flows — stage 3 (bead xboing-di8,
 * docs/specs/2026-07-11-editor-parity.md S1.3-1.6, S1.5)
 *
 * Reuses setup_edit_save/teardown_edit_save so Save's on_save_level
 * writes stay confined to the per-test tmp levels dir even though most
 * of these tests don't exercise Save directly.
 * ========================================================================= */

static void test_editor_save_flow_opens_dialogue_and_resumes(void **vstate)
{
    save_fixture_t *f = (save_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    editor_system_key_input(ctx->editor, EDITOR_KEY_SAVE);
    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_DIALOGUE);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_DIALOGUE);

    resolve_dialogue_submit(ctx, "80");

    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_NONE);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_EDIT);

    char saved_path[512];
    snprintf(saved_path, sizeof(saved_path), "%s/level80.data", f->tmp_levels_dir);
    struct stat st;
    assert_int_equal(stat(saved_path, &st), 0);
}

static void test_editor_load_flow_opens_dialogue_and_resumes(void **vstate)
{
    save_fixture_t *f = (save_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    editor_system_key_input(ctx->editor, EDITOR_KEY_LOAD);
    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_DIALOGUE);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_DIALOGUE);

    /* No level80.data seeded in the tmp dir -- on_load_level fails and
     * on_error fires, but the dialogue itself must still resolve. */
    resolve_dialogue_submit(ctx, "80");

    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_NONE);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_EDIT);
}

static void test_editor_time_flow_opens_dialogue_and_resumes(void **vstate)
{
    save_fixture_t *f = (save_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    editor_system_key_input(ctx->editor, EDITOR_KEY_TIME);
    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_DIALOGUE);

    resolve_dialogue_submit(ctx, "180");

    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_NONE);
    assert_int_equal(level_system_get_time_bonus(ctx->level), 180);
}

static void test_editor_name_flow_opens_dialogue_and_resumes(void **vstate)
{
    save_fixture_t *f = (save_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    editor_system_key_input(ctx->editor, EDITOR_KEY_NAME);
    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_DIALOGUE);

    resolve_dialogue_submit(ctx, "My Level");

    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_NONE);
    assert_string_equal(editor_system_get_level_title(ctx->editor), "My Level");
}

static void test_editor_clear_flow_opens_dialogue_and_resumes(void **vstate)
{
    save_fixture_t *f = (save_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    editor_system_select_palette(ctx->editor, 0);
    editor_system_mouse_button(ctx->editor, 192, 80, 1, 1);
    editor_system_mouse_button(ctx->editor, 192, 80, 1, 0);
    assert_true(block_system_is_occupied(ctx->block, 2, 3));

    editor_system_key_input(ctx->editor, EDITOR_KEY_CLEAR);
    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_DIALOGUE);

    resolve_dialogue_submit(ctx, "y");

    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_NONE);
    assert_false(block_system_is_occupied(ctx->block, 2, 3));
}

static void test_editor_quit_flow_opens_dialogue_and_finishes(void **vstate)
{
    save_fixture_t *f = (save_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    editor_system_key_input(ctx->editor, EDITOR_KEY_QUIT);
    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_DIALOGUE);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_DIALOGUE);

    resolve_dialogue_submit(ctx, "y");

    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_FINISH);

    /* One more tick drives editor_system_update's FINISH branch, which
     * fires on_finish -> editor_cb_on_finish -> transition to INTRO. */
    tick_frames(ctx, 1);
    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_INTRO);
}

/* S1.5(b): mode_edit_update's QUIT fallback must not force-transition to
 * SDL2ST_INTRO once a real dialogue is already open -- drives the REAL
 * mode_edit_update key dispatch (raw SDL_SCANCODE_Q keydown), not a
 * direct editor_system_key_input() call, since the fallback lives in
 * mode_edit_update itself. */
static void test_editor_quit_key_does_not_force_intro_while_dialogue_open(void **vstate)
{
    save_fixture_t *f = (save_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    tick_with_key(ctx, SDL_SCANCODE_Q); /* SDL2I_QUIT's bound scancode */

    assert_int_equal(sdl2_state_current(ctx->state), SDL2ST_DIALOGUE);
    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_DIALOGUE);
}

/* S1.5(a): a dialogue round trip must not wipe modified/level_number/
 * level_title/canvas width.  Establishes known state through two REAL
 * dialogue round trips (Save, then Name), then drives a THIRD, cancelled
 * round trip (Set-Time) and confirms nothing from the first two was
 * reset -- mode_edit_enter fires on every dialogue close, and without
 * the guard it would call editor_system_reset() on this third,
 * unrelated resume. */
static void test_editor_dialogue_round_trip_preserves_state(void **vstate)
{
    save_fixture_t *f = (save_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    int logical_w = 0, logical_h = 0;
    sdl2_renderer_get_logical_size(ctx->renderer, &logical_w, &logical_h);
    assert_int_equal(logical_w, SDL2R_LOGICAL_WIDTH + EDITOR_TOOL_WIDTH);

    editor_system_key_input(ctx->editor, EDITOR_KEY_SAVE);
    resolve_dialogue_submit(ctx, "42");
    assert_int_equal(editor_system_get_level_number(ctx->editor), 42);

    /* Draw a block directly (no dialogue) so modified is 1 again --
     * finish_save() clears it back to 0 on success. */
    editor_system_select_palette(ctx->editor, 0);
    editor_system_mouse_button(ctx->editor, 192, 80, 1, 1);
    editor_system_mouse_button(ctx->editor, 192, 80, 1, 0);
    assert_true(editor_system_is_modified(ctx->editor));

    editor_system_key_input(ctx->editor, EDITOR_KEY_NAME);
    resolve_dialogue_submit(ctx, "Persisted Title");
    assert_string_equal(editor_system_get_level_title(ctx->editor), "Persisted Title");

    editor_system_key_input(ctx->editor, EDITOR_KEY_TIME);
    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_DIALOGUE);
    resolve_dialogue_cancel(ctx);

    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_NONE);
    assert_int_equal(editor_system_get_level_number(ctx->editor), 42);
    assert_string_equal(editor_system_get_level_title(ctx->editor), "Persisted Title");
    assert_true(editor_system_is_modified(ctx->editor));

    sdl2_renderer_get_logical_size(ctx->renderer, &logical_w, &logical_h);
    assert_int_equal(logical_w, SDL2R_LOGICAL_WIDTH + EDITOR_TOOL_WIDTH);
}

/* A cancelled (Escape) confirm must not perform the destructive action
 * it was guarding. */
static void test_editor_cancelled_confirm_skips_destructive_action(void **vstate)
{
    save_fixture_t *f = (save_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    editor_system_select_palette(ctx->editor, 0);
    editor_system_mouse_button(ctx->editor, 192, 80, 1, 1);
    editor_system_mouse_button(ctx->editor, 192, 80, 1, 0);
    assert_true(block_system_is_occupied(ctx->block, 2, 3));

    editor_system_key_input(ctx->editor, EDITOR_KEY_CLEAR);
    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_DIALOGUE);

    resolve_dialogue_cancel(ctx);

    assert_int_equal(editor_system_get_state(ctx->editor), EDITOR_STATE_NONE);
    assert_true(block_system_is_occupied(ctx->block, 2, 3)); /* NOT cleared */
}

/* =========================================================================
 * Right-click inspect vs erase (bead xboing-di8, stage 10,
 * docs/specs/2026-07-11-editor-parity.md S4.1, major 4)
 *
 * original/editor.c:547-557 -- Button3 is read-only: DisplayScore(hitPoints)
 * / DisplayScore(0L), drawAction = ED_NOP, never touches the grid.  Drives
 * the REAL mode_edit_update button-3 path via a synthetic SDL_BUTTON_RIGHT
 * event (tick_with_mouse_button, same helper test_editor_erase_shows_
 * skull_cursor uses for SDL_BUTTON_MIDDLE), not editor_system_mouse_button
 * directly -- the inspect logic lives in mode_edit_update, not
 * editor_system.c.
 * ========================================================================= */

static void test_editor_right_click_inspects_without_erasing(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* Cell (2,3): play-area (192,80); window coords add PLAY_AREA_X=35 /
     * PLAY_AREA_Y=60 (game_modes.c), matching
     * test_editor_erase_shows_skull_cursor's convention. RED_BLK's hit
     * points (100) don't depend on row -- score_block_hit_points(). */
    const int row = 2, col = 3;
    const int wx = 192 + 35, wy = 80 + 60;

    assert_int_equal(block_system_add(ctx->block, row, col, RED_BLK, 0, 0), BLOCK_SYS_OK);
    int expected_hp = block_system_get_hit_points(ctx->block, row, col);
    assert_int_equal(expected_hp, 100);

    assert_int_equal(ctx->editor_inspect_active, 0);

    tick_with_mouse_button(ctx, SDL_BUTTON_RIGHT, true, wx, wy);

    assert_int_equal(ctx->editor_inspect_active, 1);
    assert_int_equal((int)ctx->editor_inspect_value, expected_hp);

    /* Read-only: the block must still be on the grid. */
    assert_true(block_system_is_occupied(ctx->block, row, col));
    assert_int_equal(block_system_get_type(ctx->block, row, col), RED_BLK);

    tick_with_mouse_button(ctx, SDL_BUTTON_RIGHT, false, wx, wy);
    assert_true(block_system_is_occupied(ctx->block, row, col));
}

static void test_editor_right_click_empty_cell_inspects_zero(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* Cell (4,1): a different, unoccupied cell. col=1 -> x≈1*55+27=82,
     * row=4 -> y≈4*32+16=144 (same COL_WIDTH/ROW_HEIGHT arithmetic as
     * test_editor_draw_block's (2,3) comment). */
    const int row = 4, col = 1;
    const int wx = 82 + 35, wy = 144 + 60;

    assert_false(block_system_is_occupied(ctx->block, row, col));

    tick_with_mouse_button(ctx, SDL_BUTTON_RIGHT, true, wx, wy);

    assert_int_equal(ctx->editor_inspect_active, 1);
    assert_int_equal((int)ctx->editor_inspect_value, 0);
    assert_false(block_system_is_occupied(ctx->block, row, col));
}

/* =========================================================================
 * Render/click agreement (bead xboing-di8) -- the palette rendered
 * correctly but clicking selected the WRONG entry because the click
 * handler (game_modes.c) had its own stale single-column math instead of
 * sharing the render loop's geometry (game_render.c's PALETTE_* macros).
 * This test pins render<->click agreement using the REAL production
 * functions on both sides -- game_render_editor_palette_entry_center()
 * computes the center from the exact constants the render loop draws
 * with, and game_render_editor_palette_index_at() is the exact function
 * game_modes.c's mouse-click handler calls.  No test-local mirror of
 * either geometry -- this bug class (render and input never verified to
 * agree) can't recur silently once this test exists.
 * ========================================================================= */

static void test_editor_palette_click_matches_render(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    int count = editor_system_get_palette_count(ctx->editor);
    assert_true(count > 0);

    /* For every palette entry, the pixel at its rendered swatch center
     * must hit-test back to that same index. */
    for (int i = 0; i < count; i++)
    {
        int cx = -1, cy = -1;
        game_render_editor_palette_entry_center(ctx, i, &cx, &cy);

        int hit = game_render_editor_palette_index_at(ctx, cx, cy);
        assert_int_equal(hit, i);
    }
}

static void test_editor_palette_click_column_and_bounds(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* Column 1 holds indices 0..min(MAX_ROW, MAX_STATIC_BLOCKS)-1 (same
     * split editor_system_init_palette fills and game_render_editor_
     * palette's render loop lays out -- game_render.c PALETTE_COL1_COUNT). */
    int col1_count = (MAX_ROW < MAX_STATIC_BLOCKS) ? MAX_ROW : MAX_STATIC_BLOCKS;
    int count = editor_system_get_palette_count(ctx->editor);
    assert_true(count > col1_count);

    /* Column 1, row 0 -> index 0. */
    int cx0 = -1, cy0 = -1;
    game_render_editor_palette_entry_center(ctx, 0, &cx0, &cy0);
    assert_int_equal(game_render_editor_palette_index_at(ctx, cx0, cy0), 0);

    /* Column 2, row 0 -> index col1_count (first entry past column 1). */
    int cx1 = -1, cy1 = -1;
    game_render_editor_palette_entry_center(ctx, col1_count, &cx1, &cy1);
    assert_int_equal(game_render_editor_palette_index_at(ctx, cx1, cy1), col1_count);
    assert_true(cx1 > cx0); /* Sanity: column 2 really is to the right */

    /* Outside the palette panel entirely (inside the play area): -1. */
    assert_int_equal(game_render_editor_palette_index_at(ctx, 0, 0), -1);
}

/* =========================================================================
 * Shift+left-click erase (mission m-2026-07-12-013, bead xboing-di8) --
 * ADR-058 (docs/DESIGN.md).  Middle-click erase (original/editor.c:534-545)
 * assumed a physical 3-button mouse; modern trackpads and 2-button mice
 * can't reach it.  Shift+left-click is the portable second binding to
 * erase, added alongside -- not instead of -- middle-click.  Drives the
 * REAL mode_edit_update path via tick_with_mouse_button(_shift), not
 * editor_system_mouse_button directly.
 * ========================================================================= */

static void test_editor_shift_left_click_erases(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* Cell (2,3): play-area (192,80); window coords add PLAY_AREA_X=35 /
     * PLAY_AREA_Y=60 (game_modes.c), matching
     * test_editor_erase_shows_skull_cursor's convention. */
    const int row = 2, col = 3;
    const int wx = 192 + 35, wy = 80 + 60;

    editor_system_select_palette(ctx->editor, 0);

    /* Plain left-click (no Shift) on an empty cell still draws. */
    assert_false(block_system_is_occupied(ctx->block, row, col));
    tick_with_mouse_button(ctx, SDL_BUTTON_LEFT, true, wx, wy);
    tick_with_mouse_button(ctx, SDL_BUTTON_LEFT, false, wx, wy);
    assert_true(block_system_is_occupied(ctx->block, row, col));

    /* Shift+left-click on the same (now occupied) cell erases it. */
    tick_with_mouse_button_shift(ctx, SDL_BUTTON_LEFT, true, wx, wy, true);
    assert_false(block_system_is_occupied(ctx->block, row, col));

    tick_with_mouse_button_shift(ctx, SDL_BUTTON_LEFT, false, wx, wy, false);
}

/* Shift+left-click-drag continues erasing under the cursor as it moves,
 * matching a middle-button drag (test_editor_erase_shows_skull_cursor's
 * Button2 case) -- editor_system_mouse_motion is driven by draw_action,
 * not the raw button/modifier, so once the press sets
 * EDITOR_ACTION_ERASE the drag needs no separate Shift-aware path. */
static void test_editor_shift_left_drag_erases_continuously(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    game_ctx_t *ctx = f->ctx;

    /* col=3 -> x≈3*55+27=192, col=4 -> x≈4*55+27=247 (same row, same
     * COL_WIDTH arithmetic as test_editor_draw_block's comment). */
    const int row_a = 2, col_a = 3, wx_a = 192 + 35, wy = 80 + 60;
    const int row_b = 2, col_b = 4, wx_b = 247 + 35;

    editor_system_select_palette(ctx->editor, 0);
    tick_with_mouse_button(ctx, SDL_BUTTON_LEFT, true, wx_a, wy);
    tick_with_mouse_motion(ctx, wx_b, wy);
    tick_with_mouse_button(ctx, SDL_BUTTON_LEFT, false, wx_b, wy);
    assert_true(block_system_is_occupied(ctx->block, row_a, col_a));
    assert_true(block_system_is_occupied(ctx->block, row_b, col_b));

    /* Shift+left press at cell A, drag to cell B: both get erased. */
    tick_with_mouse_button_shift(ctx, SDL_BUTTON_LEFT, true, wx_a, wy, true);
    assert_false(block_system_is_occupied(ctx->block, row_a, col_a));

    tick_with_mouse_motion(ctx, wx_b, wy);
    assert_false(block_system_is_occupied(ctx->block, row_b, col_b));

    tick_with_mouse_button_shift(ctx, SDL_BUTTON_LEFT, false, wx_b, wy, false);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_edit_mode_enters, setup_edit_mode, teardown),
        cmocka_unit_test_setup_teardown(test_editor_entry_clears_active_specials, setup_pre_edit,
                                        teardown),
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

        /* Async dialogue flows (stage 3, bead xboing-di8) */
        cmocka_unit_test_setup_teardown(test_editor_save_flow_opens_dialogue_and_resumes,
                                        setup_edit_save, teardown_edit_save),
        cmocka_unit_test_setup_teardown(test_editor_load_flow_opens_dialogue_and_resumes,
                                        setup_edit_save, teardown_edit_save),
        cmocka_unit_test_setup_teardown(test_editor_time_flow_opens_dialogue_and_resumes,
                                        setup_edit_save, teardown_edit_save),
        cmocka_unit_test_setup_teardown(test_editor_name_flow_opens_dialogue_and_resumes,
                                        setup_edit_save, teardown_edit_save),
        cmocka_unit_test_setup_teardown(test_editor_clear_flow_opens_dialogue_and_resumes,
                                        setup_edit_save, teardown_edit_save),
        cmocka_unit_test_setup_teardown(test_editor_quit_flow_opens_dialogue_and_finishes,
                                        setup_edit_save, teardown_edit_save),
        cmocka_unit_test_setup_teardown(
            test_editor_quit_key_does_not_force_intro_while_dialogue_open, setup_edit_save,
            teardown_edit_save),
        cmocka_unit_test_setup_teardown(test_editor_dialogue_round_trip_preserves_state,
                                        setup_edit_save, teardown_edit_save),
        cmocka_unit_test_setup_teardown(test_editor_cancelled_confirm_skips_destructive_action,
                                        setup_edit_save, teardown_edit_save),

        /* Right-click inspect vs erase (stage 10, bead xboing-di8) */
        cmocka_unit_test_setup_teardown(test_editor_right_click_inspects_without_erasing,
                                        setup_edit_mode, teardown),
        cmocka_unit_test_setup_teardown(test_editor_right_click_empty_cell_inspects_zero,
                                        setup_edit_mode, teardown),

        /* Palette render/click agreement (bead xboing-di8) */
        cmocka_unit_test_setup_teardown(test_editor_palette_click_matches_render, setup_edit_mode,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_editor_palette_click_column_and_bounds,
                                        setup_edit_mode, teardown),

        /* Shift+left-click erase (mission m-2026-07-12-013, bead xboing-di8) */
        cmocka_unit_test_setup_teardown(test_editor_shift_left_click_erases, setup_edit_mode,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_editor_shift_left_drag_erases_continuously,
                                        setup_edit_mode, teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
