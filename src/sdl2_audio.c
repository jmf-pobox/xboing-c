/*
 * sdl2_audio.c — SDL2_mixer sound playback and caching.
 *
 * See include/sdl2_audio.h for API documentation.
 * See ADR-010 in docs/DESIGN.md for design rationale.
 */

#include "sdl2_audio.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal data structures
 * ========================================================================= */

struct sdl2_audio_entry
{
    char key[SDL2A_MAX_KEY_LEN + 1];
    Mix_Chunk *chunk;
    bool occupied;
};

struct sdl2_audio
{
    struct sdl2_audio_entry entries[SDL2A_MAX_SOUNDS];
    int count;
    int volume;
    bool audio_opened;
    bool mixer_initialized;
};

/* =========================================================================
 * FNV-1a hash (same algorithm as sdl2_texture.c)
 * ========================================================================= */

static unsigned int fnv1a_hash(const char *str)
{
    unsigned int hash = 2166136261u;
    for (const char *p = str; *p != '\0'; p++)
    {
        hash ^= (unsigned int)(unsigned char)*p;
        hash *= 16777619u;
    }
    return hash;
}

/* =========================================================================
 * Hash map operations (open addressing, linear probing)
 * ========================================================================= */

static struct sdl2_audio_entry *find_slot(struct sdl2_audio_entry *entries, const char *key)
{
    unsigned int idx = fnv1a_hash(key) % SDL2A_MAX_SOUNDS;

    for (int i = 0; i < SDL2A_MAX_SOUNDS; i++)
    {
        unsigned int probe = (idx + (unsigned int)i) % SDL2A_MAX_SOUNDS;
        struct sdl2_audio_entry *e = &entries[probe];

        if (!e->occupied)
        {
            return e;
        }
        if (strcmp(e->key, key) == 0)
        {
            return e;
        }
    }

    return NULL; /* table full, key not found */
}

static const struct sdl2_audio_entry *find_entry(const struct sdl2_audio_entry *entries,
                                                 const char *key)
{
    unsigned int idx = fnv1a_hash(key) % SDL2A_MAX_SOUNDS;

    for (int i = 0; i < SDL2A_MAX_SOUNDS; i++)
    {
        unsigned int probe = (idx + (unsigned int)i) % SDL2A_MAX_SOUNDS;
        const struct sdl2_audio_entry *e = &entries[probe];

        if (!e->occupied)
        {
            return NULL;
        }
        if (strcmp(e->key, key) == 0)
        {
            return e;
        }
    }

    return NULL;
}

/* =========================================================================
 * File loading
 * ========================================================================= */

static sdl2_audio_status_t insert_sound(sdl2_audio_t *ctx, const char *key, const char *path)
{
    size_t key_len = strlen(key);
    if (key_len > SDL2A_MAX_KEY_LEN)
    {
        return SDL2A_ERR_KEY_TOO_LONG;
    }

    Mix_Chunk *chunk = Mix_LoadWAV(path);
    if (chunk == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sdl2_audio: failed to load '%s': %s", path,
                     Mix_GetError());
        return SDL2A_ERR_LOAD_FAILED;
    }

    struct sdl2_audio_entry *slot = find_slot(ctx->entries, key);
    if (slot == NULL)
    {
        Mix_FreeChunk(chunk);
        return SDL2A_ERR_CACHE_FULL;
    }

    /* If replacing an existing entry, free the old chunk. */
    if (slot->occupied && strcmp(slot->key, key) == 0)
    {
        Mix_FreeChunk(slot->chunk);
        ctx->count--;
    }

    memset(slot->key, 0, sizeof(slot->key));
    memcpy(slot->key, key, key_len);
    slot->chunk = chunk;
    slot->occupied = true;
    ctx->count++;

    return SDL2A_OK;
}

/* =========================================================================
 * Directory scanning
 * ========================================================================= */

static bool has_wav_extension(const char *name)
{
    size_t len = strlen(name);
    if (len < 4)
    {
        return false;
    }
    return strcmp(name + len - 4, ".wav") == 0;
}

/*
 * Derive a cache key from a filename by stripping the .wav extension.
 * Returns false if the key would be too long.
 */
static bool derive_key(const char *filename, char *key_buf)
{
    size_t len = strlen(filename);
    if (len < 4)
    {
        return false;
    }
    size_t key_len = len - 4; /* strip ".wav" */
    if (key_len > SDL2A_MAX_KEY_LEN)
    {
        return false;
    }

    memcpy(key_buf, filename, key_len);
    key_buf[key_len] = '\0';
    return true;
}

/*
 * Scan a flat directory for .wav files and load each into the cache.
 * Returns -1 if the directory cannot be opened, or the number of
 * files that failed to load (0 on complete success).
 */
static int scan_sound_dir(sdl2_audio_t *ctx, const char *dir_path)
{
    DIR *dir = opendir(dir_path);
    if (dir == NULL)
    {
        return -1;
    }

    int failures = 0;
    const struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        if (!has_wav_extension(entry->d_name))
        {
            continue;
        }

        char key_buf[SDL2A_MAX_KEY_LEN + 1];
        if (!derive_key(entry->d_name, key_buf))
        {
            failures++;
            continue;
        }

        char full_path[1024];
        int n = snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(full_path))
        {
            failures++;
            continue;
        }

        sdl2_audio_status_t st = insert_sound(ctx, key_buf, full_path);
        if (st != SDL2A_OK)
        {
            failures++;
        }
    }

    closedir(dir);
    return failures;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

sdl2_audio_config_t sdl2_audio_config_defaults(void)
{
    sdl2_audio_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.sound_dir = SDL2A_DEFAULT_SOUND_DIR;
    cfg.frequency = 44100;
    cfg.channels = 16;
    cfg.chunk_size = 2048;
    cfg.volume = MIX_MAX_VOLUME;
    return cfg;
}

sdl2_audio_t *sdl2_audio_create(const sdl2_audio_config_t *config, sdl2_audio_status_t *status)
{
    if (config == NULL)
    {
        if (status != NULL)
        {
            *status = SDL2A_ERR_NULL_ARG;
        }
        return NULL;
    }

    sdl2_audio_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
    {
        if (status != NULL)
        {
            *status = SDL2A_ERR_INIT_FAILED;
        }
        return NULL;
    }

    ctx->count = 0;
    ctx->volume = config->volume;
    ctx->audio_opened = false;
    ctx->mixer_initialized = false;

    /* Initialize SDL audio subsystem if not already active. */
    if (!(SDL_WasInit(0) & SDL_INIT_AUDIO))
    {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sdl2_audio: SDL_InitSubSystem(AUDIO): %s",
                         SDL_GetError());
            free(ctx);
            if (status != NULL)
            {
                *status = SDL2A_ERR_INIT_FAILED;
            }
            return NULL;
        }
    }

    /* Open the audio device via SDL_mixer. */
    if (Mix_OpenAudio(config->frequency, MIX_DEFAULT_FORMAT, 2, config->chunk_size) != 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sdl2_audio: Mix_OpenAudio: %s", Mix_GetError());
        free(ctx);
        if (status != NULL)
        {
            *status = SDL2A_ERR_INIT_FAILED;
        }
        return NULL;
    }
    ctx->audio_opened = true;
    ctx->mixer_initialized = true;

    /* Allocate playback channels. */
    Mix_AllocateChannels(config->channels);

    /* Set initial volume on all channels. */
    Mix_Volume(-1, ctx->volume);

    /* Scan the sound directory. */
    const char *sound_dir = config->sound_dir != NULL ? config->sound_dir : SDL2A_DEFAULT_SOUND_DIR;

    int failures = scan_sound_dir(ctx, sound_dir);
    if (failures < 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sdl2_audio: cannot open directory '%s'",
                     sound_dir);
        sdl2_audio_destroy(ctx);
        if (status != NULL)
        {
            *status = SDL2A_ERR_SCAN_FAILED;
        }
        return NULL;
    }

    if (failures > 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "sdl2_audio: %d file(s) failed to load from '%s'",
                    failures, sound_dir);
    }

    if (status != NULL)
    {
        *status = SDL2A_OK;
    }
    return ctx;
}

void sdl2_audio_destroy(sdl2_audio_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    for (int i = 0; i < SDL2A_MAX_SOUNDS; i++)
    {
        if (ctx->entries[i].occupied && ctx->entries[i].chunk != NULL)
        {
            Mix_FreeChunk(ctx->entries[i].chunk);
        }
    }

    if (ctx->audio_opened)
    {
        Mix_CloseAudio();
    }

    if (ctx->mixer_initialized)
    {
        /* Balance the implicit Mix_Init(0) from Mix_OpenAudio. */
        Mix_Quit();
    }

    free(ctx);
}

sdl2_audio_status_t sdl2_audio_play(sdl2_audio_t *ctx, const char *name)
{
    if (ctx == NULL || name == NULL)
    {
        return SDL2A_ERR_NULL_ARG;
    }

    const struct sdl2_audio_entry *e = find_entry(ctx->entries, name);
    if (e == NULL)
    {
        return SDL2A_ERR_NOT_FOUND;
    }

    /* -1 = first available channel, 0 = no looping */
    if (Mix_PlayChannel(-1, e->chunk, 0) == -1)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "sdl2_audio: Mix_PlayChannel('%s'): %s", name,
                    Mix_GetError());
    }

    return SDL2A_OK;
}

void sdl2_audio_set_volume(sdl2_audio_t *ctx, int volume)
{
    if (ctx == NULL)
    {
        return;
    }

    if (volume < 0)
    {
        volume = 0;
    }
    if (volume > MIX_MAX_VOLUME)
    {
        volume = MIX_MAX_VOLUME;
    }

    ctx->volume = volume;
    Mix_Volume(-1, volume);
}

int sdl2_audio_get_volume(const sdl2_audio_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    return ctx->volume;
}

void sdl2_audio_halt(sdl2_audio_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    Mix_HaltChannel(-1);
}

int sdl2_audio_count(const sdl2_audio_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    return ctx->count;
}

const char *sdl2_audio_status_string(sdl2_audio_status_t status)
{
    switch (status)
    {
        case SDL2A_OK:
            return "OK";
        case SDL2A_ERR_NULL_ARG:
            return "NULL argument";
        case SDL2A_ERR_INIT_FAILED:
            return "audio initialization failed";
        case SDL2A_ERR_NOT_FOUND:
            return "sound not found";
        case SDL2A_ERR_LOAD_FAILED:
            return "failed to load sound";
        case SDL2A_ERR_CACHE_FULL:
            return "sound cache full";
        case SDL2A_ERR_KEY_TOO_LONG:
            return "key exceeds maximum length";
        case SDL2A_ERR_SCAN_FAILED:
            return "directory scan failed";
    }
    return "unknown status";
}
