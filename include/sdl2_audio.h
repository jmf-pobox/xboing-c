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
 * Volume is a global setting (legacy per-call volume was always ignored).
 *
 * Opaque context pattern: no globals, fully testable.
 * See ADR-010 in docs/DESIGN.md for design rationale.
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

/* Maximum length of a sound cache key (basename without extension). */
#define SDL2A_MAX_KEY_LEN 64

/* Hash map capacity.  ~46 sounds at 64 slots gives ~72% load factor. */
#define SDL2A_MAX_SOUNDS 64

/* Default sound directory (relative to CWD). */
#define SDL2A_DEFAULT_SOUND_DIR "sounds"

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
    SDL2A_ERR_SCAN_FAILED
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
 * Play a cached sound by name (e.g., "boing").  Picks the first available
 * channel automatically.  Returns SDL2A_ERR_NOT_FOUND if the name is not
 * in the cache.  Fire-and-forget — the channel plays to completion.
 */
sdl2_audio_status_t sdl2_audio_play(sdl2_audio_t *ctx, const char *name);

/*
 * Set the master volume (0 = silent, MIX_MAX_VOLUME = full).
 * Applies immediately to all channels.
 */
void sdl2_audio_set_volume(sdl2_audio_t *ctx, int volume);

/* Get the current master volume. */
int sdl2_audio_get_volume(const sdl2_audio_t *ctx);

/* Halt all currently playing channels immediately. */
void sdl2_audio_halt(sdl2_audio_t *ctx);

/* Return the number of sounds currently cached. */
int sdl2_audio_count(const sdl2_audio_t *ctx);

/* Return a human-readable string for a status code. */
const char *sdl2_audio_status_string(sdl2_audio_status_t status);

#endif /* SDL2_AUDIO_H */
