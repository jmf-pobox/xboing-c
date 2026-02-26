/*
 * highscore_io.h — JSON-based high score file I/O.
 *
 * Reads and writes high score tables as JSON files, replacing the
 * legacy binary format with htonl/ntohl byte ordering.  Supports
 * both global and personal score files.
 *
 * File format is human-readable JSON:
 *   {
 *     "version": 1,
 *     "master_name": "Player One",
 *     "master_text": "Words of wisdom",
 *     "entries": [
 *       { "score": 50000, "level": 10, "game_time": 3600,
 *         "timestamp": 1700000000, "name": "Player One" },
 *       ...
 *     ]
 *   }
 *
 * Atomic writes: writes to a temp file, then renames.
 * No file locking needed (rename is atomic on POSIX).
 *
 * Uses highscore_table_t from highscore_system.h for the in-memory
 * representation.
 *
 * Legacy source: highscore.c I/O portions (ReadHighScoreTable,
 * WriteHighScoreTable, CheckAndAddScoreToHighScore, SortHighScores).
 */

#ifndef HIGHSCORE_IO_H
#define HIGHSCORE_IO_H

#include "highscore_system.h"

/* =========================================================================
 * Constants
 * ========================================================================= */

/* Current file format version. */
#define HIGHSCORE_IO_VERSION 1

/* =========================================================================
 * Result codes
 * ========================================================================= */

typedef enum
{
    HIGHSCORE_IO_OK = 0,
    HIGHSCORE_IO_ERR_NULL,       /* NULL argument */
    HIGHSCORE_IO_ERR_OPEN,       /* cannot open file */
    HIGHSCORE_IO_ERR_READ,       /* read/parse error */
    HIGHSCORE_IO_ERR_WRITE,      /* write error */
    HIGHSCORE_IO_ERR_RENAME,     /* atomic rename failed */
    HIGHSCORE_IO_ERR_VERSION,    /* unsupported file version */
    HIGHSCORE_IO_ERR_NOT_RANKED, /* score not high enough for table */
} highscore_io_result_t;

/* =========================================================================
 * File I/O
 * ========================================================================= */

/*
 * Read a high score table from a JSON file.
 * On success, fills *table and returns HIGHSCORE_IO_OK.
 * On failure, *table is zeroed and an error code is returned.
 * If the file does not exist, returns HIGHSCORE_IO_ERR_OPEN and
 * *table is initialized with defaults (caller can use it as-is).
 */
highscore_io_result_t highscore_io_read(const char *path, highscore_table_t *table);

/*
 * Write a high score table to a JSON file (atomic: temp + rename).
 * Entries are sorted descending by score before writing.
 * Creates parent directories if needed.
 */
highscore_io_result_t highscore_io_write(const char *path, const highscore_table_t *table);

/* =========================================================================
 * Score management
 * ========================================================================= */

/*
 * Sort table entries descending by score.  Entries with score == 0
 * are moved to the end.
 */
void highscore_io_sort(highscore_table_t *table);

/*
 * Try to insert a new score into the table.  If the score qualifies
 * (higher than the lowest entry), it is inserted at the correct rank
 * and the lowest entry is dropped.  Returns HIGHSCORE_IO_OK on
 * success, HIGHSCORE_IO_ERR_NOT_RANKED if the score is too low.
 *
 * Does NOT write to disk — call highscore_io_write() after.
 */
highscore_io_result_t highscore_io_insert(highscore_table_t *table, unsigned long score,
                                          unsigned long level, unsigned long game_time,
                                          unsigned long timestamp, const char *name);

/*
 * Return the rank (1-based) that the given score would achieve in
 * the table, or -1 if it would not place.
 */
int highscore_io_get_ranking(const highscore_table_t *table, unsigned long score);

/* =========================================================================
 * Table initialization
 * ========================================================================= */

/*
 * Initialize a table to empty defaults (all scores 0, default
 * master text).
 */
void highscore_io_init_table(highscore_table_t *table);

#endif /* HIGHSCORE_IO_H */
