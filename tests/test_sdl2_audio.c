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
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
    int count = sdl2_audio_count(ctx);
    assert_true(count > 0);
}

static void test_count_matches_wav_files(void **state)
{
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
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
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
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
    sdl2_audio_t *ctx = (sdl2_audio_t *)*state;
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

    const struct CMUnitTest status_tests[] = {
        cmocka_unit_test(test_status_strings_all_valid),
        cmocka_unit_test(test_status_string_unknown),
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
    failed += cmocka_run_group_tests_name("status strings", status_tests, NULL, NULL);
    return failed;
}
