#ifndef SDL2_AUDIO_H
#define SDL2_AUDIO_H

/*
 * sdl2_audio.h — SDL2_mixer sound playback and caching.
 *
 * Replaces the legacy fork+pipe /dev/dsp architecture with SDL2_mixer.
 * Scans a sound directory for .wav files at creation time, caches them
 * as Mix_Chunk objects in a hash map for O(1) lookup by name (e.g., "boing").
 *
 * Supports concurrent playback on multiple channels via Mix_PlayChannel(-1).
 * Master volume is a global setting; per-call volume is supported via
 * sdl2_audio_play_at_percent() and matches the original's Sun backend
 * (original/audio/SUNaudio.c:243).  The LINUXaudio.c shim that ships in
 * original/ as a port-aid drops the per-call volume — not the 1996
 * behavior on Justin's Sun/SGI workstations.
 *
 * Opaque context pattern: no globals, fully testable.
 * See ADR-010 in docs/DESIGN.md for design rationale.
 */

#include <stdbool.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

/* Maximum length of a sound cache key (basename without extension). */
#define SDL2A_MAX_KEY_LEN 64

/* Hash map capacity.  ~46 sounds at 64 slots gives ~72% load factor. */
#define SDL2A_MAX_SOUNDS 64

/* Capacity of the always-on call log ring buffer.  Each entry is
 * ~72 bytes (name + status), so 256 entries ≈ 18KB per context.  The
 * log records every sdl2_audio_play() call (including misses) for
 * test assertions and live troubleshooting. */
#define SDL2A_LOG_CAPACITY 256

/* Default sound directory (relative to CWD). */
#define SDL2A_DEFAULT_SOUND_DIR "sounds"

/* Legacy volume range: 0-100 percentage. */
#define SDL2A_VOLUME_PERCENT_MIN 0
#define SDL2A_VOLUME_PERCENT_MAX 100

/* Volume step for increment/decrement (1%). */
#define SDL2A_VOLUME_STEP 1

/* Status codes returned by sdl2_audio functions. */
typedef enum
{
    SDL2A_OK = 0,
    SDL2A_ERR_NULL_ARG,
    SDL2A_ERR_INIT_FAILED,
    SDL2A_ERR_NOT_FOUND,
    SDL2A_ERR_LOAD_FAILED,
    SDL2A_ERR_CACHE_FULL,
    SDL2A_ERR_KEY_TOO_LONG,
    SDL2A_ERR_SCAN_FAILED,
    SDL2A_ERR_PLAY_FAILED
} sdl2_audio_status_t;

/*
 * Configuration for sdl2_audio_create().
 * Use sdl2_audio_config_defaults() for sane starting values,
 * then override individual fields as needed.
 */
typedef struct
{
    const char *sound_dir; /* default: "sounds" */
    int frequency;         /* default: 44100 */
    int channels;          /* SDL_mixer playback channels, default: 16 */
    int chunk_size;        /* audio buffer size, default: 2048 */
    int volume;            /* master volume 0-128, default: MIX_MAX_VOLUME */
} sdl2_audio_config_t;

/* Opaque audio context — allocated by create, freed by destroy. */
typedef struct sdl2_audio sdl2_audio_t;

/*
 * Return a config struct populated with default values:
 *   sound_dir  = "sounds"
 *   frequency  = 44100
 *   channels   = 16
 *   chunk_size = 2048
 *   volume     = MIX_MAX_VOLUME (128)
 */
sdl2_audio_config_t sdl2_audio_config_defaults(void);

/*
 * Create an audio context.  Initializes SDL_mixer, opens the audio device,
 * then scans sound_dir for .wav files and caches each as a Mix_Chunk.
 *
 * Keys are derived from the filename by stripping the .wav extension
 * (e.g., "boing.wav" -> "boing").
 *
 * Partial loads succeed — individual file failures are logged but do not
 * abort.  Only structural failures (Mix_OpenAudio failure, unreadable
 * sound_dir) set *status to an error code and return NULL.
 *
 * Returns NULL on failure.  On success, *status is SDL2A_OK.
 * The caller owns the returned context and must call sdl2_audio_destroy().
 */
sdl2_audio_t *sdl2_audio_create(const sdl2_audio_config_t *config, sdl2_audio_status_t *status);

/*
 * Destroy the audio context: frees all cached Mix_Chunk objects, closes
 * the audio device, and quits SDL_mixer if this context initialized it.
 * Safe to call with NULL.
 */
void sdl2_audio_destroy(sdl2_audio_t *ctx);

/*
 * Play a cached sound by name (e.g., "boing").  Reserves the first
 * available SDL_mixer channel, sets its volume to the configured
 * master, then starts playback so the first sample is at the intended
 * volume (avoids the volume-jump that would happen if a recycled
 * channel still carried a per-call attenuation from
 * sdl2_audio_play_at_percent).  Returns SDL2A_ERR_NOT_FOUND if the
 * name is not in the cache, or SDL2A_ERR_PLAY_FAILED if no channel
 * is available or Mix_PlayChannel rejects the play.
 * Fire-and-forget — the channel plays to completion. */
sdl2_audio_status_t sdl2_audio_play(sdl2_audio_t *ctx, const char *name);

/*
 * Play a cached sound at a per-call volume, expressed as a percentage
 * of the current master volume (0 = silent, 100 = master).  Matches
 * the original's Sun backend (original/audio/SUNaudio.c:243
 * `playSoundFile(filename, volume)` mapped 0-100 to
 * `audio_set_play_gain`, where 100 was the configured ceiling).
 * Values outside 0-100 are clamped.  At percent == 100 the result is
 * identical to sdl2_audio_play(). */
sdl2_audio_status_t sdl2_audio_play_at_percent(sdl2_audio_t *ctx, const char *name, int percent);

/*
 * Set the master volume (0 = silent, MIX_MAX_VOLUME = full).
 * Applies immediately to all channels.
 */
void sdl2_audio_set_volume(sdl2_audio_t *ctx, int volume);

/* Get the current master volume. */
int sdl2_audio_get_volume(const sdl2_audio_t *ctx);

/*
 * Set the master volume as a percentage (0-100).
 * Maps legacy XBoing volume (0% = silent, 100% = full) to SDL2_mixer
 * range (0-128).  Values outside 0-100 are clamped.
 */
void sdl2_audio_set_volume_percent(sdl2_audio_t *ctx, int percent);

/*
 * Get the current master volume as a percentage (0-100).
 * Inverse of set_volume_percent — maps SDL2_mixer 0-128 back to 0-100.
 */
int sdl2_audio_get_volume_percent(const sdl2_audio_t *ctx);

/*
 * Increment the volume by SDL2A_VOLUME_STEP percent (1%).
 * No-op if already at 100%.  Returns the new percentage.
 */
int sdl2_audio_volume_up(sdl2_audio_t *ctx);

/*
 * Decrement the volume by SDL2A_VOLUME_STEP percent (1%).
 * No-op if already at 0%.  Returns the new percentage.
 */
int sdl2_audio_volume_down(sdl2_audio_t *ctx);

/* Halt all currently playing channels immediately. */
void sdl2_audio_halt(sdl2_audio_t *ctx);

/* Mute/unmute all audio.  When muted, play() returns OK but is silent. */
void sdl2_audio_set_muted(sdl2_audio_t *ctx, bool muted);

/* Return true if audio is muted. */
bool sdl2_audio_is_muted(const sdl2_audio_t *ctx);

/* Return the number of sounds currently cached. */
int sdl2_audio_count(const sdl2_audio_t *ctx);

/* Return a human-readable string for a status code. */
const char *sdl2_audio_status_string(sdl2_audio_status_t status);

/*
 * One entry in the audio call log.  Records the requested sound name
 * and the status that sdl2_audio_play() returned.
 */
typedef struct
{
    char name[SDL2A_MAX_KEY_LEN + 1];
    sdl2_audio_status_t status;
} sdl2_audio_call_t;

/*
 * Snapshot the most recent calls in the ring buffer into `out`, oldest
 * first.  Returns the number of entries written, capped at
 * `out_capacity` and at the number of calls logged since the last
 * sdl2_audio_log_clear().  Safe to call with ctx == NULL (returns 0).
 */
int sdl2_audio_log_snapshot(const sdl2_audio_t *ctx, sdl2_audio_call_t *out, int out_capacity);

/* Reset the log to empty.  Safe to call with ctx == NULL. */
void sdl2_audio_log_clear(sdl2_audio_t *ctx);

/*
 * Return the number of logged calls whose status is not SDL2A_OK.
 * Tests use this to assert "no misnamed sounds fired" without having
 * to inspect every log entry.  Safe to call with ctx == NULL.
 */
int sdl2_audio_log_error_count(const sdl2_audio_t *ctx);

#endif /* SDL2_AUDIO_H */
