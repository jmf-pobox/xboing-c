/*
 * test_highscore_io.c — Tests for JSON-based high score file I/O.
 *
 * 7 groups:
 *   1. Table initialization and count (5 tests)
 *   2. Sorting (3 tests)
 *   3. Insert and ranking (4 tests)
 *   4. Write and read round-trip (3 tests)
 *   5. Edge cases (3 tests)
 *   6. Error handling (2 tests)
 *   7. Null safety (1 test)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* cmocka must come after setjmp.h / stdarg.h / stddef.h */
#include <cmocka.h>

#include "highscore_io.h"

/* =========================================================================
 * Test helpers
 * ========================================================================= */

static char tmp_path[256];

/* Forward decl — defined alongside the atomic-insert test group below. */
static void cleanup_atomic_artifacts(void);

static int setup_tmpfile(void **state)
{
    (void)state;
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/xboing_test_hs_XXXXXX");
    int fd = mkstemp(tmp_path);
    if (fd < 0)
    {
        return -1;
    }
    close(fd);
    return 0;
}

static int teardown_tmpfile(void **state)
{
    (void)state;
    (void)remove(tmp_path);
    /* Also remove temp file used by atomic write. */
    char tmp2[260];
    snprintf(tmp2, sizeof(tmp2), "%s.tmp", tmp_path);
    (void)remove(tmp2);
    return 0;
}

static highscore_table_t make_populated_table(void)
{
    highscore_table_t t;
    highscore_io_init_table(&t);
    strncpy(t.master_name, "Champion", HIGHSCORE_NAME_LEN - 1);
    strncpy(t.master_text, "Victory is mine!", HIGHSCORE_NAME_LEN - 1);
    for (int i = 0; i < 5; i++)
    {
        t.entries[i].score = (unsigned long)(50000 - i * 5000);
        t.entries[i].level = (unsigned long)(10 - i);
        t.entries[i].game_time = (unsigned long)(3600 + i * 600);
        t.entries[i].timestamp = 1700000000UL;
        snprintf(t.entries[i].name, HIGHSCORE_NAME_LEN, "Player %d", i + 1);
    }
    return t;
}

/* =========================================================================
 * Group 1: Table initialization
 * ========================================================================= */

static void test_init_table(void **state)
{
    (void)state;
    highscore_table_t t;
    highscore_io_init_table(&t);
    assert_string_equal(t.master_text, "Anyone play this game?");
    assert_string_equal(t.master_name, "");
    for (int i = 0; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        assert_int_equal((int)t.entries[i].score, 0);
    }
}

static void test_init_table_null(void **state)
{
    (void)state;
    /* Should not crash. */
    highscore_io_init_table(NULL);
}

static void test_count_empty(void **state)
{
    (void)state;
    highscore_table_t t;
    highscore_io_init_table(&t);
    assert_int_equal(highscore_io_count(&t), 0);
}

static void test_count_populated(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    assert_int_equal(highscore_io_count(&t), 5);
}

static void test_count_null(void **state)
{
    (void)state;
    assert_int_equal(highscore_io_count(NULL), 0);
}

/* =========================================================================
 * Group 2: Sorting
 * ========================================================================= */

static void test_sort_already_sorted(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    highscore_io_sort(&t);
    assert_int_equal((int)t.entries[0].score, 50000);
    assert_int_equal((int)t.entries[1].score, 45000);
    assert_int_equal((int)t.entries[4].score, 30000);
}

static void test_sort_reversed(void **state)
{
    (void)state;
    highscore_table_t t;
    highscore_io_init_table(&t);
    for (int i = 0; i < 5; i++)
    {
        t.entries[i].score = (unsigned long)((i + 1) * 1000);
    }
    highscore_io_sort(&t);
    assert_int_equal((int)t.entries[0].score, 5000);
    assert_int_equal((int)t.entries[1].score, 4000);
    assert_int_equal((int)t.entries[4].score, 1000);
}

static void test_sort_zeros_at_end(void **state)
{
    (void)state;
    highscore_table_t t;
    highscore_io_init_table(&t);
    t.entries[0].score = 0;
    t.entries[1].score = 5000;
    t.entries[2].score = 0;
    t.entries[3].score = 3000;
    highscore_io_sort(&t);
    assert_int_equal((int)t.entries[0].score, 5000);
    assert_int_equal((int)t.entries[1].score, 3000);
    assert_int_equal((int)t.entries[2].score, 0);
}

/* =========================================================================
 * Group 3: Insert and ranking
 * ========================================================================= */

static void test_ranking_top(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    int rank = highscore_io_get_ranking(&t, 60000);
    assert_int_equal(rank, 1);
}

static void test_ranking_middle(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    int rank = highscore_io_get_ranking(&t, 42000);
    assert_int_equal(rank, 3);
}

static void test_ranking_not_placed(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    /* Fill all 10 entries. */
    for (int i = 5; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        t.entries[i].score = (unsigned long)(25000 - (i - 5) * 1000);
    }
    int rank = highscore_io_get_ranking(&t, 100);
    assert_int_equal(rank, -1);
}

static void test_insert_shifts_down(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    highscore_io_result_t r = highscore_io_insert(&t, 47000, 9, 3000, 1700000001UL, "New Player");
    assert_int_equal(r, HIGHSCORE_IO_OK);
    assert_int_equal((int)t.entries[0].score, 50000);
    assert_int_equal((int)t.entries[1].score, 47000);
    assert_string_equal(t.entries[1].name, "New Player");
    assert_int_equal((int)t.entries[2].score, 45000);
}

/* =========================================================================
 * Group 3b: predict_rank — placement prediction (insert semantics)
 * ========================================================================= */

static void test_predict_rank_empty(void **state)
{
    (void)state;
    highscore_table_t t;
    highscore_io_init_table(&t);
    /* Any positive score takes the top slot of an empty board. */
    assert_int_equal(highscore_io_predict_rank(&t, 5000), 1);
    /* A zero score does not place (insert refuses it too). */
    assert_int_equal(highscore_io_predict_rank(&t, 0), -1);
}

static void test_predict_rank_middle(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    assert_int_equal(highscore_io_predict_rank(&t, 42000), 3);
}

static void test_predict_rank_tie_lands_behind(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    /* A score tied with entry[0] (50000) does NOT displace it — predict
     * says rank 2 (behind), matching the insert.  get_ranking, by
     * contrast, reports current standing and ties ahead (rank 1).  This
     * difference is exactly the bonus-interstitial-vs-placement bug, so
     * pin both semantics here. */
    assert_int_equal(highscore_io_predict_rank(&t, 50000), 2);
    assert_int_equal(highscore_io_get_ranking(&t, 50000), 1);
}

static void test_predict_rank_full_table_too_low(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    for (int i = 5; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        t.entries[i].score = (unsigned long)(25000 - (i - 5) * 1000);
    }
    assert_int_equal(highscore_io_predict_rank(&t, 100), -1);
}

/* The whole point of predict_rank: the predicted rank must equal where
 * highscore_io_insert actually puts the score, for every score.  This
 * guards against the two ever drifting apart again. */
static void test_predict_rank_matches_insert(void **state)
{
    (void)state;
    const unsigned long scores[] = {60000, 50000, 47000, 42000, 30000, 25000, 1, 0};
    for (size_t k = 0; k < sizeof(scores) / sizeof(scores[0]); k++)
    {
        highscore_table_t t = make_populated_table();
        int predicted = highscore_io_predict_rank(&t, scores[k]);
        highscore_io_result_t r =
            highscore_io_insert(&t, scores[k], 1, 1, 1700000002UL, "Predicted");
        /* predict_rank returns a 1-based rank or -1; <= 0 means "would not
         * place" (and keeps the indexing below provably in-bounds). */
        if (predicted <= 0)
        {
            assert_int_equal(r, HIGHSCORE_IO_ERR_NOT_RANKED);
        }
        else
        {
            assert_int_equal(r, HIGHSCORE_IO_OK);
            assert_int_equal((int)t.entries[predicted - 1].score, (int)scores[k]);
            assert_string_equal(t.entries[predicted - 1].name, "Predicted");
        }
    }
}

/* =========================================================================
 * Group 4: Write and read round-trip
 * ========================================================================= */

static void test_write_read_roundtrip(void **state)
{
    (void)state;
    highscore_table_t orig = make_populated_table();
    highscore_io_result_t r = highscore_io_write(tmp_path, &orig);
    assert_int_equal(r, HIGHSCORE_IO_OK);

    highscore_table_t loaded;
    r = highscore_io_read(tmp_path, &loaded);
    assert_int_equal(r, HIGHSCORE_IO_OK);

    assert_string_equal(loaded.master_name, "Champion");
    assert_string_equal(loaded.master_text, "Victory is mine!");
    assert_int_equal((int)loaded.entries[0].score, 50000);
    assert_string_equal(loaded.entries[0].name, "Player 1");
    assert_int_equal((int)loaded.entries[4].score, 30000);
    assert_string_equal(loaded.entries[4].name, "Player 5");
}

static void test_roundtrip_empty_table(void **state)
{
    (void)state;
    highscore_table_t orig;
    highscore_io_init_table(&orig);
    highscore_io_result_t r = highscore_io_write(tmp_path, &orig);
    assert_int_equal(r, HIGHSCORE_IO_OK);

    highscore_table_t loaded;
    r = highscore_io_read(tmp_path, &loaded);
    assert_int_equal(r, HIGHSCORE_IO_OK);
    assert_string_equal(loaded.master_text, "Anyone play this game?");
    for (int i = 0; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        assert_int_equal((int)loaded.entries[i].score, 0);
    }
}

static void test_roundtrip_special_chars(void **state)
{
    (void)state;
    highscore_table_t orig;
    highscore_io_init_table(&orig);
    strncpy(orig.master_name, "Test \"User\"", HIGHSCORE_NAME_LEN - 1);
    strncpy(orig.master_text, "Back\\slash", HIGHSCORE_NAME_LEN - 1);
    orig.entries[0].score = 1000;
    strncpy(orig.entries[0].name, "Name with \"quotes\"", HIGHSCORE_NAME_LEN - 1);

    highscore_io_result_t r = highscore_io_write(tmp_path, &orig);
    assert_int_equal(r, HIGHSCORE_IO_OK);

    highscore_table_t loaded;
    r = highscore_io_read(tmp_path, &loaded);
    assert_int_equal(r, HIGHSCORE_IO_OK);
    assert_string_equal(loaded.master_name, "Test \"User\"");
    assert_string_equal(loaded.master_text, "Back\\slash");
    assert_string_equal(loaded.entries[0].name, "Name with \"quotes\"");
}

/* =========================================================================
 * Group 5: Edge cases
 * ========================================================================= */

static void test_insert_into_full_table(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    for (int i = 5; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        t.entries[i].score = (unsigned long)(25000 - (i - 5) * 1000);
        snprintf(t.entries[i].name, HIGHSCORE_NAME_LEN, "Player %d", i + 1);
    }

    /* Insert at #1 — last entry should be dropped. */
    highscore_io_result_t r = highscore_io_insert(&t, 99999, 15, 7200, 1700000002UL, "Legend");
    assert_int_equal(r, HIGHSCORE_IO_OK);
    assert_int_equal((int)t.entries[0].score, 99999);
    assert_string_equal(t.entries[0].name, "Legend");
    /* Old #1 should now be #2. */
    assert_int_equal((int)t.entries[1].score, 50000);
    /* Table still has exactly 10 entries. */
    assert_int_equal((int)t.entries[HIGHSCORE_NUM_ENTRIES - 1].score, 22000);
}

static void test_insert_too_low(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    for (int i = 5; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        t.entries[i].score = (unsigned long)(25000 - (i - 5) * 1000);
    }
    highscore_io_result_t r = highscore_io_insert(&t, 100, 1, 60, 1700000003UL, "Loser");
    assert_int_equal(r, HIGHSCORE_IO_ERR_NOT_RANKED);
}

/* Original/highscore.c:777 uses strict > for the personal insert
 * (`if (score > ntohl(highScores[i].score))`).  A tied score must NOT
 * displace.  Locks in the cycle-2 revert of the bad >= regression. */
static void test_insert_tie_does_not_displace(void **state)
{
    (void)state;
    highscore_table_t t;
    highscore_io_init_table(&t);
    t.entries[0].score = 5000;
    snprintf(t.entries[0].name, sizeof(t.entries[0].name), "Original");

    highscore_io_result_t r = highscore_io_insert(&t, 5000, 1, 60, 1700000004UL, "TieAttempt");
    /* Strict-> insert: the new 5000 cannot displace index 0 (also 5000),
     * but DOES beat index 1 (score=0 from init_table).  Lands at rank 1
     * (second place).  The key invariant being asserted: the existing
     * rank-0 entry is untouched. */
    assert_int_equal(r, HIGHSCORE_IO_OK);
    assert_int_equal((int)t.entries[0].score, 5000);
    assert_string_equal(t.entries[0].name, "Original");
}

static void test_atomic_tie_does_not_displace_existing(void **state)
{
    (void)state;
    /* Seed a different user at 5000, then attempt 5000 with our uid.
     * Standard rank insert uses strict > (matches
     * original/highscore.c:743) so the tie must rank below their
     * entry — landing at index 1, not 0.  Verifies the cycle-2 revert. */
    assert_int_equal(highscore_io_insert_global_atomic(tmp_path, 5000, 3, 120, 1700000000UL, 999,
                                                       "Other", "Their wisdom"),
                     HIGHSCORE_IO_OK);
    highscore_io_result_t r = highscore_io_insert_global_atomic(
        tmp_path, 5000, 3, 120, 1700000100UL, 1000, "Us", NULL);
    assert_int_equal(r, HIGHSCORE_IO_OK);

    highscore_table_t out;
    assert_int_equal(highscore_io_read(tmp_path, &out), HIGHSCORE_IO_OK);
    /* Other user still at rank 0; we're at rank 1. */
    assert_int_equal((int)out.entries[0].user_id, 999);
    assert_int_equal((int)out.entries[1].user_id, 1000);
    /* Other user's master_text preserved (we didn't take rank 0). */
    assert_string_equal(out.master_name, "Other");
    assert_string_equal(out.master_text, "Their wisdom");

    cleanup_atomic_artifacts();
}

static void test_insert_updates_master(void **state)
{
    (void)state;
    highscore_table_t t = make_populated_table();
    highscore_io_insert(&t, 99999, 15, 7200, 1700000002UL, "New Boss");
    assert_string_equal(t.master_name, "New Boss");
}

/* =========================================================================
 * Group 6: Error handling
 * ========================================================================= */

static void test_read_nonexistent(void **state)
{
    (void)state;
    highscore_table_t t;
    highscore_io_result_t r = highscore_io_read("/tmp/xboing_no_such_file_XXXXXX.json", &t);
    assert_int_equal(r, HIGHSCORE_IO_ERR_OPEN);
    /* Table should still be initialized to defaults. */
    assert_string_equal(t.master_text, "Anyone play this game?");
}

static void test_read_corrupt_file(void **state)
{
    (void)state;
    /* Write garbage to the file. */
    FILE *fp = fopen(tmp_path, "w");
    assert_non_null(fp);
    fprintf(fp, "this is not json");
    fclose(fp);

    highscore_table_t t;
    highscore_io_result_t r = highscore_io_read(tmp_path, &t);
    assert_int_equal(r, HIGHSCORE_IO_ERR_READ);
}

/* =========================================================================
 * Group 7: Null safety
 * ========================================================================= */

static void test_null_safety(void **state)
{
    (void)state;
    assert_int_equal(highscore_io_read(NULL, NULL), HIGHSCORE_IO_ERR_NULL);
    assert_int_equal(highscore_io_write(NULL, NULL), HIGHSCORE_IO_ERR_NULL);
    highscore_io_sort(NULL);
    assert_int_equal(highscore_io_insert(NULL, 0, 0, 0, 0, NULL), HIGHSCORE_IO_ERR_NULL);
    assert_int_equal(highscore_io_get_ranking(NULL, 0), -1);
    assert_int_equal(highscore_io_would_be_global_master(NULL, 1, 1000), 0);
    assert_int_equal(highscore_io_insert_global_atomic(NULL, 0, 0, 0, 0, 0, NULL, NULL),
                     HIGHSCORE_IO_ERR_NULL);
    highscore_io_init_table(NULL);
}

/* =========================================================================
 * Group 8: would_be_global_master (dedup-aware wisdom-prompt gate)
 * ========================================================================= */

static void test_would_be_master_empty_table(void **state)
{
    (void)state;
    highscore_table_t t;
    highscore_io_init_table(&t);
    /* Empty table — any positive score lands at rank 0. */
    assert_int_equal(highscore_io_would_be_global_master(&t, 100, 1000), 1);
}

static void test_would_be_master_existing_lower(void **state)
{
    (void)state;
    highscore_table_t t;
    highscore_io_init_table(&t);
    /* Our existing #1 is 500; we beat it with 1000 — still master. */
    t.entries[0].score = 500;
    t.entries[0].user_id = 1000;
    snprintf(t.entries[0].name, sizeof(t.entries[0].name), "us");
    assert_int_equal(highscore_io_would_be_global_master(&t, 1000, 1000), 1);
}

static void test_would_be_master_existing_higher(void **state)
{
    (void)state;
    highscore_table_t t;
    highscore_io_init_table(&t);
    /* Our existing #1 is 5000; we attempt with 1000 — dedup rejects,
     * no wisdom prompt should fire. */
    t.entries[0].score = 5000;
    t.entries[0].user_id = 1000;
    assert_int_equal(highscore_io_would_be_global_master(&t, 1000, 1000), 0);
}

static void test_would_be_master_other_user_higher(void **state)
{
    (void)state;
    highscore_table_t t;
    highscore_io_init_table(&t);
    /* Another user holds #1 at 5000; we score 1000 — not master. */
    t.entries[0].score = 5000;
    t.entries[0].user_id = 999;
    assert_int_equal(highscore_io_would_be_global_master(&t, 1000, 1000), 0);
}

static void test_would_be_master_displaces_other_user(void **state)
{
    (void)state;
    highscore_table_t t;
    highscore_io_init_table(&t);
    /* Another user holds #1 at 5000; we beat them with 6000 — master. */
    t.entries[0].score = 5000;
    t.entries[0].user_id = 999;
    assert_int_equal(highscore_io_would_be_global_master(&t, 6000, 1000), 1);
}

/* =========================================================================
 * Group 9: insert_global_atomic — per-uid dedup + master gating
 * ========================================================================= */

static void cleanup_atomic_artifacts(void)
{
    char lock_path[300];
    snprintf(lock_path, sizeof(lock_path), "%s.lock", tmp_path);
    (void)remove(lock_path);
}

static void test_atomic_first_insert_writes_master(void **state)
{
    (void)state;
    /* Empty file at tmp_path (mkstemp created it).  First insert lands
     * at rank 0, master_name from `name`, master_text from caller. */
    highscore_io_result_t r = highscore_io_insert_global_atomic(
        tmp_path, 5000, 3, 120, 1700000000UL, 1000, "Alice", "Bravely done");
    assert_int_equal(r, HIGHSCORE_IO_OK);

    highscore_table_t out;
    assert_int_equal(highscore_io_read(tmp_path, &out), HIGHSCORE_IO_OK);
    assert_int_equal((int)out.entries[0].score, 5000);
    assert_int_equal((int)out.entries[0].user_id, 1000);
    assert_string_equal(out.entries[0].name, "Alice");
    assert_string_equal(out.master_name, "Alice");
    assert_string_equal(out.master_text, "Bravely done");

    cleanup_atomic_artifacts();
}

static void test_atomic_dedup_keeps_higher_score(void **state)
{
    (void)state;
    /* Seed our entry at score 5000 then try to insert 3000 with same
     * uid — must return NOT_RANKED, leave table untouched. */
    assert_int_equal(highscore_io_insert_global_atomic(tmp_path, 5000, 3, 120, 1700000000UL, 1000,
                                                       "Alice", "Wisdom 1"),
                     HIGHSCORE_IO_OK);
    highscore_io_result_t r = highscore_io_insert_global_atomic(
        tmp_path, 3000, 2, 60, 1700000100UL, 1000, "Alice", "Wisdom 2");
    assert_int_equal(r, HIGHSCORE_IO_ERR_NOT_RANKED);

    highscore_table_t out;
    assert_int_equal(highscore_io_read(tmp_path, &out), HIGHSCORE_IO_OK);
    assert_int_equal((int)out.entries[0].score, 5000);
    assert_string_equal(out.master_text, "Wisdom 1");

    cleanup_atomic_artifacts();
}

static void test_atomic_dedup_replaces_when_better(void **state)
{
    (void)state;
    /* Seed at 5000, beat with 9000 — old entry removed, new at rank 0. */
    assert_int_equal(highscore_io_insert_global_atomic(tmp_path, 5000, 3, 120, 1700000000UL, 1000,
                                                       "Alice", "Wisdom 1"),
                     HIGHSCORE_IO_OK);
    highscore_io_result_t r = highscore_io_insert_global_atomic(
        tmp_path, 9000, 7, 300, 1700000100UL, 1000, "Alice", "Wisdom 2");
    assert_int_equal(r, HIGHSCORE_IO_OK);

    highscore_table_t out;
    assert_int_equal(highscore_io_read(tmp_path, &out), HIGHSCORE_IO_OK);
    assert_int_equal((int)out.entries[0].score, 9000);
    /* Second row should NOT be Alice — dedup removed her old 5000 entry. */
    assert_int_not_equal((int)out.entries[1].user_id, 1000);
    assert_string_equal(out.master_text, "Wisdom 2");

    cleanup_atomic_artifacts();
}

static void test_atomic_master_text_default_when_null(void **state)
{
    (void)state;
    /* NULL master_text (cancelled wisdom dialog) → master_text reset
     * to the default placeholder rather than the previous master's
     * quote.  Otherwise the boing-master headline would mismatch. */
    assert_int_equal(highscore_io_insert_global_atomic(tmp_path, 5000, 3, 120, 1700000000UL, 999,
                                                       "PrevMaster", "Old wisdom"),
                     HIGHSCORE_IO_OK);
    highscore_io_result_t r = highscore_io_insert_global_atomic(
        tmp_path, 9000, 7, 300, 1700000100UL, 1000, "NewMaster", NULL);
    assert_int_equal(r, HIGHSCORE_IO_OK);

    highscore_table_t out;
    assert_int_equal(highscore_io_read(tmp_path, &out), HIGHSCORE_IO_OK);
    assert_string_equal(out.master_name, "NewMaster");
    assert_string_equal(out.master_text, "Anyone play this game?");

    cleanup_atomic_artifacts();
}

/* Regression: parser used to consume the closing `}` after an empty
 * `[]` entries array because the "skip to end of array" block ran a
 * second skip_ws after the inner loop had already eaten the `]`.
 * Symptom: reads of postinst-seeded empty-table files (or any file
 * whose entries array is empty and version != current) returned
 * ERR_READ instead of ERR_VERSION, and version validation never ran. */
static void test_read_empty_entries_array_parses(void **state)
{
    (void)state;
    FILE *fp = fopen(tmp_path, "w");
    assert_non_null(fp);
    fprintf(fp, "{\"version\": 1, \"master_name\": \"M\", \"master_text\": \"T\", \"entries\": []}");
    fclose(fp);

    highscore_table_t t;
    highscore_io_result_t r = highscore_io_read(tmp_path, &t);
    assert_int_equal(r, HIGHSCORE_IO_OK);
    assert_string_equal(t.master_name, "M");
    assert_string_equal(t.master_text, "T");
    assert_int_equal((int)t.entries[0].score, 0);
}

static void test_atomic_does_not_clobber_wrong_version(void **state)
{
    (void)state;
    /* Plant a wrong-version file; the atomic insert must refuse to
     * overwrite under the lock rather than initialise an empty table
     * and write the new entry as the sole row. */
    FILE *fp = fopen(tmp_path, "w");
    assert_non_null(fp);
    /* Use the canonical multi-line format the writer produces — matches
     * highscore_io_write's output but with version=99 instead of 1. */
    fprintf(fp, "{\n");
    fprintf(fp, "  \"version\": 99,\n");
    fprintf(fp, "  \"master_name\": \"\",\n");
    fprintf(fp, "  \"master_text\": \"\",\n");
    fprintf(fp, "  \"entries\": [\n");
    for (int i = 0; i < 10; i++)
    {
        fprintf(fp, "    {\"score\": 0, \"level\": 0, \"game_time\": 0, "
                    "\"timestamp\": 0, \"user_id\": 0, \"name\": \"\"}%s\n",
                i == 9 ? "" : ",");
    }
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");
    fclose(fp);

    highscore_io_result_t r = highscore_io_insert_global_atomic(
        tmp_path, 9000, 7, 300, 1700000100UL, 1000, "Player", "Quote");
    assert_int_equal(r, HIGHSCORE_IO_ERR_VERSION);

    cleanup_atomic_artifacts();
}

/* Control bytes injected through the global insert path must be stripped
 * before the entry hits disk.  Without this, a player's nickname can carry
 * ANSI/OSC escapes that fire in another player's terminal when running
 * `xboing -scores`. */
static void test_atomic_strips_control_bytes_from_name(void **state)
{
    (void)state;
    /* "\x1b]0;evil\x07Player" — OSC + name.  After sanitisation the
     * stored name should be just "0;evilPlayer" (the escape and BEL
     * are removed; the ']' and '0' are printable so they stay). */
    const char *malicious = "\x1b]0;evil\x07Player";
    assert_int_equal(highscore_io_insert_global_atomic(tmp_path, 5000, 3, 120, 1700000000UL, 1000,
                                                       malicious, "Quote\x1bQ"),
                     HIGHSCORE_IO_OK);

    highscore_table_t out;
    assert_int_equal(highscore_io_read(tmp_path, &out), HIGHSCORE_IO_OK);

    for (const char *p = out.entries[0].name; *p; p++)
    {
        unsigned char c = (unsigned char)*p;
        assert_true(c >= 0x20 && c != 0x7F && !(c >= 0x80 && c <= 0x9F));
    }
    assert_string_equal(out.entries[0].name, "]0;evilPlayer");

    for (const char *p = out.master_text; *p; p++)
    {
        unsigned char c = (unsigned char)*p;
        assert_true(c >= 0x20 && c != 0x7F && !(c >= 0x80 && c <= 0x9F));
    }
    assert_string_equal(out.master_text, "QuoteQ");

    cleanup_atomic_artifacts();
}

/* Personal-table insert sanitises the same way as the atomic global path. */
static void test_insert_strips_control_bytes_from_name(void **state)
{
    (void)state;
    highscore_table_t t;
    highscore_io_init_table(&t);
    assert_int_equal(
        highscore_io_insert(&t, 5000, 3, 120, 1700000000UL, "Eve\x1b]2;x\x07il"),
        HIGHSCORE_IO_OK);
    assert_string_equal(t.entries[0].name, "Eve]2;xil");
    assert_string_equal(t.master_name, "Eve]2;xil");
}

/* Read path scrubs control bytes too — defends against hand-edited
 * files containing decoded \uXXXX escapes that resolve to controls. */
static void test_read_strips_control_bytes(void **state)
{
    (void)state;
    FILE *fp = fopen(tmp_path, "w");
    assert_non_null(fp);
    /*  is ESC;  is DEL.  Place them in name, master_name,
     * and master_text. */
    fprintf(fp, "{\"version\": 1,"
                " \"master_name\": \"M\\u001bX\","
                " \"master_text\": \"T\\u007fY\","
                " \"entries\": ["
                " {\"score\": 100, \"level\": 1, \"game_time\": 1,"
                "  \"timestamp\": 1, \"user_id\": 1, \"name\": \"N\\u001bZ\"}"
                "]}");
    fclose(fp);

    highscore_table_t t;
    assert_int_equal(highscore_io_read(tmp_path, &t), HIGHSCORE_IO_OK);
    assert_string_equal(t.master_name, "MX");
    assert_string_equal(t.master_text, "TY");
    assert_string_equal(t.entries[0].name, "NZ");
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Table initialization */
        cmocka_unit_test(test_init_table),
        cmocka_unit_test(test_init_table_null),
        cmocka_unit_test(test_count_empty),
        cmocka_unit_test(test_count_populated),
        cmocka_unit_test(test_count_null),
        /* Group 2: Sorting */
        cmocka_unit_test(test_sort_already_sorted),
        cmocka_unit_test(test_sort_reversed),
        cmocka_unit_test(test_sort_zeros_at_end),
        /* Group 3: Insert and ranking */
        cmocka_unit_test(test_ranking_top),
        cmocka_unit_test(test_ranking_middle),
        cmocka_unit_test(test_ranking_not_placed),
        cmocka_unit_test(test_insert_shifts_down),
        cmocka_unit_test(test_predict_rank_empty),
        cmocka_unit_test(test_predict_rank_middle),
        cmocka_unit_test(test_predict_rank_tie_lands_behind),
        cmocka_unit_test(test_predict_rank_full_table_too_low),
        cmocka_unit_test(test_predict_rank_matches_insert),
        /* Group 4: Write and read round-trip */
        cmocka_unit_test_setup_teardown(test_write_read_roundtrip, setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_roundtrip_empty_table, setup_tmpfile,
                                        teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_roundtrip_special_chars, setup_tmpfile,
                                        teardown_tmpfile),
        /* Group 5: Edge cases */
        cmocka_unit_test(test_insert_into_full_table),
        cmocka_unit_test(test_insert_too_low),
        cmocka_unit_test(test_insert_tie_does_not_displace),
        cmocka_unit_test_setup_teardown(test_atomic_tie_does_not_displace_existing, setup_tmpfile,
                                        teardown_tmpfile),
        cmocka_unit_test(test_insert_updates_master),
        /* Group 6: Error handling */
        cmocka_unit_test(test_read_nonexistent),
        cmocka_unit_test_setup_teardown(test_read_corrupt_file, setup_tmpfile, teardown_tmpfile),
        /* Group 7: Null safety */
        cmocka_unit_test(test_null_safety),

        /* Group 8: would_be_global_master */
        cmocka_unit_test(test_would_be_master_empty_table),
        cmocka_unit_test(test_would_be_master_existing_lower),
        cmocka_unit_test(test_would_be_master_existing_higher),
        cmocka_unit_test(test_would_be_master_other_user_higher),
        cmocka_unit_test(test_would_be_master_displaces_other_user),

        /* Group 9: insert_global_atomic */
        cmocka_unit_test_setup_teardown(test_atomic_first_insert_writes_master, setup_tmpfile,
                                        teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_atomic_dedup_keeps_higher_score, setup_tmpfile,
                                        teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_atomic_dedup_replaces_when_better, setup_tmpfile,
                                        teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_atomic_master_text_default_when_null, setup_tmpfile,
                                        teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_read_empty_entries_array_parses, setup_tmpfile,
                                        teardown_tmpfile),
        cmocka_unit_test_setup_teardown(test_atomic_does_not_clobber_wrong_version, setup_tmpfile,
                                        teardown_tmpfile),

        /* Group 10: control-byte sanitisation */
        cmocka_unit_test_setup_teardown(test_atomic_strips_control_bytes_from_name, setup_tmpfile,
                                        teardown_tmpfile),
        cmocka_unit_test(test_insert_strips_control_bytes_from_name),
        cmocka_unit_test_setup_teardown(test_read_strips_control_bytes, setup_tmpfile,
                                        teardown_tmpfile),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
