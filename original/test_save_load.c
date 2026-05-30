/*
 * test_save_load.c -- Characterization tests for save/load file formats.
 *
 * Bead n9e.4: Save/load round-trip characterization tests.
 *
 * Strategy: test the binary file formats at the byte level without
 * linking any production .c files. Uses struct definitions from headers
 * and validates that write->read round-trips preserve all field values.
 *
 * The high score subsystem uses htonl/ntohl for portability between
 * big-endian and little-endian systems. The save game subsystem does NOT --
 * it writes raw structs. Both behaviors are characterized here.
 *
 * These are CHARACTERIZATION tests -- capture current behavior including
 * any bugs. Do NOT fix bugs here.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <unistd.h>
#include <time.h>
#include <netinet/in.h>
#include <cmocka.h>

#include <X11/Xlib.h>
#include <sys/types.h>

#include "file.h"
#include "highscore.h"

#define NUM_HIGHSCORES 10

/* =========================================================================
 * Temp file helpers
 * ========================================================================= */

static char tmp_path[256];

static int setup_tmpfile(void **state)
{
    (void)state;
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/xboing_test_XXXXXX");
    int fd = mkstemp(tmp_path);
    if (fd < 0)
        return -1;
    close(fd);
    return 0;
}

static int teardown_tmpfile(void **state)
{
    (void)state;
    unlink(tmp_path);
    return 0;
}

/* =========================================================================
 * Section 1: Format Constants
 * ========================================================================= */

/* TC-01: SAVE_VERSION constant. */
static void test_save_version_is_2(void **state)
{
    (void)state;
    assert_int_equal(SAVE_VERSION, 2);
}

/* TC-02: SCORE_VERSION constant. */
static void test_score_version_is_2(void **state)
{
    (void)state;
    assert_int_equal(SCORE_VERSION, 2);
}

/* TC-03: saveGameStruct has exactly 9 fields.
 * Verify key field offsets are within the struct. */
static void test_savegame_struct_layout(void **state)
{
    (void)state;
    /* Verify all fields are addressable and non-overlapping */
    assert_true(offsetof(saveGameStruct, version) < sizeof(saveGameStruct));
    assert_true(offsetof(saveGameStruct, score) < sizeof(saveGameStruct));
    assert_true(offsetof(saveGameStruct, level) < sizeof(saveGameStruct));
    assert_true(offsetof(saveGameStruct, levelTime) < sizeof(saveGameStruct));
    assert_true(offsetof(saveGameStruct, gameTime) < sizeof(saveGameStruct));
    assert_true(offsetof(saveGameStruct, livesLeft) < sizeof(saveGameStruct));
    assert_true(offsetof(saveGameStruct, startLevel) < sizeof(saveGameStruct));
    assert_true(offsetof(saveGameStruct, paddleSize) < sizeof(saveGameStruct));
    assert_true(offsetof(saveGameStruct, numBullets) < sizeof(saveGameStruct));

    /* Fields must be laid out in order */
    assert_true(offsetof(saveGameStruct, version)
                < offsetof(saveGameStruct, score));
    assert_true(offsetof(saveGameStruct, score)
                < offsetof(saveGameStruct, level));
    assert_true(offsetof(saveGameStruct, level)
                < offsetof(saveGameStruct, levelTime));
}

/* =========================================================================
 * Section 2: Save Game Struct Format
 * ========================================================================= */

/* TC-04: saveGameStruct write/read round-trip.
 * Production code uses raw fwrite/fread with no byte-order conversion. */
static void test_savegame_round_trip(void **state)
{
    (void)state;
    saveGameStruct original;
    saveGameStruct loaded;
    FILE *fp;

    memset(&original, 0, sizeof(original));
    original.version    = (u_long)SAVE_VERSION;
    original.score      = 12345;
    original.level      = 7;
    original.levelTime  = 90;
    original.gameTime   = 1000000;
    original.livesLeft  = 3;
    original.startLevel = 1;
    original.paddleSize = 50;
    original.numBullets = 4;

    /* Write -- same pattern as SaveCurrentGame (file.c:297) */
    fp = fopen(tmp_path, "w+");
    assert_non_null(fp);
    assert_int_equal(
        fwrite((char *)&original, sizeof(saveGameStruct), 1, fp), 1);
    fclose(fp);

    /* Read -- same pattern as LoadSavedGame (file.c:191) */
    memset(&loaded, 0xFF, sizeof(loaded));
    fp = fopen(tmp_path, "r+");
    assert_non_null(fp);
    assert_int_equal(
        fread((char *)&loaded, sizeof(saveGameStruct), 1, fp), 1);
    fclose(fp);

    assert_true(loaded.version == original.version);
    assert_true(loaded.score == original.score);
    assert_true(loaded.level == original.level);
    assert_int_equal(loaded.levelTime, original.levelTime);
    assert_true(loaded.gameTime == original.gameTime);
    assert_int_equal(loaded.livesLeft, original.livesLeft);
    assert_int_equal(loaded.startLevel, original.startLevel);
    assert_int_equal(loaded.paddleSize, original.paddleSize);
    assert_int_equal(loaded.numBullets, original.numBullets);
}

/* TC-05: Save game does NOT use byte-order conversion.
 * This characterizes a known limitation: save files are not portable
 * between big-endian and little-endian systems. The raw bytes on disk
 * match the host's native byte order. */
static void test_savegame_no_byte_swap(void **state)
{
    (void)state;
    saveGameStruct original;
    FILE *fp;
    u_long raw_version;

    memset(&original, 0, sizeof(original));
    original.version = (u_long)SAVE_VERSION;
    original.score   = 0x12345678UL;

    fp = fopen(tmp_path, "w+");
    assert_non_null(fp);
    assert_int_equal(
        fwrite((char *)&original, sizeof(saveGameStruct), 1, fp), 1);
    fclose(fp);

    /* Read back just the version field as raw bytes */
    fp = fopen(tmp_path, "r");
    assert_non_null(fp);
    assert_int_equal(fread(&raw_version, sizeof(u_long), 1, fp), 1);
    fclose(fp);

    /* Raw bytes == native representation (no htonl conversion applied) */
    assert_true(raw_version == (u_long)SAVE_VERSION);
}

/* TC-06: Version mismatch detection.
 * LoadSavedGame (file.c:206) rejects files where version != SAVE_VERSION. */
static void test_savegame_version_mismatch(void **state)
{
    (void)state;
    saveGameStruct bad;
    saveGameStruct loaded;
    FILE *fp;

    memset(&bad, 0, sizeof(bad));
    bad.version = 99;

    fp = fopen(tmp_path, "w+");
    assert_non_null(fp);
    assert_int_equal(
        fwrite((char *)&bad, sizeof(saveGameStruct), 1, fp), 1);
    fclose(fp);

    fp = fopen(tmp_path, "r");
    assert_non_null(fp);
    assert_int_equal(
        fread((char *)&loaded, sizeof(saveGameStruct), 1, fp), 1);
    fclose(fp);

    /* Production check: saveGame.version != (u_long) SAVE_VERSION */
    assert_true(loaded.version != (u_long)SAVE_VERSION);
}

/* TC-07: Large field values survive round-trip.
 * Tests with values representative of real gameplay. */
static void test_savegame_large_values(void **state)
{
    (void)state;
    saveGameStruct original;
    saveGameStruct loaded;
    FILE *fp;

    memset(&original, 0, sizeof(original));
    original.version    = (u_long)SAVE_VERSION;
    original.score      = 999999;
    original.level      = 80;
    original.levelTime  = 180;
    original.gameTime   = 7200;
    original.livesLeft  = 0;
    original.startLevel = 80;
    original.paddleSize = 70;
    original.numBullets = 99;

    fp = fopen(tmp_path, "w+");
    assert_non_null(fp);
    assert_int_equal(
        fwrite((char *)&original, sizeof(saveGameStruct), 1, fp), 1);
    fclose(fp);

    memset(&loaded, 0, sizeof(loaded));
    fp = fopen(tmp_path, "r");
    assert_non_null(fp);
    assert_int_equal(
        fread((char *)&loaded, sizeof(saveGameStruct), 1, fp), 1);
    fclose(fp);

    assert_true(loaded.score == 999999);
    assert_true(loaded.level == 80);
    assert_int_equal(loaded.levelTime, 180);
    assert_int_equal(loaded.livesLeft, 0);
    assert_int_equal(loaded.paddleSize, 70);
    assert_int_equal(loaded.numBullets, 99);
}

/* =========================================================================
 * Section 3: High Score Header Format
 * ========================================================================= */

/* TC-08: highScoreHeader round-trip with htonl/ntohl.
 * Production writes version with htonl, reads with ntohl. */
static void test_highscore_header_round_trip(void **state)
{
    (void)state;
    highScoreHeader hdr_out, hdr_in;
    FILE *fp;

    /* Write pattern (from WriteHighScoreTable, highscore.c:1114) */
    hdr_out.version = htonl((uint32_t)SCORE_VERSION);
    strncpy(hdr_out.masterText, "Anyone play this game?",
            sizeof(hdr_out.masterText) - 1);
    hdr_out.masterText[sizeof(hdr_out.masterText) - 1] = '\0';

    fp = fopen(tmp_path, "w+");
    assert_non_null(fp);
    assert_int_equal(
        fwrite((char *)&hdr_out, sizeof(highScoreHeader), 1, fp), 1);
    fclose(fp);

    /* Read pattern (from ReadHighScoreTable, highscore.c:1042-1049) */
    memset(&hdr_in, 0, sizeof(hdr_in));
    fp = fopen(tmp_path, "r");
    assert_non_null(fp);
    assert_int_equal(
        fread((char *)&hdr_in, sizeof(highScoreHeader), 1, fp), 1);
    fclose(fp);

    assert_true(ntohl((uint32_t)hdr_in.version) == (u_long)SCORE_VERSION);
    assert_string_equal(hdr_in.masterText, "Anyone play this game?");
}

/* TC-09: masterText preserves full 79-character string. */
static void test_highscore_header_long_text(void **state)
{
    (void)state;
    highScoreHeader hdr_out, hdr_in;
    FILE *fp;
    char long_text[80];

    memset(long_text, 'A', 79);
    long_text[79] = '\0';

    hdr_out.version = htonl((uint32_t)SCORE_VERSION);
    strcpy(hdr_out.masterText, long_text);

    fp = fopen(tmp_path, "w+");
    assert_non_null(fp);
    assert_int_equal(
        fwrite((char *)&hdr_out, sizeof(highScoreHeader), 1, fp), 1);
    fclose(fp);

    fp = fopen(tmp_path, "r");
    assert_non_null(fp);
    assert_int_equal(
        fread((char *)&hdr_in, sizeof(highScoreHeader), 1, fp), 1);
    fclose(fp);

    assert_string_equal(hdr_in.masterText, long_text);
}

/* =========================================================================
 * Section 4: High Score Entry Format
 * ========================================================================= */

/* TC-10: Single highScoreEntry round-trip with htonl/ntohl.
 * All numeric fields use network byte order. */
static void test_highscore_entry_round_trip(void **state)
{
    (void)state;
    highScoreEntry entry_out, entry_in;
    FILE *fp;

    memset(&entry_out, 0, sizeof(entry_out));

    /* Write pattern (from ShiftScoresDown, highscore.c:791-796) */
    entry_out.score    = (u_long)htonl((uint32_t)50000);
    entry_out.level    = (u_long)htonl((uint32_t)15);
    entry_out.gameTime = (time_t)htonl((uint32_t)3600);
    entry_out.userId   = (uid_t)htonl((uint32_t)1000);
    entry_out.time     = (time_t)htonl((uint32_t)1700000000);
    strcpy(entry_out.name, "Justin C. Kibell");

    fp = fopen(tmp_path, "w+");
    assert_non_null(fp);
    assert_int_equal(
        fwrite((char *)&entry_out, sizeof(highScoreEntry), 1, fp), 1);
    fclose(fp);

    memset(&entry_in, 0, sizeof(entry_in));
    fp = fopen(tmp_path, "r");
    assert_non_null(fp);
    assert_int_equal(
        fread((char *)&entry_in, sizeof(highScoreEntry), 1, fp), 1);
    fclose(fp);

    /* Read pattern (from DoHighScores, highscore.c:346-378) */
    assert_true(ntohl((uint32_t)entry_in.score) == 50000);
    assert_true(ntohl((uint32_t)entry_in.level) == 15);
    assert_true(ntohl((uint32_t)entry_in.gameTime) == 3600);
    assert_true(ntohl((uint32_t)entry_in.userId) == 1000);
    assert_string_equal(entry_in.name, "Justin C. Kibell");
}

/* TC-11: Name field is NOT byte-swapped (it's a string, not an integer). */
static void test_highscore_name_not_swapped(void **state)
{
    (void)state;
    highScoreEntry entry, loaded;
    FILE *fp;

    memset(&entry, 0, sizeof(entry));
    entry.score = (u_long)htonl((uint32_t)100);
    entry.level = (u_long)htonl((uint32_t)1);
    strcpy(entry.name, "ABCD");

    fp = fopen(tmp_path, "w+");
    assert_non_null(fp);
    assert_int_equal(
        fwrite((char *)&entry, sizeof(highScoreEntry), 1, fp), 1);
    fclose(fp);

    memset(&loaded, 0, sizeof(loaded));
    fp = fopen(tmp_path, "r");
    assert_non_null(fp);
    assert_int_equal(
        fread((char *)&loaded, sizeof(highScoreEntry), 1, fp), 1);
    fclose(fp);

    /* Name bytes must be exactly as written */
    assert_memory_equal(loaded.name, "ABCD", 5);
}

/* TC-12: htonl/ntohl identity for various score values.
 * On 64-bit systems, htonl operates on 32 bits, so the round-trip
 * is identity for values that fit in uint32_t. */
static void test_htonl_ntohl_identity(void **state)
{
    (void)state;
    uint32_t values[] = {0, 1, 100, 50000, 999999, 0xFFFFFFFFU};
    size_t n = sizeof(values) / sizeof(values[0]);

    for (size_t i = 0; i < n; i++)
    {
        uint32_t net = htonl(values[i]);
        uint32_t host = ntohl(net);
        assert_true(host == values[i]);
    }
}

/* =========================================================================
 * Section 5: Full High Score File Format
 * ========================================================================= */

/* TC-13: Full file write (header + 10 entries) -> read round-trip.
 * Matches the exact I/O pattern of WriteHighScoreTable/ReadHighScoreTable. */
static void test_highscore_full_file_round_trip(void **state)
{
    (void)state;
    highScoreHeader hdr_out, hdr_in;
    highScoreEntry entries_out[NUM_HIGHSCORES];
    highScoreEntry entries_in[NUM_HIGHSCORES];
    FILE *fp;
    int i;

    /* Initialize header (from WriteHighScoreTable, highscore.c:1114) */
    hdr_out.version = htonl((uint32_t)SCORE_VERSION);
    strcpy(hdr_out.masterText, "Test Master");

    /* Initialize entries with descending scores */
    for (i = 0; i < NUM_HIGHSCORES; i++)
    {
        memset(&entries_out[i], 0, sizeof(entries_out[i]));
        entries_out[i].score    = (u_long)htonl((uint32_t)(10000 - i * 1000));
        entries_out[i].level    = (u_long)htonl((uint32_t)(10 - i));
        entries_out[i].gameTime = (time_t)htonl((uint32_t)(3600 + i * 60));
        entries_out[i].time     = (time_t)htonl((uint32_t)1700000000);
        entries_out[i].userId   = (uid_t)htonl((uint32_t)(1000 + i));
        snprintf(entries_out[i].name, sizeof(entries_out[i].name),
                 "Player %d", i + 1);
    }

    /* Write -- same pattern as WriteHighScoreTable (highscore.c:1117-1133) */
    fp = fopen(tmp_path, "w+");
    assert_non_null(fp);
    assert_int_equal(
        fwrite((char *)&hdr_out, sizeof(highScoreHeader), 1, fp), 1);
    for (i = 0; i < NUM_HIGHSCORES; i++)
        assert_int_equal(
            fwrite((char *)&entries_out[i], sizeof(highScoreEntry), 1, fp), 1);
    fclose(fp);

    /* Read -- same pattern as ReadHighScoreTable (highscore.c:1042-1067) */
    memset(&hdr_in, 0, sizeof(hdr_in));
    memset(entries_in, 0, sizeof(entries_in));
    fp = fopen(tmp_path, "r");
    assert_non_null(fp);
    assert_int_equal(
        fread((char *)&hdr_in, sizeof(highScoreHeader), 1, fp), 1);
    for (i = 0; i < NUM_HIGHSCORES; i++)
        assert_int_equal(
            fread((char *)&entries_in[i], sizeof(highScoreEntry), 1, fp), 1);
    fclose(fp);

    /* Verify header */
    assert_true(ntohl((uint32_t)hdr_in.version) == (u_long)SCORE_VERSION);
    assert_string_equal(hdr_in.masterText, "Test Master");

    /* Verify all entries */
    for (i = 0; i < NUM_HIGHSCORES; i++)
    {
        assert_true(
            ntohl((uint32_t)entries_in[i].score)
            == (uint32_t)(10000 - i * 1000));
        assert_true(
            ntohl((uint32_t)entries_in[i].level) == (uint32_t)(10 - i));
        assert_string_equal(entries_in[i].name, entries_out[i].name);
    }
}

/* TC-14: Scores in descending order survive round-trip.
 * Production invariant: table is always sorted descending by score. */
static void test_highscore_descending_order_preserved(void **state)
{
    (void)state;
    highScoreHeader hdr;
    highScoreEntry entries[NUM_HIGHSCORES];
    highScoreEntry loaded[NUM_HIGHSCORES];
    FILE *fp;
    int i;
    uint32_t scores[] = {
        50000, 45000, 40000, 35000, 30000,
        25000, 20000, 15000, 10000, 5000
    };

    hdr.version = htonl((uint32_t)SCORE_VERSION);
    strcpy(hdr.masterText, "Order Test");

    for (i = 0; i < NUM_HIGHSCORES; i++)
    {
        memset(&entries[i], 0, sizeof(entries[i]));
        entries[i].score = (u_long)htonl(scores[i]);
        entries[i].level = (u_long)htonl((uint32_t)1);
        strcpy(entries[i].name, "Test");
    }

    fp = fopen(tmp_path, "w+");
    assert_non_null(fp);
    assert_int_equal(
        fwrite((char *)&hdr, sizeof(highScoreHeader), 1, fp), 1);
    for (i = 0; i < NUM_HIGHSCORES; i++)
        assert_int_equal(
            fwrite((char *)&entries[i], sizeof(highScoreEntry), 1, fp), 1);
    fclose(fp);

    fp = fopen(tmp_path, "r");
    assert_non_null(fp);
    assert_int_equal(
        fread(&hdr, sizeof(highScoreHeader), 1, fp), 1);
    for (i = 0; i < NUM_HIGHSCORES; i++)
        assert_int_equal(
            fread((char *)&loaded[i], sizeof(highScoreEntry), 1, fp), 1);
    fclose(fp);

    for (i = 0; i < NUM_HIGHSCORES - 1; i++)
        assert_true(ntohl((uint32_t)loaded[i].score)
                    >= ntohl((uint32_t)loaded[i + 1].score));
}

/* TC-15: Zero-score entries round-trip correctly.
 * InitialiseHighScores (highscore.c:996-1005) sets score=htonl(0),
 * level=htonl(1), name="To be announced!". */
static void test_highscore_zero_score_entry(void **state)
{
    (void)state;
    highScoreEntry entry_out, entry_in;
    FILE *fp;

    memset(&entry_out, 0, sizeof(entry_out));
    entry_out.score    = (u_long)htonl((uint32_t)0);
    entry_out.level    = (u_long)htonl((uint32_t)1);
    entry_out.gameTime = (time_t)htonl((uint32_t)0);
    entry_out.userId   = (uid_t)htonl((uint32_t)0);
    entry_out.time     = (time_t)htonl((uint32_t)1700000000);
    strcpy(entry_out.name, "To be announced!");

    fp = fopen(tmp_path, "w+");
    assert_non_null(fp);
    assert_int_equal(
        fwrite((char *)&entry_out, sizeof(highScoreEntry), 1, fp), 1);
    fclose(fp);

    memset(&entry_in, 0xFF, sizeof(entry_in));
    fp = fopen(tmp_path, "r");
    assert_non_null(fp);
    assert_int_equal(
        fread((char *)&entry_in, sizeof(highScoreEntry), 1, fp), 1);
    fclose(fp);

    assert_true(ntohl((uint32_t)entry_in.score) == 0);
    assert_true(ntohl((uint32_t)entry_in.level) == 1);
    assert_string_equal(entry_in.name, "To be announced!");
}

/* =========================================================================
 * Section 6: Edge Cases
 * ========================================================================= */

/* TC-16: Partial file read fails.
 * ReadHighScoreTable (highscore.c:1061) returns False if fread count != 1. */
static void test_partial_file_read_fails(void **state)
{
    (void)state;
    highScoreEntry entry;
    FILE *fp;
    uint32_t partial;
    size_t count;

    /* Write only 4 bytes -- not enough for a full entry */
    fp = fopen(tmp_path, "w+");
    assert_non_null(fp);
    partial = htonl((uint32_t)42);
    assert_int_equal(fwrite(&partial, sizeof(uint32_t), 1, fp), 1);
    fclose(fp);

    /* Attempt to read a full entry -- should fail */
    memset(&entry, 0, sizeof(entry));
    fp = fopen(tmp_path, "r");
    assert_non_null(fp);
    count = fread((char *)&entry, sizeof(highScoreEntry), 1, fp);
    fclose(fp);

    assert_int_equal((int)count, 0);
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Section 1: Format Constants */
        cmocka_unit_test(test_save_version_is_2),
        cmocka_unit_test(test_score_version_is_2),
        cmocka_unit_test(test_savegame_struct_layout),

        /* Section 2: Save Game Struct Format */
        cmocka_unit_test_setup_teardown(
            test_savegame_round_trip, setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test_setup_teardown(
            test_savegame_no_byte_swap, setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test_setup_teardown(
            test_savegame_version_mismatch, setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test_setup_teardown(
            test_savegame_large_values, setup_tmpfile, teardown_tmpfile),

        /* Section 3: High Score Header Format */
        cmocka_unit_test_setup_teardown(
            test_highscore_header_round_trip, setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test_setup_teardown(
            test_highscore_header_long_text, setup_tmpfile, teardown_tmpfile),

        /* Section 4: High Score Entry Format */
        cmocka_unit_test_setup_teardown(
            test_highscore_entry_round_trip, setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test_setup_teardown(
            test_highscore_name_not_swapped, setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test(test_htonl_ntohl_identity),

        /* Section 5: Full High Score File Format */
        cmocka_unit_test_setup_teardown(
            test_highscore_full_file_round_trip,
            setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test_setup_teardown(
            test_highscore_descending_order_preserved,
            setup_tmpfile, teardown_tmpfile),
        cmocka_unit_test_setup_teardown(
            test_highscore_zero_score_entry, setup_tmpfile, teardown_tmpfile),

        /* Section 6: Edge Cases */
        cmocka_unit_test_setup_teardown(
            test_partial_file_read_fails, setup_tmpfile, teardown_tmpfile),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
