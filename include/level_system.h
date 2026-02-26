#ifndef LEVEL_SYSTEM_H
#define LEVEL_SYSTEM_H

/*
 * level_system.h — Pure C level file loading with callback-based block creation.
 *
 * Owns level file parsing (title, time bonus, 15x9 character grid),
 * character-to-block-type mapping, level number wrapping (1..80), and
 * background cycling (2..5).  Delegates block creation to the integration
 * layer via an injected callback.  Zero dependency on SDL2 or X11.
 *
 * Opaque context pattern: no globals, fully testable with CMocka stubs.
 * See ADR-020 in docs/DESIGN.md for design rationale.
 */

#include "block_types.h" /* Block type constants, MAX_ROW, MAX_COL */

/* =========================================================================
 * Status codes
 * ========================================================================= */

typedef enum
{
    LEVEL_SYS_OK = 0,
    LEVEL_SYS_ERR_NULL_ARG,
    LEVEL_SYS_ERR_ALLOC_FAILED,
    LEVEL_SYS_ERR_FILE_NOT_FOUND,
    LEVEL_SYS_ERR_PARSE_FAILED,
} level_system_status_t;

/* =========================================================================
 * Constants — match legacy level.h / file.c values
 * ========================================================================= */

#define LEVEL_MAX_NUM 80        /* Total level files (level01..level80) */
#define LEVEL_TITLE_MAX 256     /* Max title string length */
#define LEVEL_GRID_ROWS 15      /* Rows parsed from file (MAX_ROW - 3) */
#define LEVEL_GRID_COLS MAX_COL /* Columns per row (9) */
#define LEVEL_SHOTS_TO_KILL 3   /* Counter for special blocks */
#define LEVEL_BG_FIRST 2        /* First background in cycle */
#define LEVEL_BG_LAST 5         /* Last background in cycle */

/* =========================================================================
 * Callback table — injected at creation time
 * ========================================================================= */

typedef struct
{
    /*
     * Called for each non-empty cell during level loading.
     *
     * row, col:        grid position (row 0..14, col 0..8).
     * block_type:      block type constant from block_types.h.
     * counter_slide:   initial hit counter (0 or LEVEL_SHOTS_TO_KILL).
     *
     * The integration layer calls block_system_add() here.
     */
    void (*on_add_block)(int row, int col, int block_type, int counter_slide, void *ud);
} level_system_callbacks_t;

/* =========================================================================
 * Opaque context
 * ========================================================================= */

typedef struct level_system level_system_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/*
 * Create a level system context.
 *
 * callbacks: callback table (copied, caller retains ownership).
 *            NULL callbacks are safe — load still parses but skips creation.
 * user_data: opaque pointer passed to all callbacks.
 *
 * Initial state: no level loaded, background at 1 (first advance → 2).
 * Returns NULL on allocation failure (sets *status if non-NULL).
 */
level_system_t *level_system_create(const level_system_callbacks_t *callbacks, void *user_data,
                                    level_system_status_t *status);

/* Destroy the level system.  Safe to call with NULL. */
void level_system_destroy(level_system_t *ctx);

/* =========================================================================
 * Level loading
 * ========================================================================= */

/*
 * Load a level from an absolute file path.
 *
 * Parses title (line 1), time bonus (line 2), and 15 rows of 9 characters.
 * For each non-'.' cell, maps the character to a block type and fires
 * on_add_block.  The caller is responsible for clearing the block grid
 * before calling this function.
 *
 * Returns LEVEL_SYS_OK on success, LEVEL_SYS_ERR_FILE_NOT_FOUND if the
 * file cannot be opened, LEVEL_SYS_ERR_PARSE_FAILED on format errors.
 */
level_system_status_t level_system_load_file(level_system_t *ctx, const char *path);

/* =========================================================================
 * Background cycling
 * ========================================================================= */

/*
 * Advance to the next background in the cycle (2 → 3 → 4 → 5 → 2 → ...).
 *
 * Call this once per level transition, before load_file.
 * Returns the new background number.
 */
int level_system_advance_background(level_system_t *ctx);

/* Return the current background number (2..5, or 1 if never advanced). */
int level_system_get_background(const level_system_t *ctx);

/* =========================================================================
 * Queries
 * ========================================================================= */

/* Return the title of the last loaded level, or "" if none loaded. */
const char *level_system_get_title(const level_system_t *ctx);

/* Return the time bonus (seconds) of the last loaded level, or 0. */
int level_system_get_time_bonus(const level_system_t *ctx);

/* =========================================================================
 * Utilities (stateless)
 * ========================================================================= */

/*
 * Compute the effective level file number for a given level counter.
 *
 * Wrapping formula: result = level % LEVEL_MAX_NUM.
 * If result == 0, it wraps to LEVEL_MAX_NUM (80).
 * Result is always in the range 1..80.
 *
 * Level 1 → 1, level 80 → 80, level 81 → 1, level 160 → 80.
 */
int level_system_wrap_number(int level_number);

/*
 * Map a level file character to a block type and counter_slide.
 *
 * Returns the block type constant (e.g., RED_BLK, COUNTER_BLK) or
 * NONE_BLK if the character is '.' (empty cell).
 * Sets *out_counter_slide to the initial hit counter.
 */
int level_system_char_to_block(char ch, int *out_counter_slide);

/* Return a human-readable string for a status code. */
const char *level_system_status_string(level_system_status_t status);

#endif /* LEVEL_SYSTEM_H */
