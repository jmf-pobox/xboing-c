/*
 * test_editor_system.c — CMocka tests for editor_system.c
 *
 * Tests cover: lifecycle, state machine, palette management, grid editing
 * (draw/erase via mouse button and motion), board transforms (flip H/V,
 * scroll H/V), keyboard command dispatch, and query functions.
 *
 * The editor system uses callbacks for all side effects, so tests inject
 * stub callbacks that record calls into a shared log structure.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "editor_system.h"

/* =========================================================================
 * Test grid — in-memory block storage for callback stubs
 * ========================================================================= */

typedef struct
{
    int occupied;
    int block_type;
    int counter_slide;
} test_cell_t;

typedef struct
{
    test_cell_t grid[EDITOR_MAX_ROW_EDIT][EDITOR_MAX_COL_EDIT];

    /* Callback counters */
    int add_count;
    int erase_count;
    int clear_count;
    int sound_count;
    int message_count;
    int load_count;
    int save_count;
    int error_count;
    int playtest_start_count;
    int playtest_end_count;
    int finish_count;

    /* Last message */
    char last_message[256];
    int last_message_sticky;

    /* Last sound */
    char last_sound[64];

    /* Dialogue stubs */
    const char *dialogue_result;
    int yes_no_result;

    /* Load/save results */
    int load_result;
    int save_result;

    /* Time callback */
    int last_time_seconds;
} test_state_t;

/* =========================================================================
 * Callback stubs
 * ========================================================================= */

static void stub_add_block(int row, int col, int block_type, int counter_slide, int visible,
                           void *ud)
{
    test_state_t *s = (test_state_t *)ud;
    (void)visible;
    if (row >= 0 && row < EDITOR_MAX_ROW_EDIT && col >= 0 && col < EDITOR_MAX_COL_EDIT)
    {
        s->grid[row][col].occupied = 1;
        s->grid[row][col].block_type = block_type;
        s->grid[row][col].counter_slide = counter_slide;
    }
    s->add_count++;
}

static void stub_erase_block(int row, int col, void *ud)
{
    test_state_t *s = (test_state_t *)ud;
    if (row >= 0 && row < EDITOR_MAX_ROW_EDIT && col >= 0 && col < EDITOR_MAX_COL_EDIT)
    {
        s->grid[row][col].occupied = 0;
        s->grid[row][col].block_type = NONE_BLK;
        s->grid[row][col].counter_slide = 0;
    }
    s->erase_count++;
}

static void stub_clear_grid(void *ud)
{
    test_state_t *s = (test_state_t *)ud;
    memset(s->grid, 0, sizeof(s->grid));
    for (int r = 0; r < EDITOR_MAX_ROW_EDIT; r++)
        for (int c = 0; c < EDITOR_MAX_COL_EDIT; c++)
            s->grid[r][c].block_type = NONE_BLK;
    s->clear_count++;
}

static int stub_query_cell(int row, int col, editor_cell_t *cell, void *ud)
{
    const test_state_t *s = (const test_state_t *)ud;
    if (row < 0 || row >= EDITOR_MAX_ROW_EDIT || col < 0 || col >= EDITOR_MAX_COL_EDIT)
        return 0;
    cell->occupied = s->grid[row][col].occupied;
    cell->block_type = s->grid[row][col].block_type;
    cell->counter_slide = s->grid[row][col].counter_slide;
    return cell->occupied;
}

static void stub_sound(const char *name, int volume, void *ud)
{
    test_state_t *s = (test_state_t *)ud;
    (void)volume;
    snprintf(s->last_sound, sizeof(s->last_sound), "%s", name);
    s->sound_count++;
}

static void stub_message(const char *message, int sticky, void *ud)
{
    test_state_t *s = (test_state_t *)ud;
    snprintf(s->last_message, sizeof(s->last_message), "%s", message);
    s->last_message_sticky = sticky;
    s->message_count++;
}

static int stub_load_level(const char *path, void *ud)
{
    test_state_t *s = (test_state_t *)ud;
    (void)path;
    s->load_count++;
    return s->load_result;
}

static int stub_save_level(const char *path, void *ud)
{
    test_state_t *s = (test_state_t *)ud;
    (void)path;
    s->save_count++;
    return s->save_result;
}

static void stub_error(const char *message, void *ud)
{
    test_state_t *s = (test_state_t *)ud;
    (void)message;
    s->error_count++;
}

static const char *stub_input_dialogue(const char *message, int numeric_only, void *ud)
{
    const test_state_t *s = (const test_state_t *)ud;
    (void)message;
    (void)numeric_only;
    return s->dialogue_result;
}

static int stub_yes_no_dialogue(const char *message, void *ud)
{
    const test_state_t *s = (const test_state_t *)ud;
    (void)message;
    return s->yes_no_result;
}

static void stub_set_time(int seconds, void *ud)
{
    test_state_t *s = (test_state_t *)ud;
    s->last_time_seconds = seconds;
}

static void stub_playtest_start(void *ud)
{
    test_state_t *s = (test_state_t *)ud;
    s->playtest_start_count++;
}

static void stub_playtest_end(void *ud)
{
    test_state_t *s = (test_state_t *)ud;
    s->playtest_end_count++;
}

static void stub_finish(void *ud)
{
    test_state_t *s = (test_state_t *)ud;
    s->finish_count++;
}

/* =========================================================================
 * Test fixtures
 * ========================================================================= */

static editor_system_callbacks_t make_callbacks(void)
{
    editor_system_callbacks_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.on_add_block = stub_add_block;
    cb.on_erase_block = stub_erase_block;
    cb.on_clear_grid = stub_clear_grid;
    cb.query_cell = stub_query_cell;
    cb.on_sound = stub_sound;
    cb.on_message = stub_message;
    cb.on_load_level = stub_load_level;
    cb.on_save_level = stub_save_level;
    cb.on_error = stub_error;
    cb.on_input_dialogue = stub_input_dialogue;
    cb.on_yes_no_dialogue = stub_yes_no_dialogue;
    cb.on_set_time = stub_set_time;
    cb.on_playtest_start = stub_playtest_start;
    cb.on_playtest_end = stub_playtest_end;
    cb.on_finish = stub_finish;
    return cb;
}

typedef struct
{
    editor_system_t *editor;
    test_state_t state;
} test_fixture_t;

static int setup(void **vstate)
{
    test_fixture_t *f = calloc(1, sizeof(*f));
    assert_non_null(f);

    /* Default: load succeeds, save succeeds */
    f->state.load_result = 1;
    f->state.save_result = 1;
    f->state.yes_no_result = 1;
    f->state.dialogue_result = "";

    /* Initialize grid to NONE_BLK */
    for (int r = 0; r < EDITOR_MAX_ROW_EDIT; r++)
        for (int c = 0; c < EDITOR_MAX_COL_EDIT; c++)
            f->state.grid[r][c].block_type = NONE_BLK;

    editor_system_callbacks_t cb = make_callbacks();
    f->editor = editor_system_create(&cb, &f->state, "levels", 0);
    assert_non_null(f->editor);

    /* Initialize palette */
    editor_system_init_palette(f->editor, MAX_STATIC_BLOCKS);

    /* Run initial update to transition from LEVEL -> NONE */
    editor_system_update(f->editor, 0);

    *vstate = f;
    return 0;
}

static int teardown(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    editor_system_destroy(f->editor);
    free(f);
    return 0;
}

/* =========================================================================
 * Section 1: Lifecycle
 * ========================================================================= */

static void test_create_destroy(void **vstate)
{
    (void)vstate;
    editor_system_callbacks_t cb = make_callbacks();
    test_state_t s;
    memset(&s, 0, sizeof(s));
    s.load_result = 1;

    editor_system_t *ctx = editor_system_create(&cb, &s, "levels", 0);
    assert_non_null(ctx);
    assert_int_equal(editor_system_get_state(ctx), EDITOR_STATE_LEVEL);
    editor_system_destroy(ctx);
}

static void test_create_null_callbacks(void **vstate)
{
    (void)vstate;
    editor_system_t *ctx = editor_system_create(NULL, NULL, "levels", 0);
    assert_non_null(ctx);
    assert_int_equal(editor_system_get_state(ctx), EDITOR_STATE_LEVEL);
    /* Update with no callbacks should not crash */
    editor_system_update(ctx, 0);
    editor_system_destroy(ctx);
}

static void test_destroy_null(void **vstate)
{
    (void)vstate;
    editor_system_destroy(NULL); /* Should not crash */
}

static void test_initial_state_is_level(void **vstate)
{
    (void)vstate;
    editor_system_callbacks_t cb = make_callbacks();
    test_state_t s;
    memset(&s, 0, sizeof(s));
    s.load_result = 1;

    editor_system_t *ctx = editor_system_create(&cb, &s, "levels", 0);
    assert_int_equal(editor_system_get_state(ctx), EDITOR_STATE_LEVEL);
    editor_system_destroy(ctx);
}

/* =========================================================================
 * Section 2: State machine transitions
 * ========================================================================= */

static void test_update_level_transitions_to_none(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    /* After setup, state should be NONE (setup runs the initial update) */
    assert_int_equal(editor_system_get_state(f->editor), EDITOR_STATE_NONE);
}

static void test_update_level_calls_load(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    /* Setup runs the LEVEL state, which calls on_load_level */
    assert_int_equal(f->state.load_count, 1);
}

static void test_reset_returns_to_level(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    editor_system_reset(f->editor);
    assert_int_equal(editor_system_get_state(f->editor), EDITOR_STATE_LEVEL);
}

static void test_wait_state(void **vstate)
{
    (void)vstate;
    editor_system_callbacks_t cb = make_callbacks();
    test_state_t s;
    memset(&s, 0, sizeof(s));
    s.load_result = 1;

    editor_system_t *ctx = editor_system_create(&cb, &s, "levels", 0);

    /* Transition through LEVEL -> NONE */
    editor_system_update(ctx, 0);
    assert_int_equal(editor_system_get_state(ctx), EDITOR_STATE_NONE);

    editor_system_destroy(ctx);
}

/* =========================================================================
 * Section 3: Palette management
 * ========================================================================= */

static void test_palette_init_count(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    /* MAX_STATIC_BLOCKS (25) + 5 counter variants = 30 */
    assert_int_equal(editor_system_get_palette_count(f->editor), MAX_STATIC_BLOCKS + 5);
}

static void test_palette_first_entry(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    const editor_palette_entry_t *e = editor_system_get_palette_entry(f->editor, 0);
    assert_non_null(e);
    assert_int_equal(e->block_type, 0);
    assert_int_equal(e->counter_slide, 0);
}

static void test_palette_counter_entries(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    /* Counter entries start at index MAX_STATIC_BLOCKS */
    for (int i = 0; i < 5; i++)
    {
        const editor_palette_entry_t *e =
            editor_system_get_palette_entry(f->editor, MAX_STATIC_BLOCKS + i);
        assert_non_null(e);
        assert_int_equal(e->block_type, COUNTER_BLK);
        assert_int_equal(e->counter_slide, i + 1);
    }
}

static void test_palette_select(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    assert_int_equal(editor_system_select_palette(f->editor, 5), 0);
    assert_int_equal(editor_system_get_selected_palette(f->editor), 5);
}

static void test_palette_select_invalid(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    assert_int_equal(editor_system_select_palette(f->editor, -1), -1);
    assert_int_equal(editor_system_select_palette(f->editor, 999), -1);
}

static void test_palette_entry_out_of_bounds(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    assert_null(editor_system_get_palette_entry(f->editor, -1));
    assert_null(editor_system_get_palette_entry(f->editor, EDITOR_MAX_PALETTE + 1));
}

/* =========================================================================
 * Section 4: Grid editing — mouse button
 * ========================================================================= */

/* Pixel coords for center of cell (0,0) */
#define CELL_CENTER_X(col) ((col) * (EDITOR_PLAY_WIDTH / EDITOR_MAX_COL_EDIT) + 10)
#define CELL_CENTER_Y(row) ((row) * (EDITOR_PLAY_HEIGHT / MAX_ROW) + 10)

static void test_mouse_left_click_draws(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Select palette entry 0 (block_type=0, counter_slide=0) */
    editor_system_select_palette(f->editor, 0);

    int x = CELL_CENTER_X(3);
    int y = CELL_CENTER_Y(2);
    editor_draw_action_t action = editor_system_mouse_button(f->editor, x, y, 1, 1);

    assert_int_equal(action, EDITOR_ACTION_DRAW);
    assert_true(f->state.grid[2][3].occupied);
    assert_int_equal(f->state.grid[2][3].block_type, 0);
    assert_true(editor_system_is_modified(f->editor));
}

static void test_mouse_middle_click_erases(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Place a block first */
    f->state.grid[2][3].occupied = 1;
    f->state.grid[2][3].block_type = 1;

    int x = CELL_CENTER_X(3);
    int y = CELL_CENTER_Y(2);
    editor_draw_action_t action = editor_system_mouse_button(f->editor, x, y, 2, 1);

    assert_int_equal(action, EDITOR_ACTION_ERASE);
    assert_false(f->state.grid[2][3].occupied);
}

static void test_mouse_right_click_is_nop(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    int x = CELL_CENTER_X(3);
    int y = CELL_CENTER_Y(2);
    editor_draw_action_t action = editor_system_mouse_button(f->editor, x, y, 3, 1);

    assert_int_equal(action, EDITOR_ACTION_NOP);
}

static void test_mouse_release_resets_action(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    int x = CELL_CENTER_X(3);
    int y = CELL_CENTER_Y(2);
    editor_system_mouse_button(f->editor, x, y, 1, 1); /* Press */
    editor_draw_action_t action = editor_system_mouse_button(f->editor, x, y, 1, 0); /* Release */

    assert_int_equal(action, EDITOR_ACTION_NOP);
}

static void test_mouse_out_of_bounds_ignored(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    int before = f->state.add_count;
    editor_system_mouse_button(f->editor, -1, -1, 1, 1);
    assert_int_equal(f->state.add_count, before);

    editor_system_mouse_button(f->editor, EDITOR_PLAY_WIDTH + 1, 0, 1, 1);
    assert_int_equal(f->state.add_count, before);
}

static void test_mouse_below_editable_row_ignored(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Row 15+ is below the editable area */
    int x = CELL_CENTER_X(0);
    int y = CELL_CENTER_Y(EDITOR_MAX_ROW_EDIT); /* Row 15 */
    int before = f->state.add_count;
    editor_system_mouse_button(f->editor, x, y, 1, 1);
    assert_int_equal(f->state.add_count, before);
}

/* =========================================================================
 * Section 5: Grid editing — mouse motion
 * ========================================================================= */

static void test_mouse_motion_draws_new_cell(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Start drawing at (0,0) */
    editor_system_mouse_button(f->editor, CELL_CENTER_X(0), CELL_CENTER_Y(0), 1, 1);
    assert_true(f->state.grid[0][0].occupied);

    /* Drag to (0,1) */
    editor_system_mouse_motion(f->editor, CELL_CENTER_X(1), CELL_CENTER_Y(0));
    assert_true(f->state.grid[0][1].occupied);
}

static void test_mouse_motion_same_cell_no_repeat(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    editor_system_mouse_button(f->editor, CELL_CENTER_X(0), CELL_CENTER_Y(0), 1, 1);
    int count = f->state.add_count;

    /* Motion within same cell should not trigger another add */
    editor_system_mouse_motion(f->editor, CELL_CENTER_X(0), CELL_CENTER_Y(0));
    assert_int_equal(f->state.add_count, count);
}

static void test_mouse_motion_erase(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Place blocks in row 0 */
    for (int c = 0; c < 3; c++)
    {
        f->state.grid[0][c].occupied = 1;
        f->state.grid[0][c].block_type = 1;
    }

    /* Start erasing at (0,0) */
    editor_system_mouse_button(f->editor, CELL_CENTER_X(0), CELL_CENTER_Y(0), 2, 1);
    assert_false(f->state.grid[0][0].occupied);

    /* Drag to erase (0,1) */
    editor_system_mouse_motion(f->editor, CELL_CENTER_X(1), CELL_CENTER_Y(0));
    assert_false(f->state.grid[0][1].occupied);
}

/* =========================================================================
 * Section 6: Board transforms — flip horizontal
 * ========================================================================= */

static void test_flip_horizontal_swaps_columns(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Place block at (0,0) */
    f->state.grid[0][0].occupied = 1;
    f->state.grid[0][0].block_type = RED_BLK;

    editor_system_flip_horizontal(f->editor);

    /* Should now be at (0, MAX_COL-1) */
    assert_true(f->state.grid[0][EDITOR_MAX_COL_EDIT - 1].occupied);
    assert_int_equal(f->state.grid[0][EDITOR_MAX_COL_EDIT - 1].block_type, RED_BLK);
    /* Original position should be empty */
    assert_false(f->state.grid[0][0].occupied);
}

static void test_flip_horizontal_preserves_center(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* With 9 columns (odd), col 4 is the center and should stay */
    f->state.grid[0][4].occupied = 1;
    f->state.grid[0][4].block_type = GREEN_BLK;

    editor_system_flip_horizontal(f->editor);

    /* Center column (4) is not swapped when MAX_COL is odd */
    assert_true(f->state.grid[0][4].occupied);
    assert_int_equal(f->state.grid[0][4].block_type, GREEN_BLK);
}

static void test_flip_horizontal_sets_modified(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    editor_system_flip_horizontal(f->editor);
    assert_true(editor_system_is_modified(f->editor));
}

/* =========================================================================
 * Section 7: Board transforms — flip vertical
 * ========================================================================= */

static void test_flip_vertical_swaps_rows(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Place block at (0,0) */
    f->state.grid[0][0].occupied = 1;
    f->state.grid[0][0].block_type = BLUE_BLK;

    editor_system_flip_vertical(f->editor);

    /* Should now be at (MAX_ROW_EDIT-1, 0) */
    assert_true(f->state.grid[EDITOR_MAX_ROW_EDIT - 1][0].occupied);
    assert_int_equal(f->state.grid[EDITOR_MAX_ROW_EDIT - 1][0].block_type, BLUE_BLK);
    assert_false(f->state.grid[0][0].occupied);
}

/* =========================================================================
 * Section 8: Board transforms — scroll horizontal
 * ========================================================================= */

static void test_scroll_horizontal_shifts_right(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Place block at (0,0) */
    f->state.grid[0][0].occupied = 1;
    f->state.grid[0][0].block_type = TAN_BLK;

    editor_system_scroll_horizontal(f->editor);

    /* Should now be at (0,1) */
    assert_true(f->state.grid[0][1].occupied);
    assert_int_equal(f->state.grid[0][1].block_type, TAN_BLK);
}

static void test_scroll_horizontal_wraps(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Place block at (0, MAX_COL-1) */
    f->state.grid[0][EDITOR_MAX_COL_EDIT - 1].occupied = 1;
    f->state.grid[0][EDITOR_MAX_COL_EDIT - 1].block_type = YELLOW_BLK;

    editor_system_scroll_horizontal(f->editor);

    /* Should wrap to (0,0) */
    assert_true(f->state.grid[0][0].occupied);
    assert_int_equal(f->state.grid[0][0].block_type, YELLOW_BLK);
}

/* =========================================================================
 * Section 9: Board transforms — scroll vertical
 * ========================================================================= */

static void test_scroll_vertical_shifts_down(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Place block at (0,0) */
    f->state.grid[0][0].occupied = 1;
    f->state.grid[0][0].block_type = PURPLE_BLK;

    editor_system_scroll_vertical(f->editor);

    /* Should now be at (1,0) */
    assert_true(f->state.grid[1][0].occupied);
    assert_int_equal(f->state.grid[1][0].block_type, PURPLE_BLK);
}

static void test_scroll_vertical_wraps(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    /* Place block at (MAX_ROW_EDIT-1, 0) */
    f->state.grid[EDITOR_MAX_ROW_EDIT - 1][0].occupied = 1;
    f->state.grid[EDITOR_MAX_ROW_EDIT - 1][0].block_type = RED_BLK;

    editor_system_scroll_vertical(f->editor);

    /* Should wrap to (0,0) */
    assert_true(f->state.grid[0][0].occupied);
    assert_int_equal(f->state.grid[0][0].block_type, RED_BLK);
}

/* =========================================================================
 * Section 10: Clear grid
 * ========================================================================= */

static void test_clear_grid(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    f->state.grid[0][0].occupied = 1;
    f->state.grid[5][4].occupied = 1;

    editor_system_clear_grid(f->editor);

    assert_int_equal(f->state.clear_count, 1);
    assert_true(editor_system_is_modified(f->editor));
}

/* =========================================================================
 * Section 11: Keyboard commands
 * ========================================================================= */

static void test_key_quit_when_unmodified(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    f->state.yes_no_result = 1; /* User says yes */

    editor_system_key_input(f->editor, EDITOR_KEY_QUIT);
    assert_int_equal(editor_system_get_state(f->editor), EDITOR_STATE_FINISH);
}

static void test_key_quit_denied(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    f->state.yes_no_result = 0; /* User says no */

    editor_system_key_input(f->editor, EDITOR_KEY_QUIT);
    assert_int_equal(editor_system_get_state(f->editor), EDITOR_STATE_NONE);
}

static void test_key_playtest_enters_test_mode(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    editor_system_key_input(f->editor, EDITOR_KEY_PLAYTEST);
    assert_int_equal(editor_system_get_state(f->editor), EDITOR_STATE_TEST);
    assert_int_equal(f->state.playtest_start_count, 1);
}

static void test_key_playtest_exits_test_mode(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    editor_system_key_input(f->editor, EDITOR_KEY_PLAYTEST);
    assert_int_equal(editor_system_get_state(f->editor), EDITOR_STATE_TEST);

    /* Press P again to exit */
    editor_system_key_input(f->editor, EDITOR_KEY_PLAYTEST);
    assert_int_equal(editor_system_get_state(f->editor), EDITOR_STATE_NONE);
    assert_int_equal(f->state.playtest_end_count, 1);
}

static void test_key_save(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    f->state.dialogue_result = "5";

    editor_system_key_input(f->editor, EDITOR_KEY_SAVE);
    assert_int_equal(f->state.save_count, 1);
}

static void test_key_load(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    f->state.dialogue_result = "10";

    editor_system_key_input(f->editor, EDITOR_KEY_LOAD);
    /* load_count starts at 1 from setup (initial level load) */
    assert_int_equal(f->state.load_count, 2);
    assert_int_equal(editor_system_get_level_number(f->editor), 10);
}

static void test_key_load_invalid_range(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    f->state.dialogue_result = "999";

    int before = f->state.load_count;
    editor_system_key_input(f->editor, EDITOR_KEY_LOAD);
    assert_int_equal(f->state.load_count, before); /* No load call */
    assert_true(f->state.last_message_sticky);      /* Error message shown */
}

static void test_key_time(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    f->state.dialogue_result = "120";

    editor_system_key_input(f->editor, EDITOR_KEY_TIME);
    assert_int_equal(f->state.last_time_seconds, 120);
    assert_true(editor_system_is_modified(f->editor));
}

static void test_key_name(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    f->state.dialogue_result = "My Level";

    editor_system_key_input(f->editor, EDITOR_KEY_NAME);
    assert_string_equal(editor_system_get_level_title(f->editor), "My Level");
    assert_true(editor_system_is_modified(f->editor));
}

static void test_key_name_too_long(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    f->state.dialogue_result = "This level name is far too long for the limit";

    editor_system_key_input(f->editor, EDITOR_KEY_NAME);
    /* Name should NOT be updated */
    assert_string_equal(editor_system_get_level_title(f->editor), "");
}

static void test_key_clear(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    f->state.yes_no_result = 1;

    editor_system_key_input(f->editor, EDITOR_KEY_CLEAR);
    assert_int_equal(f->state.clear_count, 1);
}

static void test_key_flip_h(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;

    f->state.grid[0][0].occupied = 1;
    f->state.grid[0][0].block_type = RED_BLK;

    editor_system_key_input(f->editor, EDITOR_KEY_FLIP_H);

    assert_true(f->state.grid[0][EDITOR_MAX_COL_EDIT - 1].occupied);
}

/* =========================================================================
 * Section 12: Query functions
 * ========================================================================= */

static void test_is_modified_initially_false(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    /* After initial load, modified is false */
    assert_false(editor_system_is_modified(f->editor));
}

static void test_get_draw_action_initial(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    assert_int_equal(editor_system_get_draw_action(f->editor), EDITOR_ACTION_NOP);
}

static void test_get_level_number_initial(void **vstate)
{
    test_fixture_t *f = (test_fixture_t *)*vstate;
    assert_int_equal(editor_system_get_level_number(f->editor), 0);
}

/* =========================================================================
 * Section 13: Sound suppression
 * ========================================================================= */

static void test_no_sound_suppresses_callbacks(void **vstate)
{
    (void)vstate;
    editor_system_callbacks_t cb = make_callbacks();
    test_state_t s;
    memset(&s, 0, sizeof(s));
    s.load_result = 1;

    /* Create with no_sound=1 */
    editor_system_t *ctx = editor_system_create(&cb, &s, "levels", 1);
    editor_system_init_palette(ctx, MAX_STATIC_BLOCKS);
    editor_system_update(ctx, 0);

    /* Drawing should NOT produce sound */
    editor_system_mouse_button(ctx, CELL_CENTER_X(0), CELL_CENTER_Y(0), 1, 1);
    assert_int_equal(s.sound_count, 0);

    editor_system_destroy(ctx);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Section 1: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_create_null_callbacks),
        cmocka_unit_test(test_destroy_null),
        cmocka_unit_test(test_initial_state_is_level),

        /* Section 2: State machine */
        cmocka_unit_test_setup_teardown(test_update_level_transitions_to_none, setup, teardown),
        cmocka_unit_test_setup_teardown(test_update_level_calls_load, setup, teardown),
        cmocka_unit_test_setup_teardown(test_reset_returns_to_level, setup, teardown),
        cmocka_unit_test(test_wait_state),

        /* Section 3: Palette */
        cmocka_unit_test_setup_teardown(test_palette_init_count, setup, teardown),
        cmocka_unit_test_setup_teardown(test_palette_first_entry, setup, teardown),
        cmocka_unit_test_setup_teardown(test_palette_counter_entries, setup, teardown),
        cmocka_unit_test_setup_teardown(test_palette_select, setup, teardown),
        cmocka_unit_test_setup_teardown(test_palette_select_invalid, setup, teardown),
        cmocka_unit_test_setup_teardown(test_palette_entry_out_of_bounds, setup, teardown),

        /* Section 4: Mouse button */
        cmocka_unit_test_setup_teardown(test_mouse_left_click_draws, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mouse_middle_click_erases, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mouse_right_click_is_nop, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mouse_release_resets_action, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mouse_out_of_bounds_ignored, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mouse_below_editable_row_ignored, setup, teardown),

        /* Section 5: Mouse motion */
        cmocka_unit_test_setup_teardown(test_mouse_motion_draws_new_cell, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mouse_motion_same_cell_no_repeat, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mouse_motion_erase, setup, teardown),

        /* Section 6: Flip horizontal */
        cmocka_unit_test_setup_teardown(test_flip_horizontal_swaps_columns, setup, teardown),
        cmocka_unit_test_setup_teardown(test_flip_horizontal_preserves_center, setup, teardown),
        cmocka_unit_test_setup_teardown(test_flip_horizontal_sets_modified, setup, teardown),

        /* Section 7: Flip vertical */
        cmocka_unit_test_setup_teardown(test_flip_vertical_swaps_rows, setup, teardown),

        /* Section 8: Scroll horizontal */
        cmocka_unit_test_setup_teardown(test_scroll_horizontal_shifts_right, setup, teardown),
        cmocka_unit_test_setup_teardown(test_scroll_horizontal_wraps, setup, teardown),

        /* Section 9: Scroll vertical */
        cmocka_unit_test_setup_teardown(test_scroll_vertical_shifts_down, setup, teardown),
        cmocka_unit_test_setup_teardown(test_scroll_vertical_wraps, setup, teardown),

        /* Section 10: Clear grid */
        cmocka_unit_test_setup_teardown(test_clear_grid, setup, teardown),

        /* Section 11: Keyboard commands */
        cmocka_unit_test_setup_teardown(test_key_quit_when_unmodified, setup, teardown),
        cmocka_unit_test_setup_teardown(test_key_quit_denied, setup, teardown),
        cmocka_unit_test_setup_teardown(test_key_playtest_enters_test_mode, setup, teardown),
        cmocka_unit_test_setup_teardown(test_key_playtest_exits_test_mode, setup, teardown),
        cmocka_unit_test_setup_teardown(test_key_save, setup, teardown),
        cmocka_unit_test_setup_teardown(test_key_load, setup, teardown),
        cmocka_unit_test_setup_teardown(test_key_load_invalid_range, setup, teardown),
        cmocka_unit_test_setup_teardown(test_key_time, setup, teardown),
        cmocka_unit_test_setup_teardown(test_key_name, setup, teardown),
        cmocka_unit_test_setup_teardown(test_key_name_too_long, setup, teardown),
        cmocka_unit_test_setup_teardown(test_key_clear, setup, teardown),
        cmocka_unit_test_setup_teardown(test_key_flip_h, setup, teardown),

        /* Section 12: Queries */
        cmocka_unit_test_setup_teardown(test_is_modified_initially_false, setup, teardown),
        cmocka_unit_test_setup_teardown(test_get_draw_action_initial, setup, teardown),
        cmocka_unit_test_setup_teardown(test_get_level_number_initial, setup, teardown),

        /* Section 13: Sound */
        cmocka_unit_test(test_no_sound_suppresses_callbacks),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
