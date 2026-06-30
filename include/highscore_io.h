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

/*
 * Per-uid-dedup global insert, serialized by flock.
 *
 * Acquires an exclusive flock on `<path>.lock`, re-reads `path`, applies
 * the original game's per-uid dedup rule (highscore.c:721-737) — if the
 * caller's uid already has an entry, keep whichever score is higher —
 * then performs a standard rank insert and writes the result back to
 * `path`.  Releases the lock on return.
 *
 * Concurrency model:
 *   - flock(LOCK_EX) serializes concurrent writers — no torn reads.
 *   - The write is **in place** on the existing inode (open → ftruncate
 *     → write → fsync), NOT temp+rename.  This preserves the postinst-
 *     set `root:games` ownership / `0664` mode.  See ADR-041.
 *   - Trade-off: a crash mid-write leaves a corrupt file (the next
 *     successful write replaces it).  Torn reads still can't happen
 *     under the lock.  If your deployment needs crash-atomic global
 *     scores, write to a different module — this one is tuned for
 *     multi-user trust, not crash safety.
 *
 * Caller is responsible for elevating privileges (sys_priv_elevate)
 * before calling and dropping (sys_priv_drop) after.
 *
 * Returns HIGHSCORE_IO_OK if the score was inserted, HIGHSCORE_IO_ERR_NOT_RANKED
 * if it did not make the table (or the user already has a higher score),
 * HIGHSCORE_IO_ERR_VERSION if the on-disk file has an unsupported version
 * (refused under the lock rather than overwritten), or another I/O error
 * code on file-system failure.
 */
highscore_io_result_t
highscore_io_insert_global_atomic(const char *path, unsigned long score, unsigned long level,
                                  unsigned long game_time, unsigned long timestamp,
                                  unsigned long user_id, const char *name, const char *master_text);

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
 *
 * Single-writer assumption: the personal table is stored under
 * $XDG_DATA_HOME/xboing/, scoped to one Unix user, and has no file
 * lock around its insert+write sequence.  If the same user runs two
 * xboing processes concurrently with the same XDG_DATA_HOME and both
 * finish games at the same time, the later writer overwrites the
 * earlier without merging.  This matches original/highscore.c:771-790
 * (PERSONAL branch has no lock).  The global table uses
 * highscore_io_insert_global_atomic for multi-writer safety.
 */
highscore_io_result_t highscore_io_insert(highscore_table_t *table, unsigned long score,
                                          unsigned long level, unsigned long game_time,
                                          unsigned long timestamp, const char *name);

/*
 * Current standing (1-based) of the given score in the table, or -1 if
 * it does not place.  Uses ">=" so a score tied with an entry shares the
 * rank ahead of it — the right semantic for "where do I stand right now,
 * including my own already-inserted entry" (e.g. the post-insert master
 * check).  For "where would I land if inserted now", use
 * highscore_io_predict_rank instead — that is the function the display
 * paths should use so the shown rank matches the actual placement.
 */
int highscore_io_get_ranking(const highscore_table_t *table, unsigned long score);

/*
 * Rank (1-based) the score WOULD receive if inserted now, or -1 if it
 * would not place.  Uses insertion semantics (">" — a tie does NOT
 * displace, so the new score lands behind an equal entry): the same slot
 * scan highscore_io_insert and highscore_io_insert_global_atomic use, so
 * when the insert is accepted the score lands exactly at this rank.
 *
 * Caveat: this models slot selection only, NOT the per-uid dedup that
 * highscore_io_insert_global_atomic applies first.  On the global board a
 * user who already holds a higher (or equal) entry may be rejected
 * (NOT_RANKED) even though predict_rank would otherwise place the score.
 * For the boing-master question use highscore_io_would_be_global_master.
 */
int highscore_io_predict_rank(const highscore_table_t *table, unsigned long score);

/*
 * Would this (score, user_id) land at rank 0 after the per-uid dedup
 * that highscore_io_insert_global_atomic applies?  Caller uses this
 * to decide whether to prompt for boing-master "words of wisdom"
 * before the deferred locked insert — avoids prompting when the
 * insert will be rejected (user already has a higher score) or when
 * the score makes the table but not the top.
 *
 * Returns 1 if the new entry would be the new boing master, 0
 * otherwise (including all error cases).
 */
int highscore_io_would_be_global_master(const highscore_table_t *table, unsigned long score,
                                        unsigned long user_id);

/* =========================================================================
 * Table initialization
 * ========================================================================= */

/*
 * Initialize a table to empty defaults (all scores 0, default
 * master text).
 */
void highscore_io_init_table(highscore_table_t *table);

#endif /* HIGHSCORE_IO_H */
