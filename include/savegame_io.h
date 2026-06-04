/*
 * savegame_io.h — JSON-based save/load game state I/O.
 *
 * Reads and writes game state as JSON files, replacing the legacy
 * binary saveGameStruct format.
 *
 * Two file types:
 *   - save-info.dat (savegame_data_t): player + meta + per-system state
 *   - save-level.dat (savegame_level_t): block grid snapshot
 *
 * Atomic writes: writes to a temp file, then renames.
 *
 * Legacy source: file.c (SaveCurrentGame, LoadSavedGame,
 * SaveLevelDataFile, LoadLevelDataFile).
 */

#ifndef SAVEGAME_IO_H
#define SAVEGAME_IO_H

#include "ball_types.h"
#include "block_types.h"
#include "level_system.h"

/* =========================================================================
 * Constants
 * ========================================================================= */

#define SAVEGAME_IO_VERSION 2
#define SAVEGAME_LEVEL_VERSION 1

/* =========================================================================
 * Types
 * ========================================================================= */

/* Per-ball save state.  Inactive slots have active=0. */
typedef struct
{
    int active;
    int state; /* enum BallStates */
    int x, y;
    int dx, dy;
    int wait_mode; /* enum BallStates */
} savegame_ball_t;

/* Active specials at save time.  paddle reverse/sticky live on the
 * paddle struct and are saved separately in savegame_data_t. */
typedef struct
{
    int sticky;
    int saving;
    int fast_gun;
    int no_walls;
    int killer;
    int x2;
    int x4;
} savegame_specials_t;

/* EyeDude internal state needed for mid-walk restore. */
typedef struct
{
    int state; /* eyedude_state_t */
    int dir;   /* eyedude_dir_t */
    int x, y;
    int slide;
    int inc;
    int turn;
} savegame_eyedude_t;

/* Full game state for save-info.dat. */
typedef struct
{
    /* v1 fields */
    unsigned long score;
    unsigned long level;
    int level_time;
    unsigned long game_time;
    int lives_left;
    int start_level;
    int paddle_size; /* pixel width, kept for compat */
    int num_bullets;

    /* v2 additions */
    int time_remaining;
    int user_tilts;
    int bonus_count;
    int paddle_pos;
    int paddle_size_type; /* PADDLE_SIZE_SMALL / MEDIUM / HUGE */
    int paddle_reverse;
    int paddle_sticky;
    int gun_unlimited;
    savegame_specials_t specials;
    savegame_eyedude_t eyedude;
    savegame_ball_t balls[MAX_BALLS];
} savegame_data_t;

/* Per-cell block state for save-level.dat.
 *
 * `hit_points` is informational: it's deterministic from
 * (block_type, row) via score_logic::score_block_hit_points, so on
 * restore the value is recomputed by block_system_add and any saved
 * value is overwritten.  We still capture and write it so the JSON
 * file is a complete snapshot of cell state.
 *
 * Damage/cooldown for BLACK_BLK is tracked via `next_frame_offset`,
 * not `hit_points`. */
typedef struct
{
    int occupied;
    int block_type;
    int counter_slide;
    int random;
    int hit_points;
    int next_frame_offset; /* BLACK_BLK cooldown offset; 0 = no cooldown */
} savegame_cell_t;

/* Level grid snapshot for save-level.dat.
 *
 * Stored sparsely (only occupied cells written), but loaded into the
 * full 2D array indexed by [row][col]. */
typedef struct
{
    char title[LEVEL_TITLE_MAX];
    int time_bonus;
    savegame_cell_t cells[MAX_ROW][MAX_COL];
} savegame_level_t;

/* Result codes. */
typedef enum
{
    SAVEGAME_IO_OK = 0,
    SAVEGAME_IO_ERR_NULL,
    SAVEGAME_IO_ERR_OPEN,
    SAVEGAME_IO_ERR_READ,
    SAVEGAME_IO_ERR_WRITE,
    SAVEGAME_IO_ERR_RENAME,
    SAVEGAME_IO_ERR_VERSION,
} savegame_io_result_t;

/* =========================================================================
 * File I/O — save-info.dat
 * ========================================================================= */

/*
 * Read game state from a JSON file.
 * On success, fills *data and returns SAVEGAME_IO_OK.
 * On failure, *data is zeroed and an error code is returned.
 *
 * Version mismatch returns SAVEGAME_IO_ERR_VERSION.  This codebase
 * does not support v1 files — they are rejected outright.
 */
savegame_io_result_t savegame_io_read(const char *path, savegame_data_t *data);

/*
 * Write game state to a JSON file (atomic: temp + rename).
 * Creates parent directories if needed.
 */
savegame_io_result_t savegame_io_write(const char *path, const savegame_data_t *data);

/*
 * Check if a save file exists at the given path.
 * Returns 1 if it exists, 0 otherwise.
 */
int savegame_io_exists(const char *path);

/*
 * Delete a save file.  Returns SAVEGAME_IO_OK on success or if
 * the file doesn't exist.
 */
savegame_io_result_t savegame_io_delete(const char *path);

/*
 * Initialize a savegame_data_t to sensible defaults (new game).
 */
void savegame_io_init(savegame_data_t *data);

/* =========================================================================
 * File I/O — save-level.dat
 * ========================================================================= */

/*
 * Read block grid snapshot from a JSON file.
 * On success, fills *level and returns SAVEGAME_IO_OK.
 * On failure, *level is zeroed and an error code is returned.
 */
savegame_io_result_t savegame_level_read(const char *path, savegame_level_t *level);

/*
 * Write block grid snapshot to a JSON file (atomic: temp + rename).
 * Only occupied cells are emitted to keep the file sparse.
 */
savegame_io_result_t savegame_level_write(const char *path, const savegame_level_t *level);

/*
 * Initialize a savegame_level_t to defaults (empty grid).
 */
void savegame_level_init(savegame_level_t *level);

#endif /* SAVEGAME_IO_H */
