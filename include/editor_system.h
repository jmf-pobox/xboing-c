/*
 * editor_system.h — Pure C level editor state machine.
 *
 * Owns editor grid operations (draw, erase, clear), board transforms
 * (flip H/V, scroll H/V), palette tracking, play-test lifecycle, and
 * save/load coordination.  Communicates side effects through injected
 * callbacks.  Zero dependency on SDL2 or X11.
 *
 * Grid constraints: 15 editable rows x 9 columns.  The bottom 3 rows
 * of the 18-row block grid are reserved for the paddle area.
 *
 * Opaque context pattern: no globals, fully testable with CMocka stubs.
 * See ADR-037 in docs/DESIGN.md for design rationale.
 */

#ifndef EDITOR_SYSTEM_H
#define EDITOR_SYSTEM_H

#include "block_types.h" /* MAX_ROW, MAX_COL, block type constants */

/* =========================================================================
 * Constants
 * ========================================================================= */

#define EDITOR_PLAY_WIDTH 495
#define EDITOR_PLAY_HEIGHT 580
#define EDITOR_TOOL_WIDTH 120
#define EDITOR_MAX_ROW_EDIT (MAX_ROW - 3) /* 15 editable rows */
#define EDITOR_MAX_COL_EDIT MAX_COL       /* 9 columns */
#define EDITOR_MAX_PALETTE 50             /* Max palette entries */
#define EDITOR_LEVEL_NAME_MAX 26          /* Max level name length (25 + NUL) */
#define EDITOR_MAX_TIME 3600              /* Max time limit in seconds */
#define EDITOR_MAX_LEVELS 80              /* Total level files (level01..level80) */

/* =========================================================================
 * Types
 * ========================================================================= */

typedef enum
{
    EDITOR_STATE_LEVEL, /* Initial: load default editor level */
    EDITOR_STATE_NONE,  /* Idle: accepting user input */
    EDITOR_STATE_TEST,  /* Play-testing the level */
    EDITOR_STATE_WAIT,  /* Waiting for a target frame */
    EDITOR_STATE_FINISH /* Shutting down the editor */
} editor_state_t;

typedef enum
{
    EDITOR_ACTION_NOP = 0,
    EDITOR_ACTION_DRAW,
    EDITOR_ACTION_ERASE
} editor_draw_action_t;

/* A single palette entry: block type + counter slide. */
typedef struct
{
    int block_type;    /* Block type constant (e.g. RED_BLK, COUNTER_BLK) */
    int counter_slide; /* Counter slide index (0 for most, 1-5 for COUNTER_BLK) */
} editor_palette_entry_t;

/* A single grid cell snapshot for rendering. */
typedef struct
{
    int occupied;
    int block_type;
    int counter_slide;
} editor_cell_t;

/* =========================================================================
 * Callbacks — injected at creation time
 * ========================================================================= */

typedef struct
{
    /* Called when a block should be placed in the grid.
     * row, col: grid position.  block_type, counter_slide: block data.
     * visible: nonzero if the block should be rendered immediately. */
    void (*on_add_block)(int row, int col, int block_type, int counter_slide, int visible,
                         void *ud);

    /* Called when a block should be erased from the grid.
     * row, col: grid position. */
    void (*on_erase_block)(int row, int col, void *ud);

    /* Called when the entire grid should be cleared. */
    void (*on_clear_grid)(void *ud);

    /* Called to query a block at (row, col).
     * Must fill *cell and return nonzero if occupied, 0 otherwise. */
    int (*query_cell)(int row, int col, editor_cell_t *cell, void *ud);

    /* Called to play a sound effect. name: sound file name, volume: 0-100. */
    void (*on_sound)(const char *name, int volume, void *ud);

    /* Called to display a status message. sticky: nonzero to persist. */
    void (*on_message)(const char *message, int sticky, void *ud);

    /* Called when the editor needs to load a level file.
     * path: absolute path.  Returns nonzero on success. */
    int (*on_load_level)(const char *path, void *ud);

    /* Called when the editor needs to save a level file.
     * path: absolute path.  Returns nonzero on success. */
    int (*on_save_level)(const char *path, void *ud);

    /* Called when a fatal error occurs. message: error description. */
    void (*on_error)(const char *message, void *ud);

    /* Called when the editor requests a user input dialogue.
     * Returns a pointer to the entered string (empty if cancelled).
     * message: prompt text.  numeric_only: nonzero for digits only. */
    const char *(*on_input_dialogue)(const char *message, int numeric_only, void *ud);

    /* Called when the editor requests a yes/no confirmation.
     * message: prompt text.  Returns nonzero for yes, 0 for no. */
    int (*on_yes_no_dialogue)(const char *message, void *ud);

    /* Called to set the level time bonus display. seconds: time in seconds. */
    void (*on_set_time)(int seconds, void *ud);

    /* Called when play-test mode starts. */
    void (*on_playtest_start)(void *ud);

    /* Called when play-test mode ends. */
    void (*on_playtest_end)(void *ud);

    /* Called when the editor finishes (user quits). */
    void (*on_finish)(void *ud);
} editor_system_callbacks_t;

/* =========================================================================
 * Opaque context
 * ========================================================================= */

typedef struct editor_system editor_system_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/*
 * Create an editor system context.
 *
 * callbacks: callback table (copied, caller retains ownership).
 *            NULL callbacks are safe — operations that need them are skipped.
 * user_data: opaque pointer passed to all callbacks.
 * levels_dir: path to levels directory (e.g. "levels/").
 * no_sound: nonzero to suppress sound callbacks.
 *
 * Returns NULL on allocation failure.
 */
editor_system_t *editor_system_create(const editor_system_callbacks_t *callbacks, void *user_data,
                                      const char *levels_dir, int no_sound);

/* Destroy the editor system.  Safe to call with NULL. */
void editor_system_destroy(editor_system_t *ctx);

/* =========================================================================
 * State machine
 * ========================================================================= */

/* Run one frame of the editor state machine. frame: current game frame. */
void editor_system_update(editor_system_t *ctx, int frame);

/* Return the current editor state. */
editor_state_t editor_system_get_state(const editor_system_t *ctx);

/* Reset editor to EDIT_LEVEL state. */
void editor_system_reset(editor_system_t *ctx);

/* =========================================================================
 * Palette
 * ========================================================================= */

/*
 * Initialize the block palette.
 *
 * Populates up to EDITOR_MAX_PALETTE entries: first MAX_STATIC_BLOCKS
 * block types (one per info entry), then COUNTER_BLK variants (slides 1-5).
 * Returns the number of palette entries created.
 *
 * block_count: number of static block types (MAX_STATIC_BLOCKS).
 */
int editor_system_init_palette(editor_system_t *ctx, int block_count);

/* Return a palette entry.  Returns NULL if index out of bounds. */
const editor_palette_entry_t *editor_system_get_palette_entry(const editor_system_t *ctx,
                                                              int index);

/* Return the total number of palette entries. */
int editor_system_get_palette_count(const editor_system_t *ctx);

/* Select a palette entry by index.  Returns 0 on success, -1 if invalid. */
int editor_system_select_palette(editor_system_t *ctx, int index);

/* Return the currently selected palette index. */
int editor_system_get_selected_palette(const editor_system_t *ctx);

/* =========================================================================
 * Grid editing
 * ========================================================================= */

/*
 * Process a mouse button press/release on the play area.
 *
 * x, y:    pixel coordinates within the play window.
 * button:  1=left (draw), 2=middle (erase), 3=right (inspect).
 * pressed: nonzero for press, 0 for release.
 *
 * Returns the resulting draw action (NOP, DRAW, or ERASE).
 */
editor_draw_action_t editor_system_mouse_button(editor_system_t *ctx, int x, int y, int button,
                                                int pressed);

/*
 * Process mouse drag motion during drawing/erasing.
 *
 * x, y: pixel coordinates within the play window.
 * Called continuously while a button is held.
 */
void editor_system_mouse_motion(editor_system_t *ctx, int x, int y);

/* =========================================================================
 * Keyboard commands
 * ========================================================================= */

typedef enum
{
    EDITOR_KEY_QUIT,
    EDITOR_KEY_REDRAW,
    EDITOR_KEY_LOAD,
    EDITOR_KEY_SAVE,
    EDITOR_KEY_TIME,
    EDITOR_KEY_NAME,
    EDITOR_KEY_PLAYTEST,
    EDITOR_KEY_CLEAR,
    EDITOR_KEY_FLIP_H,
    EDITOR_KEY_SCROLL_H,
    EDITOR_KEY_FLIP_V,
    EDITOR_KEY_SCROLL_V,
    /* Play-test mode keys */
    EDITOR_KEY_PADDLE_LEFT,
    EDITOR_KEY_PADDLE_RIGHT,
    EDITOR_KEY_SHOOT,
    EDITOR_KEY_COUNT /* sentinel */
} editor_key_t;

/* Process a key command.  Dispatches based on current state. */
void editor_system_key_input(editor_system_t *ctx, editor_key_t key);

/* =========================================================================
 * Board transforms
 * ========================================================================= */

/* Flip the board horizontally (mirror left-right). */
void editor_system_flip_horizontal(editor_system_t *ctx);

/* Flip the board vertically (mirror top-bottom). */
void editor_system_flip_vertical(editor_system_t *ctx);

/* Scroll the board one column to the right (wrapping). */
void editor_system_scroll_horizontal(editor_system_t *ctx);

/* Scroll the board one row down (wrapping). */
void editor_system_scroll_vertical(editor_system_t *ctx);

/* Clear all blocks in the editable region. */
void editor_system_clear_grid(editor_system_t *ctx);

/* =========================================================================
 * Queries
 * ========================================================================= */

/* Return nonzero if the level has been modified since last save/load. */
int editor_system_is_modified(const editor_system_t *ctx);

/* Return the current draw action (NOP, DRAW, or ERASE). */
editor_draw_action_t editor_system_get_draw_action(const editor_system_t *ctx);

/* Return the current level number being edited (1-based, or 0 if none). */
int editor_system_get_level_number(const editor_system_t *ctx);

/* Return the level title buffer (mutable for set-name). */
const char *editor_system_get_level_title(const editor_system_t *ctx);

#endif /* EDITOR_SYSTEM_H */
