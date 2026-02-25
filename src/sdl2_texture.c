/*
 * sdl2_texture.c — SDL2 texture loading and caching.
 *
 * See include/sdl2_texture.h for API documentation.
 * See ADR-005 in docs/DESIGN.md for design rationale.
 */

#include "sdl2_texture.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL_image.h>

/* =========================================================================
 * Internal data structures
 * ========================================================================= */

struct sdl2_texture_entry
{
    char key[SDL2T_MAX_KEY_LEN + 1];
    SDL_Texture *texture;
    int width;
    int height;
    bool occupied;
};

struct sdl2_texture
{
    SDL_Renderer *renderer;
    struct sdl2_texture_entry entries[SDL2T_MAX_TEXTURES];
    int count;
    bool img_initialized;
};

/* =========================================================================
 * FNV-1a hash
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

/*
 * Find the slot for a key.  If the key is already present, returns a
 * pointer to that slot.  If not, returns a pointer to the first empty
 * slot where it could be inserted.  Returns NULL if the table is full
 * and the key is not present.
 */
static struct sdl2_texture_entry *find_slot(struct sdl2_texture_entry *entries, const char *key)
{
    unsigned int idx = fnv1a_hash(key) % SDL2T_MAX_TEXTURES;

    for (int i = 0; i < SDL2T_MAX_TEXTURES; i++)
    {
        unsigned int probe = (idx + (unsigned int)i) % SDL2T_MAX_TEXTURES;
        struct sdl2_texture_entry *e = &entries[probe];

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

/*
 * Find an occupied slot matching the key exactly.  Returns NULL if not found.
 */
static const struct sdl2_texture_entry *find_entry(const struct sdl2_texture_entry *entries,
                                                   const char *key)
{
    unsigned int idx = fnv1a_hash(key) % SDL2T_MAX_TEXTURES;

    for (int i = 0; i < SDL2T_MAX_TEXTURES; i++)
    {
        unsigned int probe = (idx + (unsigned int)i) % SDL2T_MAX_TEXTURES;
        const struct sdl2_texture_entry *e = &entries[probe];

        if (!e->occupied)
        {
            return NULL; /* empty slot means key is not in table */
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

/*
 * Load a single PNG and insert/replace into the hash map.
 * Internal helper used by both create() and load_file().
 */
static sdl2_texture_status_t insert_texture(sdl2_texture_t *ctx, const char *key, const char *path)
{
    size_t key_len = strlen(key);
    if (key_len > SDL2T_MAX_KEY_LEN)
    {
        return SDL2T_ERR_KEY_TOO_LONG;
    }

    SDL_Surface *surface = IMG_Load(path);
    if (surface == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sdl2_texture: failed to load '%s': %s", path,
                     IMG_GetError());
        return SDL2T_ERR_LOAD_FAILED;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(ctx->renderer, surface);
    int w = surface->w;
    int h = surface->h;
    SDL_FreeSurface(surface);

    if (texture == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "sdl2_texture: failed to create texture for '%s': %s", path, SDL_GetError());
        return SDL2T_ERR_LOAD_FAILED;
    }

    struct sdl2_texture_entry *slot = find_slot(ctx->entries, key);
    if (slot == NULL)
    {
        SDL_DestroyTexture(texture);
        return SDL2T_ERR_CACHE_FULL;
    }

    /* If replacing an existing entry, destroy the old texture. */
    if (slot->occupied && strcmp(slot->key, key) == 0)
    {
        SDL_DestroyTexture(slot->texture);
        ctx->count--;
    }

    memset(slot->key, 0, sizeof(slot->key));
    memcpy(slot->key, key, key_len);
    slot->texture = texture;
    slot->width = w;
    slot->height = h;
    slot->occupied = true;
    ctx->count++;

    return SDL2T_OK;
}

/* =========================================================================
 * Recursive directory scanning
 * ========================================================================= */

/*
 * Returns true if name ends with ".png" (case-sensitive).
 */
static bool has_png_extension(const char *name)
{
    size_t len = strlen(name);
    if (len < 4)
    {
        return false;
    }
    return strcmp(name + len - 4, ".png") == 0;
}

/*
 * Derive a cache key from a full file path by stripping the base_dir prefix
 * and .png suffix.  Writes into key_buf (which must be at least
 * SDL2T_MAX_KEY_LEN + 1 bytes).  Returns false if the key would be too long.
 */
static bool derive_key(const char *path, size_t base_dir_len, char *key_buf)
{
    /* Skip base_dir prefix and trailing slash. */
    const char *rel = path + base_dir_len;
    if (*rel == '/')
    {
        rel++;
    }

    /* Strip .png suffix. */
    size_t rel_len = strlen(rel);
    if (rel_len < 4)
    {
        return false;
    }
    size_t key_len = rel_len - 4; /* strip ".png" */

    if (key_len > SDL2T_MAX_KEY_LEN)
    {
        return false;
    }

    memcpy(key_buf, rel, key_len);
    key_buf[key_len] = '\0';
    return true;
}

/*
 * Recursively scan a directory for .png files and load each into the cache.
 * dir_path is the full path to the current directory.
 * base_dir_len is the length of the root base_dir (for key derivation).
 * Returns the number of files that failed to load.
 */
static int scan_directory(sdl2_texture_t *ctx, const char *dir_path, size_t base_dir_len)
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
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        /* Build full path. */
        char full_path[1024];
        int n = snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(full_path))
        {
            failures++;
            continue;
        }

        /*
         * Use d_type when available; fall back to trying opendir() if
         * d_type is DT_UNKNOWN (e.g., on some filesystems).
         */
#ifdef _DIRENT_HAVE_D_TYPE
        if (entry->d_type == DT_DIR)
        {
            int sub_failures = scan_directory(ctx, full_path, base_dir_len);
            if (sub_failures > 0)
            {
                failures += sub_failures;
            }
            continue;
        }
        if (entry->d_type != DT_REG && entry->d_type != DT_UNKNOWN)
        {
            continue;
        }
#endif

        if (has_png_extension(entry->d_name))
        {
            char key_buf[SDL2T_MAX_KEY_LEN + 1];
            if (!derive_key(full_path, base_dir_len, key_buf))
            {
                failures++;
                continue;
            }
            sdl2_texture_status_t st = insert_texture(ctx, key_buf, full_path);
            if (st != SDL2T_OK)
            {
                failures++;
            }
        }
#ifdef _DIRENT_HAVE_D_TYPE
        else if (entry->d_type == DT_UNKNOWN)
        {
            /* Could be a directory on a filesystem without d_type. */
            DIR *probe = opendir(full_path);
            if (probe != NULL)
            {
                closedir(probe);
                int sub_failures = scan_directory(ctx, full_path, base_dir_len);
                if (sub_failures > 0)
                {
                    failures += sub_failures;
                }
            }
        }
#else
        else
        {
            /* No d_type support -- try opening as directory. */
            DIR *probe = opendir(full_path);
            if (probe != NULL)
            {
                closedir(probe);
                int sub_failures = scan_directory(ctx, full_path, base_dir_len);
                if (sub_failures > 0)
                {
                    failures += sub_failures;
                }
            }
        }
#endif
    }

    closedir(dir);
    return failures;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

sdl2_texture_config_t sdl2_texture_config_defaults(void)
{
    sdl2_texture_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.renderer = NULL;
    cfg.base_dir = "assets/images";
    return cfg;
}

sdl2_texture_t *sdl2_texture_create(const sdl2_texture_config_t *config,
                                    sdl2_texture_status_t *status)
{
    sdl2_texture_status_t local_status = SDL2T_OK;

    if (config == NULL)
    {
        if (status != NULL)
        {
            *status = SDL2T_ERR_NULL_ARG;
        }
        return NULL;
    }

    if (config->renderer == NULL)
    {
        if (status != NULL)
        {
            *status = SDL2T_ERR_RENDERER;
        }
        return NULL;
    }

    sdl2_texture_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
    {
        if (status != NULL)
        {
            *status = SDL2T_ERR_LOAD_FAILED;
        }
        return NULL;
    }

    ctx->renderer = config->renderer;
    ctx->count = 0;
    ctx->img_initialized = false;

    /* Initialize SDL_image PNG support if not already active. */
    int img_flags = IMG_INIT_PNG;
    if (!(IMG_Init(0) & IMG_INIT_PNG))
    {
        if ((IMG_Init(img_flags) & IMG_INIT_PNG) == 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sdl2_texture: IMG_Init failed: %s",
                         IMG_GetError());
            free(ctx);
            if (status != NULL)
            {
                *status = SDL2T_ERR_IMG_INIT;
            }
            return NULL;
        }
        ctx->img_initialized = true;
    }

    /* Scan the base directory. */
    const char *base_dir = config->base_dir != NULL ? config->base_dir : "assets/images";

    int failures = scan_directory(ctx, base_dir, strlen(base_dir));
    if (failures < 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sdl2_texture: cannot open directory '%s'",
                     base_dir);
        sdl2_texture_destroy(ctx);
        if (status != NULL)
        {
            *status = SDL2T_ERR_SCAN_FAILED;
        }
        return NULL;
    }

    if (failures > 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "sdl2_texture: %d file(s) failed to load from '%s'", failures, base_dir);
    }

    local_status = SDL2T_OK;
    if (status != NULL)
    {
        *status = local_status;
    }
    return ctx;
}

void sdl2_texture_destroy(sdl2_texture_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    for (int i = 0; i < SDL2T_MAX_TEXTURES; i++)
    {
        if (ctx->entries[i].occupied && ctx->entries[i].texture != NULL)
        {
            SDL_DestroyTexture(ctx->entries[i].texture);
        }
    }

    if (ctx->img_initialized)
    {
        IMG_Quit();
    }

    free(ctx);
}

sdl2_texture_status_t sdl2_texture_get(const sdl2_texture_t *ctx, const char *key,
                                       sdl2_texture_info_t *info)
{
    if (ctx == NULL || key == NULL || info == NULL)
    {
        return SDL2T_ERR_NULL_ARG;
    }

    const struct sdl2_texture_entry *e = find_entry(ctx->entries, key);
    if (e == NULL)
    {
        return SDL2T_ERR_NOT_FOUND;
    }

    info->texture = e->texture;
    info->width = e->width;
    info->height = e->height;
    return SDL2T_OK;
}

sdl2_texture_status_t sdl2_texture_load_file(sdl2_texture_t *ctx, const char *key, const char *path)
{
    if (ctx == NULL || key == NULL || path == NULL)
    {
        return SDL2T_ERR_NULL_ARG;
    }

    return insert_texture(ctx, key, path);
}

int sdl2_texture_count(const sdl2_texture_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    return ctx->count;
}

const char *sdl2_texture_status_string(sdl2_texture_status_t status)
{
    switch (status)
    {
        case SDL2T_OK:
            return "OK";
        case SDL2T_ERR_NULL_ARG:
            return "NULL argument";
        case SDL2T_ERR_RENDERER:
            return "NULL renderer";
        case SDL2T_ERR_IMG_INIT:
            return "SDL_image initialization failed";
        case SDL2T_ERR_NOT_FOUND:
            return "texture not found";
        case SDL2T_ERR_LOAD_FAILED:
            return "failed to load texture";
        case SDL2T_ERR_CACHE_FULL:
            return "texture cache full";
        case SDL2T_ERR_KEY_TOO_LONG:
            return "key exceeds maximum length";
        case SDL2T_ERR_SCAN_FAILED:
            return "directory scan failed";
    }
    return "unknown status";
}
