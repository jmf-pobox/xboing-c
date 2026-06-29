/*
 * test_audio_name_validation.c — catch misnamed sound asset references
 * at CI time.  Verifies every string literal that production code
 * passes to sdl2_audio_play() resolves to an entry in the audio
 * cache loaded from sounds/.
 *
 * This is the test that would have caught the broken "hyperspace"
 * literal in PR #138.  Spec: docs/specs/2026-06-03-sfx-testability.md
 * (Component 3b + N1 deliverable Component 4).
 *
 * Peer review N1: the k_known_literals[] array MUST be kept in sync
 * with all string literals passed to sdl2_audio_play in src/.  The
 * `make audio-literals-check` Makefile target enforces this by diffing
 * source-grep output against the array below.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include <cmocka.h>

#include "block_sound.h"
#include "block_types.h"
#include "sdl2_audio.h"

/* =========================================================================
 * SDL setup / teardown — uses dummy audio driver so no hardware needed.
 * Working directory is the project root (set in tests/CMakeLists.txt).
 * ========================================================================= */

static int group_setup(void **state)
{
    (void)state;
    return SDL_Init(0) == 0 ? 0 : -1;
}

static int group_teardown(void **state)
{
    (void)state;
    SDL_Quit();
    return 0;
}

static int setup_audio(void **state)
{
    sdl2_audio_config_t cfg = sdl2_audio_config_defaults();
    sdl2_audio_status_t st;
    sdl2_audio_t *audio = sdl2_audio_create(&cfg, &st);
    if (!audio)
    {
        fprintf(stderr, "sdl2_audio_create failed: %s\n", sdl2_audio_status_string(st));
        return -1;
    }
    *state = audio;
    return 0;
}

static int teardown_audio(void **state)
{
    sdl2_audio_destroy((sdl2_audio_t *)*state);
    *state = NULL;
    return 0;
}

/* =========================================================================
 * Every name returned by block_sound_lookup resolves in the cache.
 *
 * This is the test that would have caught the broken "hyperspace"
 * literal: block_sound_lookup(HYPERSPACE_BLK).name used to return that
 * name, sdl2_audio_play would return SDL2A_ERR_NOT_FOUND, this assert
 * fails.
 * ========================================================================= */

static void test_every_block_sound_name_resolves(void **state)
{
    sdl2_audio_t *audio = (sdl2_audio_t *)*state;
    for (int t = 0; t < MAX_BLOCKS; t++)
    {
        const char *name = block_sound_lookup(t).name;
        if (name == NULL)
        {
            continue;
        }
        /* Free up channels between plays — the configured pool size
         * (16 under sdl2_audio_config_defaults()) is exceeded by this
         * loop, and an exhausted allocator would return
         * SDL2A_ERR_PLAY_FAILED even though the name resolves
         * correctly.  Halting between plays keeps the test focused
         * on name→asset resolution, not channel allocation. */
        sdl2_audio_halt(audio);
        sdl2_audio_status_t st = sdl2_audio_play(audio, name);
        if (st != SDL2A_OK)
        {
            fprintf(stderr, "block_type=%d → name=\"%s\" → status=%s\n", t, name,
                    sdl2_audio_status_string(st));
        }
        assert_int_equal(st, SDL2A_OK);
    }
}

/* =========================================================================
 * Every literal currently passed to sdl2_audio_play in src/.
 *
 * Generated and kept in sync via `make audio-literals-check`, which
 * diffs this array against scripts/audio-literals.sh output.  CI fails
 * on drift.
 *
 * KEEP SORTED to match the lint output.
 * ========================================================================= */

static const char *const k_known_literals[] = {
    "applause", "balllost", "bomb",    "buzzer", "game_over",
    "paddle",   "tone",     "toggle",  "youagod", NULL,
};

static void test_every_known_literal_resolves(void **state)
{
    sdl2_audio_t *audio = (sdl2_audio_t *)*state;
    for (const char *const *p = k_known_literals; *p; p++)
    {
        sdl2_audio_status_t st = sdl2_audio_play(audio, *p);
        if (st != SDL2A_OK)
        {
            fprintf(stderr, "literal=\"%s\" → status=%s\n", *p, sdl2_audio_status_string(st));
        }
        assert_int_equal(st, SDL2A_OK);
    }
}

/* =========================================================================
 * Replay-style: after firing a synthetic sequence of plays, the
 * audio call log records zero errors.  Direct exercise of the
 * Component 1 observability API.
 * ========================================================================= */

static void test_log_records_zero_errors_for_valid_names(void **state)
{
    sdl2_audio_t *audio = (sdl2_audio_t *)*state;
    sdl2_audio_log_clear(audio);
    for (int t = 0; t < MAX_BLOCKS; t++)
    {
        const char *name = block_sound_lookup(t).name;
        if (name)
        {
            sdl2_audio_halt(audio); /* see note above on channel pool */
            (void)sdl2_audio_play(audio, name);
        }
    }
    assert_int_equal(sdl2_audio_log_error_count(audio), 0);
}

static void test_log_records_misname(void **state)
{
    sdl2_audio_t *audio = (sdl2_audio_t *)*state;
    sdl2_audio_log_clear(audio);
    /* Deliberate misname: the asset is named "hypspc", not "hyperspace".
     * This is the exact bug PR #138 fixed.  We assert the observability
     * layer would have caught it. */
    sdl2_audio_status_t st = sdl2_audio_play(audio, "hyperspace");
    assert_int_equal(st, SDL2A_ERR_NOT_FOUND);
    assert_int_equal(sdl2_audio_log_error_count(audio), 1);

    sdl2_audio_call_t entries[4];
    int n = sdl2_audio_log_snapshot(audio, entries, 4);
    assert_int_equal(n, 1);
    assert_string_equal(entries[0].name, "hyperspace");
    assert_int_equal(entries[0].status, SDL2A_ERR_NOT_FOUND);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_every_block_sound_name_resolves, setup_audio,
                                        teardown_audio),
        cmocka_unit_test_setup_teardown(test_every_known_literal_resolves, setup_audio,
                                        teardown_audio),
        cmocka_unit_test_setup_teardown(test_log_records_zero_errors_for_valid_names, setup_audio,
                                        teardown_audio),
        cmocka_unit_test_setup_teardown(test_log_records_misname, setup_audio, teardown_audio),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
