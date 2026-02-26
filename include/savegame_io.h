/*
 * savegame_io.h — JSON-based save/load game state I/O.
 *
 * Reads and writes game state as JSON files, replacing the legacy
 * binary saveGameStruct format.  Level block data is NOT handled
 * here (it uses the existing level file format separately).
 *
 * File format:
 *   {
 *     "version": 1,
 *     "score": 12500,
 *     "level": 5,
 *     "level_time": 120,
 *     "game_time": 3600,
 *     "lives_left": 3,
 *     "start_level": 1,
 *     "paddle_size": 50,
 *     "num_bullets": 4
 *   }
 *
 * Atomic writes: writes to a temp file, then renames.
 *
 * Legacy source: file.c (SaveCurrentGame, LoadSavedGame).
 */

#ifndef SAVEGAME_IO_H
#define SAVEGAME_IO_H

/* =========================================================================
 * Constants
 * ========================================================================= */

/* Current save file format version. */
#define SAVEGAME_IO_VERSION 1

/* =========================================================================
 * Types
 * ========================================================================= */

/* Game state for save/load. */
typedef struct
{
    unsigned long score;
    unsigned long level;
    int level_time;
    unsigned long game_time;
    int lives_left;
    int start_level;
    int paddle_size;
    int num_bullets;
} savegame_data_t;

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
 * File I/O
 * ========================================================================= */

/*
 * Read game state from a JSON file.
 * On success, fills *data and returns SAVEGAME_IO_OK.
 * On failure, *data is zeroed and an error code is returned.
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

#endif /* SAVEGAME_IO_H */
