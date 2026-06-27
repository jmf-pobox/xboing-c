/*
 * test_sdl2_audio.c — CMocka tests for SDL2_mixer audio module.
 *
 * Uses SDL_AUDIODRIVER=dummy for headless CI testing.
 * Loads real WAV files from the project's sounds/ directory.
 *
 * Bead: xboing-0lu.1
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "sdl2_audio.h"

/* =========================================================================
 * Group fixtures — SDL init/teardown
 * ========================================================================= */

static int group_setup_sdl(void **state)
{
    (void)state;
    if (SDL_Init(0) != 0)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    return 0;
}

static int group_teardown_sdl(void **state)
{
    (void)state;
    SDL_Quit();
    return 0;
}

/* =========================================================================
 * Per-test fixtures — audio context lifecycle
 * ========================================================================= */

static int setup_audio_ctx(void **state)
{
    sdl2_audio_config_t cfg = sdl2_audio_config_defaults();
    sdl2_audio_status_t st;
    sdl2_audio_t *ctx = sdl2_audio_create(&cfg, &st);
    if (ctx == NULL)
    {
        fprintf(stderr, "sdl2_audio_create failed: %s\n", sdl2_audio_status_string(st));
        return -1;
    }
    *state = ctx;
    return 0;
}

static int teardown_audio_ctx(void **state)
{
    sdl2_audio_destroy((sdl2_audio_t *)*state);
    *state = NULL;
    return 0;
}

/* =========================================================================
 * Group 1: Config defaults
 * ========================================================================= */

static void test_config_defaults_sound_dir(void **state)
{
    (void)state;
    sdl2_audio_config_t cfg = sdl2_audio_config_defaults();
    assert_string_equal(cfg.sound_dir, "sounds");
}

static void test_config_defaults_frequency(void **state)
{
    (void)state;
    sdl2_audio_config_t cfg = sdl2_audio_config_defaults();
    assert_int_equal(cfg.frequency, 44100);
}

static void test_config_defaults_channels(void **state)
{
    (void)state;
    sdl2_audio_config_t cfg = sdl2_audio_config_defaults();
    assert_int_equal(cfg.channels, 16);
}

static void test_config_defaults_volume(void **state)
{
    (void)state;
    sdl2_audio_config_t cfg = sdl2_audio_config_defaults();
    assert_int_equal(cfg.volume, MIX_MAX_VOLUME);
}

/* =========================================================================
 * Group 2: Error handling
 * ========================================================================= */

static void test_create_null_config(void **state)
{
    (void)state;
    sdl2_audio_status_t st;
    sdl2_audio_t *ctx = sdl2_audio_create(NULL, &st);
    assert_null(ctx);
    assert_int_equal(st, SDL2A_ERR_NULL_ARG);
}

static void test_create_bad_dir(void **state)
{
    (void)state;
    sdl2_audio_config_t cfg = sdl2_audio_config_defaults();
    cfg.sound_dir = "/nonexistent/path/to/sounds";
    sdl2_audio_status_t st;
    sdl2_audio_t *ctx = sdl2_audio_create(&cfg, &st);
    assert_null(ctx);
    assert_int_equal(st, SDL2A_ERR_SCAN_FAILED);
}

static void test_destroy_null(void **state)
{
    (void)state;
    sdl2_audio_destroy(NULL); /* must not crash */
}

static void test_play_null_ctx(void **state)
{
    (void)state;
    sdl2_audio_status_t st = sdl2_audio_play(NULL, "boing");
    assert_int_equal(st, SDL2A_ERR_NULL_ARG);
}

static void test_play_null_name(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_status_t st = sdl2_audio_play(ctx, NULL);
    assert_int_equal(st, SDL2A_ERR_NULL_ARG);
}

static void test_play_not_found(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_status_t st = sdl2_audio_play(ctx, "nonexistent_sound_xyz");
    assert_int_equal(st, SDL2A_ERR_NOT_FOUND);
}

/* =========================================================================
 * Group 3: Lifecycle and caching
 * ========================================================================= */

static void test_create_destroy(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    assert_non_null(ctx);
    /* teardown_audio_ctx verifies destroy doesn't crash */
}

static void test_count_positive(void **state)
{
    const sdl2_audio_t *ctx = (const sdl2_audio_t *)*state;
    int count = sdl2_audio_count(ctx);
    assert_true(count > 0);
}

static void test_count_matches_wav_files(void **state)
{
    const sdl2_audio_t *ctx = (const sdl2_audio_t *)*state;
    int count = sdl2_audio_count(ctx);
    /* sounds/ has 46 .wav files; allow range for robustness */
    assert_true(count >= 40);
    assert_true(count <= SDL2A_MAX_SOUNDS);
}

static void test_count_null_ctx(void **state)
{
    (void)state;
    assert_int_equal(sdl2_audio_count(NULL), 0);
}

/* =========================================================================
 * Group 4: Playback
 * ========================================================================= */

static void test_play_known_sound(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_status_t st = sdl2_audio_play(ctx, "boing");
    assert_int_equal(st, SDL2A_OK);
}

static void test_play_multiple_sounds(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    assert_int_equal(sdl2_audio_play(ctx, "boing"), SDL2A_OK);
    assert_int_equal(sdl2_audio_play(ctx, "paddle"), SDL2A_OK);
    assert_int_equal(sdl2_audio_play(ctx, "bomb"), SDL2A_OK);
}

static void test_halt_no_crash(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_play(ctx, "boing");
    sdl2_audio_halt(ctx);
    /* Must not crash; halt NULL also safe */
    sdl2_audio_halt(NULL);
}

/* =========================================================================
 * Group 5: Volume control
 * ========================================================================= */

static void test_volume_default(void **state)
{
    const sdl2_audio_t *ctx = (const sdl2_audio_t *)*state;
    assert_int_equal(sdl2_audio_get_volume(ctx), MIX_MAX_VOLUME);
}

static void test_volume_set_get(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_set_volume(ctx, 64);
    assert_int_equal(sdl2_audio_get_volume(ctx), 64);
}

static void test_volume_clamp_low(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_set_volume(ctx, -10);
    assert_int_equal(sdl2_audio_get_volume(ctx), 0);
}

static void test_volume_clamp_high(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_set_volume(ctx, 999);
    assert_int_equal(sdl2_audio_get_volume(ctx), MIX_MAX_VOLUME);
}

static void test_volume_null_ctx(void **state)
{
    (void)state;
    sdl2_audio_set_volume(NULL, 64); /* must not crash */
    assert_int_equal(sdl2_audio_get_volume(NULL), 0);
}

/* =========================================================================
 * Group 6: Volume percentage API
 * ========================================================================= */

static void test_volume_percent_default(void **state)
{
    const sdl2_audio_t *ctx = (const sdl2_audio_t *)*state;
    /* Default volume is MIX_MAX_VOLUME (128), which maps to 100%. */
    assert_int_equal(sdl2_audio_get_volume_percent(ctx), 100);
}

static void test_volume_percent_set_get(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_set_volume_percent(ctx, 50);
    assert_int_equal(sdl2_audio_get_volume_percent(ctx), 50);
}

static void test_volume_percent_zero(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_set_volume_percent(ctx, 0);
    assert_int_equal(sdl2_audio_get_volume_percent(ctx), 0);
    assert_int_equal(sdl2_audio_get_volume(ctx), 0);
}

static void test_volume_percent_full(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_set_volume_percent(ctx, 100);
    assert_int_equal(sdl2_audio_get_volume_percent(ctx), 100);
    assert_int_equal(sdl2_audio_get_volume(ctx), MIX_MAX_VOLUME);
}

static void test_volume_percent_clamp_low(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_set_volume_percent(ctx, -10);
    assert_int_equal(sdl2_audio_get_volume_percent(ctx), 0);
}

static void test_volume_percent_clamp_high(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_set_volume_percent(ctx, 200);
    assert_int_equal(sdl2_audio_get_volume_percent(ctx), 100);
}

static void test_volume_percent_round_trip(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    /* Test that set→get round-trips for all valid percentages. */
    for (int pct = 0; pct <= 100; pct++)
    {
        sdl2_audio_set_volume_percent(ctx, pct);
        int got = sdl2_audio_get_volume_percent(ctx);
        /* Allow ±1 rounding tolerance due to integer 0-100 ↔ 0-128 mapping. */
        assert_true(got >= pct - 1 && got <= pct + 1);
    }
}

static void test_volume_percent_null_ctx(void **state)
{
    (void)state;
    sdl2_audio_set_volume_percent(NULL, 50); /* must not crash */
    assert_int_equal(sdl2_audio_get_volume_percent(NULL), 0);
}

static void test_volume_up(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_set_volume_percent(ctx, 50);
    int pct = sdl2_audio_volume_up(ctx);
    assert_int_equal(pct, 51);
}

static void test_volume_down(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_set_volume_percent(ctx, 50);
    int pct = sdl2_audio_volume_down(ctx);
    assert_int_equal(pct, 49);
}

static void test_volume_up_at_max(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_set_volume_percent(ctx, 100);
    int pct = sdl2_audio_volume_up(ctx);
    assert_int_equal(pct, 100);
}

static void test_volume_down_at_min(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_set_volume_percent(ctx, 0);
    int pct = sdl2_audio_volume_down(ctx);
    assert_int_equal(pct, 0);
}

static void test_volume_up_null(void **state)
{
    (void)state;
    assert_int_equal(sdl2_audio_volume_up(NULL), 0);
}

static void test_volume_down_null(void **state)
{
    (void)state;
    assert_int_equal(sdl2_audio_volume_down(NULL), 0);
}

/* =========================================================================
 * Group 7: Status strings
 * ========================================================================= */

static void test_status_strings_all_valid(void **state)
{
    (void)state;
    sdl2_audio_status_t codes[] = {
        SDL2A_OK,
        SDL2A_ERR_NULL_ARG,
        SDL2A_ERR_INIT_FAILED,
        SDL2A_ERR_NOT_FOUND,
        SDL2A_ERR_LOAD_FAILED,
        SDL2A_ERR_CACHE_FULL,
        SDL2A_ERR_KEY_TOO_LONG,
        SDL2A_ERR_SCAN_FAILED,
        SDL2A_ERR_PLAY_FAILED,
    };
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
    {
        const char *s = sdl2_audio_status_string(codes[i]);
        assert_non_null(s);
        assert_true(strlen(s) > 0);
    }
}

static void test_status_string_unknown(void **state)
{
    (void)state;
    const char *s = sdl2_audio_status_string((sdl2_audio_status_t)999);
    assert_non_null(s);
    assert_string_equal(s, "unknown status");
}

/* =========================================================================
 * Group 8: Call log ring buffer (Component 1 — bead xboing-c-cfy)
 *
 * Covers the four cases the spec called out: snapshot order, clear
 * resets count, error counter is accurate across wraps, and the
 * buffer wraps cleanly at SDL2A_LOG_CAPACITY.
 * ========================================================================= */

static void test_log_starts_empty(void **state)
{
    const sdl2_audio_t *ctx = (const sdl2_audio_t *)*state;
    sdl2_audio_call_t entries[8];
    assert_int_equal(sdl2_audio_log_snapshot(ctx, entries, 8), 0);
    assert_int_equal(sdl2_audio_log_error_count(ctx), 0);
}

static void test_log_records_each_play(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_log_clear(ctx);
    (void)sdl2_audio_play(ctx, "boing");
    (void)sdl2_audio_play(ctx, "paddle");
    (void)sdl2_audio_play(ctx, "bomb");

    sdl2_audio_call_t entries[8];
    int n = sdl2_audio_log_snapshot(ctx, entries, 8);
    assert_int_equal(n, 3);
    /* Snapshot returns oldest-first. */
    assert_string_equal(entries[0].name, "boing");
    assert_string_equal(entries[1].name, "paddle");
    assert_string_equal(entries[2].name, "bomb");
    assert_int_equal(entries[0].status, SDL2A_OK);
    assert_int_equal(entries[1].status, SDL2A_OK);
    assert_int_equal(entries[2].status, SDL2A_OK);
}

static void test_log_clear_resets(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    (void)sdl2_audio_play(ctx, "boing");
    (void)sdl2_audio_play(ctx, "nonexistent_xyz");
    assert_int_equal(sdl2_audio_log_error_count(ctx), 1);
    sdl2_audio_log_clear(ctx);
    assert_int_equal(sdl2_audio_log_error_count(ctx), 0);

    sdl2_audio_call_t entries[4];
    assert_int_equal(sdl2_audio_log_snapshot(ctx, entries, 4), 0);
}

static void test_log_wraps_at_capacity(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_log_clear(ctx);

    /* Fill exactly to capacity with successful plays.  Halt the
     * channel pool between plays so the channel allocator always
     * has a slot; otherwise tight loops exhaust the configured pool
     * (16 channels under sdl2_audio_config_defaults()) and the
     * overflow plays return SDL2A_ERR_PLAY_FAILED. */
    for (int i = 0; i < SDL2A_LOG_CAPACITY; i++)
    {
        sdl2_audio_halt(ctx);
        (void)sdl2_audio_play(ctx, "boing");
    }
    assert_int_equal(sdl2_audio_log_error_count(ctx), 0);

    sdl2_audio_call_t entries[SDL2A_LOG_CAPACITY];
    int n = sdl2_audio_log_snapshot(ctx, entries, SDL2A_LOG_CAPACITY);
    assert_int_equal(n, SDL2A_LOG_CAPACITY);

    /* Overflow by 5 — older entries are discarded. */
    for (int i = 0; i < 5; i++)
    {
        sdl2_audio_halt(ctx);
        (void)sdl2_audio_play(ctx, "paddle");
    }
    n = sdl2_audio_log_snapshot(ctx, entries, SDL2A_LOG_CAPACITY);
    assert_int_equal(n, SDL2A_LOG_CAPACITY);
    /* Newest entry (at the end) should be the most recent push. */
    assert_string_equal(entries[SDL2A_LOG_CAPACITY - 1].name, "paddle");
    /* The 5 oldest "boing" entries got overwritten — the first
     * surviving entry should still be a "boing" (entries 5..254 of
     * the original burst). */
    assert_string_equal(entries[0].name, "boing");
}

static void test_log_error_count_accurate_after_wrap(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_log_clear(ctx);

    /* Fill with errors first, then wrap with successes.  After the
     * wrap, the live window contains zero errors — but a naive
     * cached counter would still report N errors from the discarded
     * entries.  This is the exact failure mode from the local review. */
    for (int i = 0; i < SDL2A_LOG_CAPACITY; i++)
    {
        (void)sdl2_audio_play(ctx, "nonexistent_xyz");
    }
    assert_int_equal(sdl2_audio_log_error_count(ctx), SDL2A_LOG_CAPACITY);

    /* Overwrite every error slot with a successful play.  Halt the
     * channel pool between plays so the allocator always has a slot. */
    for (int i = 0; i < SDL2A_LOG_CAPACITY; i++)
    {
        sdl2_audio_halt(ctx);
        (void)sdl2_audio_play(ctx, "boing");
    }
    /* Live window is now all successes; error count must be 0. */
    assert_int_equal(sdl2_audio_log_error_count(ctx), 0);
}

static void test_log_snapshot_cap_smaller_than_count(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_log_clear(ctx);
    (void)sdl2_audio_play(ctx, "boing");
    (void)sdl2_audio_play(ctx, "paddle");
    (void)sdl2_audio_play(ctx, "bomb");

    /* Request only 2 — caller sees the most recent 2 (oldest first). */
    sdl2_audio_call_t entries[2];
    int n = sdl2_audio_log_snapshot(ctx, entries, 2);
    assert_int_equal(n, 2);
    assert_string_equal(entries[0].name, "paddle");
    assert_string_equal(entries[1].name, "bomb");
}

static void test_log_null_safety(void **state)
{
    (void)state;
    sdl2_audio_call_t entries[4];
    assert_int_equal(sdl2_audio_log_snapshot(NULL, entries, 4), 0);
    assert_int_equal(sdl2_audio_log_error_count(NULL), 0);
    sdl2_audio_log_clear(NULL); /* must not crash */
}

/* =========================================================================
 * Group N: Per-call volume (sdl2_audio_play_at_percent)
 * ========================================================================= */

static void test_play_at_percent_null_ctx(void **state)
{
    (void)state;
    sdl2_audio_status_t st = sdl2_audio_play_at_percent(NULL, "boing", 50);
    assert_int_equal(st, SDL2A_ERR_NULL_ARG);
}

static void test_play_at_percent_null_name(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_status_t st = sdl2_audio_play_at_percent(ctx, NULL, 50);
    assert_int_equal(st, SDL2A_ERR_NULL_ARG);
}

static void test_play_at_percent_not_found(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_status_t st = sdl2_audio_play_at_percent(ctx, "nonexistent_sound_xyz", 50);
    assert_int_equal(st, SDL2A_ERR_NOT_FOUND);
}

static void test_play_at_percent_known_sound(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_status_t st = sdl2_audio_play_at_percent(ctx, "boing", 50);
    assert_int_equal(st, SDL2A_OK);
}

/* Per-call volume must not mutate the master volume — the contract is
 * "percent of master" not "set master." */
static void test_play_at_percent_does_not_mutate_master(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_set_volume_percent(ctx, 80);
    int before = sdl2_audio_get_volume_percent(ctx);
    /* Assert each play succeeds — otherwise the no-mutation check
     * would still pass even if Mix_PlayChannel never ran (e.g. a
     * channel-pool exhaustion), weakening the regression signal. */
    assert_int_equal(sdl2_audio_play_at_percent(ctx, "boing", 10), SDL2A_OK);
    assert_int_equal(sdl2_audio_play_at_percent(ctx, "boing", 90), SDL2A_OK);
    int after = sdl2_audio_get_volume_percent(ctx);
    assert_int_equal(before, after);
}

/* Out-of-range percent values must clamp without error. */
static void test_play_at_percent_clamps_negative(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_status_t st = sdl2_audio_play_at_percent(ctx, "boing", -25);
    assert_int_equal(st, SDL2A_OK);
}

static void test_play_at_percent_clamps_over_max(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_status_t st = sdl2_audio_play_at_percent(ctx, "boing", 150);
    assert_int_equal(st, SDL2A_OK);
}

/* Zero percent is silent but still a successful play. */
static void test_play_at_percent_zero(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_status_t st = sdl2_audio_play_at_percent(ctx, "boing", 0);
    assert_int_equal(st, SDL2A_OK);
}

/* When muted, sdl2_audio_play_at_percent returns OK and logs a no-op,
 * matching sdl2_audio_play's behavior. */
static void test_play_at_percent_muted_returns_ok(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    sdl2_audio_set_muted(ctx, true);
    sdl2_audio_status_t st = sdl2_audio_play_at_percent(ctx, "boing", 50);
    assert_int_equal(st, SDL2A_OK);
    sdl2_audio_set_muted(ctx, false);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest config_tests[] = {
        cmocka_unit_test(test_config_defaults_sound_dir),
        cmocka_unit_test(test_config_defaults_frequency),
        cmocka_unit_test(test_config_defaults_channels),
        cmocka_unit_test(test_config_defaults_volume),
    };

    const struct CMUnitTest error_tests[] = {
        cmocka_unit_test(test_create_null_config),
        cmocka_unit_test(test_create_bad_dir),
        cmocka_unit_test(test_destroy_null),
        cmocka_unit_test(test_play_null_ctx),
        cmocka_unit_test_setup_teardown(test_play_null_name, setup_audio_ctx, teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_play_not_found, setup_audio_ctx, teardown_audio_ctx),
    };

    const struct CMUnitTest lifecycle_tests[] = {
        cmocka_unit_test_setup_teardown(test_create_destroy, setup_audio_ctx, teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_count_positive, setup_audio_ctx, teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_count_matches_wav_files, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test(test_count_null_ctx),
    };

    const struct CMUnitTest playback_tests[] = {
        cmocka_unit_test_setup_teardown(test_play_known_sound, setup_audio_ctx, teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_play_multiple_sounds, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_halt_no_crash, setup_audio_ctx, teardown_audio_ctx),
    };

    const struct CMUnitTest volume_tests[] = {
        cmocka_unit_test_setup_teardown(test_volume_default, setup_audio_ctx, teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_volume_set_get, setup_audio_ctx, teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_volume_clamp_low, setup_audio_ctx, teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_volume_clamp_high, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test(test_volume_null_ctx),
    };

    const struct CMUnitTest volume_percent_tests[] = {
        cmocka_unit_test_setup_teardown(test_volume_percent_default, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_volume_percent_set_get, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_volume_percent_zero, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_volume_percent_full, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_volume_percent_clamp_low, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_volume_percent_clamp_high, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_volume_percent_round_trip, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test(test_volume_percent_null_ctx),
        cmocka_unit_test_setup_teardown(test_volume_up, setup_audio_ctx, teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_volume_down, setup_audio_ctx, teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_volume_up_at_max, setup_audio_ctx, teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_volume_down_at_min, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test(test_volume_up_null),
        cmocka_unit_test(test_volume_down_null),
    };

    const struct CMUnitTest play_at_percent_tests[] = {
        cmocka_unit_test(test_play_at_percent_null_ctx),
        cmocka_unit_test_setup_teardown(test_play_at_percent_null_name, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_play_at_percent_not_found, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_play_at_percent_known_sound, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_play_at_percent_does_not_mutate_master,
                                        setup_audio_ctx, teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_play_at_percent_clamps_negative, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_play_at_percent_clamps_over_max, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_play_at_percent_zero, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_play_at_percent_muted_returns_ok, setup_audio_ctx,
                                        teardown_audio_ctx),
    };

    const struct CMUnitTest status_tests[] = {
        cmocka_unit_test(test_status_strings_all_valid),
        cmocka_unit_test(test_status_string_unknown),
    };

    const struct CMUnitTest log_tests[] = {
        cmocka_unit_test_setup_teardown(test_log_starts_empty, setup_audio_ctx, teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_log_records_each_play, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_log_clear_resets, setup_audio_ctx, teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_log_wraps_at_capacity, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_log_error_count_accurate_after_wrap, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test_setup_teardown(test_log_snapshot_cap_smaller_than_count, setup_audio_ctx,
                                        teardown_audio_ctx),
        cmocka_unit_test(test_log_null_safety),
    };

    int failed = 0;
    failed += cmocka_run_group_tests_name("config defaults", config_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("error handling", error_tests, group_setup_sdl,
                                          group_teardown_sdl);
    failed += cmocka_run_group_tests_name("lifecycle and caching", lifecycle_tests, group_setup_sdl,
                                          group_teardown_sdl);
    failed += cmocka_run_group_tests_name("playback", playback_tests, group_setup_sdl,
                                          group_teardown_sdl);
    failed += cmocka_run_group_tests_name("volume control", volume_tests, group_setup_sdl,
                                          group_teardown_sdl);
    failed += cmocka_run_group_tests_name("volume percentage", volume_percent_tests,
                                          group_setup_sdl, group_teardown_sdl);
    failed += cmocka_run_group_tests_name("play at percent", play_at_percent_tests,
                                          group_setup_sdl, group_teardown_sdl);
    failed += cmocka_run_group_tests_name("status strings", status_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("call log", log_tests, group_setup_sdl,
                                          group_teardown_sdl);
    return failed;
}
